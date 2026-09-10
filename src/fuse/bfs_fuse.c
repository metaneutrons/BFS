/* SPDX-License-Identifier: MPL-2.0 */
/* Linux libfuse3 adapter for the shared BFS core. */

#define FUSE_USE_VERSION 30

#include <fuse_lowlevel.h>

#include "bfs_dir.h"
#include "bfs_file.h"
#include "bfs_fs.h"
#include "bfs_inode.h"
#include "bfs_posix_bio.h"
#include "bfs_snapshot.h"
#include "bfs_superblock.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/xattr.h>
#include <time.h>

#define BFS_FUSE_ESCAPE_PREFIX "@bfs-hex-"
#define BFS_FUSE_ESCAPE_PREFIX_LEN 9u
#define BFS_FUSE_COMMENT_MAX 79u
#define BFS_FUSE_MAX_REPLY (16u * 1024u * 1024u)

typedef struct {
    bfs_fs_t fs;
    bfs_bio_t *bio;
    bfs_dir_tree_t snapshot_dir;
    bfs_btree_t snapshot_inode;
    bfs_dir_tree_t *dir_tree;
    bfs_btree_t *inode_tree;
    struct bfs_fuse_file_handle *handles;
    bool read_only;
} bfs_fuse_ctx_t;

typedef struct bfs_fuse_file_handle {
    bfs_file_t file;
    uint32_t inode;
    bool append;
    struct bfs_fuse_file_handle *next;
} bfs_fuse_file_handle_t;

typedef struct {
    const char *name;
    uint32_t id;
    bfs_snapshot_record_t record;
    bool by_id;
    bool found;
} snapshot_selector_t;

static bool has_open_inode(const bfs_fuse_ctx_t *ctx, uint32_t inode);
static bfs_fuse_file_handle_t *find_open_inode(const bfs_fuse_ctx_t *ctx, uint32_t inode);
static bfs_err_t detach_file_handle(bfs_fuse_ctx_t *ctx, bfs_fuse_file_handle_t *handle);

static int fuse_error(bfs_err_t error)
{
    switch (error) {
    case BFS_OK: return 0;
    case BFS_ERR_NOTFOUND: return ENOENT;
    case BFS_ERR_EXISTS: return EEXIST;
    case BFS_ERR_NOTEMPTY: return ENOTEMPTY;
    case BFS_ERR_INVAL: return EINVAL;
    case BFS_ERR_NOSPC: return ENOSPC;
    case BFS_ERR_OVERFLOW: return EOVERFLOW;
    case BFS_ERR_NOMEM: return ENOMEM;
    case BFS_ERR_AGAIN: return EAGAIN;
    case BFS_ERR_UNSUPPORTED: return EOPNOTSUPP;
    case BFS_ERR_CORRUPT:
    case BFS_ERR_IO:
    default: return EIO;
    }
}

static bool inode_number_valid(fuse_ino_t inode)
{
    return inode > 0 && inode <= UINT32_MAX;
}

static bool ctx_is_writable(const bfs_fuse_ctx_t *ctx)
{
    return ctx && !ctx->read_only;
}

static bool inode_allows(const bfs_inode_t *inode, uint32_t protection_bit)
{
    return (bfs_be32(inode->protection) & protection_bit) == 0;
}

static bfs_err_t read_inode(const bfs_fuse_ctx_t *ctx, fuse_ino_t inode,
                            bfs_inode_t *out)
{
    if (!inode_number_valid(inode)) return BFS_ERR_OVERFLOW;
    return bfs_inode_read(ctx->inode_tree, (uint32_t)inode, out);
}

/* Namespace mutations must never use a regular inode as a directory-tree key. */
static bfs_err_t require_directory(const bfs_fuse_ctx_t *ctx, fuse_ino_t inode)
{
    bfs_inode_t node;
    bfs_err_t error = read_inode(ctx, inode, &node);
    if (error == BFS_OK && bfs_be32(node.type) != BFS_INODE_DIR) return BFS_ERR_INVAL;
    return error;
}

static int fuse_directory_error(bfs_err_t error)
{
    return error == BFS_ERR_INVAL ? ENOTDIR : fuse_error(error);
}

static bool ascii_hex(uint8_t c, uint8_t *value)
{
    if (c >= '0' && c <= '9') {
        *value = c - '0';
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *value = c - 'A' + 10u;
        return true;
    }
    return false;
}

static bool copy_bytes(char *destination, size_t capacity, size_t *offset,
                       const char *source, size_t length)
{
    if (!destination || !offset || !source || *offset > capacity ||
        length > capacity - *offset)
        return false;
    for (size_t index = 0; index < length; index++) destination[*offset + index] = source[index];
    *offset += length;
    return true;
}

/* Decode only a complete escape. A malformed escape remains a direct name. */
static bfs_err_t decode_name(const char *name, char raw[BFS_NAME_MAX], uint8_t *length)
{
    if (!name || !length) return BFS_ERR_INVAL;
    size_t direct_length = strnlen(name, BFS_NAME_MAX + 1u);
    if (direct_length == 0 || direct_length > BFS_NAME_MAX) return BFS_ERR_INVAL;

    if (direct_length <= BFS_FUSE_ESCAPE_PREFIX_LEN ||
        memcmp(name, BFS_FUSE_ESCAPE_PREFIX, BFS_FUSE_ESCAPE_PREFIX_LEN) != 0 ||
        ((direct_length - BFS_FUSE_ESCAPE_PREFIX_LEN) & 1u) != 0) {
        size_t copied = 0;
        if (!copy_bytes(raw, BFS_NAME_MAX, &copied, name, direct_length)) return BFS_ERR_OVERFLOW;
        *length = (uint8_t)direct_length;
        return BFS_OK;
    }

    size_t decoded_length = (direct_length - BFS_FUSE_ESCAPE_PREFIX_LEN) / 2u;
    if (decoded_length == 0 || decoded_length > BFS_NAME_MAX) return BFS_ERR_INVAL;
    for (size_t index = 0; index < decoded_length; index++) {
        uint8_t high, low;
        if (!ascii_hex((uint8_t)name[BFS_FUSE_ESCAPE_PREFIX_LEN + index * 2u], &high) ||
            !ascii_hex((uint8_t)name[BFS_FUSE_ESCAPE_PREFIX_LEN + index * 2u + 1u], &low)) {
            size_t copied = 0;
            if (!copy_bytes(raw, BFS_NAME_MAX, &copied, name, direct_length)) return BFS_ERR_OVERFLOW;
            *length = (uint8_t)direct_length;
            return BFS_OK;
        }
        raw[index] = (char)((high << 4) | low);
    }
    *length = (uint8_t)decoded_length;
    return BFS_OK;
}

