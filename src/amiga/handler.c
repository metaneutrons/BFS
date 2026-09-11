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
#include <proto/intuition.h>
#include <string.h>

#include "bfs_fs.h"
#include "bfs_cache.h"
#include "bfs_file.h"
#include "bfs_dir.h"
#include "bfs_inode.h"
#include "bfs_snapshot.h"
#include "bfs_fsck.h"
#include "bfs_diagnostics.h"
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

#include "dos_packets.h"

#define BFS_SNAPSHOT_ENTRY_META_OFFSET 260
#define BFS_SNAPSHOT_ENTRY_SIZE        284

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

struct bfs_open_file;

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
    bfs_err_t mount_error;
    char format_error[BFS_FORMAT_ERROR_MAX];
    bool format_error_reported;
    struct DosList *volnode;
    struct bfs_notify *notify_list;
    struct bfs_open_file *open_files;
    uint32_t open_file_count;
    uint32_t lock_count;
    bool notify_pending;
    bool should_exit;
    bool media_changed;
    BYTE diskchange_sig;
    struct IOExtTD *diskchange_req;
    struct Interrupt *diskchange_int;
};

/* Lock structure — stored as BPTR in FileLock */
typedef struct {
    struct FileLock fl;
    uint32_t ino;
    uint32_t type; /* BFS_INODE_FILE or BFS_INODE_DIR */
    uint32_t parent_ino;
} bfs_lock_t;

typedef struct bfs_open_file {
    bfs_file_t file;
    struct bfs_open_file *next;
    LONG access;
    uint32_t parent_ino;
    uint32_t type;
} bfs_open_file_t;

_Static_assert(offsetof(bfs_open_file_t, file) == 0,
               "bfs_file_t must be the first open-file field");

/* Amiga library bases — set globally for proto headers */
struct ExecBase *SysBase;
struct DosLibrary *DOSBase;

static ULONG DiskChangeHandler(register struct bfs_handler *h __asm("a1"))
{
    Signal(h->msgport->mp_SigTask, 1UL << h->diskchange_sig);
    return 0;
}

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
    pkt->dp_Link->mn_Node.ln_Name = (char *)pkt;
    pkt->dp_Link->mn_Node.ln_Succ = NULL;
    pkt->dp_Link->mn_Node.ln_Pred = NULL;
    pkt->dp_Port = h->msgport;
    PutMsg(replyport, pkt->dp_Link);
}

static struct DosList *RegisterVolumeNode(struct bfs_handler *h,
                                          const char *name)
{
    if (!name || !name[0]) {
        SetIoErr(ERROR_INVALID_COMPONENT_NAME);
        return NULL;
    }

    SetIoErr(0);
    struct DosList *vol = MakeDosEntry(name, DLT_VOLUME);
    if (!vol) return NULL;

    vol->dol_Task = h->msgport;
    vol->dol_misc.dol_volume.dol_DiskType = BFS_SB_MAGIC;
    DateStamp(&vol->dol_misc.dol_volume.dol_VolumeDate);
    if (AddDosEntry(vol) == DOSFALSE) {
        LONG error = IoErr();
        FreeDosEntry(vol);
        SetIoErr(error ? error : ERROR_OBJECT_EXISTS);
        return NULL;
    }
    return vol;
}

static void RemoveVolumeNode(struct bfs_handler *h)
{
    if (!h->volnode) return;
    RemDosEntry(h->volnode);
    FreeDosEntry(h->volnode);
    h->volnode = NULL;
}

static UBYTE *AllocateBstr(const UBYTE *text, uint8_t len)
{
    UBYTE *bstr = (UBYTE *)AllocVec((ULONG)len + 2, MEMF_PUBLIC | MEMF_CLEAR);
    if (!bstr) return NULL;
    bstr[0] = len;
    /* Allocation above includes the length byte, len bytes and a terminator. */
    memcpy(&bstr[1], text, len); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return bstr;
}

static void ReplaceVolumeNodeName(struct bfs_handler *h, UBYTE *new_name)
{
    BPTR old_name = h->volnode->dol_Name;
    LockDosList(LDF_VOLUMES | LDF_WRITE);
    h->volnode->dol_Name = MKBADDR(new_name);
    UnLockDosList(LDF_VOLUMES | LDF_WRITE);
    if (old_name) FreeVec(BADDR(old_name));
}

static bool HandlerIsInUse(const struct bfs_handler *h);

