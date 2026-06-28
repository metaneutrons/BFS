/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — AmigaOS DOS packet handler
 *
 * Main entry point and DOS packet dispatcher.
 * Receives packets from AmigaDOS, dispatches to BFS core.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/alerts.h>
#include <exec/interrupts.h>
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <dos/notify.h>
#include <dos/exall.h>
#include <devices/trackdisk.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

#include "bfs_fs.h"
#include "bfs_cache.h"
#include "bfs_file.h"
#include "bfs_dir.h"
#include "bfs_inode.h"
#include "bfs_snapshot.h"
#include "amiga_bio.h"

/* ── Packet number constants ────────────────────────────────── */
/* Only define if not already provided by NDK headers */
#ifndef ACTION_CURRENT_VOLUME
#define ACTION_CURRENT_VOLUME      7
#endif
#ifndef ACTION_INFO
#define ACTION_INFO               26
#endif
#ifndef ACTION_SET_DATE
#define ACTION_SET_DATE           34
#endif
#ifndef ACTION_FORMAT
#define ACTION_FORMAT           1020
#endif
#ifndef ACTION_MAKE_LINK
#define ACTION_MAKE_LINK        1021
#endif
#ifndef ACTION_SET_FILE_SIZE
#define ACTION_SET_FILE_SIZE    1022
#endif
#ifndef ACTION_WRITE_PROTECT
#define ACTION_WRITE_PROTECT    1023
#endif
#ifndef ACTION_READ_LINK
#define ACTION_READ_LINK        1024
#endif
#ifndef ACTION_FH_FROM_LOCK
#define ACTION_FH_FROM_LOCK     1026
#endif
#ifndef ACTION_FLUSH
#define ACTION_FLUSH            1027
#endif
#ifndef ACTION_CHANGE_MODE
#define ACTION_CHANGE_MODE      1028
#endif
#ifndef ACTION_COPY_DIR_FH
#define ACTION_COPY_DIR_FH      1030
#endif
#ifndef ACTION_PARENT_FH
#define ACTION_PARENT_FH        1031
#endif
#ifndef ACTION_EXAMINE_ALL
#define ACTION_EXAMINE_ALL      1033
#endif
#ifndef ACTION_EXAMINE_FH
#define ACTION_EXAMINE_FH       1034
#endif
#ifndef ACTION_SET_OWNER
#define ACTION_SET_OWNER        1036
#endif
#ifndef ACTION_SET_COMMENT
#define ACTION_SET_COMMENT        28
#endif

/* 64-bit packets (MorphOS convention) */
#define ACTION_SEEK64                26400
#define ACTION_SET_FILE_SIZE64       26401
#define ACTION_EXAMINE_OBJECT64      26407
#define ACTION_GET_FILE_POSITION64   26408
#define ACTION_EXAMINE_NEXT64        26409
#define ACTION_GET_FILE_SIZE64       26410

/* 64-bit packets (OS4 convention) */
#define ACTION_CHANGE_FILE_POSITION64 8001
#define ACTION_CHANGE_FILE_SIZE64     8003

/* CHANGE_MODE types */
#ifndef CHANGE_FH
#define CHANGE_FH   1
#endif
#ifndef CHANGE_LOCK
#define CHANGE_LOCK 2
#endif

/* Handler global state */
struct bfs_notify {
    struct bfs_notify *next;
    struct NotifyRequest *nr;
};

struct bfs_handler {
    struct ExecBase *SysBase;
    struct DosLibrary *DOSBase;
    struct MsgPort *msgport;
    struct MsgPort *devport;
    struct IOExtTD *request;
    struct DeviceNode *devnode;
    struct DosEnvec *dosenvec;
    bfs_fs_t fs;
    bfs_cache_t cache;
    bool dirty;
    bool write_protected;
    struct DosList *volnode;
    struct bfs_notify *notify_list;
    BYTE diskchange_sig;
    struct IOExtTD *diskchange_req;
    struct Interrupt *diskchange_int;

    /* Mounted snapshots (read-only volumes) */
    #define BFS_MAX_SNAP_MOUNTS 4
    struct {
        struct DosList *volnode;
        bfs_dir_tree_t dir_tree;
        bfs_btree_t inode_tree;
        uint64_t txn_id;
        bool active;
    } snap_mounts[BFS_MAX_SNAP_MOUNTS];
};

/* Lock structure — stored as BPTR in FileLock */
typedef struct {
    struct FileLock fl;
    uint32_t ino;
    uint32_t type; /* BFS_INODE_FILE or BFS_INODE_DIR */
    uint32_t parent_ino;
} bfs_lock_t;

/* Amiga library bases — set globally for proto headers */
struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

/* ── Packet helpers ────────────────────────────────────────── */

static struct DosPacket *GetPacket(struct MsgPort *port)
{
    struct Message *msg = GetMsg(port);
    if (!msg) return NULL;
    return (struct DosPacket *)msg->mn_Node.ln_Name;
}

static void ReplyPacket(struct DosPacket *pkt, struct bfs_handler *h)
{
    struct MsgPort *replyport = pkt->dp_Port;
    pkt->dp_Link->mn_Node.ln_Name = (UBYTE *)pkt;
    pkt->dp_Link->mn_Node.ln_Succ = NULL;
    pkt->dp_Link->mn_Node.ln_Pred = NULL;
    pkt->dp_Port = h->msgport;
    PutMsg(replyport, pkt->dp_Link);
}

/* ── Lock helpers ──────────────────────────────────────────── */

static bfs_lock_t *MakeLock(struct bfs_handler *h, uint32_t ino,
                             uint32_t type, LONG access, uint32_t parent_ino)
{
    bfs_lock_t *lk = (bfs_lock_t *)AllocVec(sizeof(bfs_lock_t), MEMF_CLEAR);
    if (!lk) return NULL;
    lk->ino = ino;
    lk->type = type;
    lk->parent_ino = parent_ino;
    lk->fl.fl_Access = access;
    lk->fl.fl_Task = h->msgport;
    lk->fl.fl_Volume = MKBADDR(h->devnode);
    lk->fl.fl_Key = ino;
    return lk;
}

static uint32_t LockIno(BPTR lock)
{
    if (!lock) return BFS_ROOT_INO;
    bfs_lock_t *lk = (bfs_lock_t *)BADDR(lock);
    return lk->ino;
}

/* ── BSTR / path helpers ──────────────────────────────────── */

/* Extract parent inode and filename from a lock + BSTR name.
 * Copies the filename into 'namebuf' (null-terminated).
 * Returns the parent inode number. */
static uint32_t ResolvePath(BPTR lock, BPTR bstr_name,
                            char *namebuf, uint8_t *namelen_out,
                            struct bfs_handler *h)
{
    uint32_t parent_ino = LockIno(lock);
    UBYTE *bstr = (UBYTE *)BADDR(bstr_name);
    if (!bstr) { *namelen_out = 0; namebuf[0] = 0; return parent_ino; }
    uint8_t len = bstr[0];
    char *name = (char *)&bstr[1];

    /* Skip volume prefix (e.g. "VOL:") — resets to root */
    for (uint8_t i = 0; i < len; i++) {
        if (name[i] == ':') {
            name += i + 1;
            len -= i + 1;
            parent_ino = BFS_ROOT_INO;
            break;
        }
    }

    /* Resolve path components separated by '/' */
    while (len > 0) {
        /* Find next separator */
        uint8_t comp_len = 0;
        while (comp_len < len && name[comp_len] != '/') comp_len++;

        if (comp_len == 0) {
            /* Leading or double '/' = parent directory */
            uint32_t par_ino, par_type;
            if (bfs_dir_lookup(&h->fs.dir_tree, parent_ino, "..", 2, &par_ino, &par_type) == BFS_OK)
                parent_ino = par_ino;
            else
                parent_ino = BFS_ROOT_INO;
            name++;
            len--;
            continue;
        }

        /* Check if this is the last component (the filename) */
        uint8_t remaining = len - comp_len;
        if (remaining == 0 || (remaining == 1 && name[comp_len] == '/')) {
            /* Last component — this is the filename */
            if (comp_len > BFS_NAME_MAX) comp_len = BFS_NAME_MAX;
            memcpy(namebuf, name, comp_len);
            namebuf[comp_len] = 0;
            *namelen_out = comp_len;
            return parent_ino;
        }

        /* Intermediate component — must be a directory, resolve it */
        uint32_t ino, type;
        if (bfs_dir_lookup(&h->fs.dir_tree, parent_ino, name, comp_len, &ino, &type) != BFS_OK)
            break; /* path not found — return what we have, caller will get NOTFOUND */
        if (type != BFS_INODE_DIR)
            break; /* not a directory — path invalid */

        parent_ino = ino;
        name += comp_len + 1; /* skip component + '/' */
        len -= comp_len + 1;
    }

    /* Final component (or empty path) */
    if (len > BFS_NAME_MAX) len = BFS_NAME_MAX;
    memcpy(namebuf, name, len);
    namebuf[len] = 0;
    *namelen_out = len;
    return parent_ino;
}