static bfs_err_t encode_name(const char *raw, uint8_t raw_length,
                             char encoded[2u * BFS_NAME_MAX + BFS_FUSE_ESCAPE_PREFIX_LEN + 1u])
{
    static const char hex[] = "0123456789ABCDEF";
    if (!raw || raw_length == 0) return BFS_ERR_INVAL;
    bool direct = raw_length != 1 || raw[0] != '.';
    direct = direct && !(raw_length == 2 && raw[0] == '.' && raw[1] == '.');
    direct = direct && (raw_length < BFS_FUSE_ESCAPE_PREFIX_LEN ||
                        memcmp(raw, BFS_FUSE_ESCAPE_PREFIX, BFS_FUSE_ESCAPE_PREFIX_LEN) != 0);
    for (uint8_t index = 0; index < raw_length && direct; index++)
        direct = raw[index] != '\0' && raw[index] != '/';

    if (direct) {
        size_t copied = 0;
        if (!copy_bytes(encoded, 2u * BFS_NAME_MAX + BFS_FUSE_ESCAPE_PREFIX_LEN + 1u,
                        &copied, raw, raw_length))
            return BFS_ERR_OVERFLOW;
        encoded[raw_length] = '\0';
        return BFS_OK;
    }
    size_t encoded_length = BFS_FUSE_ESCAPE_PREFIX_LEN + (size_t)raw_length * 2u;
    if (encoded_length > 255u) return BFS_ERR_OVERFLOW;
    size_t copied = 0;
    if (!copy_bytes(encoded, 2u * BFS_NAME_MAX + BFS_FUSE_ESCAPE_PREFIX_LEN + 1u,
                    &copied, BFS_FUSE_ESCAPE_PREFIX, BFS_FUSE_ESCAPE_PREFIX_LEN))
        return BFS_ERR_OVERFLOW;
    for (uint8_t index = 0; index < raw_length; index++) {
        uint8_t value = (uint8_t)raw[index];
        encoded[BFS_FUSE_ESCAPE_PREFIX_LEN + index * 2u] = hex[value >> 4];
        encoded[BFS_FUSE_ESCAPE_PREFIX_LEN + index * 2u + 1u] = hex[value & 0x0fu];
    }
    encoded[encoded_length] = '\0';
    return BFS_OK;
}

static bool datestamp_is_zero(const bfs_inode_t *inode, bool create)
{
    uint16_t days = bfs_be16(create ? inode->create_days : inode->modify_days);
    uint16_t mins = bfs_be16(create ? inode->create_mins : inode->modify_mins);
    uint16_t ticks = bfs_be16(create ? inode->create_ticks : inode->modify_ticks);
    return days == 0 && mins == 0 && ticks == 0;
}

static bfs_err_t datestamp_seconds(const bfs_inode_t *inode, bool create, time_t *out)
{
    if (!inode || !out) return BFS_ERR_INVAL;
    if (datestamp_is_zero(inode, create)) {
        *out = 0;
        return BFS_OK;
    }
    uint64_t days = bfs_be16(create ? inode->create_days : inode->modify_days);
    uint64_t mins = bfs_be16(create ? inode->create_mins : inode->modify_mins);
    uint64_t ticks = bfs_be16(create ? inode->create_ticks : inode->modify_ticks);
    uint64_t seconds = 252460800u + days * 86400u + mins * 60u + ticks / 50u;
    if (seconds > (uint64_t)INT64_MAX || (time_t)(int64_t)seconds != (time_t)seconds)
        return BFS_ERR_OVERFLOW;
    *out = (time_t)seconds;
    return BFS_OK;
}

static bool inode_is_executable(const bfs_inode_t *inode)
{
    /* Amiga protection bits deny rights. FIBF_EXECUTE is bit 1. */
    return inode_allows(inode, 2u);
}

static bfs_err_t inode_stat(const bfs_fuse_ctx_t *ctx, fuse_ino_t inode_number,
                            const bfs_inode_t *inode, struct stat *st)
{
    if (!ctx || !inode || !st || !inode_number_valid(inode_number)) return BFS_ERR_INVAL;
    uint32_t type = bfs_be32(inode->type);
    uint64_t size = ((uint64_t)bfs_be32(inode->size_hi) << 32) | bfs_be32(inode->size_lo);
    uint32_t links = bfs_be32(inode->link_count);
    if (size > (uint64_t)INT64_MAX) return BFS_ERR_OVERFLOW;

    memset(st, 0, sizeof(*st));
    st->st_ino = (ino_t)inode_number;
    st->st_uid = (uid_t)bfs_be16(inode->uid);
    st->st_gid = (gid_t)bfs_be16(inode->gid);
    st->st_nlink = (nlink_t)links;
    st->st_blksize = (blksize_t)ctx->fs.bio->block_size;
    st->st_size = (off_t)size;
    st->st_blocks = (blkcnt_t)((size + 511u) / 512u);
    if ((uint64_t)st->st_size != size || st->st_ino != (ino_t)inode_number ||
        st->st_nlink != (nlink_t)links || st->st_blocks < 0)
        return BFS_ERR_OVERFLOW;

    mode_t permissions = type == BFS_INODE_DIR ? 0555 : 0444;
    if (ctx_is_writable(ctx)) {
        permissions = 0;
        if (inode_allows(inode, 8u)) permissions |= 0444;
        if (inode_allows(inode, 4u) &&
            (type != BFS_INODE_DIR || inode_allows(inode, 1u)))
            permissions |= 0222;
        if (inode_is_executable(inode)) permissions |= 0111;
    }
    switch (type) {
    case BFS_INODE_DIR:
        st->st_mode = S_IFDIR | permissions;
        break;
    case BFS_INODE_FILE:
    case BFS_INODE_HARDLINK:
        st->st_mode = S_IFREG | permissions;
        break;
    case BFS_INODE_SOFTLINK:
        st->st_mode = S_IFLNK | 0777;
        break;
    default:
        return BFS_ERR_CORRUPT;
    }
    bfs_err_t error = datestamp_seconds(inode, false, &st->st_mtim.tv_sec);
    if (error != BFS_OK) return error;
    error = datestamp_seconds(inode, true, &st->st_ctim.tv_sec);
    if (error != BFS_OK) return error;
    st->st_atim = st->st_mtim;
    return BFS_OK;
}

static bfs_err_t lookup_child(const bfs_fuse_ctx_t *ctx, fuse_ino_t parent,
                              const char *name, uint32_t *child_out)
{
    bfs_err_t error = require_directory(ctx, parent);
    if (error != BFS_OK) return error;
    char raw[BFS_NAME_MAX];
    uint8_t length;
    error = decode_name(name, raw, &length);
    if (error != BFS_OK) return error;
    return bfs_dir_lookup(ctx->dir_tree, (uint32_t)parent, raw, length, child_out, NULL);
}

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    bool found;
    bool corrupt;
} comment_view_t;

static bool comment_scan(const char *name, uint8_t name_length, uint32_t inode,
                         uint32_t entry_type, void *opaque)
{
    (void)inode;
    (void)entry_type;
    comment_view_t *result = opaque;
    if (!result) return false;
    if (result->found || name_length > result->capacity) {
        result->corrupt = true;
        return false;
    }
    size_t copied = 0;
    if (!copy_bytes(result->buffer, result->capacity, &copied, name, name_length)) {
        result->corrupt = true;
        return false;
    }
    result->length = name_length;
    result->found = true;
    return false;
}

static bfs_err_t view_comment(const bfs_fuse_ctx_t *ctx, uint32_t inode,
                              char buffer[BFS_FUSE_COMMENT_MAX], size_t *length)
{
    if (!ctx || !buffer || !length || inode >= 0x80000000u) return BFS_ERR_INVAL;
    comment_view_t result = { buffer, BFS_FUSE_COMMENT_MAX, 0, false, false };
    bfs_err_t error = bfs_dir_scan(ctx->dir_tree, inode | 0x80000000u, comment_scan, &result);
    if (error != BFS_OK) return error;
    if (result.corrupt) return BFS_ERR_CORRUPT;
    if (!result.found) return BFS_ERR_NOTFOUND;
    *length = result.length;
    return BFS_OK;
}

static bfs_err_t make_entry(const bfs_fuse_ctx_t *ctx, uint32_t inode_number,
                            struct fuse_entry_param *entry)
{
    bfs_inode_t inode;
    bfs_err_t error = read_inode(ctx, inode_number, &inode);
    if (error != BFS_OK) return error;
    memset(entry, 0, sizeof(*entry));
    entry->ino = inode_number;
    error = inode_stat(ctx, inode_number, &inode, &entry->attr);
    if (error != BFS_OK) return error;
    entry->attr_timeout = 0;
    entry->entry_timeout = 0;
    return BFS_OK;
}