static void SetMountError(struct bfs_handler *h, bfs_err_t err,
                          const bfs_superblock_t *sb)
{
    char message[BFS_FORMAT_ERROR_MAX] = {0};
    if (err == BFS_ERR_UNSUPPORTED) bfs_sb_describe_unsupported(sb, message);
    if (strcmp(message, h->format_error) != 0) h->format_error_reported = false;
    /* Both arrays have BFS_FORMAT_ERROR_MAX bytes. */
    memcpy(h->format_error, message, sizeof(message)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    h->mount_error = err;
}

static void ReportFormatError(struct bfs_handler *h, struct MsgPort *reply_port)
{
    if (h->mount_error != BFS_ERR_UNSUPPORTED || h->format_error_reported ||
        !reply_port || (reply_port->mp_Flags & PF_ACTION) != PA_SIGNAL || !reply_port->mp_SigTask ||
        ((struct Task *)reply_port->mp_SigTask)->tc_Node.ln_Type != NT_PROCESS)
        return;
    struct Process *caller = (struct Process *)reply_port->mp_SigTask;
    if (caller->pr_WindowPtr == (APTR)-1) return;
    struct IntuitionBase *IntuitionBase =
        (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
    if (!IntuitionBase) return;
    struct EasyStruct request = {
        sizeof(struct EasyStruct), 0, "BFS: unsupported disk format",
        h->format_error, "OK",
    };
    h->format_error_reported = true;
    EasyRequestArgs((struct Window *)caller->pr_WindowPtr, &request, NULL, NULL);
    CloseLibrary((struct Library *)IntuitionBase);
}

static bool TryRemountMedia(struct bfs_handler *h)
{
    if (HandlerIsInUse(h)) return false;

    RemoveVolumeNode(h);
    bfs_cache_destroy(&h->cache);

    amiga_bio_t *ab = (amiga_bio_t *)(h + 1);

    bfs_superblock_t sb;
    bfs_err_t err = bfs_amiga_bio_probe_superblock(ab, &sb);
    SetMountError(h, err, &sb);
    if (err != BFS_OK) return false;

    err = bfs_cache_init(&h->cache, (bfs_bio_t *)ab,
                         h->dosenvec->de_NumBuffers);
    h->mount_error = err;
    if (err != BFS_OK) return false;

    err = bfs_fs_mount(&h->fs, &h->cache.bio);
    SetMountError(h, err, &h->fs.txn.sb);
    if (err != BFS_OK) return false;

    h->volnode = RegisterVolumeNode(h, (const char *)h->fs.txn.sb.volname);
    if (!h->volnode) {
        bfs_fs_abandon(&h->fs);
        bfs_cache_destroy(&h->cache);
        return false;
    }

    h->media_changed = false;
    return true;
}

/* ── Lock helpers ──────────────────────────────────────────── */

static void AttachLock(struct bfs_handler *h, bfs_lock_t *lock, uint32_t ino,
                       uint32_t type, LONG access, uint32_t parent_ino)
{
    lock->ino = ino;
    lock->type = type;
    lock->parent_ino = parent_ino;
    lock->fl.fl_Access = access;
    lock->fl.fl_Task = h->msgport;
    lock->fl.fl_Volume = h->volnode ? MKBADDR(h->volnode) : 0;
    lock->fl.fl_Key = ino;
    if (h->volnode) {
        lock->fl.fl_Link = h->volnode->dol_misc.dol_volume.dol_LockList;
        h->volnode->dol_misc.dol_volume.dol_LockList = MKBADDR(lock);
    }
    h->lock_count++;
}

static bool InodeAccessConflicts(const struct bfs_handler *h, uint32_t ino,
                                  LONG access)
{
    if (h->volnode) {
        BPTR next = h->volnode->dol_misc.dol_volume.dol_LockList;
        while (next) {
            bfs_lock_t *other = (bfs_lock_t *)BADDR(next);
            if (other->ino == ino &&
                (access == EXCLUSIVE_LOCK ||
                 other->fl.fl_Access == EXCLUSIVE_LOCK)) {
                return true;
            }
            next = other->fl.fl_Link;
        }
    }
    for (const bfs_open_file_t *file = h->open_files; file; file = file->next) {
        if (file->file.inode_nr == ino &&
            (access == EXCLUSIVE_LOCK || file->access == EXCLUSIVE_LOCK))
            return true;
    }
    return false;
}

static bfs_lock_t *MakeLock(struct bfs_handler *h, uint32_t ino,
                            uint32_t type, LONG access, uint32_t parent_ino)
{
    if (access != SHARED_LOCK && access != EXCLUSIVE_LOCK) {
        SetIoErr(ERROR_BAD_NUMBER);
        return NULL;
    }
    if (InodeAccessConflicts(h, ino, access)) {
        SetIoErr(ERROR_OBJECT_IN_USE);
        return NULL;
    }

    SetIoErr(0);
    bfs_lock_t *lk = (bfs_lock_t *)AllocVec(sizeof(bfs_lock_t), MEMF_CLEAR);
    if (!lk) return NULL;
    AttachLock(h, lk, ino, type, access, parent_ino);
    return lk;
}

static bool FreeLock(struct bfs_handler *h, bfs_lock_t *lock)
{
    if (!lock || !h->volnode) return false;

    BPTR *link = &h->volnode->dol_misc.dol_volume.dol_LockList;
    while (*link) {
        bfs_lock_t *current = (bfs_lock_t *)BADDR(*link);
        if (current == lock) {
            *link = current->fl.fl_Link;
            FreeVec(current);
            if (h->lock_count > 0) h->lock_count--;
            return true;
        }
        link = &current->fl.fl_Link;
    }
    return false;
}

static bool LockIsOwned(const struct bfs_handler *h, const bfs_lock_t *lock)
{
    if (!lock || !h->volnode) return false;
    BPTR next = h->volnode->dol_misc.dol_volume.dol_LockList;
    while (next) {
        const bfs_lock_t *current = (const bfs_lock_t *)BADDR(next);
        if (current == lock) return true;
        next = current->fl.fl_Link;
    }
    return false;
}

static bfs_open_file_t *AllocateOpenFile(void)
{
    return (bfs_open_file_t *)AllocVec(sizeof(bfs_open_file_t), MEMF_CLEAR);
}

static void TrackOpenFile(struct bfs_handler *h, bfs_open_file_t *open_file,
                          LONG access, uint32_t parent_ino, uint32_t type)
{
    open_file->access = access;
    open_file->parent_ino = parent_ino;
    open_file->type = type;
    open_file->next = h->open_files;
    h->open_files = open_file;
    h->open_file_count++;
}

static bfs_open_file_t *FindOpenFile(struct bfs_handler *h, bfs_file_t *file)
{
    bfs_open_file_t *current = h->open_files;
    while (current) {
        if (&current->file == file) return current;
        current = current->next;
    }
    return NULL;
}

static bool FreeOpenFile(struct bfs_handler *h, bfs_file_t *file)
{
    bfs_open_file_t **link = &h->open_files;
    while (*link) {
        bfs_open_file_t *current = *link;
        if (&current->file == file) {
            *link = current->next;
            FreeVec(current);
            if (h->open_file_count > 0) h->open_file_count--;
            return true;
        }
        link = &current->next;
    }
    return false;
}

static bool InodeIsInUse(const struct bfs_handler *h, uint32_t ino)
{
    return InodeAccessConflicts(h, ino, EXCLUSIVE_LOCK);
}

static bool HandlerIsInUse(const struct bfs_handler *h)
{
    return h->lock_count != 0 || h->open_file_count != 0 ||
           h->notify_list != NULL;
}

static uint32_t LockIno(BPTR lock)
{
    if (!lock) return BFS_ROOT_INO;
    bfs_lock_t *lk = (bfs_lock_t *)BADDR(lock);
    return lk->ino;
}

typedef struct {
    uint32_t ino;
    char *name;
    uint8_t name_len;
    bool found;
} lock_name_ctx_t;

static bool FindLockName(const char *name, uint8_t name_len,
                         uint32_t inode_nr, uint32_t entry_type, void *ctx)
{
    lock_name_ctx_t *lookup = (lock_name_ctx_t *)ctx;
    (void)entry_type;

    if (inode_nr != lookup->ino ||
        (name_len == 2 && name[0] == '.' && name[1] == '.'))
        return true;

    /* NameForLock supplies BFS_NAME_MAX + 1 bytes; name_len is uint8_t. */
    memcpy(lookup->name, name, name_len); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    lookup->name[name_len] = 0;
    lookup->name_len = name_len;
    lookup->found = true;
    return false;
}

static bfs_err_t NameForLock(struct bfs_handler *h, const bfs_lock_t *lock,
                             char *name, uint8_t *name_len)
{
    if (lock->ino == BFS_ROOT_INO) {
        uint8_t len = 0;
        const char *volname = h->fs.txn.sb.volname;
        while (len < BFS_VOLNAME_MAX && volname[len]) len++;
        /* The caller supplies BFS_NAME_MAX + 1, exceeding BFS_VOLNAME_MAX. */
        memcpy(name, volname, len); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        name[len] = 0;
        *name_len = len;
        return BFS_OK;
    }

    lock_name_ctx_t lookup = {
        .ino = lock->ino,
        .name = name,
        .name_len = 0,
        .found = false,
    };
    bfs_err_t err = bfs_dir_scan(&h->fs.dir_tree, lock->parent_ino,
                                 FindLockName, &lookup);
    if (err != BFS_OK) return err;
    if (!lookup.found) return BFS_ERR_NOTFOUND;
    *name_len = lookup.name_len;
    return BFS_OK;
}

/* ── BSTR / path helpers ──────────────────────────────────── */

/* Extract the parent inode and final component from a lock-relative BSTR path. */
static bfs_err_t ResolveDirectories(struct bfs_handler *h, uint32_t *parent,
                                    const char **name, uint8_t *len)
{
    while (*len > 0) {
        uint8_t component = 0;
        while (component < *len && (*name)[component] != '/') component++;
        uint8_t remaining = *len - component;
        if (component && (remaining == 0 || remaining == 1)) break;

        uint32_t ino, type;
        if (component == 0 && *parent == BFS_ROOT_INO) {
            ino = BFS_ROOT_INO;
        } else {
            const char *key = component ? *name : "..";
            bfs_err_t err = bfs_dir_lookup(&h->fs.dir_tree, *parent, key,
                                           component ? component : 2, &ino, &type);
            if (err != BFS_OK) return err;
            if (type != BFS_INODE_DIR) return BFS_ERR_INVAL;
        }
        *parent = ino;
        *name += component + 1;
        *len -= component + 1;
    }
    return BFS_OK;
}

static bfs_err_t ResolvePath(BPTR lock, BPTR bstr_name,
                             char *namebuf, uint8_t *namelen_out,
                             uint32_t *parent_out,
                             struct bfs_handler *h)
{
    if (!namebuf || !namelen_out || !parent_out || !h || !h->fs.mounted)
        return BFS_ERR_INVAL;

    bfs_lock_t *base_lock = (bfs_lock_t *)BADDR(lock);
    if (base_lock && !LockIsOwned(h, base_lock)) return BFS_ERR_INVAL;

    uint32_t parent_ino = LockIno(lock);
    UBYTE *bstr = (UBYTE *)BADDR(bstr_name);
    if (!bstr) {
        *namelen_out = 0;
        namebuf[0] = 0;
        *parent_out = parent_ino;
        return BFS_OK;
    }
    uint8_t len = bstr[0];
    const char *name = (const char *)&bstr[1];

    /* Skip volume prefix (e.g. "VOL:") — resets to root */
    for (uint8_t i = 0; i < len; i++) {
        if (name[i] == ':') {
            name += i + 1;
            len -= i + 1;
            parent_ino = BFS_ROOT_INO;
            break;
        }
    }

    bfs_err_t err = ResolveDirectories(h, &parent_ino, &name, &len);
    if (err != BFS_OK) return err;
    if (len && name[len - 1] == '/') len--;
    /* All callers supply BFS_NAME_MAX + 1 bytes; a BSTR length is at most 255. */
    memcpy(namebuf, name, len); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    namebuf[len] = 0;
    *namelen_out = len;
    *parent_out = parent_ino;
    return BFS_OK;
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
    case BFS_ERR_OVERFLOW: return ERROR_BAD_NUMBER;
    case BFS_ERR_UNSUPPORTED: return ERROR_NOT_IMPLEMENTED;
    case BFS_ERR_CORRUPT:  return ERROR_NOT_A_DOS_DISK;
    case BFS_ERR_AGAIN:    return ERROR_DISK_FULL;
    case BFS_ERR_IO:       return ERROR_SEEK_ERROR;
    default:                return ERROR_SEEK_ERROR;
    }
}

static LONG CheckProtection(struct bfs_handler *h, uint32_t ino, uint32_t mask)
{
    bfs_inode_t inode;
    bfs_err_t err = bfs_inode_read(&h->fs.inode_tree, ino, &inode);
    if (err != BFS_OK) return Pfs4ToDosError(err);
    uint32_t denied = bfs_be32(inode.protection) & mask;
    if (denied & FIBF_DELETE) return ERROR_DELETE_PROTECTED;
    if (denied & FIBF_WRITE) return ERROR_WRITE_PROTECTED;
    if (denied & FIBF_READ) return ERROR_READ_PROTECTED;
    return 0;
}

static LONG MarkFileChanged(struct bfs_handler *h, uint32_t ino)
{
    h->dirty = true;
    h->notify_pending = true;
    bfs_inode_t inode;
    bfs_err_t err = bfs_inode_read(&h->fs.inode_tree, ino, &inode);
    if (err != BFS_OK) return Pfs4ToDosError(err);
    struct DateStamp ds;
    DateStamp(&ds);
    inode.modify_days = bfs_be16((uint16_t)ds.ds_Days);
    inode.modify_mins = bfs_be16((uint16_t)ds.ds_Minute);
    inode.modify_ticks = bfs_be16((uint16_t)ds.ds_Tick);
    inode.protection = bfs_be32(bfs_be32(inode.protection) & ~FIBF_ARCHIVE);
    return Pfs4ToDosError(bfs_inode_write(&h->fs.inode_tree, ino, &inode));
}

static uint64_t RestrictedTruncateSize(const struct bfs_handler *h,
                                       const bfs_file_t *file, uint64_t size,
                                       uint64_t current_size)
{
    if (size >= current_size) return size;
    for (const bfs_open_file_t *other = h->open_files; other; other = other->next) {
        if (&other->file != file && other->file.inode_nr == file->inode_nr &&
            other->file.offset > size)
            size = other->file.offset < current_size ? other->file.offset : current_size;
    }
    return size;
}

static int FileSeekMode(LONG mode)
{
    switch (mode) {
    case OFFSET_BEGINNING: return BFS_SEEK_SET;
    case OFFSET_CURRENT: return BFS_SEEK_CUR;
    case OFFSET_END: return BFS_SEEK_END;
    default: return -1;
    }
}

static LONG ResizeFile(struct bfs_handler *h, bfs_file_t *file, int64_t offset,
                        LONG mode, uint64_t limit, uint64_t *size)
{
    if (h->write_protected) return ERROR_DISK_WRITE_PROTECTED;
    LONG error = CheckProtection(h, file->inode_nr, FIBF_WRITE);
    if (error) return error;
    int seek_mode = FileSeekMode(mode);
    if (seek_mode < 0) return ERROR_BAD_NUMBER;
    bfs_file_t cursor = *file;
    int64_t target = bfs_file_seek(&cursor, offset, seek_mode);
    if (target < 0) return Pfs4ToDosError((bfs_err_t)target);
    uint64_t adjusted = RestrictedTruncateSize(h, file, (uint64_t)target, cursor.size);
    if (adjusted > limit) return ERROR_BAD_NUMBER;
    bfs_err_t err = bfs_file_truncate(file, adjusted);
    if (err != BFS_OK) return Pfs4ToDosError(err);
    error = MarkFileChanged(h, file->inode_nr);
    if (!error) *size = adjusted;
    return error;
}

static void HandleDosPacket64(bfs_dos_packet64_t *packet, struct bfs_handler *h)
{
    bool getter = packet->type == BFS_ACTION_GET_FILE_POSITION64 ||
                  packet->type == BFS_ACTION_GET_FILE_SIZE64;
    packet->result = getter ? -1 : DOSFALSE;
    packet->error = h->mount_error == BFS_ERR_UNSUPPORTED ?
                    Pfs4ToDosError(h->mount_error) : ERROR_NOT_A_DOS_DISK;
    if (!h->fs.mounted) return;
    if (h->fs.recovery_error != BFS_OK) {
        packet->error = Pfs4ToDosError(h->fs.recovery_error);
        return;
    }
    bfs_file_t *file = (bfs_file_t *)packet->handle;
    packet->error = ERROR_INVALID_LOCK;
    if (!FindOpenFile(h, file)) return;
    if (packet->type == BFS_ACTION_CHANGE_FILE_SIZE64) {
        uint64_t size;
        packet->error = ResizeFile(h, file, packet->offset, packet->mode, INT64_MAX, &size);
    } else {
        int mode = getter ? BFS_SEEK_CUR : FileSeekMode(packet->mode);
        packet->error = ERROR_BAD_NUMBER;
        if (mode < 0) return;
        int64_t position = bfs_file_seek(file, getter ? 0 : packet->offset, mode);
        packet->error = position < 0 ? Pfs4ToDosError((bfs_err_t)position) : 0;
        if (!packet->error && getter)
            packet->result = packet->type == BFS_ACTION_GET_FILE_SIZE64 ?
                             (int64_t)file->size : position;
    }
    if (!packet->error && !getter) packet->result = DOSTRUE;
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
    fib->fib_Size = size > INT32_MAX ? INT32_MAX : (LONG)size;
    uint64_t blocks = size / 512 + (size % 512 != 0);
    fib->fib_NumBlocks = blocks > INT32_MAX ? INT32_MAX : (LONG)blocks;

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

static void FillFib64(struct FileInfoBlock *fib, uint64_t size)
{
    uint64_t blocks = size / 512 + (size % 512 != 0);
    _Static_assert(sizeof(fib->fib_Reserved) >= 2 * sizeof(uint64_t), "FIB quadword capacity");
    /* Two adjacent quadwords fit the reserved ABI region verified above. */
    memcpy(fib->fib_Reserved, &size, sizeof(size)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    memcpy(fib->fib_Reserved + sizeof(size), &blocks, sizeof(blocks)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    if (size > INT32_MAX) fib->fib_Size = 0;
    if (blocks > INT32_MAX) fib->fib_NumBlocks = 0;
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
    bfs_err_t err;
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

    if ((size_t)(ec->end - ec->pos) < (size_t)entry_size) {
        ec->overflow = true;
        return false; /* stop scan */
    }

    /* Read inode for metadata fields */
    bfs_inode_t inode;
    uint64_t fsize = 0; uint32_t prot = 0;
    bool have_inode = false;
    if (ec->type >= ED_SIZE) {
        ec->err = bfs_inode_read(&ec->h->fs.inode_tree, inode_nr, &inode);
        if (ec->err != BFS_OK) return false;
        have_inode = true;
    }
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
    if (ec->type >= ED_SIZE) ead->ed_Size = fsize > INT32_MAX ? INT32_MAX : (ULONG)fsize;
    if (ec->type >= ED_PROTECTION) ead->ed_Prot = prot;
    if (ec->type >= ED_DATE && have_inode) {
        ead->ed_Days = bfs_be16(inode.modify_days);
        ead->ed_Mins = bfs_be16(inode.modify_mins);
        ead->ed_Ticks = bfs_be16(inode.modify_ticks);
    }
    if (ec->type >= ED_COMMENT) {
        char cbuf[80]; cbuf[0] = 0;
        bfs_err_t comment_err = bfs_fs_get_comment(&ec->h->fs, inode_nr,
                                                    cbuf, 79);
        if (comment_err != BFS_OK && comment_err != BFS_ERR_NOTFOUND) {
            ec->err = comment_err;
            return false;
        }
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
    LONG res1 = (pkt->dp_Type == ACTION_READ || pkt->dp_Type == ACTION_WRITE ||
                 pkt->dp_Type == ACTION_SEEK || pkt->dp_Type == ACTION_SET_FILE_SIZE ||
                 pkt->dp_Type == ACTION_READ_LINK) ?
                -1 : DOSFALSE;
    LONG res2 = ERROR_ACTION_NOT_KNOWN;

    if (pkt->dp_Type >= BFS_ACTION_CHANGE_FILE_POSITION64 &&
        pkt->dp_Type <= BFS_ACTION_GET_FILE_SIZE64) {
        if (pkt->dp_Res1 != BFS_DP64_INIT) goto reply;
        if (!h->fs.mounted) ReportFormatError(h, pkt->dp_Port);
        HandleDosPacket64((bfs_dos_packet64_t *)pkt, h);
        ReplyPacket(pkt, h);
        return;
    }

    if (!h->fs.mounted && pkt->dp_Type != ACTION_FORMAT &&
        pkt->dp_Type != BFS_ACTION_FORMAT_ERROR &&
        pkt->dp_Type != ACTION_DIE && pkt->dp_Type != ACTION_DISK_INFO &&
        pkt->dp_Type != ACTION_INFO &&
        pkt->dp_Type != ACTION_CURRENT_VOLUME &&
        pkt->dp_Type != ACTION_IS_FILESYSTEM &&
        pkt->dp_Type != ACTION_INHIBIT &&
        pkt->dp_Type != ACTION_WRITE_PROTECT &&
        pkt->dp_Type != ACTION_FREE_LOCK &&
        pkt->dp_Type != ACTION_END &&
        pkt->dp_Type != ACTION_REMOVE_NOTIFY) {
        res2 = h->mount_error != BFS_OK ? Pfs4ToDosError(h->mount_error) :
                                        ERROR_NOT_A_DOS_DISK;
        ReportFormatError(h, pkt->dp_Port);
        goto reply;
    }

    switch (pkt->dp_Type) {

    case BFS_ACTION_FORMAT_ERROR: {
        char *buffer = (char *)pkt->dp_Arg1;
        if (!buffer || pkt->dp_Arg2 < BFS_FORMAT_ERROR_MAX) {
            res2 = ERROR_BAD_NUMBER;
            break;
        }
        /* Packet capacity was checked above; the source has exactly this size. */
        memcpy(buffer, h->format_error, BFS_FORMAT_ERROR_MAX); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        res1 = h->format_error[0] ? DOSTRUE : DOSFALSE;
        res2 = 0;
        break;
    }

    case BFS_ACTION_CHECK: {
        ULONG *summary = (ULONG *)pkt->dp_Arg1;
        bfs_fs_t checked;
        bfs_fsck_report_t report;
        bfs_err_t err;

        if (!summary || pkt->dp_Arg2 <
                        (LONG)(BFS_CHECK_REPORT_WORDS * sizeof(*summary))) {
            res2 = ERROR_BAD_NUMBER;
            break;
        }

        /* The handler processes packets serially. Scan a separate read-only
         * mount so CHECK observes the last committed state without changing
         * the live write transaction. */
        err = bfs_fs_mount_readonly(&checked, &h->cache.bio);
        if (err != BFS_OK) {
            res2 = Pfs4ToDosError(err);
            break;
        }
        err = bfs_fs_check(&checked, false, &report);
        if (bfs_fs_unmount(&checked) != BFS_OK && err == BFS_OK)
            err = BFS_ERR_IO;
        if (err != BFS_OK && err != BFS_ERR_CORRUPT) {
            res2 = Pfs4ToDosError(err);
            break;
        }
        summary[0] = report.errors;
        summary[1] = report.warnings;
        summary[2] = report.leaked_blocks;
        summary[3] = report.repaired_blocks;
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── LOCATE_OBJECT ─────────────────────────────────────── */
    case ACTION_LOCATE_OBJECT: {
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino;
        bfs_err_t err = ResolvePath((BPTR)pkt->dp_Arg1,
                                    (BPTR)pkt->dp_Arg2, namebuf, &len,
                                    &parent_ino, h);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        uint32_t ino, type;

        uint32_t lock_parent = parent_ino;
        if (len == 0) {
            /* Empty final component refers to the resolved directory, not
             * necessarily the original lock (e.g. '/', '//' or ':'). */
            bfs_lock_t *parent_lock = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
            if (parent_lock && parent_lock->ino == parent_ino) {
                ino = parent_lock->ino;
                type = parent_lock->type;
                lock_parent = parent_lock->parent_ino;
            } else {
                ino = parent_ino;
                type = BFS_INODE_DIR;
                lock_parent = BFS_ROOT_INO;
                if (ino != BFS_ROOT_INO) {
                    err = bfs_dir_lookup(&h->fs.dir_tree, ino, "..", 2,
                                          &lock_parent, NULL);
                    if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
                }
            }
        } else {
            err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len,
                                  &ino, &type);
            if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        }

        bfs_lock_t *lk = MakeLock(h, ino, type, pkt->dp_Arg3, lock_parent);
        if (!lk) { res2 = IoErr() ? IoErr() : ERROR_NO_FREE_STORE; break; }
        res1 = (LONG)MKBADDR(lk);
        res2 = 0;
        break;
    }

    /* ── FREE_LOCK ─────────────────────────────────────────── */
    case ACTION_FREE_LOCK: {
        bfs_lock_t *lk = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        if (lk && !FreeLock(h, lk)) { res2 = ERROR_INVALID_LOCK; break; }
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
        if (!fh) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino;
        uint32_t ino, type;
        bfs_err_t err = ResolvePath((BPTR)pkt->dp_Arg2,
                                    (BPTR)pkt->dp_Arg3, namebuf, &len,
                                    &parent_ino, h);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        bfs_open_file_t *open_file = AllocateOpenFile();
        if (!open_file) { res2 = ERROR_NO_FREE_STORE; break; }

        err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len,
                              &ino, &type);
        BOOL existed = (err == BFS_OK);

        if (err == BFS_ERR_NOTFOUND) {
            if (pkt->dp_Type == ACTION_FINDOUTPUT ||
                pkt->dp_Type == ACTION_FINDUPDATE) {
                /* Create the file */
                err = bfs_fs_reserve(&h->fs, 5);
                if (err != BFS_OK) {
                    FreeVec(open_file); res2 = Pfs4ToDosError(err); break;
                }
                err = bfs_fs_create_file(&h->fs, parent_ino, namebuf, len, &ino);
                if (err != BFS_OK) {
                    FreeVec(open_file); res2 = Pfs4ToDosError(err); break;
                }
                type = BFS_INODE_FILE;
                /* Set creation/modification timestamp */
                { struct DateStamp ds; DateStamp(&ds);
                  bfs_inode_t ni;
                  err = bfs_inode_read(&h->fs.inode_tree, ino, &ni);
                  if (err == BFS_OK) {
                      ni.create_days = bfs_be16((uint16_t)ds.ds_Days);
                      ni.create_mins = bfs_be16((uint16_t)ds.ds_Minute);
                      ni.create_ticks = bfs_be16((uint16_t)ds.ds_Tick);
                      ni.modify_days = ni.create_days;
                      ni.modify_mins = ni.create_mins;
                      ni.modify_ticks = ni.create_ticks;
                      err = bfs_inode_write(&h->fs.inode_tree, ino, &ni);
                  }
                }
                if (err != BFS_OK) {
                    bfs_err_t cleanup_err = bfs_fs_delete_file(&h->fs, parent_ino, namebuf, len);
                    if (cleanup_err != BFS_OK) err = cleanup_err;
                    FreeVec(open_file);
                    h->dirty = true;
                    res2 = Pfs4ToDosError(err);
                    break;
                }
                h->dirty = true;
                h->notify_pending = true;
            } else {
                FreeVec(open_file);
                res2 = ERROR_OBJECT_NOT_FOUND;
                break;
            }
        } else if (err != BFS_OK) {
            FreeVec(open_file);
            res2 = Pfs4ToDosError(err);
            break;
        }

        if (type != BFS_INODE_FILE && type != BFS_INODE_HARDLINK) {
            FreeVec(open_file);
            res2 = ERROR_OBJECT_WRONG_TYPE;
            break;
        }

        LONG access = pkt->dp_Type == ACTION_FINDOUTPUT ? EXCLUSIVE_LOCK : SHARED_LOCK;
        if (InodeAccessConflicts(h, ino, access)) {
            FreeVec(open_file);
            res2 = ERROR_OBJECT_IN_USE;
            break;
        }

        uint32_t mask = pkt->dp_Type == ACTION_FINDOUTPUT ?
                        FIBF_DELETE | FIBF_WRITE : FIBF_READ;
        res2 = CheckProtection(h, ino, mask);
        if (res2) { FreeVec(open_file); break; }

        err = bfs_file_open(&open_file->file, &h->fs, ino);
        if (err != BFS_OK) {
            if (!existed) {
                bfs_err_t cleanup_err = bfs_fs_delete_file(&h->fs, parent_ino, namebuf, len);
                if (cleanup_err != BFS_OK) err = cleanup_err;
            }
            FreeVec(open_file);
            res2 = Pfs4ToDosError(err);
            break;
        }
        if (pkt->dp_Type == ACTION_FINDOUTPUT && existed) {
            /* Existing file opened for output — truncate to 0 */
            err = bfs_file_truncate(&open_file->file, 0);
            if (err != BFS_OK) {
                FreeVec(open_file);
                res2 = Pfs4ToDosError(err);
                break;
            }
            res2 = MarkFileChanged(h, ino);
            if (res2) { FreeVec(open_file); break; }
        }

        TrackOpenFile(h, open_file, access, parent_ino, type);
        fh->fh_Arg1 = (LONG)&open_file->file;
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── READ ──────────────────────────────────────────────── */
    case ACTION_READ: {
        res1 = -1;
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        void *buf = (void *)pkt->dp_Arg2;
        LONG len = pkt->dp_Arg3;
        if (!FindOpenFile(h, f) || len < 0 || (!buf && len != 0)) {
            res1 = -1;
            res2 = ERROR_BAD_NUMBER;
            break;
        }

        res2 = CheckProtection(h, f->inode_nr, FIBF_READ);
        if (res2) break;
        int32_t n = bfs_file_read(f, buf, (uint32_t)len);
        if (n < 0) {
            res1 = -1;
            res2 = Pfs4ToDosError((bfs_err_t)n);
        } else {
            res1 = n;
            res2 = 0;
        }
        break;
    }

    /* ── WRITE ─────────────────────────────────────────────── */
    case ACTION_WRITE: {
        res1 = -1;
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        void *buf = (void *)pkt->dp_Arg2;
        LONG len = pkt->dp_Arg3;
        bfs_open_file_t *open_file = FindOpenFile(h, f);
        if (!open_file || len < 0 ||
            (!buf && len != 0)) {
            res1 = -1;
            res2 = open_file ? ERROR_BAD_NUMBER : ERROR_INVALID_LOCK;
            break;
        }

        res2 = CheckProtection(h, f->inode_nr, FIBF_WRITE);
        if (res2) break;
        int32_t n = bfs_file_write(f, buf, (uint32_t)len);
        if (n < 0) {
            res1 = -1;
            res2 = Pfs4ToDosError((bfs_err_t)n);
        } else {
            res2 = n > 0 ? MarkFileChanged(h, f->inode_nr) : 0;
            res1 = res2 ? -1 : n;
        }
        break;
    }

    /* ── SEEK ──────────────────────────────────────────────── */
    case ACTION_SEEK: {
        res1 = -1;
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        LONG offset = pkt->dp_Arg2;
        LONG mode = pkt->dp_Arg3;
        if (!FindOpenFile(h, f)) { res2 = ERROR_INVALID_LOCK; break; }

        /* Return old position */
        LONG old_pos = (LONG)f->offset;

        /* Amiga seek modes: -1=BEGINNING, 0=CURRENT, 1=END */
        int bfs_mode;
        switch (mode) {
        case OFFSET_BEGINNING: bfs_mode = BFS_SEEK_SET; break;
        case OFFSET_CURRENT:   bfs_mode = BFS_SEEK_CUR; break;
        case OFFSET_END:       bfs_mode = BFS_SEEK_END; break;
        default:               res2 = ERROR_BAD_NUMBER; break;
        }
        if (res2 == ERROR_BAD_NUMBER) break;

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
        if (!FindOpenFile(h, f)) { res2 = ERROR_INVALID_LOCK; break; }
        bfs_err_t err = BFS_OK;
        if (h->dirty) {
            err = bfs_fs_sync(&h->fs);
            if (err == BFS_OK) {
                h->dirty = false;
                if (h->notify_pending) SendNotifications(h);
                h->notify_pending = false;
            }
        }
        FreeOpenFile(h, f);
        res1 = (err == BFS_OK) ? DOSTRUE : DOSFALSE;
        res2 = Pfs4ToDosError(err);
        break;
    }

    /* ── CREATE_DIR ────────────────────────────────────────── */
    case ACTION_CREATE_DIR: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino;
        bfs_err_t err = ResolvePath((BPTR)pkt->dp_Arg1,
                                    (BPTR)pkt->dp_Arg2, namebuf, &len,
                                    &parent_ino, h);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        bfs_lock_t *lk = (bfs_lock_t *)AllocVec(sizeof(bfs_lock_t), MEMF_CLEAR);
        if (!lk) { res2 = ERROR_NO_FREE_STORE; break; }
        err = bfs_fs_reserve(&h->fs, 5);
        if (err != BFS_OK) {
            FreeVec(lk); res2 = Pfs4ToDosError(err); break;
        }
        uint32_t ino;
        err = bfs_fs_mkdir(&h->fs, parent_ino, namebuf, len, &ino);
        if (err != BFS_OK) {
            FreeVec(lk); res2 = Pfs4ToDosError(err); break;
        }

        AttachLock(h, lk, ino, BFS_INODE_DIR, SHARED_LOCK, parent_ino);
        res1 = (LONG)MKBADDR(lk);
        res2 = 0;
        h->dirty = true;
        h->notify_pending = true;
        break;
    }

    /* ── DELETE_OBJECT ─────────────────────────────────────── */
    case ACTION_DELETE_OBJECT: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        res2 = Pfs4ToDosError(bfs_fs_reserve(&h->fs, 10));
        if (res2) break;
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino;
        bfs_err_t err = ResolvePath((BPTR)pkt->dp_Arg1,
                                    (BPTR)pkt->dp_Arg2, namebuf, &len,
                                    &parent_ino, h);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        /* Look up to determine type */
        uint32_t ino, type;
        err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len,
                             &ino, &type);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        if (InodeIsInUse(h, ino)) { res2 = ERROR_OBJECT_IN_USE; break; }

        res2 = CheckProtection(h, ino, FIBF_DELETE);
        if (res2) break;

        if (type == BFS_INODE_DIR)
            err = bfs_fs_rmdir(&h->fs, parent_ino, namebuf, len);
        else
            err = bfs_fs_delete_file(&h->fs, parent_ino, namebuf, len);

        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        res1 = DOSTRUE;
        res2 = 0;
        h->dirty = true;
        h->notify_pending = true;
        break;
    }

    /* ── RENAME_OBJECT ─────────────────────────────────────── */
    case ACTION_RENAME_OBJECT: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        res2 = Pfs4ToDosError(bfs_fs_reserve(&h->fs, 8));
        if (res2) break;
        char old_name[BFS_NAME_MAX + 1], new_name[BFS_NAME_MAX + 1];
        uint8_t old_len, new_len;
        uint32_t old_parent, new_parent;
        bfs_err_t err = ResolvePath((BPTR)pkt->dp_Arg1,
                                    (BPTR)pkt->dp_Arg2, old_name, &old_len,
                                    &old_parent, h);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        err = ResolvePath((BPTR)pkt->dp_Arg3, (BPTR)pkt->dp_Arg4,
                          new_name, &new_len, &new_parent, h);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        uint32_t ino;
        err = bfs_dir_lookup(&h->fs.dir_tree, old_parent, old_name,
                             old_len, &ino, NULL);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        if (InodeIsInUse(h, ino)) { res2 = ERROR_OBJECT_IN_USE; break; }

        err = bfs_fs_rename(&h->fs, old_parent, old_name, old_len,
                            new_parent, new_name, new_len);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        res1 = DOSTRUE;
        res2 = 0;
        h->dirty = true;
        h->notify_pending = true;
        break;
    }

    /* ── EXAMINE_OBJECT ────────────────────────────────────── */
    case BFS_ACTION_EXAMINE_OBJECT64:
    case ACTION_EXAMINE_OBJECT: {
        bfs_lock_t *lk = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        struct FileInfoBlock *fib = (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
        if (!lk || !fib) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        if (!LockIsOwned(h, lk)) { res2 = ERROR_INVALID_LOCK; break; }

        uint64_t size = 0;
        uint32_t prot = 0;
        bfs_inode_t inode;
        bfs_err_t err = bfs_inode_read(&h->fs.inode_tree, lk->ino, &inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        size = ((uint64_t)bfs_be32(inode.size_hi) << 32) | bfs_be32(inode.size_lo);
        prot = bfs_be32(inode.protection);

        char name[BFS_NAME_MAX + 1];
        uint8_t name_len;
        err = NameForLock(h, lk, name, &name_len);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        FillFib(fib, name, name_len, lk->ino, lk->type, size, prot,
                &inode);
        if (pkt->dp_Type == BFS_ACTION_EXAMINE_OBJECT64) FillFib64(fib, size);
        /* Read file comment */
        { char cb[80];
          err = bfs_fs_get_comment(&h->fs, lk->ino, cb, 79);
          if (err != BFS_OK && err != BFS_ERR_NOTFOUND) {
              res2 = Pfs4ToDosError(err); break;
          }
          if (err == BFS_OK) {
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
    case BFS_ACTION_EXAMINE_NEXT64:
    case ACTION_EXAMINE_NEXT: {
        bfs_lock_t *lk = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        struct FileInfoBlock *fib = (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
        if (!lk || !fib) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        if (!LockIsOwned(h, lk)) { res2 = ERROR_INVALID_LOCK; break; }

        char namebuf[BFS_NAME_MAX + 1];
        exam_next_ctx_t ctx;
        ctx.skip_count = (uint32_t)fib->fib_DiskKey;
        ctx.seen = 0;
        ctx.name_out = namebuf;
        ctx.got_entry = false;

        bfs_err_t err = bfs_dir_scan(&h->fs.dir_tree, lk->ino,
                                     exam_next_cb, &ctx);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        if (!ctx.got_entry) {
            res2 = ERROR_NO_MORE_ENTRIES;
            break;
        }

        /* Read inode for size, protection, dates */
        bfs_inode_t en_inode;
        uint64_t en_size = 0; uint32_t en_prot = 0;
        err = bfs_inode_read(&h->fs.inode_tree, ctx.ino_out, &en_inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        en_size = ((uint64_t)bfs_be32(en_inode.size_hi) << 32) | bfs_be32(en_inode.size_lo);
        en_prot = bfs_be32(en_inode.protection);
        FillFib(fib, namebuf, ctx.name_len, ctx.ino_out, ctx.type_out, en_size, en_prot, &en_inode);
        if (pkt->dp_Type == BFS_ACTION_EXAMINE_NEXT64) FillFib64(fib, en_size);
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
        uint32_t parent_ino;
        bfs_err_t err = ResolvePath((BPTR)pkt->dp_Arg2,
                                    (BPTR)pkt->dp_Arg3, namebuf, &len,
                                    &parent_ino, h);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        uint32_t ino, type;
        err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len,
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
        h->notify_pending = true;
        break;
    }

    /* ── SAME_LOCK ─────────────────────────────────────────── */
    case ACTION_SAME_LOCK: {
        bfs_lock_t *lock1 = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        bfs_lock_t *lock2 = (bfs_lock_t *)BADDR(pkt->dp_Arg2);
        if ((lock1 && !LockIsOwned(h, lock1)) ||
            (lock2 && !LockIsOwned(h, lock2))) {
            res2 = ERROR_INVALID_LOCK; break;
        }
        uint32_t ino1 = LockIno((BPTR)pkt->dp_Arg1);
        uint32_t ino2 = LockIno((BPTR)pkt->dp_Arg2);
        /* The packet is Boolean; dos.library translates it to LOCK_* codes. */
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
            res1 = DOSTRUE;
            res2 = 0;
        } else {
            res2 = ERROR_REQUIRED_ARG_MISSING;
        }
        break;
    }

    /* ── PARENT ────────────────────────────────────────────── */
    case ACTION_PARENT: {
        bfs_lock_t *src = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        if (src && !LockIsOwned(h, src)) { res2 = ERROR_INVALID_LOCK; break; }
        uint32_t src_ino = src ? src->ino : BFS_ROOT_INO;

        if (src_ino == BFS_ROOT_INO) {
            res1 = 0; /* NULL lock = root has no parent */
            res2 = 0;
            break;
        }

        uint32_t par = src ? src->parent_ino : BFS_ROOT_INO;
        uint32_t grandparent = BFS_ROOT_INO;
        if (par != BFS_ROOT_INO) {
            bfs_err_t err = bfs_dir_lookup(&h->fs.dir_tree, par, "..", 2,
                                           &grandparent, NULL);
            if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        }
        bfs_lock_t *lk = MakeLock(h, par, BFS_INODE_DIR, SHARED_LOCK, grandparent);
        if (!lk) { res2 = IoErr() ? IoErr() : ERROR_NO_FREE_STORE; break; }
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
        uint32_t parent_ino;
        bfs_err_t err = ResolvePath((BPTR)pkt->dp_Arg1,
                                    (BPTR)pkt->dp_Arg2, namebuf, &len,
                                    &parent_ino, h);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        LONG soft_flag = pkt->dp_Arg4;

        if (soft_flag == 0) {
            if (!pkt->dp_Arg3) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
            bfs_lock_t *target = (bfs_lock_t *)BADDR(pkt->dp_Arg3);
            if (!LockIsOwned(h, target)) { res2 = ERROR_INVALID_LOCK; break; }
            uint32_t target_ino = LockIno((BPTR)pkt->dp_Arg3);
            err = bfs_fs_make_hardlink(&h->fs, parent_ino, namebuf, len,
                                       target_ino);
            if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        } else {
            UBYTE *bpath = (UBYTE *)BADDR(pkt->dp_Arg3);
            if (!bpath || bpath[0] == 0) {
                res2 = ERROR_REQUIRED_ARG_MISSING; break;
            }
            uint8_t plen = bpath[0];
            err = bfs_fs_make_softlink(&h->fs, parent_ino, namebuf, len,
                                       (const char *)&bpath[1], plen);
            if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        }
        res1 = DOSTRUE;
        res2 = 0;
        h->dirty = true;
        h->notify_pending = true;
        break;
    }

    /* ── ACTION_READ_LINK ──────────────────────────────────── */
    case ACTION_READ_LINK: {
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino;
        bfs_err_t err = ResolvePath((BPTR)pkt->dp_Arg1,
                                    (BPTR)pkt->dp_Arg2, namebuf, &len,
                                    &parent_ino, h);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        uint32_t ino, type;
        err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len,
                             &ino, &type);
        if (err != BFS_OK || type != BFS_INODE_SOFTLINK) { res2 = ERROR_OBJECT_NOT_FOUND; break; }

        bfs_file_t f;
        err = bfs_file_open(&f, &h->fs, ino);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        char *buf = (char *)pkt->dp_Arg3;
        LONG bufsize = pkt->dp_Arg4;
        if (!buf || bufsize <= 0) { res2 = ERROR_BAD_NUMBER; break; }
        int32_t n = bfs_file_read(&f, buf, (uint32_t)(bufsize - 1));
        if (n < 0) { res2 = Pfs4ToDosError((bfs_err_t)n); break; }
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
        uint32_t parent_ino;
        bfs_err_t err = ResolvePath((BPTR)pkt->dp_Arg2,
                                    (BPTR)pkt->dp_Arg3, namebuf, &len,
                                    &parent_ino, h);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        uint32_t ino, type;
        err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len,
                             &ino, &type);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        UBYTE *bcomment = (UBYTE *)BADDR(pkt->dp_Arg4);
        if (!bcomment) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        uint8_t clen = bcomment[0];
        err = bfs_fs_set_comment(&h->fs, ino, (const char *)&bcomment[1], clen);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        res1 = DOSTRUE;
        res2 = 0;
        h->dirty = true;
        h->notify_pending = true;
        break;
    }

    /* ── ACTION_SET_FILE_SIZE ──────────────────────────────── */
    case ACTION_SET_FILE_SIZE: {
        res1 = -1;
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        LONG offset = pkt->dp_Arg2;
        LONG mode = pkt->dp_Arg3;
        bfs_open_file_t *open_file = FindOpenFile(h, f);
        if (!open_file) {
            res2 = ERROR_INVALID_LOCK; break;
        }

        uint64_t new_size;
        res2 = ResizeFile(h, f, offset, mode, INT32_MAX, &new_size);
        if (res2) break;
        res1 = (LONG)new_size;
        break;
    }

    /* ── ACTION_COPY_DIR ───────────────────────────────────── */
    case ACTION_COPY_DIR: {
        bfs_lock_t *src = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        if (src && !LockIsOwned(h, src)) { res2 = ERROR_INVALID_LOCK; break; }
        if (!src) {
            bfs_lock_t *lk = MakeLock(h, BFS_ROOT_INO, BFS_INODE_DIR, SHARED_LOCK, 0);
            if (!lk) { res2 = IoErr() ? IoErr() : ERROR_NO_FREE_STORE; break; }
            res1 = (LONG)MKBADDR(lk);
        } else {
            bfs_lock_t *lk = MakeLock(h, src->ino, src->type, src->fl.fl_Access, src->parent_ino);
            if (!lk) { res2 = IoErr() ? IoErr() : ERROR_NO_FREE_STORE; break; }
            res1 = (LONG)MKBADDR(lk);
        }
        res2 = 0;
        break;
    }

    /* ── ACTION_EXAMINE_ALL ────────────────────────────────── */
    case ACTION_EXAMINE_ALL: {
        bfs_lock_t *lk = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
        UBYTE *buffer = (UBYTE *)pkt->dp_Arg2;
        LONG bufsize = pkt->dp_Arg3;
        LONG type = pkt->dp_Arg4;
        struct ExAllControl *eac = (struct ExAllControl *)pkt->dp_Arg5;
        if (!lk || !buffer || !eac) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        if (!LockIsOwned(h, lk)) { res2 = ERROR_INVALID_LOCK; break; }
        if (lk->type != BFS_INODE_DIR) { res2 = ERROR_OBJECT_WRONG_TYPE; break; }
        /* Let dos.library's ExNext fallback invoke optional caller hooks. */
        if (eac->eac_MatchFunc) { res2 = ERROR_ACTION_NOT_KNOWN; break; }
        if (bufsize < (LONG)sizeof(struct ExAllData) ||
            type < ED_NAME || type > ED_COMMENT) {
            res2 = ERROR_BAD_NUMBER; break;
        }

        eac->eac_Entries = 0;
        exall_optimized_ctx_t ectx = {
            .h = h, .eac = eac, .buffer = buffer, .pos = buffer,
            .end = buffer + bufsize, .type = type,
            .skip_count = (uint32_t)eac->eac_LastKey, .seen = 0,
            .last_ead = NULL, .overflow = false, .err = BFS_OK
        };

        bfs_err_t err = bfs_dir_scan(&h->fs.dir_tree, lk->ino,
                                     exall_optimized_cb, &ectx);
        if (err == BFS_OK) err = ectx.err;
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        if (eac->eac_Entries > 0) {
            /* Return DOSTRUE if we found any entries; ectx.overflow signals if more remain */
            res1 = ectx.overflow ? DOSTRUE : DOSFALSE;
            res2 = ectx.overflow ? 0 : ERROR_NO_MORE_ENTRIES;
        } else {
            res2 = ectx.overflow ? ERROR_BUFFER_OVERFLOW : ERROR_NO_MORE_ENTRIES;
        }
        break;
    }

    case ACTION_EXAMINE_ALL_END:
        res1 = DOSTRUE; res2 = 0;
        break;

    /* ── ACTION_LOCK_RECORD / ACTION_FREE_RECORD ──────────── */
    case ACTION_LOCK_RECORD:
    case ACTION_FREE_RECORD:
        res2 = ERROR_ACTION_NOT_KNOWN;
        break;

    /* ── BFS Snapshot packets ──────────────────────────────── */
    case 3000: { /* ACTION_BFS_SNAPSHOT_CREATE */
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        UBYTE *bname = (UBYTE *)BADDR(pkt->dp_Arg1);
        if (!bname || bname[0] == 0 || bname[0] >= BFS_SNAPSHOT_NAME_MAX) {
            res2 = ERROR_INVALID_COMPONENT_NAME; break;
        }
        uint8_t nlen = bname[0];
        char name[BFS_NAME_BSTR_MAX];
        memcpy(name, &bname[1], nlen);
        name[nlen] = 0;
        bfs_err_t err = bfs_snapshot_create(&h->fs, name);
        if (err == BFS_OK) {
            res1 = DOSTRUE;
            res2 = 0;
            SendNotifications(h);
        } else {
            res2 = Pfs4ToDosError(err);
        }
        break;
    }
    case 3001: { /* ACTION_BFS_SNAPSHOT_DELETE */
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        UBYTE *bname = (UBYTE *)BADDR(pkt->dp_Arg1);
        if (!bname || bname[0] == 0 || bname[0] >= BFS_SNAPSHOT_NAME_MAX) {
            res2 = ERROR_INVALID_COMPONENT_NAME; break;
        }
        uint8_t nlen = bname[0];
        char name[BFS_NAME_BSTR_MAX];
        memcpy(name, &bname[1], nlen);
        name[nlen] = 0;
        uint32_t id = 0;
        bfs_err_t err = bfs_snapshot_find_by_name(&h->fs, name, &id, NULL);
        if (err == BFS_OK)
            err = bfs_snapshot_delete(&h->fs, id);
        if (err == BFS_OK) {
            res1 = DOSTRUE;
            res2 = 0;
            SendNotifications(h);
        } else {
            res2 = Pfs4ToDosError(err);
        }
        break;
    }
    case 3002: { /* ACTION_BFS_SNAPSHOT_LIST */
        /* dp_Arg1 = buffer (APTR), dp_Arg2 = bufsize, dp_Arg3 = last_id (0=start)
         * Returns: DOSTRUE + name\0 + big-endian id + big-endian timestamp
         *          DOSFALSE when no more entries
         *          dp_Res2 = next last_id */
        char *outbuf = (char *)pkt->dp_Arg1;
        LONG bufsize = pkt->dp_Arg2;
        uint32_t last_id = (uint32_t)pkt->dp_Arg3;

        if (!outbuf || bufsize < 64) {
            res2 = ERROR_BAD_NUMBER;
            break;
        }
        if (!h->fs.has_snapshots) {
            res2 = 0;
            break;
        }

        snap_next_ctx_t sn = { .last_id = last_id, .found = false };
        bfs_err_t err = bfs_snapshot_list(&h->fs, snap_next_cb, &sn);
        if (err != BFS_OK) {
            res2 = Pfs4ToDosError(err);
            break;
        }
        if (!sn.found) {
            res2 = 0;
            break;
        }

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
         *          Buffer contains type, name, size, protection and date. */
        if (!h->fs.has_snapshots) { res2 = ERROR_OBJECT_NOT_FOUND; break; }
        UBYTE *bname = (UBYTE *)BADDR(pkt->dp_Arg1);
        char *outbuf = (char *)pkt->dp_Arg2;
        LONG bufsize = pkt->dp_Arg3;
        uint32_t last_key = (uint32_t)pkt->dp_Arg4;
        if (!bname || bname[0] == 0 ||
            bname[0] >= BFS_SNAPSHOT_NAME_MAX || !outbuf ||
            bufsize < BFS_SNAPSHOT_ENTRY_SIZE) {
            res2 = ERROR_BAD_NUMBER; break;
        }

        /* Find snapshot by name */
        uint8_t nlen = bname[0];
        char sname[BFS_NAME_BSTR_MAX]; memcpy(sname, &bname[1], nlen); sname[nlen] = 0;

        bfs_snapshot_record_t rec;
        if (bfs_snapshot_find_by_name(&h->fs, sname, NULL, &rec) != BFS_OK) {
            res2 = ERROR_OBJECT_NOT_FOUND; break;
        }

        /* Walk the snapshot's dir tree */
        bfs_dir_tree_t snap_dir;
        bfs_btree_t snap_inodes;
        bfs_err_t err = bfs_snapshot_open(
            &rec, h->fs.bio, bfs_freespace_allocator(&h->fs.freespace),
            &snap_dir, &snap_inodes);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        char namebuf[BFS_NAME_MAX + 1];
        exam_next_ctx_t ectx;
        ectx.skip_count = last_key;
        ectx.seen = 0;
        ectx.name_out = namebuf;
        ectx.got_entry = false;
        err = bfs_dir_scan(&snap_dir, BFS_ROOT_INO, exam_next_cb, &ectx);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        if (ectx.got_entry) {
            bfs_inode_t inode;
            err = bfs_inode_read(&snap_inodes, ectx.ino_out, &inode);
            if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
            uint64_t size = ((uint64_t)bfs_be32(inode.size_hi) << 32) |
                            bfs_be32(inode.size_lo);

            memset(outbuf, 0, BFS_SNAPSHOT_ENTRY_SIZE);
            outbuf[0] = (ectx.type_out == BFS_INODE_DIR) ? 'D' : 'F';
            outbuf[1] = (char)ectx.name_len;
            memcpy(outbuf + 2, namebuf, ectx.name_len);
            bfs_store_be32(outbuf + BFS_SNAPSHOT_ENTRY_META_OFFSET,
                           (uint32_t)(size >> 32));
            bfs_store_be32(outbuf + BFS_SNAPSHOT_ENTRY_META_OFFSET + 4,
                           (uint32_t)size);
            bfs_store_be32(outbuf + BFS_SNAPSHOT_ENTRY_META_OFFSET + 8,
                           bfs_be32(inode.protection));
            bfs_store_be32(outbuf + BFS_SNAPSHOT_ENTRY_META_OFFSET + 12,
                           bfs_be16(inode.modify_days));
            bfs_store_be32(outbuf + BFS_SNAPSHOT_ENTRY_META_OFFSET + 16,
                           bfs_be16(inode.modify_mins));
            bfs_store_be32(outbuf + BFS_SNAPSHOT_ENTRY_META_OFFSET + 20,
                           bfs_be16(inode.modify_ticks));
            res1 = DOSTRUE;
            res2 = (LONG)(ectx.skip_count + 1);
        } else {
            res2 = 0;
        }
        break;
    }

    /* ── ACTION_ADD_NOTIFY ─────────────────────────────────── */
    case ACTION_ADD_NOTIFY: {
        struct NotifyRequest *nr = (struct NotifyRequest *)pkt->dp_Arg1;
        if (!nr) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
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
        if (!nr) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        struct bfs_notify **pp = &h->notify_list;
        bool found = false;
        while (*pp) {
            if ((*pp)->nr == nr) {
                struct bfs_notify *tmp = *pp;
                *pp = tmp->next;
                FreeVec(tmp);
                found = true;
                break;
            }
            pp = &(*pp)->next;
        }
        if (!found) { res2 = ERROR_OBJECT_NOT_FOUND; break; }
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── ACTION_RENAME_DISK ────────────────────────────────── */
    case ACTION_RENAME_DISK: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        UBYTE *bname = (UBYTE *)BADDR(pkt->dp_Arg1);
        if (!bname || bname[0] == 0 || bname[0] >= BFS_VOLNAME_MAX) {
            res2 = ERROR_INVALID_COMPONENT_NAME; break;
        }
        uint8_t nlen = bname[0];
        UBYTE *new_node_name = AllocateBstr(&bname[1], nlen);
        if (!new_node_name) { res2 = ERROR_NO_FREE_STORE; break; }

        memset(h->fs.txn.sb_new.volname, 0, BFS_VOLNAME_MAX);
        memcpy(h->fs.txn.sb_new.volname, &bname[1], nlen);
        bfs_err_t err = bfs_fs_sync(&h->fs);
        bool committed = memcmp(h->fs.txn.sb.volname, &bname[1], nlen) == 0 &&
                         h->fs.txn.sb.volname[nlen] == 0;
        if (committed && h->volnode) {
            ReplaceVolumeNodeName(h, new_node_name);
            new_node_name = NULL;
        }
        if (new_node_name) FreeVec(new_node_name);
        if (err != BFS_OK) {
            if (!committed) h->fs.txn.sb_new = h->fs.txn.sb;
            res2 = Pfs4ToDosError(err);
            break;
        }
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── DIE ───────────────────────────────────────────────── */
    case ACTION_DIE: {
        if (HandlerIsInUse(h)) { res2 = ERROR_OBJECT_IN_USE; break; }
        bfs_err_t err = h->fs.mounted ? bfs_fs_unmount(&h->fs) : BFS_OK;
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        h->dirty = false;
        h->should_exit = true;
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── ACTION_CURRENT_VOLUME ─────────────────────────────── */
    case ACTION_CURRENT_VOLUME:
        res1 = h->volnode ? (LONG)MKBADDR(h->volnode) : 0;
        res2 = 0;
        break;

    /* ── ACTION_INFO ───────────────────────────────────────── */
    case ACTION_INFO: {
        struct InfoData *id = (struct InfoData *)BADDR(pkt->dp_Arg2);
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
            res1 = DOSTRUE;
            res2 = 0;
        } else {
            res2 = ERROR_REQUIRED_ARG_MISSING;
        }
        break;
    }

    /* ── ACTION_FLUSH ──────────────────────────────────────── */
    case ACTION_FLUSH:
    {
        bfs_err_t err = bfs_fs_sync(&h->fs);
        if (err == BFS_OK) {
            h->dirty = false;
            if (h->notify_pending) SendNotifications(h);
            h->notify_pending = false;
        }
        res1 = (err == BFS_OK) ? DOSTRUE : DOSFALSE;
        res2 = Pfs4ToDosError(err);
        break;
    }

    /* ── ACTION_CHANGE_MODE ────────────────────────────────── */
    case ACTION_CHANGE_MODE:
        res2 = ERROR_ACTION_NOT_KNOWN;
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
        if (h->mount_error == BFS_ERR_UNSUPPORTED) {
            ReportFormatError(h, pkt->dp_Port);
            res2 = Pfs4ToDosError(h->mount_error); break;
        }
        if (HandlerIsInUse(h)) { res2 = ERROR_OBJECT_IN_USE; break; }
        UBYTE *bname = (UBYTE *)BADDR(pkt->dp_Arg1);
        if (!bname || bname[0] == 0 || bname[0] >= BFS_VOLNAME_MAX) {
            res2 = ERROR_INVALID_COMPONENT_NAME; break;
        }
        uint8_t nlen = bname[0];
        char volname[BFS_VOLNAME_MAX];
        memcpy(volname, &bname[1], nlen);
        volname[nlen] = 0;

        /* Check the proposed geometry before unmounting or changing the cache. */
        amiga_bio_t *ab = (amiga_bio_t *)(h + 1);
        amiga_bio_t proposed = *ab;
        bfs_err_t err = bfs_amiga_bio_set_blocksize(&proposed, 4096);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        if (h->fs.mounted) err = bfs_fs_unmount(&h->fs);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        h->dirty = false;
        h->notify_pending = false;
        RemoveVolumeNode(h);
        /* Set block size to 4096 (BFS default) and reinit cache */
        ab->base.block_size = proposed.base.block_size;
        ab->base.block_count = proposed.base.block_count;
        bfs_cache_destroy(&h->cache);
        err = bfs_cache_init(&h->cache, (bfs_bio_t *)ab,
                             h->dosenvec->de_NumBuffers);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        err = bfs_fs_format(&h->cache.bio, volname, 0);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        memset(&h->fs, 0, sizeof(h->fs));
        bfs_cache_invalidate(&h->cache);
        err = bfs_fs_mount(&h->fs, &h->cache.bio);
        SetMountError(h, err, &h->fs.txn.sb);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        h->volnode = RegisterVolumeNode(h, volname);
        if (!h->volnode) {
            res2 = IoErr() ? IoErr() : ERROR_NO_FREE_STORE;
            bfs_fs_abandon(&h->fs);
            bfs_cache_destroy(&h->cache);
            break;
        }
        h->media_changed = false;
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── ACTION_FH_FROM_LOCK ───────────────────────────────── */
    case ACTION_FH_FROM_LOCK: {
        struct FileHandle *fh = (struct FileHandle *)BADDR(pkt->dp_Arg1);
        bfs_lock_t *lk = (bfs_lock_t *)BADDR(pkt->dp_Arg2);
        if (!fh || !lk) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        if (!LockIsOwned(h, lk)) { res2 = ERROR_INVALID_LOCK; break; }
        if (lk->type != BFS_INODE_FILE && lk->type != BFS_INODE_HARDLINK) {
            res2 = ERROR_OBJECT_WRONG_TYPE; break;
        }

        bfs_open_file_t *open_file = AllocateOpenFile();
        if (!open_file) { res2 = ERROR_NO_FREE_STORE; break; }
        bfs_err_t err = bfs_file_open(&open_file->file, &h->fs, lk->ino);
        if (err != BFS_OK) {
            FreeVec(open_file);
            res2 = Pfs4ToDosError(err);
            break;
        }
        LONG access = lk->fl.fl_Access;
        uint32_t parent_ino = lk->parent_ino;
        uint32_t type = lk->type;
        if (!FreeLock(h, lk)) {
            FreeVec(open_file);
            res2 = ERROR_INVALID_LOCK;
            break;
        }
        TrackOpenFile(h, open_file, access, parent_ino, type);
        fh->fh_Arg1 = (LONG)&open_file->file;
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── ACTION_PARENT_FH ──────────────────────────────────── */
    case ACTION_PARENT_FH: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        bfs_open_file_t *open_file = FindOpenFile(h, f);
        if (!open_file) { res2 = ERROR_INVALID_LOCK; break; }
        uint32_t ino = f->inode_nr;
        if (ino == BFS_ROOT_INO) { res1 = 0; res2 = 0; break; }

        uint32_t par_ino = open_file->parent_ino;
        uint32_t grandparent = BFS_ROOT_INO;
        if (par_ino != BFS_ROOT_INO) {
            bfs_err_t err = bfs_dir_lookup(&h->fs.dir_tree, par_ino, "..", 2,
                                           &grandparent, NULL);
            if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        }

        bfs_lock_t *lk = MakeLock(h, par_ino, BFS_INODE_DIR, SHARED_LOCK,
                                  grandparent);
        if (!lk) { res2 = IoErr() ? IoErr() : ERROR_NO_FREE_STORE; break; }
        res1 = (LONG)MKBADDR(lk);
        res2 = 0;
        break;
    }

    /* ── ACTION_COPY_DIR_FH ────────────────────────────────── */
    case ACTION_COPY_DIR_FH: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        bfs_open_file_t *open_file = FindOpenFile(h, f);
        if (!open_file) { res2 = ERROR_INVALID_LOCK; break; }
        bfs_lock_t *lk = MakeLock(h, f->inode_nr, open_file->type,
                                  SHARED_LOCK, open_file->parent_ino);
        if (!lk) { res2 = IoErr() ? IoErr() : ERROR_NO_FREE_STORE; break; }
        res1 = (LONG)MKBADDR(lk);
        res2 = 0;
        break;
    }

    /* ── ACTION_EXAMINE_FH ─────────────────────────────────── */
    case BFS_ACTION_EXAMINE_FH64:
    case ACTION_EXAMINE_FH: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        struct FileInfoBlock *fib = (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
        if (!FindOpenFile(h, f) || !fib) {
            res2 = ERROR_REQUIRED_ARG_MISSING; break;
        }

        bfs_inode_t inode;
        bfs_err_t err = bfs_inode_read(&h->fs.inode_tree, f->inode_nr, &inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        uint64_t size = ((uint64_t)bfs_be32(inode.size_hi) << 32) | bfs_be32(inode.size_lo);
        uint32_t type = bfs_be32(inode.type);
        FillFib(fib, "", 0, f->inode_nr, type, size, bfs_be32(inode.protection), &inode);
        if (pkt->dp_Type == BFS_ACTION_EXAMINE_FH64) FillFib64(fib, size);
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
        uint32_t parent_ino;
        bfs_err_t err = ResolvePath((BPTR)pkt->dp_Arg2,
                                    (BPTR)pkt->dp_Arg3, namebuf, &len,
                                    &parent_ino, h);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        uint32_t ino, type;
        err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len,
                             &ino, &type);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        bfs_inode_t inode;
        err = bfs_inode_read(&h->fs.inode_tree, ino, &inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        LONG *ds = (LONG *)pkt->dp_Arg4; /* DateStamp: days, minute, tick */
        if (!ds) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        inode.modify_days = bfs_be16((uint16_t)ds[0]);
        inode.modify_mins = bfs_be16((uint16_t)ds[1]);
        inode.modify_ticks = bfs_be16((uint16_t)ds[2]);

        err = bfs_inode_write(&h->fs.inode_tree, ino, &inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        res1 = DOSTRUE;
        res2 = 0;
        h->dirty = true;
        h->notify_pending = true;
        break;
    }

    /* ── ACTION_SET_OWNER ──────────────────────────────────── */
    case ACTION_SET_OWNER: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        char namebuf[BFS_NAME_MAX + 1];
        uint8_t len;
        uint32_t parent_ino;
        bfs_err_t err = ResolvePath((BPTR)pkt->dp_Arg2,
                                    (BPTR)pkt->dp_Arg3, namebuf, &len,
                                    &parent_ino, h);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        uint32_t ino, type;
        err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len,
                             &ino, &type);
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
        h->notify_pending = true;
        break;
    }

    /* MorphOS passes input and output quadwords by pointer. */
    case BFS_ACTION_SEEK64:
    case BFS_ACTION_SET_FILE_SIZE64: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        if (!FindOpenFile(h, f)) { res2 = ERROR_INVALID_LOCK; break; }
        if (!pkt->dp_Arg2 || !pkt->dp_Arg4 ||
            (pkt->dp_Arg2 & 1) || (pkt->dp_Arg4 & 1)) {
            res2 = ERROR_BAD_NUMBER; break;
        }
        int64_t offset;
        uint64_t value = f->offset;
        /* MorphOS ABI supplies two aligned quadwords; destination is int64_t. */
        memcpy(&offset, (const void *)pkt->dp_Arg2, sizeof(offset)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        if (pkt->dp_Type == BFS_ACTION_SET_FILE_SIZE64) {
            res2 = ResizeFile(h, f, offset, pkt->dp_Arg3, INT64_MAX, &value);
        } else {
            int mode = FileSeekMode(pkt->dp_Arg3);
            if (mode < 0) { res2 = ERROR_BAD_NUMBER; break; }
            int64_t position = bfs_file_seek(f, offset, mode);
            res2 = position < 0 ? Pfs4ToDosError((bfs_err_t)position) : 0;
        }
        if (!res2) {
            /* The caller-owned output is exactly one ABI quadword. */
            memcpy((void *)pkt->dp_Arg4, &value, sizeof(value)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
            res1 = DOSTRUE;
        }
        break;
    }

    default:
        res2 = ERROR_ACTION_NOT_KNOWN;
        break;
    }

    /* Sync standalone metadata operations. File mutations are committed by
     * ACTION_END so rapid writes to one open handle share one transaction. */
    if (h->dirty && res2 == 0 &&
                    (pkt->dp_Type == ACTION_DELETE_OBJECT ||
                     pkt->dp_Type == ACTION_CREATE_DIR ||
                     pkt->dp_Type == ACTION_RENAME_OBJECT ||
                     pkt->dp_Type == ACTION_MAKE_LINK ||
                     pkt->dp_Type == ACTION_SET_COMMENT ||
                     pkt->dp_Type == ACTION_SET_PROTECT ||
                     pkt->dp_Type == ACTION_SET_DATE ||
                     pkt->dp_Type == ACTION_SET_OWNER ||
                     pkt->dp_Type == ACTION_RENAME_DISK ||
                     pkt->dp_Type == ACTION_FORMAT)) {
        bfs_err_t sync_err = bfs_fs_sync(&h->fs);
        if (sync_err == BFS_OK) {
            h->dirty = false;
            if (h->notify_pending) SendNotifications(h);
            h->notify_pending = false;
        } else {
            res1 = DOSFALSE;
            res2 = Pfs4ToDosError(sync_err);
        }
    }

reply:
    pkt->dp_Res1 = res1;
    pkt->dp_Res2 = res2;
    ReplyPacket(pkt, h);
}

/* ── Main handler entry ────────────────────────────────────── */

void EntryPointNoStack(void)
{
    SysBase = *((struct ExecBase **)4);
    struct Process *process = (struct Process *)FindTask(NULL);
    WaitPort(&process->pr_MsgPort);
    struct Message *message = GetMsg(&process->pr_MsgPort);
    struct DosPacket *packet = (struct DosPacket *)message->mn_Node.ln_Name;
    struct MsgPort *reply = packet->dp_Port;
    packet->dp_Res1 = DOSFALSE;
    packet->dp_Res2 = ERROR_NO_FREE_STORE;
    packet->dp_Port = &process->pr_MsgPort;
    PutMsg(reply, message);
}

void EntryPoint(void)
{
    struct Process *myproc;
    struct bfs_handler *h = NULL;
    struct DosPacket *pkt;
    struct Message *msg;
    BOOL running = TRUE;
    BOOL device_open = FALSE;
    BOOL removable_device = FALSE;

    SysBase = *((struct ExecBase **)4);

    myproc = (struct Process *)FindTask(NULL);

    /* Wait for startup packet */
    WaitPort(&myproc->pr_MsgPort);
    msg = GetMsg(&myproc->pr_MsgPort);
    pkt = (struct DosPacket *)msg->mn_Node.ln_Name;

    /* Allocate handler state */
    h = AllocMem(sizeof(struct bfs_handler) + sizeof(amiga_bio_t), MEMF_CLEAR);
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
    if (!h->devnode || !h->devnode->dn_Startup) {
        pkt->dp_Res1 = DOSFALSE;
        pkt->dp_Res2 = ERROR_REQUIRED_ARG_MISSING;
        goto fail_startup;
    }
    struct FileSysStartupMsg *fssm = (struct FileSysStartupMsg *)BADDR(h->devnode->dn_Startup);
    h->dosenvec = (struct DosEnvec *)BADDR(fssm->fssm_Environ);

    /* Open device */
    h->devport = CreateMsgPort();
    if (!h->devport) {
        pkt->dp_Res1 = DOSFALSE;
        pkt->dp_Res2 = ERROR_NO_FREE_STORE;
        goto fail_startup;
    }
    h->request = (struct IOExtTD *)CreateIORequest(h->devport, sizeof(struct IOExtTD));
    if (!h->request) {
        pkt->dp_Res1 = DOSFALSE;
        pkt->dp_Res2 = ERROR_NO_FREE_STORE;
        goto fail_startup;
    }
    {
        UBYTE devname[108];
        UBYTE *bname = (UBYTE *)BADDR(fssm->fssm_Device);
        if (!bname || bname[0] == 0 || bname[0] >= sizeof(devname)) {
            pkt->dp_Res1 = DOSFALSE;
            pkt->dp_Res2 = ERROR_BAD_NUMBER;
            goto fail_startup;
        }
        memcpy(devname, bname + 1, bname[0]);
        devname[bname[0]] = 0;
        removable_device = strcmp((char *)devname, "trackdisk.device") == 0;
        if (OpenDevice(devname, fssm->fssm_Unit, (struct IORequest *)h->request, fssm->fssm_Flags)) {
            pkt->dp_Res1 = DOSFALSE;
            pkt->dp_Res2 = ERROR_DEVICE_NOT_MOUNTED;
            goto fail_startup;
        }
        device_open = TRUE;
    }
    /* Initialize block I/O */
    bfs_err_t init_err = bfs_amiga_bio_init((struct amiga_bio *)(h + 1),
        h->request, h->devport, h->dosenvec, removable_device != FALSE);
    if (init_err != BFS_OK) {
        pkt->dp_Res1 = DOSFALSE;
        pkt->dp_Res2 = Pfs4ToDosError(init_err);
        goto fail_startup;
    }

    /* Mount filesystem: read superblock to get block_size, switch, then mount */
    bfs_superblock_t sb;
    bfs_err_t mount_err = bfs_amiga_bio_probe_superblock((amiga_bio_t *)(h + 1), &sb);
    if (mount_err == BFS_OK) {
        mount_err = bfs_cache_init(&h->cache, (bfs_bio_t *)(h + 1),
                                   h->dosenvec->de_NumBuffers);
        if (mount_err == BFS_OK) {
            mount_err = bfs_fs_mount(&h->fs, &h->cache.bio);
            if (mount_err == BFS_ERR_UNSUPPORTED) sb = h->fs.txn.sb;
        }
    }
    /* Stay available for unformatted media, but retain incompatible-format
     * errors so ordinary packets and ACTION_FORMAT cannot overwrite it. */
    SetMountError(h, mount_err, &sb);
    if (mount_err != BFS_OK) {
        h->fs.bio = (bfs_bio_t *)(h + 1);
    }

    /* Set up message port for DOS packets */
    h->msgport = &myproc->pr_MsgPort;
    h->devnode->dn_Task = h->msgport;

    /* Register VolumeNode (only if mounted successfully) */
    if (mount_err == BFS_OK) {
        int vlen = 0;
        while (vlen < BFS_VOLNAME_MAX && h->fs.txn.sb.volname[vlen]) vlen++;
        char vname[BFS_VOLNAME_MAX + 1];
        /* vlen is bounded above by BFS_VOLNAME_MAX; leave room for NUL. */
        memcpy(vname, h->fs.txn.sb.volname, vlen); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        vname[vlen] = 0;
        h->volnode = RegisterVolumeNode(h, vname);
        if (!h->volnode) {
            pkt->dp_Res1 = DOSFALSE;
            pkt->dp_Res2 = IoErr() ? IoErr() : ERROR_NO_FREE_STORE;
            goto fail_startup;
        }
    }

    /* Set up disk change notification */
    h->diskchange_sig = -1;
    if (removable_device)
        h->diskchange_sig = AllocSignal(-1);
    if (h->diskchange_sig >= 0) {
        h->diskchange_int = (struct Interrupt *)AllocVec(sizeof(struct Interrupt), MEMF_CLEAR);
        if (h->diskchange_int) {
            h->diskchange_int->is_Node.ln_Type = NT_INTERRUPT;
            h->diskchange_int->is_Node.ln_Name = (char *)"BFS-DiskChange";
            h->diskchange_int->is_Data = h;
            h->diskchange_int->is_Code = (void (*)(void))DiskChangeHandler;
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
        pkt->dp_Link->mn_Node.ln_Name = (char *)pkt;
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
            /* The old medium is gone: discard cached state without writing it
             * to whatever medium is now present. Existing clients may release
             * handles; remount waits until those references are gone. */
            if (h->fs.mounted) bfs_fs_abandon(&h->fs);
            bfs_cache_invalidate(&h->cache);
            h->dirty = false;
            h->notify_pending = false;
            h->media_changed = true;
            if (!HandlerIsInUse(h)) TryRemountMedia(h);
        }

        while ((pkt = GetPacket(h->msgport)) != NULL) {
            HandlePacket(pkt, h);
            if (h->should_exit) {
                running = FALSE;
                break;
            }
            if (h->media_changed && !HandlerIsInUse(h)) TryRemountMedia(h);
        }

    }

    /* Cleanup */
    h->devnode->dn_Task = NULL;
    if (h->diskchange_req) {
        h->diskchange_req->iotd_Req.io_Command = TD_REMCHANGEINT;
        h->diskchange_req->iotd_Req.io_Data = h->diskchange_int;
        h->diskchange_req->iotd_Req.io_Length = sizeof(struct Interrupt);
        DoIO((struct IORequest *)h->diskchange_req);
        DeleteIORequest((struct IORequest *)h->diskchange_req);
    }
    if (h->diskchange_int) FreeVec(h->diskchange_int);
    if (h->diskchange_sig >= 0) FreeSignal(h->diskchange_sig);
    while (h->notify_list) {
        struct bfs_notify *next = h->notify_list->next;
        FreeVec(h->notify_list);
        h->notify_list = next;
    }
    RemoveVolumeNode(h);
    bfs_cache_destroy(&h->cache);
    CloseDevice((struct IORequest *)h->request);
    DeleteIORequest((struct IORequest *)h->request);
    DeleteMsgPort(h->devport);
    CloseLibrary((struct Library *)DOSBase);
    FreeMem(h, sizeof(struct bfs_handler) + sizeof(amiga_bio_t));
    return;

fail_startup:
    {
        struct MsgPort *replyport = pkt->dp_Port;
        pkt->dp_Link->mn_Node.ln_Name = (char *)pkt;
        pkt->dp_Link->mn_Node.ln_Succ = NULL;
        pkt->dp_Link->mn_Node.ln_Pred = NULL;
        pkt->dp_Port = &myproc->pr_MsgPort;
        PutMsg(replyport, pkt->dp_Link);
    }
    if (h) {
        if (h->devnode) h->devnode->dn_Task = NULL;
        RemoveVolumeNode(h);
        if (h->fs.mounted) bfs_fs_abandon(&h->fs);
        bfs_cache_destroy(&h->cache);
        if (device_open) CloseDevice((struct IORequest *)h->request);
        if (h->request) DeleteIORequest((struct IORequest *)h->request);
        if (h->devport) DeleteMsgPort(h->devport);
        if (DOSBase) CloseLibrary((struct Library *)DOSBase);
        FreeMem(h, sizeof(struct bfs_handler) + sizeof(amiga_bio_t));
    }

}