/* ── BFS error to AmigaDOS error mapping ─────────────────── */

static LONG Pfs4ToDosError(bfs_err_t err)
{
    switch (err) {
    case BFS_OK:          return 0;
    case BFS_ERR_NOTFOUND: return ERROR_OBJECT_NOT_FOUND;
    case BFS_ERR_EXISTS:   return ERROR_OBJECT_EXISTS;
    case BFS_ERR_NOSPC:    return ERROR_DISK_FULL;
    case BFS_ERR_NOTEMPTY: return ERROR_DIRECTORY_NOT_EMPTY;
    case BFS_ERR_NOMEM:    return ERROR_NO_FREE_STORE;
    case BFS_ERR_INVAL:    return ERROR_BAD_NUMBER;
    default:                return ERROR_SEEK_ERROR;
    }
}

/* ── Directory scan context for EXAMINE_NEXT ──────────────── */

typedef struct {
    uint32_t skip_count; /* number of entries to skip (cursor position) */
    uint32_t seen;       /* entries seen so far */
    char *name_out;
    uint8_t name_len;
    uint32_t ino_out;
    uint32_t type_out;
    bool got_entry;
} exam_next_ctx_t;

static bool exam_next_cb(const char *name, uint8_t name_len,
                         uint32_t inode_nr, uint32_t entry_type, void *ctx)
{
    exam_next_ctx_t *ec = (exam_next_ctx_t *)ctx;

    if (ec->seen < ec->skip_count) {
        ec->seen++;
        return true; /* skip */
    }

    /* This is the next entry */
    ec->name_len = name_len;
    memcpy(ec->name_out, name, name_len);
    ec->name_out[name_len] = 0;
    ec->ino_out = inode_nr;
    ec->type_out = entry_type;
    ec->got_entry = true;
    return false; /* stop */
}

/* ── Fill FileInfoBlock ───────────────────────────────────── */

static void FillFib(struct FileInfoBlock *fib, const char *name, uint8_t name_len,
                    uint32_t ino, uint32_t type, uint64_t size, uint32_t prot,
                    const bfs_inode_t *inode)
{
    memset(fib, 0, sizeof(*fib));
    fib->fib_DiskKey = ino;
    fib->fib_DirEntryType = (type == BFS_INODE_DIR) ? ST_USERDIR : ST_FILE;
    fib->fib_EntryType = fib->fib_DirEntryType;
    fib->fib_Protection = prot;
    fib->fib_Size = (LONG)size;
    fib->fib_NumBlocks = (LONG)((size + 511) / 512);

    /* BSTR filename in fib_FileName */
    if (name_len > 107) name_len = 107;
    fib->fib_FileName[0] = name_len;
    memcpy(&fib->fib_FileName[1], name, name_len);

    /* Date (modification time) */
    if (inode) {
        fib->fib_Date.ds_Days = bfs_be16(inode->modify_days);
        fib->fib_Date.ds_Minute = bfs_be16(inode->modify_mins);
        fib->fib_Date.ds_Tick = bfs_be16(inode->modify_ticks);
        fib->fib_OwnerUID = bfs_be16(inode->uid);
        fib->fib_OwnerGID = bfs_be16(inode->gid);
    }
}

/* ── Notification helper ───────────────────────────────────── */

static void SendNotifications(struct bfs_handler *h)
{
    struct bfs_notify *n = h->notify_list;
    while (n) {
        if (n->nr->nr_Flags & NRF_SEND_SIGNAL) {
            struct Task *task = n->nr->nr_stuff.nr_Signal.nr_Task;
            ULONG sigbit = n->nr->nr_stuff.nr_Signal.nr_SignalNum;
            Signal(task, 1UL << sigbit);
        }
        n = n->next;
    }
}

/* ── Packet dispatch ───────────────────────────────────────── */

/* DEBUG: ring buffer of last 64 packet types */
static LONG pkt_log[64];
static int pkt_log_idx = 0;

typedef struct {
    uint32_t last_id;
    uint32_t found_id;
    bfs_snapshot_record_t rec;
    bool found;
} snap_next_ctx_t;

static bool snap_next_cb(uint32_t id, const bfs_snapshot_record_t *rec, void *ctx)
{
    snap_next_ctx_t *sn = (snap_next_ctx_t *)ctx;
    if (id > sn->last_id) {
        sn->found_id = id;
        sn->rec = *rec;
        sn->found = true;
        return false;
    }
    return true;
}

/* ── Directory scan context for EXAMINE_ALL ───────────────── */

typedef struct {
    struct bfs_handler *h;
    struct ExAllControl *eac;
    UBYTE *buffer;
    UBYTE *pos;
    UBYTE *end;
    LONG type;
    uint32_t skip_count;
    uint32_t seen;
    struct ExAllData *last_ead;
    bool overflow;
} exall_optimized_ctx_t;

/* Stateful callback for single-pass linear directory scanning.
 * Manages skip_count, pattern matching, and user buffer overflow. */
static bool exall_optimized_cb(const char *name, uint8_t name_len,
                               uint32_t inode_nr, uint32_t entry_type, void *ctx)
{
    exall_optimized_ctx_t *ec = (exall_optimized_ctx_t *)ctx;

    if (ec->seen < ec->skip_count) {
        ec->seen++;
        return true;
    }

    char namebuf[BFS_NAME_MAX + 1];
    memcpy(namebuf, name, name_len); namebuf[name_len] = 0;

    /* Pattern match */
    if (ec->eac->eac_MatchString && !MatchPatternNoCase(ec->eac->eac_MatchString, namebuf)) {
        ec->seen++;
        ec->skip_count++;
        ec->eac->eac_LastKey = (ULONG)ec->skip_count;
        return true;
    }

    /* Calculate entry size */
    LONG entry_size = (LONG)sizeof(struct ExAllData) + name_len + 1;
    if (ec->type >= ED_COMMENT) entry_size += 80;
    entry_size = (entry_size + 3) & ~3;

    if (ec->pos + entry_size > ec->end) {
        ec->overflow = true;
        return false; /* stop scan */
    }

    /* Read inode for metadata fields */
    bfs_inode_t inode;
    uint64_t fsize = 0; uint32_t prot = 0;
    bool have_inode = (ec->type >= ED_SIZE && bfs_inode_read(&ec->h->fs.inode_tree, inode_nr, &inode) == BFS_OK);
    if (have_inode) {
        fsize = ((uint64_t)bfs_be32(inode.size_hi) << 32) | bfs_be32(inode.size_lo);
        prot = bfs_be32(inode.protection);
    }

    struct ExAllData *ead = (struct ExAllData *)ec->pos;
    memset(ead, 0, entry_size);
    UBYTE *str = ec->pos + sizeof(struct ExAllData);
    memcpy(str, namebuf, name_len); str[name_len] = 0;
    ead->ed_Name = str; str += name_len + 1;
    if (ec->type >= ED_TYPE) ead->ed_Type = (entry_type == BFS_INODE_DIR) ? ST_USERDIR : ST_FILE;
    if (ec->type >= ED_SIZE) ead->ed_Size = (ULONG)fsize;
    if (ec->type >= ED_PROTECTION) ead->ed_Prot = prot;
    if (ec->type >= ED_DATE && have_inode) {
        ead->ed_Days = bfs_be16(inode.modify_days);
        ead->ed_Mins = bfs_be16(inode.modify_mins);
        ead->ed_Ticks = bfs_be16(inode.modify_ticks);
    }
    if (ec->type >= ED_COMMENT) {
        char cbuf[80]; cbuf[0] = 0;
        bfs_fs_get_comment(&ec->h->fs, inode_nr, cbuf, 79);
        int cl = 0; while (cbuf[cl]) cl++;
        memcpy(str, cbuf, cl); str[cl] = 0;
        ead->ed_Comment = str;
    }

    ead->ed_Next = NULL;
    if (ec->last_ead) ec->last_ead->ed_Next = ead;
    ec->last_ead = ead;
    ec->pos += entry_size;
    ec->eac->eac_Entries++;
    ec->seen++;
    ec->skip_count++;
    ec->eac->eac_LastKey = (ULONG)ec->skip_count;

    return true;
}