static void reply_entry(fuse_req_t request, const bfs_fuse_ctx_t *ctx, uint32_t inode_number)
{
    struct fuse_entry_param entry;
    bfs_err_t error = make_entry(ctx, inode_number, &entry);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    fuse_reply_entry(request, &entry);
}

static void bfs_fuse_lookup(fuse_req_t request, fuse_ino_t parent, const char *name)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!inode_number_valid(parent)) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    bfs_err_t error = require_directory(ctx, parent);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_directory_error(error));
        return;
    }
    if (strcmp(name, ".") == 0) {
        reply_entry(request, ctx, (uint32_t)parent);
        return;
    }
    if (strcmp(name, "..") == 0) {
        uint32_t parent_inode = BFS_ROOT_INO;
        if (parent != BFS_ROOT_INO) {
            error = bfs_dir_lookup(ctx->dir_tree, (uint32_t)parent, "..", 2,
                                   &parent_inode, NULL);
            if (error != BFS_OK) {
                fuse_reply_err(request, fuse_error(error));
                return;
            }
        }
        reply_entry(request, ctx, parent_inode);
        return;
    }
    uint32_t child;
    error = lookup_child(ctx, parent, name, &child);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_directory_error(error));
        return;
    }
    reply_entry(request, ctx, child);
}

static void bfs_fuse_forget(fuse_req_t request, fuse_ino_t inode, uint64_t nlookup)
{
    (void)inode;
    (void)nlookup;
    fuse_reply_none(request);
}

static void bfs_fuse_getattr(fuse_req_t request, fuse_ino_t inode, struct fuse_file_info *info)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    bfs_inode_t node;
    bfs_fuse_file_handle_t *handle = info && info->fh
        ? (bfs_fuse_file_handle_t *)(uintptr_t)info->fh : NULL;
    if (!handle || handle->inode != inode) handle = find_open_inode(ctx, (uint32_t)inode);
    bool unlinked = handle && handle->file.unlinked;
    bfs_err_t error = unlinked
        ? bfs_inode_read_unlinked(ctx->inode_tree, (uint32_t)inode, &node)
        : read_inode(ctx, inode, &node);
    struct stat st;
    if (error == BFS_OK) error = inode_stat(ctx, inode, &node, &st);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    fuse_reply_attr(request, &st, 0);
}

typedef struct {
    bfs_fuse_ctx_t *ctx;
    fuse_ino_t directory;
    char *buffer;
    size_t capacity;
    size_t used;
    uint64_t index;
    uint64_t offset;
    bfs_err_t error;
    bool full;
} readdir_ctx_t;

/* fuse_add_direntry needs the request object. Keep it in the callback context. */
typedef struct {
    fuse_req_t request;
    readdir_ctx_t state;
} readdir_request_t;

static bool readdir_scan(const char *name, uint8_t name_length, uint32_t inode,
                         uint32_t entry_type, void *opaque)
{
    (void)entry_type;
    readdir_request_t *request = opaque;
    readdir_ctx_t *ctx = &request->state;
    if (name_length == 2 && name[0] == '.' && name[1] == '.') return true;
    char encoded[2u * BFS_NAME_MAX + BFS_FUSE_ESCAPE_PREFIX_LEN + 1u];
    bfs_err_t error = encode_name(name, name_length, encoded);
    if (error != BFS_OK) {
        ctx->error = error;
        return false;
    }
    uint64_t entry_offset = ctx->index++;
    if (entry_offset < ctx->offset) return true;
    bfs_inode_t node;
    struct stat st;
    error = read_inode(ctx->ctx, inode, &node);
    if (error == BFS_OK) error = inode_stat(ctx->ctx, inode, &node, &st);
    if (error != BFS_OK) {
        ctx->error = error;
        return false;
    }
    size_t needed = fuse_add_direntry(request->request, NULL, 0, encoded, &st,
                                      (off_t)ctx->index);
    if (needed > ctx->capacity - ctx->used) {
        ctx->full = true;
        return false;
    }
    fuse_add_direntry(request->request, ctx->buffer + ctx->used,
                      ctx->capacity - ctx->used, encoded, &st,
                      (off_t)ctx->index);
    ctx->used += needed;
    return true;
}

static void bfs_fuse_readdir(fuse_req_t request, fuse_ino_t inode, size_t size,
                             off_t offset, struct fuse_file_info *info)
{
    (void)info;
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (offset < 0 || size > BFS_FUSE_MAX_REPLY) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    bfs_inode_t directory;
    bfs_err_t error = read_inode(ctx, inode, &directory);
    if (error == BFS_OK && bfs_be32(directory.type) != BFS_INODE_DIR) error = BFS_ERR_INVAL;
    if (error != BFS_OK) {
        fuse_reply_err(request, error == BFS_ERR_INVAL ? ENOTDIR : fuse_error(error));
        return;
    }
    char *buffer = malloc(size ? size : 1u);
    if (!buffer) {
        fuse_reply_err(request, ENOMEM);
        return;
    }
    readdir_request_t scan = {
        .request = request,
        /* Offsets 0 and 1 belong to . and ..; scanned entries begin at 2. */
        .state = { ctx, inode, buffer, size, 0, 0, (uint64_t)offset, BFS_OK, false },
    };
    uint32_t parent = BFS_ROOT_INO;
    if (inode != BFS_ROOT_INO) {
        error = bfs_dir_lookup(ctx->dir_tree, (uint32_t)inode, "..", 2, &parent, NULL);
        if (error != BFS_OK) scan.state.error = error;
    }
    if (scan.state.error == BFS_OK && scan.state.index++ >= scan.state.offset) {
        struct stat st;
        error = inode_stat(ctx, inode, &directory, &st);
        size_t needed = error == BFS_OK ? fuse_add_direntry(request, NULL, 0, ".", &st, 1) : 0;
        if (error != BFS_OK) scan.state.error = error;
        else if (needed > scan.state.capacity - scan.state.used) scan.state.full = true;
        else {
            fuse_add_direntry(request, scan.state.buffer + scan.state.used,
                              scan.state.capacity - scan.state.used, ".", &st, 1);
            scan.state.used += needed;
        }
    }
    if (scan.state.error == BFS_OK && !scan.state.full && scan.state.index++ >= scan.state.offset) {
        bfs_inode_t parent_node;
        struct stat st;
        error = read_inode(ctx, parent, &parent_node);
        if (error == BFS_OK) error = inode_stat(ctx, parent, &parent_node, &st);
        size_t needed = error == BFS_OK ? fuse_add_direntry(request, NULL, 0, "..", &st, 2) : 0;
        if (error != BFS_OK) scan.state.error = error;
        else if (needed > scan.state.capacity - scan.state.used) scan.state.full = true;
        else {
            fuse_add_direntry(request, scan.state.buffer + scan.state.used,
                              scan.state.capacity - scan.state.used, "..", &st, 2);
            scan.state.used += needed;
        }
    }
    if (scan.state.error == BFS_OK && !scan.state.full)
        error = bfs_dir_scan(ctx->dir_tree, (uint32_t)inode, readdir_scan, &scan);
    else
        error = scan.state.error;
    if (scan.state.error != BFS_OK) error = scan.state.error;
    if (error != BFS_OK && !scan.state.full) {
        free(buffer);
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    fuse_reply_buf(request, buffer, scan.state.used);
    free(buffer);
}

static bfs_err_t open_file_handle(bfs_fuse_ctx_t *ctx, uint32_t inode,
                                  struct fuse_file_info *info)
{
    bfs_fuse_file_handle_t *handle = calloc(1, sizeof(*handle));
    if (!handle) return BFS_ERR_NOMEM;
    bfs_err_t error = ctx_is_writable(ctx)
        ? bfs_file_open(&handle->file, &ctx->fs, inode)
        : bfs_file_open_readonly_view(&handle->file, &ctx->fs, ctx->inode_tree, inode);
    if (error != BFS_OK) {
        free(handle);
        return error;
    }
    handle->inode = inode;
    handle->append = (info->flags & O_APPEND) != 0;
    handle->next = ctx->handles;
    ctx->handles = handle;
    info->fh = (uint64_t)(uintptr_t)handle;
    info->keep_cache = 0;
    return BFS_OK;
}

static void bfs_fuse_open(fuse_req_t request, fuse_ino_t inode, struct fuse_file_info *info)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    int access = info->flags & O_ACCMODE;
    if (!inode_number_valid(inode)) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    if (!ctx_is_writable(ctx) &&
        (access != O_RDONLY || (info->flags & (O_CREAT | O_TRUNC)))) {
        fuse_reply_err(request, EROFS);
        return;
    }
    bfs_inode_t node;
    bfs_err_t error = read_inode(ctx, inode, &node);
    if (error == BFS_OK && bfs_be32(node.type) == BFS_INODE_DIR) {
        fuse_reply_err(request, EISDIR);
        return;
    }
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    error = open_file_handle(ctx, (uint32_t)inode, info);
    if (error == BFS_OK && ctx_is_writable(ctx) && (info->flags & O_TRUNC))
        error = bfs_file_truncate(&((bfs_fuse_file_handle_t *)(uintptr_t)info->fh)->file, 0);
    if (error != BFS_OK) {
        bfs_fuse_file_handle_t *handle = (bfs_fuse_file_handle_t *)(uintptr_t)info->fh;
        if (handle && ctx->handles == handle) {
            ctx->handles = handle->next;
            free(handle);
        }
        info->fh = 0;
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    fuse_reply_open(request, info);
}

static void bfs_fuse_opendir(fuse_req_t request, fuse_ino_t inode, struct fuse_file_info *info)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    bfs_inode_t node;
    bfs_err_t error = read_inode(ctx, inode, &node);
    if (error == BFS_OK && bfs_be32(node.type) != BFS_INODE_DIR) error = BFS_ERR_INVAL;
    if (error != BFS_OK) {
        fuse_reply_err(request, error == BFS_ERR_INVAL ? ENOTDIR : fuse_error(error));
        return;
    }
    info->fh = 0;
    info->keep_cache = 0;
    fuse_reply_open(request, info);
}

static void bfs_fuse_read(fuse_req_t request, fuse_ino_t inode, size_t size,
                          off_t offset, struct fuse_file_info *info)
{
    if (offset < 0 || size > BFS_FUSE_MAX_REPLY || !info->fh) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    bfs_fuse_file_handle_t *handle = (bfs_fuse_file_handle_t *)(uintptr_t)info->fh;
    if (!inode_number_valid(inode) || handle->inode != inode ||
        !has_open_inode(ctx, handle->inode)) {
        fuse_reply_err(request, EINVAL);
        return;
    }
    int64_t seek = bfs_file_seek(&handle->file, offset, BFS_SEEK_SET);
    if (seek < 0) {
        fuse_reply_err(request, fuse_error((bfs_err_t)seek));
        return;
    }
    char *buffer = malloc(size ? size : 1u);
    if (!buffer) {
        fuse_reply_err(request, ENOMEM);
        return;
    }
    int32_t count = bfs_file_read(&handle->file, buffer, (uint32_t)size);
    if (count < 0) {
        free(buffer);
        fuse_reply_err(request, fuse_error((bfs_err_t)count));
        return;
    }
    fuse_reply_buf(request, buffer, (size_t)count);
    free(buffer);
}

static void bfs_fuse_release(fuse_req_t request, fuse_ino_t inode,
                             struct fuse_file_info *info)
{
    (void)inode;
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    bfs_fuse_file_handle_t *handle = (bfs_fuse_file_handle_t *)(uintptr_t)info->fh;
    bfs_err_t error = handle ? detach_file_handle(ctx, handle) : BFS_ERR_INVAL;
    info->fh = 0;
    fuse_reply_err(request, fuse_error(error));
}

static void bfs_fuse_readlink(fuse_req_t request, fuse_ino_t inode)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    bfs_inode_t node;
    bfs_err_t error = read_inode(ctx, inode, &node);
    if (error == BFS_OK && bfs_be32(node.type) != BFS_INODE_SOFTLINK) error = BFS_ERR_INVAL;
    if (error != BFS_OK) {
        fuse_reply_err(request, error == BFS_ERR_INVAL ? EINVAL : fuse_error(error));
        return;
    }
    uint64_t length = ((uint64_t)bfs_be32(node.size_hi) << 32) | bfs_be32(node.size_lo);
    if (length > BFS_FUSE_MAX_REPLY) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    char *target = malloc((size_t)length + 1u);
    if (!target) {
        fuse_reply_err(request, ENOMEM);
        return;
    }
    bfs_file_t file;
    error = ctx_is_writable(ctx)
        ? bfs_file_open(&file, &ctx->fs, (uint32_t)inode)
        : bfs_file_open_readonly_view(&file, &ctx->fs, ctx->inode_tree, (uint32_t)inode);
    int32_t count = error == BFS_OK ? bfs_file_read(&file, target, (uint32_t)length) : error;
    if (count != (int32_t)length || memchr(target, '\0', (size_t)length) != NULL) {
        free(target);
        fuse_reply_err(request, count < 0 ? fuse_error((bfs_err_t)count) : EIO);
        return;
    }
    target[length] = '\0';
    fuse_reply_readlink(request, target);
    free(target);
}

static void bfs_fuse_statfs(fuse_req_t request, fuse_ino_t inode)
{
    (void)inode;
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    struct statvfs st;
    memset(&st, 0, sizeof(st));
    st.f_bsize = ctx->fs.bio->block_size;
    st.f_frsize = ctx->fs.bio->block_size;
    st.f_blocks = ctx->fs.bio->block_count;
    st.f_bfree = ctx->fs.freespace.total_free;
    st.f_bavail = ctx->fs.freespace.total_free;
    st.f_files = ctx->fs.next_ino - 1u;
    st.f_ffree = 0;
    st.f_namemax = 255;
    fuse_reply_statfs(request, &st);
}

static size_t format_u16_decimal(char output[5], uint16_t value)
{
    char reversed[5];
    size_t length = 0;
    do {
        reversed[length++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    for (size_t index = 0; index < length; index++) output[index] = reversed[length - index - 1u];
    return length;
}

static size_t format_datestamp(char output[17], uint16_t days, uint16_t minutes, uint16_t ticks)
{
    size_t length = format_u16_decimal(output, days);
    output[length++] = ':';
    length += format_u16_decimal(output + length, minutes);
    output[length++] = ':';
    length += format_u16_decimal(output + length, ticks);
    return length;
}

static void format_protection(char output[8], uint32_t protection)
{
    static const char hex[] = "0123456789ABCDEF";
    for (size_t index = 0; index < 8u; index++)
        output[index] = hex[(protection >> ((7u - index) * 4u)) & 0x0fu];
}

static bfs_err_t xattr_value(const bfs_fuse_ctx_t *ctx, uint32_t inode,
                             const char *name, char value[96], size_t *length)
{
    bfs_inode_t node;
    bfs_err_t error = read_inode(ctx, inode, &node);
    if (error != BFS_OK) return error;
    if (strcmp(name, "user.bfs.comment") == 0)
        return view_comment(ctx, inode, value, length);
    if (strcmp(name, "user.bfs.protection") == 0) {
        format_protection(value, bfs_be32(node.protection));
        *length = 8;
    } else if (strcmp(name, "user.bfs.uid") == 0) {
        *length = format_u16_decimal(value, bfs_be16(node.uid));
    } else if (strcmp(name, "user.bfs.gid") == 0) {
        *length = format_u16_decimal(value, bfs_be16(node.gid));
    } else if (strcmp(name, "user.bfs.create_datestamp") == 0) {
        *length = format_datestamp(value, bfs_be16(node.create_days),
                                   bfs_be16(node.create_mins), bfs_be16(node.create_ticks));
    } else if (strcmp(name, "user.bfs.modify_datestamp") == 0) {
        *length = format_datestamp(value, bfs_be16(node.modify_days),
                                   bfs_be16(node.modify_mins), bfs_be16(node.modify_ticks));
    } else {
        return BFS_ERR_NOTFOUND;
    }
    return BFS_OK;
}

static void bfs_fuse_getxattr(fuse_req_t request, fuse_ino_t inode, const char *name,
                              size_t size)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!inode_number_valid(inode)) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    char value[96];
    size_t length;
    bfs_err_t error = xattr_value(ctx, (uint32_t)inode, name, value, &length);
    if (error != BFS_OK) {
        fuse_reply_err(request, error == BFS_ERR_NOTFOUND ? ENODATA : fuse_error(error));
        return;
    }
    if (size == 0) {
        fuse_reply_xattr(request, length);
        return;
    }
    if (size < length) {
        fuse_reply_err(request, ERANGE);
        return;
    }
    fuse_reply_buf(request, value, length);
}