static void HandlePacket(struct DosPacket *pkt, struct bfs_handler *h)
{
    LONG res1 = DOSFALSE;
    LONG res2 = ERROR_ACTION_NOT_KNOWN;

    pkt_log[pkt_log_idx++ & 63] = pkt->dp_Type;

    /* Note: if !h->fs.mounted, most operations will fail gracefully
     * (btree ops return NOTFOUND on NULL root). ACTION_FORMAT works
     * regardless of mount state. */

    switch (pkt->dp_Type) {

    /* ── LOCATE_OBJECT ─────────────────────────────────────── */
    case ACTION_LOCATE_OBJECT: {
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino = ResolvePath((BPTR)pkt->dp_Arg1, (BPTR)pkt->dp_Arg2, namebuf, &len, h);
        uint32_t ino, type;

        if (len == 0) {
            /* Lock on parent directory itself */
            ino = parent_ino;
            type = BFS_INODE_DIR;
        } else if (bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len,
                                   &ino, &type) != BFS_OK) {
            res2 = ERROR_OBJECT_NOT_FOUND;
            break;
        }

        bfs_lock_t *lk = MakeLock(h, ino, type, pkt->dp_Arg3, parent_ino);
        if (!lk) { res2 = ERROR_NO_FREE_STORE; break; }
        res1 = (LONG)MKBADDR(lk);
        res2 = 0;
        break;
    }

    /* ── FREE_LOCK ─────────────────────────────────────────── */
    case ACTION_FREE_LOCK: {
        bfs_lock_t *lk = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        if (lk) FreeVec(lk);
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── FINDINPUT / FINDOUTPUT / FINDUPDATE ───────────────── */
    case ACTION_FINDINPUT:
    case ACTION_FINDOUTPUT:
    case ACTION_FINDUPDATE: {
        if (h->write_protected && pkt->dp_Type != ACTION_FINDINPUT) {
            res2 = ERROR_DISK_WRITE_PROTECTED; break;
        }
        struct FileHandle *fh = (struct FileHandle *)BADDR(pkt->dp_Arg1);
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino = ResolvePath((BPTR)pkt->dp_Arg2, (BPTR)pkt->dp_Arg3, namebuf, &len, h);
        uint32_t ino, type;
        bfs_err_t err;

        err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len,
                              &ino, &type);

        if (err == BFS_ERR_NOTFOUND) {
            if (pkt->dp_Type == ACTION_FINDOUTPUT ||
                pkt->dp_Type == ACTION_FINDUPDATE) {
                /* Create the file */
                if (bfs_fs_reserve(&h->fs, 5) != BFS_OK) { res2 = ERROR_DISK_FULL; break; }
                err = bfs_fs_create_file(&h->fs, parent_ino, namebuf, len, &ino);
                if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
                type = BFS_INODE_FILE;
                /* Set creation/modification timestamp */
                { struct DateStamp ds; DateStamp(&ds);
                  bfs_inode_t ni;
                  if (bfs_inode_read(&h->fs.inode_tree, ino, &ni) == BFS_OK) {
                      ni.create_days = bfs_be16((uint16_t)ds.ds_Days);
                      ni.create_mins = bfs_be16((uint16_t)ds.ds_Minute);
                      ni.create_ticks = bfs_be16((uint16_t)ds.ds_Tick);
                      ni.modify_days = ni.create_days;
                      ni.modify_mins = ni.create_mins;
                      ni.modify_ticks = ni.create_ticks;
                      bfs_inode_write(&h->fs.inode_tree, ino, &ni);
                  }
                }
                h->dirty = true;
            } else {
                res2 = ERROR_OBJECT_NOT_FOUND;
                break;
            }
        } else if (err != BFS_OK) {
            res2 = Pfs4ToDosError(err);
            break;
        }

        if (type != BFS_INODE_FILE && type != BFS_INODE_HARDLINK) {
            res2 = ERROR_OBJECT_WRONG_TYPE;
            break;
        }

        /* For FINDOUTPUT on existing file, truncate */
        bfs_file_t *f = (bfs_file_t *)AllocVec(sizeof(bfs_file_t), MEMF_CLEAR);
        if (!f) { res2 = ERROR_NO_FREE_STORE; break; }

        if (bfs_file_open(f, &h->fs, ino) != BFS_OK) {
            FreeVec(f);
            res2 = ERROR_SEEK_ERROR;
            break;
        }

        if (pkt->dp_Type == ACTION_FINDOUTPUT && err == BFS_OK) {
            /* Existing file opened for output — truncate to 0 */
            bfs_file_truncate(f, 0);
        }

        fh->fh_Arg1 = (LONG)f;
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── READ ──────────────────────────────────────────────── */
    case ACTION_READ: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        void *buf = (void *)pkt->dp_Arg2;
        LONG len = pkt->dp_Arg3;

        int32_t n = bfs_file_read(f, buf, (uint32_t)len);
        if (n < 0) {
            res1 = -1;
            res2 = ERROR_SEEK_ERROR;
        } else {
            res1 = n;
            res2 = 0;
        }
        break;
    }

    /* ── WRITE ─────────────────────────────────────────────── */
    case ACTION_WRITE: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        void *buf = (void *)pkt->dp_Arg2;
        LONG len = pkt->dp_Arg3;

        int32_t n = bfs_file_write(f, buf, (uint32_t)len);
        if (n < 0) {
            res1 = -1;
            res2 = ERROR_DISK_FULL;
        } else {
            res1 = n;
            res2 = 0;
            h->dirty = true;
            SendNotifications(h);
        }
        break;
    }

    /* ── SEEK ──────────────────────────────────────────────── */
    case ACTION_SEEK: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        LONG offset = pkt->dp_Arg2;
        LONG mode = pkt->dp_Arg3;

        /* Return old position */
        LONG old_pos = (LONG)f->offset;

        /* Amiga seek modes: -1=BEGINNING, 0=CURRENT, 1=END */
        int bfs_mode;
        switch (mode) {
        case OFFSET_BEGINNING: bfs_mode = BFS_SEEK_SET; break;
        case OFFSET_END:       bfs_mode = BFS_SEEK_END; break;
        default:               bfs_mode = BFS_SEEK_CUR; break;
        }

        int64_t new_pos = bfs_file_seek(f, (int64_t)offset, bfs_mode);
        if (new_pos < 0) {
            res1 = -1;
            res2 = ERROR_SEEK_ERROR;
        } else {
            res1 = old_pos;
            res2 = 0;
        }
        break;
    }

    /* ── END (close file) ──────────────────────────────────── */
    case ACTION_END: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        if (h->dirty) {
            bfs_fs_sync(&h->fs);
            h->dirty = false;
        }
        if (f) FreeVec(f);
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── CREATE_DIR ────────────────────────────────────────── */
    case ACTION_CREATE_DIR: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        if (bfs_fs_reserve(&h->fs, 5) != BFS_OK) { res2 = ERROR_DISK_FULL; break; }
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino = ResolvePath((BPTR)pkt->dp_Arg1, (BPTR)pkt->dp_Arg2, namebuf, &len, h);
        uint32_t ino;
        bfs_err_t err = bfs_fs_mkdir(&h->fs, parent_ino, namebuf, len, &ino);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        bfs_lock_t *lk = MakeLock(h, ino, BFS_INODE_DIR, SHARED_LOCK, parent_ino);
        if (!lk) { res2 = ERROR_NO_FREE_STORE; break; }
        res1 = (LONG)MKBADDR(lk);
        res2 = 0;
        h->dirty = true;
        SendNotifications(h);
        break;
    }

    /* ── DELETE_OBJECT ─────────────────────────────────────── */
    case ACTION_DELETE_OBJECT: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        if (bfs_fs_reserve(&h->fs, 10) != BFS_OK) { res2 = ERROR_DISK_FULL; break; }
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino = ResolvePath((BPTR)pkt->dp_Arg1, (BPTR)pkt->dp_Arg2, namebuf, &len, h);
        /* Look up to determine type */
        uint32_t ino, type;
        bfs_err_t err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len,
                                         &ino, &type);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        if (type == BFS_INODE_DIR)
            err = bfs_fs_rmdir(&h->fs, parent_ino, namebuf, len);
        else
            err = bfs_fs_delete_file(&h->fs, parent_ino, namebuf, len);

        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        res1 = DOSTRUE;
        res2 = 0;
        h->dirty = true;
        SendNotifications(h);
        err = bfs_fs_sync(&h->fs);
        if (err != BFS_OK) { res1 = DOSFALSE; res2 = Pfs4ToDosError(err); break; }
        h->dirty = false;
        break;
    }

    /* ── RENAME_OBJECT ─────────────────────────────────────── */
    case ACTION_RENAME_OBJECT: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        if (bfs_fs_reserve(&h->fs, 8) != BFS_OK) { res2 = ERROR_DISK_FULL; break; }
        char old_name[BFS_NAME_MAX + 1], new_name[BFS_NAME_MAX + 1];
        uint8_t old_len, new_len;
        uint32_t old_parent = ResolvePath((BPTR)pkt->dp_Arg1, (BPTR)pkt->dp_Arg2, old_name, &old_len, h);
        uint32_t new_parent = ResolvePath((BPTR)pkt->dp_Arg3, (BPTR)pkt->dp_Arg4, new_name, &new_len, h);

        bfs_err_t err = bfs_fs_rename(&h->fs, old_parent, old_name, old_len,
                                        new_parent, new_name, new_len);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        res1 = DOSTRUE;
        res2 = 0;
        h->dirty = true;
        SendNotifications(h);
        break;
    }

    /* ── EXAMINE_OBJECT ────────────────────────────────────── */
    case ACTION_EXAMINE_OBJECT: {
        bfs_lock_t *lk = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        struct FileInfoBlock *fib = (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
        if (!lk || !fib) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }

        uint64_t size = 0;
        uint32_t prot = 0;
        bfs_inode_t inode;
        if (bfs_inode_read(&h->fs.inode_tree, lk->ino, &inode) == BFS_OK) {
            size = ((uint64_t)bfs_be32(inode.size_hi) << 32) | bfs_be32(inode.size_lo);
            prot = bfs_be32(inode.protection);
        }

        FillFib(fib, "", 0, lk->ino, lk->type, size, prot, &inode);
        /* Read file comment */
        { char cb[80];
          if (bfs_fs_get_comment(&h->fs, lk->ino, cb, 79) == BFS_OK) {
              int cl = 0; while (cb[cl]) cl++;
              fib->fib_Comment[0] = cl; memcpy(&fib->fib_Comment[1], cb, cl);
          }
        }
        /* Store scan index 0 in fib_DiskKey for EXAMINE_NEXT */
        fib->fib_DiskKey = 0;
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── EXAMINE_NEXT ──────────────────────────────────────── */
    case ACTION_EXAMINE_NEXT: {
        bfs_lock_t *lk = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        struct FileInfoBlock *fib = (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
        if (!lk || !fib) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }

        char namebuf[BFS_NAME_MAX + 1];
        exam_next_ctx_t ctx;
        ctx.skip_count = (uint32_t)fib->fib_DiskKey;
        ctx.seen = 0;
        ctx.name_out = namebuf;
        ctx.got_entry = false;

        bfs_dir_scan(&h->fs.dir_tree, lk->ino, exam_next_cb, &ctx);

        if (!ctx.got_entry) {
            res2 = ERROR_NO_MORE_ENTRIES;
            break;
        }

        /* Read inode for size, protection, dates */
        bfs_inode_t en_inode;
        uint64_t en_size = 0; uint32_t en_prot = 0;
        if (bfs_inode_read(&h->fs.inode_tree, ctx.ino_out, &en_inode) == BFS_OK) {
            en_size = ((uint64_t)bfs_be32(en_inode.size_hi) << 32) | bfs_be32(en_inode.size_lo);
            en_prot = bfs_be32(en_inode.protection);
        } else {
            memset(&en_inode, 0, sizeof(en_inode));
        }
        FillFib(fib, namebuf, ctx.name_len, ctx.ino_out, ctx.type_out, en_size, en_prot, &en_inode);
        fib->fib_DiskKey = (LONG)(ctx.skip_count + 1);
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── SET_PROTECT ───────────────────────────────────────── */
    case ACTION_SET_PROTECT: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        /* dp_Arg1=unused, dp_Arg2=lock, dp_Arg3=name(BSTR), dp_Arg4=mask */
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino = ResolvePath((BPTR)pkt->dp_Arg2, (BPTR)pkt->dp_Arg3, namebuf, &len, h);
        uint32_t ino, type;
        bfs_err_t err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len,
                                         &ino, &type);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        bfs_inode_t inode;
        err = bfs_inode_read(&h->fs.inode_tree, ino, &inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        inode.protection = bfs_be32((uint32_t)pkt->dp_Arg4);
        err = bfs_inode_write(&h->fs.inode_tree, ino, &inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        res1 = DOSTRUE;
        res2 = 0;
        h->dirty = true;
        break;
    }

    /* ── SAME_LOCK ─────────────────────────────────────────── */
    case ACTION_SAME_LOCK: {
        uint32_t ino1 = LockIno((BPTR)pkt->dp_Arg1);
        uint32_t ino2 = LockIno((BPTR)pkt->dp_Arg2);
        res1 = (ino1 == ino2) ? DOSTRUE : DOSFALSE;
        res2 = 0;
        break;
    }

    /* ── IS_FILESYSTEM ─────────────────────────────────────── */
    case ACTION_IS_FILESYSTEM:
        res1 = DOSTRUE;
        res2 = 0;
        break;

    /* ── DISK_INFO ─────────────────────────────────────────── */
    case ACTION_DISK_INFO: {
        struct InfoData *id = (struct InfoData *)BADDR(pkt->dp_Arg1);
        if (id) {
            bfs_bio_t *bio = (bfs_bio_t *)(h + 1);
            memset(id, 0, sizeof(*id));
            id->id_NumSoftErrors = 0;
            id->id_UnitNumber = 0;
            id->id_DiskState = h->fs.mounted ?
                (h->write_protected ? ID_WRITE_PROTECTED : ID_VALIDATED) :
                ID_VALIDATING;
            id->id_NumBlocks = bio->block_count;
            id->id_NumBlocksUsed = h->fs.mounted ?
                (bio->block_count - h->fs.freespace.total_free) : 0;
            id->id_BytesPerBlock = bio->block_size;
            id->id_DiskType = h->fs.mounted ? BFS_SB_MAGIC : ID_UNREADABLE_DISK;
            id->id_VolumeNode = MKBADDR(h->volnode);
            id->id_InUse = DOSTRUE;
            id->id_InUse = DOSTRUE;
            res1 = DOSTRUE;
            res2 = 0;
        }
        break;
    }

    /* ── PARENT ────────────────────────────────────────────── */
    case ACTION_PARENT: {
        bfs_lock_t *src = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        uint32_t src_ino = src ? src->ino : BFS_ROOT_INO;

        if (src_ino == BFS_ROOT_INO) {
            res1 = 0; /* NULL lock = root has no parent */
            res2 = 0;
            break;
        }

        uint32_t par = src ? src->parent_ino : BFS_ROOT_INO;
        bfs_lock_t *lk = MakeLock(h, par, BFS_INODE_DIR, SHARED_LOCK, BFS_ROOT_INO);
        if (!lk) { res2 = ERROR_NO_FREE_STORE; break; }
        res1 = (LONG)MKBADDR(lk);
        res2 = 0;
        break;
    }

    /* ── INHIBIT ───────────────────────────────────────────── */
    case ACTION_INHIBIT:
        res1 = DOSTRUE;
        res2 = 0;
        break;

    /* ── ACTION_MAKE_LINK (hard/soft links) ────────────────── */
    case ACTION_MAKE_LINK: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino = ResolvePath((BPTR)pkt->dp_Arg1, (BPTR)pkt->dp_Arg2, namebuf, &len, h);
        LONG soft_flag = pkt->dp_Arg4;

        if (soft_flag == 0) {
            uint32_t target_ino = LockIno((BPTR)pkt->dp_Arg3);
            bfs_err_t err = bfs_fs_make_hardlink(&h->fs, parent_ino, namebuf, len, target_ino);
            if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        } else {
            UBYTE *bpath = (UBYTE *)BADDR(pkt->dp_Arg3);
            uint8_t plen = bpath[0];
            bfs_err_t err = bfs_fs_make_softlink(&h->fs, parent_ino, namebuf, len,
                                                   (const char *)&bpath[1], plen);
            if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        }
        res1 = DOSTRUE;
        res2 = 0;
        h->dirty = true;
        SendNotifications(h);
        break;
    }

    /* ── ACTION_READ_LINK ──────────────────────────────────── */
    case ACTION_READ_LINK: {
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino = ResolvePath((BPTR)pkt->dp_Arg1, (BPTR)pkt->dp_Arg2, namebuf, &len, h);
        uint32_t ino, type;
        bfs_err_t err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len, &ino, &type);
        if (err != BFS_OK || type != BFS_INODE_SOFTLINK) { res2 = ERROR_OBJECT_NOT_FOUND; break; }

        bfs_file_t f;
        if (bfs_file_open(&f, &h->fs, ino) != BFS_OK) { res2 = ERROR_SEEK_ERROR; break; }
        char *buf = (char *)pkt->dp_Arg3;
        LONG bufsize = pkt->dp_Arg4;
        int32_t n = bfs_file_read(&f, buf, (uint32_t)(bufsize - 1));
        if (n < 0) { res2 = ERROR_SEEK_ERROR; break; }
        buf[n] = 0;
        res1 = n;
        res2 = 0;
        break;
    }

    /* ── ACTION_SET_COMMENT ────────────────────────────────── */
    case ACTION_SET_COMMENT: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino = ResolvePath((BPTR)pkt->dp_Arg2, (BPTR)pkt->dp_Arg3, namebuf, &len, h);
        uint32_t ino, type;
        bfs_err_t err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len, &ino, &type);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        UBYTE *bcomment = (UBYTE *)BADDR(pkt->dp_Arg4);
        uint8_t clen = bcomment[0];
        err = bfs_fs_set_comment(&h->fs, ino, (const char *)&bcomment[1], clen);
        if (err != BFS_OK && err != BFS_ERR_NOTFOUND) { res2 = Pfs4ToDosError(err); break; }
        res1 = DOSTRUE;
        res2 = 0;
        h->dirty = true;
        break;
    }

    /* ── ACTION_SET_FILE_SIZE ──────────────────────────────── */
    case ACTION_SET_FILE_SIZE: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        LONG offset = pkt->dp_Arg2;
        LONG mode = pkt->dp_Arg3;

        int bfs_mode;
        switch (mode) {
        case OFFSET_BEGINNING: bfs_mode = BFS_SEEK_SET; break;
        case OFFSET_END:       bfs_mode = BFS_SEEK_END; break;
        default:               bfs_mode = BFS_SEEK_CUR; break;
        }
        int64_t new_size = bfs_file_seek(f, (int64_t)offset, bfs_mode);
        if (new_size < 0) { res2 = ERROR_SEEK_ERROR; break; }
        bfs_err_t err = bfs_file_truncate(f, (uint64_t)new_size);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        res1 = (LONG)new_size;
        res2 = 0;
        h->dirty = true;
        break;
    }

    /* ── ACTION_COPY_DIR ───────────────────────────────────── */
    case ACTION_COPY_DIR: {
        bfs_lock_t *src = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        if (!src) {
            bfs_lock_t *lk = MakeLock(h, BFS_ROOT_INO, BFS_INODE_DIR, SHARED_LOCK, 0);
            if (!lk) { res2 = ERROR_NO_FREE_STORE; break; }
            res1 = (LONG)MKBADDR(lk);
        } else {
            bfs_lock_t *lk = MakeLock(h, src->ino, src->type, src->fl.fl_Access, src->parent_ino);
            if (!lk) { res2 = ERROR_NO_FREE_STORE; break; }
            res1 = (LONG)MKBADDR(lk);
        }
        res2 = 0;
        break;
    }

    /* ── ACTION_EXAMINE_ALL ────────────────────────────────── */
    /* ── ACTION_EXAMINE_ALL ────────────────────────────────── */
    case ACTION_EXAMINE_ALL: {
        bfs_lock_t *lk = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        UBYTE *buffer = (UBYTE *)pkt->dp_Arg2;
        LONG bufsize = pkt->dp_Arg3;
        LONG type = pkt->dp_Arg4;
        struct ExAllControl *eac = (struct ExAllControl *)BADDR(pkt->dp_Arg5);
        if (!lk || !buffer || !eac) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        if (type > ED_COMMENT) { res2 = ERROR_BAD_NUMBER; break; }

        eac->eac_Entries = 0;
        exall_optimized_ctx_t ectx = {
            .h = h, .eac = eac, .buffer = buffer, .pos = buffer,
            .end = buffer + bufsize, .type = type,
            .skip_count = (uint32_t)eac->eac_LastKey, .seen = 0,
            .last_ead = NULL, .overflow = false
        };

        bfs_dir_scan(&h->fs.dir_tree, lk->ino, exall_optimized_cb, &ectx);

        if (eac->eac_Entries > 0) {
            /* Return DOSTRUE if we found any entries; ectx.overflow signals if more remain */
            res1 = ectx.overflow ? DOSTRUE : DOSFALSE;
            res2 = 0;
        } else {
            res2 = ERROR_NO_MORE_ENTRIES;
        }
        break;    }

    case ACTION_EXAMINE_ALL_END:
        res1 = DOSTRUE; res2 = 0;
        break;

    /* ── ACTION_LOCK_RECORD / ACTION_FREE_RECORD ──────────── */
    case ACTION_LOCK_RECORD:
        /* BFS doesn't enforce byte-range locks — always succeed.
         * This is acceptable for a single-user Amiga filesystem. */
        res1 = DOSTRUE; res2 = 0;
        break;

    case ACTION_FREE_RECORD:
        res1 = DOSTRUE; res2 = 0;
        break;

    /* ── BFS Snapshot packets ──────────────────────────────── */
    case 3000: { /* ACTION_BFS_SNAPSHOT_CREATE */
        UBYTE *bname = (UBYTE *)BADDR(pkt->dp_Arg1);
        uint8_t nlen = bname[0];
        char name[34];
        if (nlen > 32) nlen = 32;
        memcpy(name, &bname[1], nlen);
        name[nlen] = 0;
        bfs_err_t err = bfs_snapshot_create(&h->fs, name);
        if (err == BFS_OK) { res1 = DOSTRUE; res2 = 0; }
        else { res2 = ERROR_DISK_FULL; }
        break;
    }
    case 3001: { /* ACTION_BFS_SNAPSHOT_DELETE */
        UBYTE *bname = (UBYTE *)BADDR(pkt->dp_Arg1);
        uint8_t nlen = bname[0];
        char name[34];
        if (nlen > 32) nlen = 32;
        memcpy(name, &bname[1], nlen);
        name[nlen] = 0;
        uint32_t id = 0;
        bfs_err_t err = bfs_snapshot_find_by_name(&h->fs, name, &id, NULL);
        if (err == BFS_OK)
            err = bfs_snapshot_delete(&h->fs, id);
        if (err == BFS_OK) { res1 = DOSTRUE; res2 = 0; }
        else { res2 = ERROR_OBJECT_NOT_FOUND; }
        break;
    }
    case 3002: { /* ACTION_BFS_SNAPSHOT_LIST */
        /* dp_Arg1 = buffer (APTR), dp_Arg2 = bufsize, dp_Arg3 = last_id (0=start)
         * Returns: DOSTRUE + fills buffer with "id name timestamp\n"
         *          DOSFALSE when no more entries
         *          dp_Res2 = next last_id */
        char *outbuf = (char *)pkt->dp_Arg1;
        LONG bufsize = pkt->dp_Arg2;
        uint32_t last_id = (uint32_t)pkt->dp_Arg3;

        if (!h->fs.has_snapshots || !outbuf || bufsize < 64) {
            break; /* DOSFALSE */
        }

        snap_next_ctx_t sn = { .last_id = last_id, .found = false };
        if (bfs_snapshot_list(&h->fs, snap_next_cb, &sn) != BFS_OK || !sn.found)
            break;

        uint32_t id = sn.found_id;
        bfs_snapshot_record_t rec = sn.rec;
        /* Format: "name  (id N, day DDDD)\n" */
        char *p = outbuf;
        int nlen = 0; while (nlen < 32 && rec.name[nlen]) nlen++;
        memcpy(p, rec.name, nlen); p += nlen;
        *p++ = 0; /* null-terminate name */
        /* Store id and timestamp after the name for the tool to parse */
        bfs_store_be32(p, id); p += 4;
        bfs_store_be32(p, bfs_be32(rec.timestamp)); p += 4;

        res1 = DOSTRUE;
        res2 = (LONG)id; /* next last_id */
        break;
    }
    case 3003: { /* ACTION_BFS_SNAPSHOT_SHOW — list files in a snapshot */
        /* dp_Arg1 = BSTR snapshot name
         * dp_Arg2 = output buffer (APTR)
         * dp_Arg3 = buffer size
         * dp_Arg4 = last_key (0 = start from beginning)
         * Returns: dp_Res1 = DOSTRUE if entry returned, DOSFALSE if done
         *          dp_Res2 = next last_key (for continuation)
         *          Buffer filled with: "type name\n" (type=D or F) */
        if (!h->fs.has_snapshots) { res2 = ERROR_OBJECT_NOT_FOUND; break; }
        UBYTE *bname = (UBYTE *)BADDR(pkt->dp_Arg1);
        char *outbuf = (char *)pkt->dp_Arg2;
        LONG bufsize = pkt->dp_Arg3;
        uint32_t last_key = (uint32_t)pkt->dp_Arg4;

        /* Find snapshot by name */
        uint8_t nlen = bname[0];
        if (nlen > 32) nlen = 32;
        char sname[34]; memcpy(sname, &bname[1], nlen); sname[nlen] = 0;

        bfs_snapshot_record_t rec;
        if (bfs_snapshot_find_by_name(&h->fs, sname, NULL, &rec) != BFS_OK) {
            res2 = ERROR_OBJECT_NOT_FOUND; break;
        }

        /* Walk the snapshot's dir tree */
        bfs_blk_t dir_root = bfs_be32(rec.dir_tree_root);
        bfs_dir_tree_t snap_dir;
        bfs_dir_init(&snap_dir, h->fs.bio, bfs_freespace_allocator(&h->fs.freespace),
                     dir_root, ((uint64_t)bfs_be32(rec.txn_id_hi) << 32 | bfs_be32(rec.txn_id_lo)));

        char namebuf[BFS_NAME_MAX + 1];
        exam_next_ctx_t ectx;
        ectx.skip_count = last_key;
        ectx.seen = 0;
        ectx.name_out = namebuf;
        ectx.got_entry = false;
        bfs_dir_scan(&snap_dir, BFS_ROOT_INO, exam_next_cb, &ectx);

        if (ectx.got_entry && bufsize > ectx.name_len + 3) {
            outbuf[0] = (ectx.type_out == BFS_INODE_DIR) ? 'D' : 'F';
            outbuf[1] = ' ';
            memcpy(outbuf + 2, namebuf, ectx.name_len);
            outbuf[2 + ectx.name_len] = '\n';
            outbuf[3 + ectx.name_len] = 0;
            res1 = DOSTRUE;
            res2 = (LONG)(ectx.skip_count + 1);
        } else {
            res2 = 0;
        }
        break;
    }

    case 3004: { /* ACTION_BFS_SNAPSHOT_MOUNT — mount snapshot as read-only volume */
        /* dp_Arg1 = BSTR snapshot name, dp_Arg2 = BSTR volume name */
        UBYTE *bsnap = (UBYTE *)BADDR(pkt->dp_Arg1);
        UBYTE *bvol = (UBYTE *)BADDR(pkt->dp_Arg2);
        uint8_t snlen = bsnap[0], vlen = bvol[0];
        if (snlen > 32) snlen = 32;
        if (vlen > 32) vlen = 32;
        char sname[34], vname[34];
        memcpy(sname, &bsnap[1], snlen); sname[snlen] = 0;
        memcpy(vname, &bvol[1], vlen); vname[vlen] = 0;

        /* Find free mount slot */
        int slot = -1;
        for (int i = 0; i < BFS_MAX_SNAP_MOUNTS; i++)
            if (!h->snap_mounts[i].active) { slot = i; break; }
        if (slot < 0) { res2 = ERROR_NO_FREE_STORE; break; }

        bfs_snapshot_record_t rec;
        if (bfs_snapshot_find_by_name(&h->fs, sname, NULL, &rec) != BFS_OK) { res2 = ERROR_OBJECT_NOT_FOUND; break; }

        /* Initialize snapshot trees (read-only) */
        static bfs_allocator_t ro_alloc = {0};
        bfs_dir_init(&h->snap_mounts[slot].dir_tree, h->fs.bio, &ro_alloc,
                     bfs_be32(rec.dir_tree_root), ((uint64_t)bfs_be32(rec.txn_id_hi) << 32 | bfs_be32(rec.txn_id_lo)));
        bfs_inode_init(&h->snap_mounts[slot].inode_tree, h->fs.bio, &ro_alloc,
                       bfs_be32(rec.inode_tree_root), ((uint64_t)bfs_be32(rec.txn_id_hi) << 32 | bfs_be32(rec.txn_id_lo)));
        h->snap_mounts[slot].txn_id = ((uint64_t)bfs_be32(rec.txn_id_hi) << 32 | bfs_be32(rec.txn_id_lo));

        /* Register VolumeNode */
        struct DosList *vol = MakeDosEntry(vname, DLT_VOLUME);
        if (vol) {
            vol->dol_Task = h->msgport;
            vol->dol_misc.dol_volume.dol_DiskType = BFS_SB_MAGIC;
            DateStamp(&vol->dol_misc.dol_volume.dol_VolumeDate);
            AddDosEntry(vol);
            h->snap_mounts[slot].volnode = vol;
            h->snap_mounts[slot].active = true;
            res1 = DOSTRUE; res2 = 0;
        } else {
            res2 = ERROR_NO_FREE_STORE;
        }
        break;
    }
    case 3005: { /* ACTION_BFS_SNAPSHOT_UNMOUNT */
        UBYTE *bvol = (UBYTE *)BADDR(pkt->dp_Arg1);
        uint8_t vlen = bvol[0];
        if (vlen > 32) vlen = 32;
        char vname[34]; memcpy(vname, &bvol[1], vlen); vname[vlen] = 0;
        /* Find and deactivate */
        for (int i = 0; i < BFS_MAX_SNAP_MOUNTS; i++) {
            if (h->snap_mounts[i].active && h->snap_mounts[i].volnode) {
                RemDosEntry(h->snap_mounts[i].volnode);
                FreeDosEntry(h->snap_mounts[i].volnode);
                h->snap_mounts[i].active = false;
                res1 = DOSTRUE; res2 = 0;
                break;
            }
        }
        break;
    }

    /* ── ACTION_ADD_NOTIFY ─────────────────────────────────── */
    case ACTION_ADD_NOTIFY: {
        struct NotifyRequest *nr = (struct NotifyRequest *)pkt->dp_Arg1;
        struct bfs_notify *node = (struct bfs_notify *)AllocVec(sizeof(struct bfs_notify), MEMF_CLEAR);
        if (!node) { res2 = ERROR_NO_FREE_STORE; break; }
        node->nr = nr;
        node->next = h->notify_list;
        h->notify_list = node;
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── ACTION_REMOVE_NOTIFY ──────────────────────────────── */
    case ACTION_REMOVE_NOTIFY: {
        struct NotifyRequest *nr = (struct NotifyRequest *)pkt->dp_Arg1;
        struct bfs_notify **pp = &h->notify_list;
        while (*pp) {
            if ((*pp)->nr == nr) {
                struct bfs_notify *tmp = *pp;
                *pp = tmp->next;
                FreeVec(tmp);
                break;
            }
            pp = &(*pp)->next;
        }
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── ACTION_RENAME_DISK ────────────────────────────────── */
    case ACTION_RENAME_DISK: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        UBYTE *bname = (UBYTE *)BADDR(pkt->dp_Arg1);
        uint8_t nlen = bname[0];
        if (nlen > BFS_VOLNAME_MAX - 1) nlen = BFS_VOLNAME_MAX - 1;
        memset(h->fs.txn.sb_new.volname, 0, BFS_VOLNAME_MAX);
        memcpy(h->fs.txn.sb_new.volname, &bname[1], nlen);
        bfs_err_t err = bfs_fs_sync(&h->fs);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── DIE ───────────────────────────────────────────────── */
    case ACTION_DIE:
        bfs_fs_unmount(&h->fs);
        res1 = DOSTRUE;
        res2 = 0;
        break;

    /* ── ACTION_CURRENT_VOLUME ─────────────────────────────── */
    case ACTION_CURRENT_VOLUME:
        res1 = (LONG)MKBADDR(h->devnode);
        res2 = 0;
        break;

    /* ── ACTION_INFO ───────────────────────────────────────── */
    case ACTION_INFO: {
        struct InfoData *id = (struct InfoData *)BADDR(pkt->dp_Arg2);
        if (id) {
            memset(id, 0, sizeof(*id));
            id->id_NumSoftErrors = 0;
            id->id_UnitNumber = 0;
            id->id_DiskState = h->write_protected ? ID_WRITE_PROTECTED : ID_VALIDATED;
            id->id_NumBlocks = h->fs.bio->block_count;
            id->id_NumBlocksUsed = h->fs.bio->block_count - h->fs.freespace.total_free;
            id->id_BytesPerBlock = h->fs.bio->block_size;
            id->id_DiskType = BFS_SB_MAGIC; /* 'BFS\0' */
            id->id_VolumeNode = MKBADDR(h->volnode);
            id->id_InUse = DOSTRUE;
            res1 = DOSTRUE;
            res2 = 0;
        }
        break;
    }

    /* ── ACTION_FLUSH ──────────────────────────────────────── */
    case ACTION_FLUSH:
        bfs_fs_sync(&h->fs);
        h->dirty = false;
        res1 = DOSTRUE;
        res2 = 0;
        break;

    /* ── ACTION_CHANGE_MODE ────────────────────────────────── */
    case ACTION_CHANGE_MODE:
        res1 = DOSTRUE;
        res2 = 0;
        break;

    /* ── ACTION_WRITE_PROTECT ──────────────────────────────── */
    case ACTION_WRITE_PROTECT:
        h->write_protected = (pkt->dp_Arg1 != 0);
        res1 = DOSTRUE;
        res2 = 0;
        break;

    /* ── ACTION_FORMAT ─────────────────────────────────────── */
    case ACTION_FORMAT: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        UBYTE *bname = (UBYTE *)BADDR(pkt->dp_Arg1);
        uint8_t nlen = bname[0];
        char volname[BFS_VOLNAME_MAX];
        if (nlen > BFS_VOLNAME_MAX - 1) nlen = BFS_VOLNAME_MAX - 1;
        memcpy(volname, &bname[1], nlen);
        volname[nlen] = 0;

        if (h->fs.mounted) bfs_fs_unmount(&h->fs);
        /* Set block size to 4096 (BFS default) and reinit cache */
        amiga_bio_t *ab = (amiga_bio_t *)(h + 1);
        bfs_amiga_bio_set_blocksize(ab, 4096);
        bfs_cache_destroy(&h->cache);
        bfs_cache_init(&h->cache, (bfs_bio_t *)ab, h->dosenvec->de_NumBuffers);

        bfs_err_t err = bfs_fs_format(&h->cache.bio, volname, 0);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        memset(&h->fs, 0, sizeof(h->fs));
        bfs_cache_invalidate(&h->cache);
        err = bfs_fs_mount(&h->fs, &h->cache.bio);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        if (!h->volnode) {
            struct DosList *vol = MakeDosEntry(volname, DLT_VOLUME);
            if (vol) {
                vol->dol_Task = h->msgport;
                vol->dol_misc.dol_volume.dol_DiskType = BFS_SB_MAGIC;
                DateStamp(&vol->dol_misc.dol_volume.dol_VolumeDate);
                AddDosEntry(vol);
                h->volnode = vol;
            }
        }
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── ACTION_FH_FROM_LOCK ───────────────────────────────── */
    case ACTION_FH_FROM_LOCK: {
        struct FileHandle *fh = (struct FileHandle *)BADDR(pkt->dp_Arg1);
        bfs_lock_t *lk = (bfs_lock_t *)BADDR(pkt->dp_Arg2);
        if (!lk) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }

        bfs_file_t *f = (bfs_file_t *)AllocVec(sizeof(bfs_file_t), MEMF_CLEAR);
        if (!f) { res2 = ERROR_NO_FREE_STORE; break; }
        if (bfs_file_open(f, &h->fs, lk->ino) != BFS_OK) {
            FreeVec(f);
            res2 = ERROR_SEEK_ERROR;
            break;
        }
        fh->fh_Arg1 = (LONG)f;
        FreeVec(lk);
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── ACTION_PARENT_FH ──────────────────────────────────── */
    case ACTION_PARENT_FH: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        if (!f) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        uint32_t ino = f->inode_nr;
        if (ino == BFS_ROOT_INO) { res1 = 0; res2 = 0; break; }

        uint32_t par_ino, par_type;
        if (bfs_dir_lookup(&h->fs.dir_tree, ino, "..", 2, &par_ino, &par_type) != BFS_OK)
            par_ino = BFS_ROOT_INO;

        bfs_lock_t *lk = MakeLock(h, par_ino, BFS_INODE_DIR, SHARED_LOCK, BFS_ROOT_INO);
        if (!lk) { res2 = ERROR_NO_FREE_STORE; break; }
        res1 = (LONG)MKBADDR(lk);
        res2 = 0;
        break;
    }

    /* ── ACTION_COPY_DIR_FH ────────────────────────────────── */
    case ACTION_COPY_DIR_FH: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        if (!f) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        bfs_lock_t *lk = MakeLock(h, f->inode_nr, BFS_INODE_FILE, SHARED_LOCK, BFS_ROOT_INO);
        if (!lk) { res2 = ERROR_NO_FREE_STORE; break; }
        res1 = (LONG)MKBADDR(lk);
        res2 = 0;
        break;
    }

    /* ── ACTION_EXAMINE_FH ─────────────────────────────────── */
    case ACTION_EXAMINE_FH: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        struct FileInfoBlock *fib = (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
        if (!f || !fib) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }

        bfs_inode_t inode;
        if (bfs_inode_read(&h->fs.inode_tree, f->inode_nr, &inode) != BFS_OK) {
            res2 = ERROR_SEEK_ERROR; break;
        }
        uint64_t size = ((uint64_t)bfs_be32(inode.size_hi) << 32) | bfs_be32(inode.size_lo);
        uint32_t type = bfs_be32(inode.type);
        FillFib(fib, "", 0, f->inode_nr, type, size, bfs_be32(inode.protection), &inode);
        fib->fib_DiskKey = 0;
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── ACTION_SET_DATE ───────────────────────────────────── */
    case ACTION_SET_DATE: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino = ResolvePath((BPTR)pkt->dp_Arg2, (BPTR)pkt->dp_Arg3, namebuf, &len, h);
        uint32_t ino, type;
        bfs_err_t err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len, &ino, &type);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        bfs_inode_t inode;
        err = bfs_inode_read(&h->fs.inode_tree, ino, &inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        LONG *ds = (LONG *)pkt->dp_Arg4; /* DateStamp: days, minute, tick */
        inode.modify_days = bfs_be16((uint16_t)ds[0]);
        inode.modify_mins = bfs_be16((uint16_t)ds[1]);
        inode.modify_ticks = bfs_be16((uint16_t)ds[2]);

        err = bfs_inode_write(&h->fs.inode_tree, ino, &inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        res1 = DOSTRUE;
        res2 = 0;
        h->dirty = true;
        break;
    }

    /* ── ACTION_SET_OWNER ──────────────────────────────────── */
    case ACTION_SET_OWNER: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino = ResolvePath((BPTR)pkt->dp_Arg2, (BPTR)pkt->dp_Arg3, namebuf, &len, h);
        uint32_t ino, type;
        bfs_err_t err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len, &ino, &type);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        bfs_inode_t inode;
        err = bfs_inode_read(&h->fs.inode_tree, ino, &inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        uint32_t owner_info = (uint32_t)pkt->dp_Arg4;
        inode.uid = bfs_be16((uint16_t)(owner_info >> 16));
        inode.gid = bfs_be16((uint16_t)(owner_info & 0xFFFF));

        err = bfs_inode_write(&h->fs.inode_tree, ino, &inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        res1 = DOSTRUE;
        res2 = 0;
        h->dirty = true;
        break;
    }

    /* ── ACTION_SEEK64 / ACTION_CHANGE_FILE_POSITION64 ─────── */
    case ACTION_SEEK64:
    case ACTION_CHANGE_FILE_POSITION64: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        if (!f) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        uint64_t old_pos = f->offset;
        int64_t offset = ((int64_t)(LONG)pkt->dp_Arg2 << 32) | (uint32_t)pkt->dp_Arg3;
        LONG mode = pkt->dp_Arg4;
        int bfs_mode;
        switch (mode) {
        case OFFSET_BEGINNING: bfs_mode = BFS_SEEK_SET; break;
        case OFFSET_END:       bfs_mode = BFS_SEEK_END; break;
        default:               bfs_mode = BFS_SEEK_CUR; break;
        }
        int64_t new_pos = bfs_file_seek(f, offset, bfs_mode);
        if (new_pos < 0) { res2 = ERROR_SEEK_ERROR; break; }
        res1 = (LONG)(old_pos >> 32);
        res2 = (LONG)(old_pos & 0xFFFFFFFF);
        break;
    }

    /* ── ACTION_SET_FILE_SIZE64 / ACTION_CHANGE_FILE_SIZE64 ── */
    case ACTION_SET_FILE_SIZE64:
    case ACTION_CHANGE_FILE_SIZE64: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        if (!f) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        uint64_t new_size = ((uint64_t)(uint32_t)pkt->dp_Arg2 << 32) | (uint32_t)pkt->dp_Arg3;
        bfs_err_t err = bfs_file_truncate(f, new_size);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        res1 = (LONG)(new_size >> 32);
        res2 = (LONG)(new_size & 0xFFFFFFFF);
        h->dirty = true;
        break;
    }

    /* ── ACTION_GET_FILE_SIZE64 ────────────────────────────── */
    case ACTION_GET_FILE_SIZE64: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        if (!f) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        res1 = (LONG)(f->size >> 32);
        res2 = (LONG)(f->size & 0xFFFFFFFF);
        break;
    }

    /* ── ACTION_GET_FILE_POSITION64 ────────────────────────── */
    case ACTION_GET_FILE_POSITION64: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        if (!f) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        res1 = (LONG)(f->offset >> 32);
        res2 = (LONG)(f->offset & 0xFFFFFFFF);
        break;
    }

    /* ── ACTION_EXAMINE_OBJECT64 ──────────────────────────── */
    case ACTION_EXAMINE_OBJECT64: {
        bfs_lock_t *lk = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        struct FileInfoBlock *fib = (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
        if (!lk || !fib) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }

        uint64_t size = 0;
        uint32_t prot = 0;
        bfs_inode_t inode;
        if (bfs_inode_read(&h->fs.inode_tree, lk->ino, &inode) == BFS_OK) {
            size = ((uint64_t)bfs_be32(inode.size_hi) << 32) | bfs_be32(inode.size_lo);
            prot = bfs_be32(inode.protection);
        }
        FillFib(fib, "", 0, lk->ino, lk->type, size, prot, &inode);
        fib->fib_DiskKey = 0;
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── ACTION_EXAMINE_NEXT64 ─────────────────────────────── */
    case ACTION_EXAMINE_NEXT64: {
        bfs_lock_t *lk = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        struct FileInfoBlock *fib = (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
        if (!lk || !fib) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }

        char namebuf[BFS_NAME_MAX + 1];
        exam_next_ctx_t ctx;
        ctx.skip_count = (uint32_t)fib->fib_DiskKey;
        ctx.seen = 0;
        ctx.name_out = namebuf;
        ctx.got_entry = false;

        bfs_dir_scan(&h->fs.dir_tree, lk->ino, exam_next_cb, &ctx);

        if (!ctx.got_entry) { res2 = ERROR_NO_MORE_ENTRIES; break; }

        /* Read inode for 64-bit size */
        uint64_t size = 0;
        uint32_t prot = 0;
        bfs_inode_t inode;
        if (bfs_inode_read(&h->fs.inode_tree, ctx.ino_out, &inode) == BFS_OK) {
            size = ((uint64_t)bfs_be32(inode.size_hi) << 32) | bfs_be32(inode.size_lo);
            prot = bfs_be32(inode.protection);
        }
        FillFib(fib, namebuf, ctx.name_len, ctx.ino_out, ctx.type_out, size, prot, &inode);
        fib->fib_DiskKey = (LONG)(ctx.skip_count + 1);
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    default:
        res2 = ERROR_ACTION_NOT_KNOWN;
        break;
    }

    /* Sync after metadata-changing operations to commit pending frees.
     * ACTION_END (close) is NOT synced here — deferred to idle sync
     * in the main loop for better write batching performance. */
    if (h->dirty && (pkt->dp_Type == ACTION_DELETE_OBJECT ||
                     pkt->dp_Type == ACTION_CREATE_DIR ||
                     pkt->dp_Type == ACTION_RENAME_OBJECT ||
                     pkt->dp_Type == ACTION_SET_COMMENT ||
                     pkt->dp_Type == ACTION_SET_PROTECT ||
                     pkt->dp_Type == ACTION_SET_DATE ||
                     pkt->dp_Type == ACTION_RENAME_DISK ||
                     pkt->dp_Type == ACTION_FORMAT)) {
        bfs_fs_sync(&h->fs);
        h->dirty = false;
    }

    pkt->dp_Res1 = res1;
    pkt->dp_Res2 = res2;
    ReplyPacket(pkt, h);
}