static void bfs_fuse_listxattr(fuse_req_t request, fuse_ino_t inode, size_t size)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    static const char fixed[] =
        "user.bfs.protection\0user.bfs.uid\0user.bfs.gid\0"
        "user.bfs.create_datestamp\0user.bfs.modify_datestamp\0";
    char comment[BFS_FUSE_COMMENT_MAX];
    size_t comment_length;
    if (!inode_number_valid(inode)) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    bfs_err_t error = read_inode(ctx, inode, &(bfs_inode_t){0});
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    error = view_comment(ctx, (uint32_t)inode, comment, &comment_length);
    bool have_comment = error == BFS_OK;
    if (error != BFS_OK && error != BFS_ERR_NOTFOUND) {
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    static const char comment_name[] = "user.bfs.comment\0";
    size_t fixed_length = sizeof(fixed) - 1u;
    size_t total = fixed_length + (have_comment ? sizeof(comment_name) - 1u : 0u);
    if (size == 0) {
        fuse_reply_xattr(request, total);
        return;
    }
    if (size < total) {
        fuse_reply_err(request, ERANGE);
        return;
    }
    char *names = malloc(total);
    if (!names) {
        fuse_reply_err(request, ENOMEM);
        return;
    }
    size_t copied = 0;
    if (!copy_bytes(names, total, &copied, fixed, fixed_length) ||
        (have_comment && !copy_bytes(names, total, &copied, comment_name,
                                     sizeof(comment_name) - 1u))) {
        free(names);
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    fuse_reply_buf(request, names, total);
    free(names);
}

static void bfs_fuse_readonly(fuse_req_t request)
{
    fuse_reply_err(request, EROFS);
}

static bfs_fuse_file_handle_t *find_open_inode(const bfs_fuse_ctx_t *ctx, uint32_t inode)
{
    for (bfs_fuse_file_handle_t *handle = ctx->handles; handle; handle = handle->next)
        if (handle->inode == inode) return handle;
    return NULL;
}

static bool has_open_inode(const bfs_fuse_ctx_t *ctx, uint32_t inode)
{
    return find_open_inode(ctx, inode) != NULL;
}

static bfs_err_t mark_open_inode_unlinked(bfs_fuse_ctx_t *ctx, uint32_t inode)
{
    bool found = false;
    for (bfs_fuse_file_handle_t *handle = ctx->handles; handle; handle = handle->next) {
        if (handle->inode != inode) continue;
        bfs_err_t error = bfs_file_mark_unlinked(&handle->file);
        if (error != BFS_OK) return error;
        found = true;
    }
    return found ? BFS_OK : BFS_ERR_NOTFOUND;
}

static bfs_err_t detach_file_handle(bfs_fuse_ctx_t *ctx, bfs_fuse_file_handle_t *handle)
{
    bfs_fuse_file_handle_t **current = &ctx->handles;
    while (*current && *current != handle) current = &(*current)->next;
    if (!*current) return BFS_ERR_INVAL;
    *current = handle->next;
    bool reap = handle->file.unlinked && !has_open_inode(ctx, handle->inode);
    uint32_t inode = handle->inode;
    free(handle);
    return reap ? bfs_fs_reap_unlinked_file(&ctx->fs, inode) : BFS_OK;
}

static void bfs_fuse_write(fuse_req_t request, fuse_ino_t inode, const char *buffer,
                           size_t size, off_t offset, struct fuse_file_info *info)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!ctx_is_writable(ctx)) {
        bfs_fuse_readonly(request);
        return;
    }
    if (!inode_number_valid(inode) || offset < 0 || size > INT32_MAX || !info->fh) {
        fuse_reply_err(request, EINVAL);
        return;
    }
    bfs_fuse_file_handle_t *handle = (bfs_fuse_file_handle_t *)(uintptr_t)info->fh;
    if (handle->inode != inode || !has_open_inode(ctx, handle->inode)) {
        fuse_reply_err(request, EINVAL);
        return;
    }
    int32_t written;
    if (handle->append) {
        written = bfs_file_append(&handle->file, buffer, (uint32_t)size);
    } else {
        int64_t seek = bfs_file_seek(&handle->file, offset, BFS_SEEK_SET);
        written = seek < 0 ? (int32_t)seek :
            bfs_file_write(&handle->file, buffer, (uint32_t)size);
    }
    if (written < 0) {
        fuse_reply_err(request, fuse_error((bfs_err_t)written));
        return;
    }
    fuse_reply_write(request, (size_t)written);
}

static void bfs_fuse_setattr(fuse_req_t request, fuse_ino_t inode, struct stat *attr,
                             int to_set, struct fuse_file_info *info)
{
    (void)info;
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!ctx_is_writable(ctx)) {
        bfs_fuse_readonly(request);
        return;
    }
    if (!inode_number_valid(inode) || !attr) {
        fuse_reply_err(request, EINVAL);
        return;
    }
    if ((to_set & ~FUSE_SET_ATTR_SIZE) != 0) {
        fuse_reply_err(request, EOPNOTSUPP);
        return;
    }
    if ((to_set & FUSE_SET_ATTR_SIZE) == 0 || attr->st_size < 0) {
        fuse_reply_err(request, EINVAL);
        return;
    }
    bfs_fuse_file_handle_t *handle = info && info->fh
        ? (bfs_fuse_file_handle_t *)(uintptr_t)info->fh : NULL;
    bfs_file_t temporary;
    bfs_err_t error;
    if (handle && handle->inode == inode && has_open_inode(ctx, handle->inode)) {
        error = bfs_file_truncate(&handle->file, (uint64_t)attr->st_size);
    } else {
        error = bfs_file_open(&temporary, &ctx->fs, (uint32_t)inode);
        if (error == BFS_OK) error = bfs_file_truncate(&temporary, (uint64_t)attr->st_size);
    }
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    bfs_inode_t node;
    struct stat result;
    error = handle && handle->file.unlinked
        ? bfs_inode_read_unlinked(ctx->inode_tree, (uint32_t)inode, &node)
        : read_inode(ctx, inode, &node);
    if (error == BFS_OK) error = inode_stat(ctx, inode, &node, &result);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    fuse_reply_attr(request, &result, 0);
}

static void bfs_fuse_setxattr(fuse_req_t request, fuse_ino_t inode, const char *name,
                               const char *value, size_t size, int flags)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!ctx_is_writable(ctx)) {
        bfs_fuse_readonly(request);
        return;
    }
    if (!inode_number_valid(inode)) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    if (!name || size == 0 || size > BFS_FUSE_COMMENT_MAX || !value ||
        (flags & ~(XATTR_CREATE | XATTR_REPLACE)) != 0 ||
        ((flags & XATTR_CREATE) != 0 && (flags & XATTR_REPLACE) != 0) ||
        (size != 0 && memchr(value, '\0', size) != NULL)) {
        fuse_reply_err(request, EINVAL);
        return;
    }
    if (strcmp(name, "user.bfs.comment") != 0) {
        fuse_reply_err(request, EOPNOTSUPP);
        return;
    }
    char previous[BFS_FUSE_COMMENT_MAX];
    size_t previous_length;
    bfs_err_t prior = view_comment(ctx, (uint32_t)inode, previous, &previous_length);
    if (prior != BFS_OK && prior != BFS_ERR_NOTFOUND) {
        fuse_reply_err(request, fuse_error(prior));
        return;
    }
    if ((flags & XATTR_CREATE) != 0 && prior == BFS_OK) {
        fuse_reply_err(request, EEXIST);
        return;
    }
    if ((flags & XATTR_REPLACE) != 0 && prior == BFS_ERR_NOTFOUND) {
        fuse_reply_err(request, ENODATA);
        return;
    }
    bfs_err_t error = bfs_fs_set_comment(&ctx->fs, (uint32_t)inode, value, (uint8_t)size);
    fuse_reply_err(request, fuse_error(error));
}

static void bfs_fuse_removexattr(fuse_req_t request, fuse_ino_t inode, const char *name)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!ctx_is_writable(ctx)) {
        bfs_fuse_readonly(request);
        return;
    }
    if (!inode_number_valid(inode)) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    if (!name) {
        fuse_reply_err(request, EINVAL);
        return;
    }
    if (strcmp(name, "user.bfs.comment") != 0) {
        fuse_reply_err(request, EOPNOTSUPP);
        return;
    }
    char previous[BFS_FUSE_COMMENT_MAX];
    size_t previous_length;
    bfs_err_t error = view_comment(ctx, (uint32_t)inode, previous, &previous_length);
    if (error == BFS_ERR_NOTFOUND) {
        fuse_reply_err(request, ENODATA);
        return;
    }
    if (error == BFS_OK) error = bfs_fs_set_comment(&ctx->fs, (uint32_t)inode, NULL, 0);
    fuse_reply_err(request, fuse_error(error));
}

static void bfs_fuse_create(fuse_req_t request, fuse_ino_t parent, const char *name,
                            mode_t mode, struct fuse_file_info *info)
{
    (void)mode;
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!ctx_is_writable(ctx)) {
        bfs_fuse_readonly(request);
        return;
    }
    if (!inode_number_valid(parent) || !info) {
        fuse_reply_err(request, EINVAL);
        return;
    }
    bfs_err_t error = require_directory(ctx, parent);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_directory_error(error));
        return;
    }
    char raw[BFS_NAME_MAX];
    uint8_t length;
    error = decode_name(name, raw, &length);
    uint32_t inode;
    if (error == BFS_OK)
        error = bfs_fs_create_file(&ctx->fs, (uint32_t)parent, raw, length, &inode);
    if (error == BFS_OK) error = open_file_handle(ctx, inode, info);
    struct fuse_entry_param entry;
    if (error == BFS_OK) error = make_entry(ctx, inode, &entry);
    if (error != BFS_OK) {
        bfs_fuse_file_handle_t *handle = (bfs_fuse_file_handle_t *)(uintptr_t)info->fh;
        if (handle && ctx->handles == handle) {
            ctx->handles = handle->next;
            free(handle);
        }
        info->fh = 0;
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    fuse_reply_create(request, &entry, info);
}

static void bfs_fuse_mkdir(fuse_req_t request, fuse_ino_t parent, const char *name, mode_t mode)
{
    (void)mode;
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!ctx_is_writable(ctx)) {
        bfs_fuse_readonly(request);
        return;
    }
    if (!inode_number_valid(parent)) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    bfs_err_t error = require_directory(ctx, parent);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_directory_error(error));
        return;
    }
    char raw[BFS_NAME_MAX];
    uint8_t length;
    error = decode_name(name, raw, &length);
    uint32_t inode;
    if (error == BFS_OK)
        error = bfs_fs_mkdir(&ctx->fs, (uint32_t)parent, raw, length, &inode);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    reply_entry(request, ctx, inode);
}

static void bfs_fuse_mknod(fuse_req_t request, fuse_ino_t parent, const char *name,
                           mode_t mode, dev_t rdev)
{
    (void)rdev;
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!ctx_is_writable(ctx)) {
        bfs_fuse_readonly(request);
        return;
    }
    if (!S_ISREG(mode)) {
        fuse_reply_err(request, EOPNOTSUPP);
        return;
    }
    if (!inode_number_valid(parent)) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    bfs_err_t error = require_directory(ctx, parent);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_directory_error(error));
        return;
    }
    char raw[BFS_NAME_MAX];
    uint8_t length;
    error = decode_name(name, raw, &length);
    uint32_t inode;
    if (error == BFS_OK)
        error = bfs_fs_create_file(&ctx->fs, (uint32_t)parent, raw, length, &inode);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    reply_entry(request, ctx, inode);
}

static void bfs_fuse_unlink(fuse_req_t request, fuse_ino_t parent, const char *name)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!ctx_is_writable(ctx)) {
        bfs_fuse_readonly(request);
        return;
    }
    if (!inode_number_valid(parent)) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    bfs_err_t error = require_directory(ctx, parent);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_directory_error(error));
        return;
    }
    char raw[BFS_NAME_MAX];
    uint8_t length;
    error = decode_name(name, raw, &length);
    uint32_t inode = 0, type = 0, orphan = 0;
    if (error == BFS_OK)
        error = bfs_dir_lookup(ctx->dir_tree, (uint32_t)parent, raw, length, &inode, &type);
    if (error == BFS_OK && type == BFS_INODE_DIR) {
        fuse_reply_err(request, EISDIR);
        return;
    }
    bool preserve = error == BFS_OK && has_open_inode(ctx, inode);
    if (error == BFS_OK) {
        error = preserve
            ? bfs_fs_unlink_open_file(&ctx->fs, (uint32_t)parent, raw, length, &orphan)
            : bfs_fs_delete_file(&ctx->fs, (uint32_t)parent, raw, length);
    }
    if (error == BFS_OK && orphan != 0) error = mark_open_inode_unlinked(ctx, orphan);
    fuse_reply_err(request, fuse_error(error));
}

static void bfs_fuse_rmdir(fuse_req_t request, fuse_ino_t parent, const char *name)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!ctx_is_writable(ctx)) {
        bfs_fuse_readonly(request);
        return;
    }
    if (!inode_number_valid(parent)) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    bfs_err_t error = require_directory(ctx, parent);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_directory_error(error));
        return;
    }
    char raw[BFS_NAME_MAX];
    uint8_t length;
    error = decode_name(name, raw, &length);
    uint32_t type = 0;
    if (error == BFS_OK)
        error = bfs_dir_lookup(ctx->dir_tree, (uint32_t)parent, raw, length, NULL, &type);
    if (error == BFS_OK && type != BFS_INODE_DIR) {
        fuse_reply_err(request, ENOTDIR);
        return;
    }
    if (error == BFS_OK)
        error = bfs_fs_rmdir(&ctx->fs, (uint32_t)parent, raw, length);
    fuse_reply_err(request, fuse_error(error));
}