/* ── Main handler entry ────────────────────────────────────── */

void EntryPoint(void)
{
    struct Process *myproc;
    struct bfs_handler *h;
    struct DosPacket *pkt;
    struct Message *msg;
    BOOL running = TRUE;

    SysBase = *((struct ExecBase **)4);

    /* Swap to a larger stack (FS-UAE may give us only 4KB) */
    struct StackSwapStruct sss;
    APTR newstack = AllocMem(131072, MEMF_PUBLIC);
    if (!newstack) return;
    sss.stk_Lower = newstack;
    sss.stk_Upper = (ULONG)newstack + 131072;
    sss.stk_Pointer = (APTR)sss.stk_Upper;
    StackSwap(&sss);

    myproc = (struct Process *)FindTask(NULL);

    /* Wait for startup packet */
    WaitPort(&myproc->pr_MsgPort);
    msg = GetMsg(&myproc->pr_MsgPort);
    pkt = (struct DosPacket *)msg->mn_Node.ln_Name;

    /* Allocate handler state */
    h = AllocMem(sizeof(struct bfs_handler) + 256, MEMF_CLEAR);
    if (!h) {
        pkt->dp_Res1 = DOSFALSE;
        pkt->dp_Res2 = ERROR_NO_FREE_STORE;
        goto fail_startup;
    }

    h->SysBase = SysBase;
    DOSBase = (struct DosLibrary *)OpenLibrary("dos.library", 37);
    h->DOSBase = DOSBase;
    if (!DOSBase) {
        pkt->dp_Res1 = DOSFALSE;
        pkt->dp_Res2 = ERROR_INVALID_RESIDENT_LIBRARY;
        goto fail_startup;
    }

    /* Extract startup info */
    h->devnode = (struct DeviceNode *)BADDR(pkt->dp_Arg3);
    struct FileSysStartupMsg *fssm = (struct FileSysStartupMsg *)BADDR(h->devnode->dn_Startup);
    h->dosenvec = (struct DosEnvec *)BADDR(fssm->fssm_Environ);

    /* Open device */
    h->devport = CreateMsgPort();
    h->request = (struct IOExtTD *)CreateIORequest(h->devport, sizeof(struct IOExtTD));
    {
        UBYTE devname[108];
        UBYTE *bname = (UBYTE *)BADDR(fssm->fssm_Device);
        memcpy(devname, bname + 1, bname[0]);
        devname[bname[0]] = 0;
        if (OpenDevice(devname, fssm->fssm_Unit, (struct IORequest *)h->request, fssm->fssm_Flags)) {
            pkt->dp_Res1 = DOSFALSE;
            pkt->dp_Res2 = ERROR_DEVICE_NOT_MOUNTED;
            goto fail_startup;
        }
    }

    /* Initialize block I/O */
    bfs_amiga_bio_init((struct amiga_bio *)(h + 1), h->request, h->devport, h->dosenvec);

    /* Mount filesystem: read superblock to get block_size, switch, then mount */
    bfs_superblock_t sb;
    bfs_err_t mount_err = bfs_sb_read((bfs_bio_t *)(h + 1), &sb);
    if (mount_err == BFS_OK) {
        uint32_t fs_bs = bfs_be32(sb.block_size);
        bfs_amiga_bio_set_blocksize((struct amiga_bio *)(h + 1), fs_bs);
        bfs_cache_init(&h->cache, (bfs_bio_t *)(h + 1), h->dosenvec->de_NumBuffers);
        mount_err = bfs_fs_mount(&h->fs, &h->cache.bio);
    }
    /* If mount fails (unformatted disk), continue anyway.
     * Like PFS3: accept packets, return ERROR_NOT_A_DOS_DISK for most,
     * but allow ACTION_FORMAT to format the disk. */
    if (mount_err != BFS_OK) {
        h->fs.bio = (bfs_bio_t *)(h + 1);
    }

    /* Set up message port for DOS packets */
    h->msgport = &myproc->pr_MsgPort;
    h->devnode->dn_Task = h->msgport;

    /* Register VolumeNode (only if mounted successfully) */
    if (mount_err == BFS_OK) {
        int vlen = 0;
        while (vlen < BFS_VOLNAME_MAX && sb.volname[vlen]) vlen++;
        char vname[BFS_VOLNAME_MAX + 1];
        memcpy(vname, sb.volname, vlen);
        vname[vlen] = 0;
        struct DosList *vol = MakeDosEntry(vname, DLT_VOLUME);
        if (vol) {
            vol->dol_Task = h->msgport;
            vol->dol_misc.dol_volume.dol_DiskType = BFS_SB_MAGIC;
            DateStamp(&vol->dol_misc.dol_volume.dol_VolumeDate);
            AddDosEntry(vol);
            h->volnode = vol;
        }
    }

    /* Set up disk change notification */
    h->diskchange_sig = AllocSignal(-1);
    if (h->diskchange_sig >= 0) {
        h->diskchange_int = (struct Interrupt *)AllocVec(sizeof(struct Interrupt), MEMF_CLEAR);
        if (h->diskchange_int) {
            h->diskchange_int->is_Node.ln_Type = NT_INTERRUPT;
            h->diskchange_int->is_Node.ln_Name = (char *)"BFS-DiskChange";
            h->diskchange_int->is_Data = FindTask(NULL);
            h->diskchange_int->is_Code = NULL; /* signal-based, no code needed */
            h->diskchange_req = (struct IOExtTD *)CreateIORequest(h->devport, sizeof(struct IOExtTD));
            if (h->diskchange_req) {
                *h->diskchange_req = *h->request;
                h->diskchange_req->iotd_Req.io_Command = TD_ADDCHANGEINT;
                h->diskchange_req->iotd_Req.io_Data = h->diskchange_int;
                h->diskchange_req->iotd_Req.io_Length = sizeof(struct Interrupt);
                SendIO((struct IORequest *)h->diskchange_req);
            }
        }
    }

    /* Reply to startup packet — success */
    pkt->dp_Res1 = DOSTRUE;
    pkt->dp_Res2 = 0;
    {
        struct MsgPort *replyport = pkt->dp_Port;
        pkt->dp_Link->mn_Node.ln_Name = (UBYTE *)pkt;
        pkt->dp_Link->mn_Node.ln_Succ = NULL;
        pkt->dp_Link->mn_Node.ln_Pred = NULL;
        pkt->dp_Port = h->msgport;
        PutMsg(replyport, pkt->dp_Link);
    }

    /* ── Main packet loop ──────────────────────────────────── */
    while (running) {
        ULONG sigs = Wait((1UL << h->msgport->mp_SigBit) |
                          ((h->diskchange_sig >= 0) ? (1UL << h->diskchange_sig) : 0));

        if (h->diskchange_sig >= 0 && (sigs & (1UL << h->diskchange_sig))) {
            /* Disk changed — flush and re-read */
            if (h->dirty) {
                bfs_fs_sync(&h->fs);
                h->dirty = false;
            }
        }

        while ((pkt = GetPacket(h->msgport)) != NULL) {
            if (pkt->dp_Type == ACTION_DIE) {
                running = FALSE;
            }
            HandlePacket(pkt, h);
        }

        /* Idle sync: commit when no more packets are queued.
         * This batches rapid write+close sequences into a single sync,
         * dramatically improving random/sequential write performance.
         * Data is already on disk (bfs_bio_write is immediate), this
         * just commits the superblock + processes pending_frees. */
        if (h->dirty) {
            bfs_fs_sync(&h->fs);
            h->dirty = false;
        }
    }

    /* Cleanup */
    h->devnode->dn_Task = NULL;
    CloseDevice((struct IORequest *)h->request);
    DeleteIORequest((struct IORequest *)h->request);
    DeleteMsgPort(h->devport);
    CloseLibrary((struct Library *)DOSBase);
    FreeMem(h, sizeof(struct bfs_handler) + 256);
    return;

fail_startup:
    {
        struct MsgPort *replyport = pkt->dp_Port;
        pkt->dp_Link->mn_Node.ln_Name = (UBYTE *)pkt;
        pkt->dp_Link->mn_Node.ln_Succ = NULL;
        pkt->dp_Link->mn_Node.ln_Pred = NULL;
        pkt->dp_Port = &myproc->pr_MsgPort;
        PutMsg(replyport, pkt->dp_Link);
    }
    if (h) {
        if (DOSBase) CloseLibrary((struct Library *)DOSBase);
        FreeMem(h, sizeof(struct bfs_handler) + 256);
    }

    /* Restore original stack */
    StackSwap(&sss);
    FreeMem(newstack, 131072);
}