static void bfs_fuse_rename(fuse_req_t request, fuse_ino_t parent, const char *name,
                            fuse_ino_t new_parent, const char *new_name, unsigned int flags)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!ctx_is_writable(ctx)) {
        bfs_fuse_readonly(request);
        return;
    }
    if (!inode_number_valid(parent) || !inode_number_valid(new_parent)) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    if (flags != 0) {
        fuse_reply_err(request, EOPNOTSUPP);
        return;
    }
    bfs_err_t error = require_directory(ctx, parent);
    if (error == BFS_OK) error = require_directory(ctx, new_parent);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_directory_error(error));
        return;
    }
    char old_raw[BFS_NAME_MAX], new_raw[BFS_NAME_MAX];
    uint8_t old_length, new_length;
    error = decode_name(name, old_raw, &old_length);
    if (error == BFS_OK) error = decode_name(new_name, new_raw, &new_length);
    uint32_t source_type = 0, replaced = 0, replaced_type = 0, orphan = 0;
    if (error == BFS_OK)
        error = bfs_dir_lookup(ctx->dir_tree, (uint32_t)parent,
                               old_raw, old_length, NULL, &source_type);
    if (error == BFS_OK) {
        bfs_err_t lookup = bfs_dir_lookup(ctx->dir_tree, (uint32_t)new_parent,
                                          new_raw, new_length, &replaced, &replaced_type);
        if (lookup != BFS_OK && lookup != BFS_ERR_NOTFOUND) error = lookup;
    }
    if (error == BFS_OK && replaced != 0 &&
        (source_type == BFS_INODE_DIR) != (replaced_type == BFS_INODE_DIR)) {
        fuse_reply_err(request, source_type == BFS_INODE_DIR ? ENOTDIR : EISDIR);
        return;
    }
    bool preserve = error == BFS_OK && replaced != 0 &&
        replaced_type != BFS_INODE_DIR && has_open_inode(ctx, replaced);
    if (error == BFS_OK)
        error = bfs_fs_rename_replace(&ctx->fs, (uint32_t)parent, old_raw, old_length,
                                      (uint32_t)new_parent, new_raw, new_length,
                                      preserve, &orphan);
    if (error == BFS_OK && orphan != 0) error = mark_open_inode_unlinked(ctx, orphan);
    fuse_reply_err(request, fuse_error(error));
}

static void bfs_fuse_link(fuse_req_t request, fuse_ino_t inode, fuse_ino_t new_parent,
                          const char *new_name)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!ctx_is_writable(ctx)) {
        bfs_fuse_readonly(request);
        return;
    }
    if (!inode_number_valid(inode) || !inode_number_valid(new_parent)) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    bfs_err_t error = require_directory(ctx, new_parent);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_directory_error(error));
        return;
    }
    char raw[BFS_NAME_MAX];
    uint8_t length;
    error = decode_name(new_name, raw, &length);
    if (error == BFS_OK)
        error = bfs_fs_make_hardlink(&ctx->fs, (uint32_t)new_parent, raw, length,
                                     (uint32_t)inode);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    reply_entry(request, ctx, (uint32_t)inode);
}

static void bfs_fuse_symlink(fuse_req_t request, const char *link, fuse_ino_t parent,
                             const char *name)
{
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!ctx_is_writable(ctx)) {
        bfs_fuse_readonly(request);
        return;
    }
    if (!inode_number_valid(parent) || !link) {
        fuse_reply_err(request, EINVAL);
        return;
    }
    bfs_err_t error = require_directory(ctx, parent);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_directory_error(error));
        return;
    }
    char raw[BFS_NAME_MAX];
    uint8_t name_length;
    error = decode_name(name, raw, &name_length);
    size_t path_length = strnlen(link, UINT16_MAX + 1u);
    if (error == BFS_OK && (path_length == 0 || path_length > UINT16_MAX)) error = BFS_ERR_OVERFLOW;
    if (error == BFS_OK)
        error = bfs_fs_make_softlink(&ctx->fs, (uint32_t)parent, raw, name_length,
                                     link, (uint16_t)path_length);
    uint32_t inode;
    if (error == BFS_OK)
        error = bfs_dir_lookup(ctx->dir_tree, (uint32_t)parent, raw, name_length,
                               &inode, NULL);
    if (error != BFS_OK) {
        fuse_reply_err(request, fuse_error(error));
        return;
    }
    reply_entry(request, ctx, inode);
}

static void bfs_fuse_fsync(fuse_req_t request, fuse_ino_t inode, int datasync,
                           struct fuse_file_info *info)
{
    (void)datasync;
    (void)info;
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    if (!inode_number_valid(inode)) {
        fuse_reply_err(request, EOVERFLOW);
        return;
    }
    if (!ctx_is_writable(ctx)) {
        bfs_fuse_readonly(request);
        return;
    }
    fuse_reply_err(request, fuse_error(bfs_fs_sync(&ctx->fs)));
}

static void bfs_fuse_fsyncdir(fuse_req_t request, fuse_ino_t inode, int datasync,
                              struct fuse_file_info *info)
{
    bfs_fuse_fsync(request, inode, datasync, info);
}

static void bfs_fuse_flush(fuse_req_t request, fuse_ino_t inode, struct fuse_file_info *info)
{
    (void)inode;
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    bfs_fuse_file_handle_t *handle = info
        ? (bfs_fuse_file_handle_t *)(uintptr_t)info->fh : NULL;
    if (!handle || !has_open_inode(ctx, handle->inode)) {
        fuse_reply_err(request, EINVAL);
        return;
    }
    /* flush/release only retire a file descriptor. Durable persistence is fsync. */
    fuse_reply_err(request, 0);
}

static void bfs_fuse_fallocate(fuse_req_t request, fuse_ino_t inode, int mode,
                               off_t offset, off_t length, struct fuse_file_info *info)
{
    (void)inode;
    (void)mode;
    (void)offset;
    (void)length;
    (void)info;
    bfs_fuse_ctx_t *ctx = fuse_req_userdata(request);
    fuse_reply_err(request, ctx_is_writable(ctx) ? EOPNOTSUPP : EROFS);
}

static void bfs_fuse_releasedir(fuse_req_t request, fuse_ino_t inode,
                                struct fuse_file_info *info)
{
    (void)inode;
    if (info) info->fh = 0;
    fuse_reply_err(request, 0);
}

static void bfs_fuse_init(void *userdata, struct fuse_conn_info *connection)
{
    (void)userdata;
    connection->want &= ~FUSE_CAP_WRITEBACK_CACHE;
}

static const struct fuse_lowlevel_ops bfs_fuse_operations = {
    .init = bfs_fuse_init,
    .lookup = bfs_fuse_lookup,
    .forget = bfs_fuse_forget,
    .getattr = bfs_fuse_getattr,
    .setattr = bfs_fuse_setattr,
    .readlink = bfs_fuse_readlink,
    .mknod = bfs_fuse_mknod,
    .mkdir = bfs_fuse_mkdir,
    .unlink = bfs_fuse_unlink,
    .rmdir = bfs_fuse_rmdir,
    .symlink = bfs_fuse_symlink,
    .rename = bfs_fuse_rename,
    .link = bfs_fuse_link,
    .open = bfs_fuse_open,
    .read = bfs_fuse_read,
    .write = bfs_fuse_write,
    .flush = bfs_fuse_flush,
    .release = bfs_fuse_release,
    .fsync = bfs_fuse_fsync,
    .opendir = bfs_fuse_opendir,
    .readdir = bfs_fuse_readdir,
    .releasedir = bfs_fuse_releasedir,
    .fsyncdir = bfs_fuse_fsyncdir,
    .statfs = bfs_fuse_statfs,
    .setxattr = bfs_fuse_setxattr,
    .getxattr = bfs_fuse_getxattr,
    .listxattr = bfs_fuse_listxattr,
    .removexattr = bfs_fuse_removexattr,
    .create = bfs_fuse_create,
    .fallocate = bfs_fuse_fallocate,
};

static bool select_snapshot_cb(uint32_t id, const bfs_snapshot_record_t *record, void *opaque)
{
    snapshot_selector_t *selector = opaque;
    bool matches = selector->by_id ? id == selector->id :
        strncmp((const char *)record->name, selector->name, BFS_SNAPSHOT_NAME_MAX) == 0;
    if (!matches) return true;
    selector->id = id;
    selector->record = *record;
    selector->found = true;
    return false;
}

static bool parse_u64(const char *text, uint64_t *out)
{
    if (!text || !*text || !out) return false;
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || !end || *end != '\0') return false;
    *out = value;
    return true;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s --image PATH [--offset BYTES] [--length BYTES] "
                    "[--read-write] [--snapshot NAME | --snapshot-id ID] MOUNTPOINT\n", program);
}

static bfs_err_t free_open_handles(bfs_fuse_ctx_t *ctx)
{
    bfs_err_t result = BFS_OK;
    while (ctx->handles) {
        bfs_err_t error = detach_file_handle(ctx, ctx->handles);
        if (result == BFS_OK && error != BFS_OK) result = error;
    }
    return result;
}

int main(int argc, char **argv)
{
    const char *image = NULL;
    const char *mountpoint = NULL;
    uint64_t offset = 0, length = 0;
    snapshot_selector_t selector = {0};
    bool read_write = false;
    for (int index = 1; index < argc; index++) {
        const char *arg = argv[index];
        if (strcmp(arg, "--read-write") == 0) {
            read_write = true;
        } else if ((strcmp(arg, "--image") == 0 || strcmp(arg, "--offset") == 0 ||
             strcmp(arg, "--length") == 0 || strcmp(arg, "--snapshot") == 0 ||
             strcmp(arg, "--snapshot-id") == 0) && ++index < argc) {
            const char *value = argv[index];
            if (strcmp(arg, "--image") == 0) image = value;
            else if (strcmp(arg, "--offset") == 0 && !parse_u64(value, &offset)) {
                usage(argv[0]); return 2;
            } else if (strcmp(arg, "--length") == 0 && !parse_u64(value, &length)) {
                usage(argv[0]); return 2;
            } else if (strcmp(arg, "--snapshot") == 0) {
                if (selector.by_id || selector.name || !*value ||
                    strnlen(value, BFS_SNAPSHOT_NAME_MAX) >= BFS_SNAPSHOT_NAME_MAX) {
                    usage(argv[0]); return 2;
                }
                selector.name = value;
            } else if (strcmp(arg, "--snapshot-id") == 0) {
                uint64_t id;
                if (selector.name || selector.by_id || !parse_u64(value, &id) || id == 0 || id > UINT32_MAX) {
                    usage(argv[0]); return 2;
                }
                selector.by_id = true;
                selector.id = (uint32_t)id;
            }
        } else if (arg[0] == '-') {
            usage(argv[0]);
            return 2;
        } else if (!mountpoint) {
            mountpoint = arg;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (!image || !mountpoint) {
        usage(argv[0]);
        return 2;
    }
    if (read_write && (selector.name || selector.by_id)) {
        fprintf(stderr, "bfs-fuse: snapshots are always read-only\n");
        return 2;
    }

    bfs_fuse_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.read_only = !read_write;
    bfs_posix_bio_options_t options = {
        .byte_offset = offset,
        .byte_length = length,
        .block_size = BFS_MIN_BLOCK_SIZE,
        .writable = read_write,
        .lock = true,
    };
    ctx.bio = bfs_posix_bio_open(image, &options);
    if (!ctx.bio) {
        perror("bfs-fuse: cannot open image");
        return 1;
    }
    uint64_t selected_offset, selected_length;
    bfs_superblock_t superblock;
    bfs_err_t error = bfs_posix_bio_get_range(ctx.bio, &selected_offset, &selected_length);
    if (error == BFS_OK) error = bfs_sb_probe(ctx.bio, selected_length, &superblock);
    if (error == BFS_OK)
        error = read_write ? bfs_fs_mount(&ctx.fs, ctx.bio) : bfs_fs_mount_readonly(&ctx.fs, ctx.bio);
    if (error != BFS_OK) {
        char diagnostic[BFS_FORMAT_ERROR_MAX] = {0};
        if (error == BFS_ERR_UNSUPPORTED)
            bfs_sb_describe_unsupported(&superblock, diagnostic);
        fprintf(stderr, "bfs-fuse: cannot mount %s: %s%sBFS error %d\n", image,
                diagnostic, diagnostic[0] ? "; " : "", error);
        bfs_bio_close(ctx.bio);
        return 1;
    }
    ctx.dir_tree = &ctx.fs.dir_tree;
    ctx.inode_tree = &ctx.fs.inode_tree;
    if (selector.name || selector.by_id) {
        error = bfs_snapshot_list(&ctx.fs, select_snapshot_cb, &selector);
        if (error == BFS_OK && !selector.found) error = BFS_ERR_NOTFOUND;
        if (error == BFS_OK)
            error = bfs_snapshot_open(&selector.record, ctx.bio,
                                      bfs_freespace_allocator(&ctx.fs.freespace),
                                      &ctx.snapshot_dir, &ctx.snapshot_inode);
        if (error == BFS_OK) {
            ctx.dir_tree = &ctx.snapshot_dir;
            ctx.inode_tree = &ctx.snapshot_inode;
            bfs_inode_t root;
            error = bfs_inode_read(ctx.inode_tree, BFS_ROOT_INO, &root);
            if (error == BFS_OK && bfs_be32(root.type) != BFS_INODE_DIR) error = BFS_ERR_CORRUPT;
            uint32_t root_inode = 0, root_type = 0;
            if (error == BFS_OK)
                error = bfs_dir_lookup(ctx.dir_tree, 0, "/", 1, &root_inode, &root_type);
            if (error == BFS_OK &&
                (root_inode != BFS_ROOT_INO || root_type != BFS_INODE_DIR))
                error = BFS_ERR_CORRUPT;
        }
        if (error != BFS_OK) {
            fprintf(stderr, "bfs-fuse: snapshot selection failed: BFS error %d\n", error);
            bfs_fs_unmount(&ctx.fs);
            bfs_bio_close(ctx.bio);
            return 1;
        }
    }

    char *fuse_argv[] = { argv[0], "-o",
        read_write ? "default_permissions,nodev,nosuid" : "ro,default_permissions,nodev,nosuid", NULL };
    struct fuse_args arguments = FUSE_ARGS_INIT(3, fuse_argv);
    struct fuse_session *session = fuse_session_new(&arguments, &bfs_fuse_operations,
                                                    sizeof(bfs_fuse_operations), &ctx);
    fuse_opt_free_args(&arguments);
    if (!session) {
        fprintf(stderr, "bfs-fuse: cannot initialize libfuse\n");
        bfs_fs_unmount(&ctx.fs);
        bfs_bio_close(ctx.bio);
        return 1;
    }
    if (fuse_session_mount(session, mountpoint) != 0) {
        fprintf(stderr, "bfs-fuse: cannot mount %s\n", mountpoint);
        fuse_session_destroy(session);
        bfs_fs_unmount(&ctx.fs);
        bfs_bio_close(ctx.bio);
        return 1;
    }
    int result = fuse_session_loop(session);
    fuse_session_unmount(session);
    fuse_session_destroy(session);
    if (free_open_handles(&ctx) != BFS_OK) result = 1;
    bfs_posix_bio_stats_t stats;
    if (bfs_posix_bio_get_stats(ctx.bio, &stats) != BFS_OK ||
        (ctx.read_only && (stats.write_calls != 0 || stats.sync_calls != 0))) {
        fprintf(stderr, "bfs-fuse: read-only transport observed a write or sync\n");
        result = 1;
    }
    if (bfs_fs_unmount(&ctx.fs) != BFS_OK) result = 1;
    bfs_bio_close(ctx.bio);
    return result == 0 ? 0 : 1;
}
