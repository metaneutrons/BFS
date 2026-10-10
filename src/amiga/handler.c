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
#include <dos/dostags.h>
#include <dos/filehandler.h>
#include <dos/notify.h>
#include <dos/exall.h>
#include <devices/trackdisk.h>
#include <devices/timer.h>
#include <devices/keyboard.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <string.h>
#ifdef BFS_AROS
#include <aros/asmcall.h>
#include <dos/dos64.h>
#endif

#include "bfs_fs.h"
#include "bfs_cache.h"
#include "bfs_file.h"
#include "bfs_dir.h"
#include "bfs_inode.h"
#include "bfs_snapshot.h"
#include "bfs_fsck.h"
#include "bfs_diagnostics.h"
#include "bfs_dos_name.h"
#include "amiga_bio.h"
#include "snapshot_mount.h"
#ifdef BFS_PERF_PROBE
#include "perf_probe.h"
#ifdef BFS_PERF_WRITE_DETAIL
#include "write_probe.h"
#endif
#endif

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
#include "commit_protocol.h"

#ifdef BFS_AROS
typedef SIPTR bfs_packet_word_t;
_Static_assert(sizeof(bfs_packet_word_t) == sizeof(void *),
               "AROS packet arguments must preserve pointers");
_Static_assert(BFS_CPU_BE == 0, "pc-x86_64 AROS must swap on-disk words");
#else
typedef LONG bfs_packet_word_t;
#endif
#define BFS_PACKET_PTR(value) ((bfs_packet_word_t)(value))

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

struct bfs_snapshot_pin {
    struct bfs_snapshot_pin *next;
    uint32_t id;
    uint32_t token;
    bfs_snapshot_startup_t *startup;
};

struct bfs_handler {
    struct ExecBase *SysBase;
    struct DosLibrary *DOSBase;
    struct MsgPort *msgport;
    struct MsgPort *devport;
    struct IOExtTD *request;
    struct DeviceNode *devnode;
    struct DosEnvec *dosenvec;
    struct FileSysStartupMsg *startup;
    bfs_fs_t fs;
    bfs_cache_t cache;
    bool dirty;
    bool write_protected;
    bfs_err_t mount_error;
    bool format_replaceable;  /* unsupported medium holds only older formats */
    char format_error[BFS_FORMAT_ERROR_MAX];
    bool format_error_reported;
    struct DosList *volnode;
    struct bfs_notify *notify_list;
    struct bfs_open_file *open_files;
    struct bfs_snapshot_pin *snapshot_pins;
    bfs_snapshot_startup_t *snapshot_startup;
    struct DosPacket *shutdown_packet;
    uint32_t next_snapshot_pin;
    uint32_t open_file_count;
    uint32_t lock_count;
    bool notify_pending;
    bool should_exit;
    bool media_changed;
    BYTE diskchange_sig;
    struct IOExtTD *diskchange_req;
    struct Interrupt *diskchange_int;
    /* Commit policy. Delayed: commit after a quiet period, at the latest
     * after COMMIT_MAX_PERIODS, and before any packet that needs durable or
     * quiescent state. Sync: also commit at every close and standalone
     * metadata packet. */
    bool sync_commits;
    /* Mountlist Control word LONGNAMES: allow new names of up to 255 bytes.
     * Otherwise new names must fit fib_FileName (107 bytes). */
    bool long_names;
    bool reset_pending;       /* reset warning seen: commit before every reply */
    bool commit_failed;      /* after a failure, retry only every max period */
    bool commit_activity;    /* a packet arrived since the timer was armed */
    bool commit_timer_pending;
    uint8_t commit_periods;  /* timer periods since the timer was first armed */
    struct MsgPort *commit_port;
    struct timerequest *commit_timer;
    bool commit_timer_open;
    /* Keyboard reset handler: commit before a warm reboot. */
    BYTE reset_sig;
    struct IOStdReq *reset_req;
    struct Interrupt *reset_int;
    bool reset_handler_added;
};

/* Delayed commit timing: one quiet period of 200 ms, at most five periods. */
#define COMMIT_PERIOD_MICROS 200000
#define COMMIT_MAX_PERIODS   5

/* The source handler keeps this code resident for each mounted snapshot.
 * Snapshot workers are explicit processes with this entry point, rather than
 * DOS nodes with a segment list that DOS could respawn against the live root. */
void EntryPoint(void);

/* Lock structure — stored as BPTR in FileLock */
/* Resume point of a directory enumeration on a lock: the key position of the
 * entry consumed last and how many entries had been consumed. ExNext and ExAll
 * carry that count in fib_DiskKey and eac_LastKey; when it matches, the next
 * call continues after the position in key order instead of counting from the
 * first entry. That keeps a listing linear and keeps its place when entries
 * are deleted while it runs. The leaf copy in tree lets most calls skip the
 * descent. */
typedef struct {
    uint32_t position;
    bfs_dir_pos_t last;
    bool consumed_stop; /* tree stop is the entry ExNext returned to DOS */
    bfs_btree_cursor_t tree;
} bfs_scan_cursor_t;

typedef struct {
    struct FileLock fl;
    uint32_t ino;
    uint32_t type; /* BFS_INODE_FILE or BFS_INODE_DIR */
    uint32_t parent_ino;
    bfs_scan_cursor_t *cursor; /* allocated by the first enumeration */
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

#ifdef BFS_AROS
static AROS_INTH1(DiskChangeHandler, struct bfs_handler *, h)
{
    AROS_INTFUNC_INIT
    (void)__ufi_intmask;
    (void)__ufi_custom;
    (void)__ufi_code;
    Signal(h->msgport->mp_SigTask, 1UL << h->diskchange_sig);
    return 0;
    AROS_INTFUNC_EXIT
}
#else
static ULONG DiskChangeHandler(register struct bfs_handler *h __asm("a1"))
{
    Signal(h->msgport->mp_SigTask, 1UL << h->diskchange_sig);
    return 0;
}
#endif

/* Runs in the keyboard reset interrupt: the handler task commits and then
 * acknowledges with KBD_RESETHANDLERDONE. */
#ifdef BFS_AROS
static AROS_INTH1(ResetHandler, struct bfs_handler *, h)
{
    AROS_INTFUNC_INIT
    (void)__ufi_intmask;
    (void)__ufi_custom;
    (void)__ufi_code;
    Signal(h->msgport->mp_SigTask, 1UL << h->reset_sig);
    return 0;
    AROS_INTFUNC_EXIT
}
#else
static ULONG ResetHandler(register struct bfs_handler *h __asm("a1"))
{
    Signal(h->msgport->mp_SigTask, 1UL << h->reset_sig);
    return 0;
}
#endif

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

/* AROS_FAST_BSTR uses C strings, unlike the length-prefixed m68k ABI.
 * A bounded length also rejects malformed packet names before copying them. */
static uint32_t BstrLength(const UBYTE *bstr, uint32_t limit)
{
    return bfs_dos_name_length(bstr, limit);
}

static const UBYTE *BstrText(const UBYTE *bstr)
{
    return bfs_dos_name_text(bstr);
}

static UBYTE *AllocateBstr(const UBYTE *text, uint8_t len)
{
    UBYTE *bstr = (UBYTE *)AllocVec((ULONG)len + 2, MEMF_PUBLIC | MEMF_CLEAR);
    if (!bstr) return NULL;
    (void)bfs_dos_name_encode(bstr, (unsigned)len + 2, text, len);
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
static LONG Pfs4ToDosError(bfs_err_t err);

static bool HandlerHasSnapshotPins(const struct bfs_handler *h)
{
    return h->snapshot_pins != NULL;
}

static bool SnapshotPinExists(const struct bfs_handler *h, uint32_t id)
{
    const struct bfs_snapshot_pin *pin = h->snapshot_pins;
    while (pin) {
        if (pin->id == id) return true;
        pin = pin->next;
    }
    return false;
}

static bool CopySnapshotMountName(const char *source,
                                  char destination[BFS_VOLNAME_MAX + 1])
{
    uint32_t length = 0;
    if (!source) return false;
    while (source[length] && length <= BFS_VOLNAME_MAX) {
        char c = source[length];
        if (c == ':' && source[length + 1] == '\0') break;
        if (c == ':' || c == '/' || c == '\\') return false;
        length++;
    }
    if (length == 0 || length >= BFS_VOLNAME_MAX || source[length] != ':' ||
        source[length + 1] != '\0')
        return false;
    memcpy(destination, source, length); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    destination[length] = '\0';
    return true;
}

static void CopySnapshotDeviceName(const char *mount_name,
                                   char destination[BFS_VOLNAME_MAX + 16])
{
    static const char prefix[] = "BFS-SNAPSHOT-";
    uint32_t index = 0;
    uint32_t source = 0;

    while (prefix[index]) {
        destination[index] = prefix[index];
        index++;
    }
    while (mount_name[source]) destination[index++] = mount_name[source++];
    destination[index] = '\0';
}

static bool CopySnapshotBstrName(const UBYTE *bstr, char *destination,
                                 uint32_t destination_size)
{
    uint32_t length;
    if (!bstr || destination_size == 0) return false;
    length = BstrLength(bstr, destination_size);
    if (length == 0 || length >= destination_size) return false;
    memcpy(destination, BstrText(bstr), length); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    destination[length] = '\0';
    return true;
}

static bool SnapshotStartupIsValid(const bfs_snapshot_startup_t *startup,
                                   const struct DeviceNode *devnode)
{
    return startup && startup->magic == BFS_SNAPSHOT_STARTUP_MAGIC &&
           startup->version == BFS_SNAPSHOT_STARTUP_VERSION &&
           startup->size == sizeof(*startup) && startup->source_port &&
           startup->device_node == devnode && startup->volume_node &&
           startup->pin != 0 &&
           startup->mount_name[0] != '\0';
}

static bfs_err_t OpenSnapshotNamespace(struct bfs_handler *h,
                                       const bfs_snapshot_record_t *record)
{
    uint32_t inode_nr = 0, inode_type = 0;
    uint64_t txn_id;
    bfs_inode_t inode;
    if (!record) return BFS_ERR_INVAL;
    txn_id = bfs_snapshot_record_txn_id(record);
    bfs_err_t err = bfs_snapshot_open(record, h->fs.bio,
                                      bfs_freespace_allocator(&h->fs.freespace),
                                      &h->fs.dir_tree, &h->fs.inode_tree);
    if (err != BFS_OK) return err;
    err = bfs_dir_lookup(&h->fs.dir_tree, 0, "/", 1, &inode_nr, &inode_type);
    if (err != BFS_OK) return err;
    err = bfs_inode_read(&h->fs.inode_tree, BFS_ROOT_INO, &inode);
    if (err != BFS_OK) return err;
    if (inode_nr != BFS_ROOT_INO || inode_type != BFS_INODE_DIR ||
        bfs_be32(inode.type) != BFS_INODE_DIR)
        return BFS_ERR_CORRUPT;
    /* File extents opened below this namespace inherit live_txn_id.  A
     * snapshot record names a different committed transaction than the live
     * superblock, so keep the tree handles and subsequent extent reads on the
     * record's immutable transaction boundary. */
    h->fs.live_txn_id = txn_id;
    h->fs.dir_tree.tree.txn_id_ptr = &h->fs.live_txn_id;
    h->fs.inode_tree.txn_id_ptr = &h->fs.live_txn_id;
    return BFS_OK;
}

static void ReleaseSnapshotPin(struct bfs_handler *h)
{
    bfs_snapshot_startup_t *startup = h->snapshot_startup;
    if (!startup) return;
    (void)DoPkt(startup->source_port, BFS_ACTION_SNAPSHOT_RELEASE,
                (LONG)startup->pin, BFS_PACKET_PTR(startup), 0, 0, 0);
    h->snapshot_startup = NULL;
}

static void DestroySnapshotPin(struct bfs_handler *h,
                               struct bfs_snapshot_pin *pin,
                               struct bfs_snapshot_pin **previous)
{
    *previous = pin->next;
    if (pin->startup && pin->startup->volume_node) {
        RemDosEntry(pin->startup->volume_node);
        FreeDosEntry(pin->startup->volume_node);
    }
    if (pin->startup && pin->startup->device_node) {
        FreeDosEntry((struct DosList *)pin->startup->device_node);
    }
    FreeVec(pin->startup);
    FreeVec(pin);
    (void)h;
}

static void DiscardSnapshotMount(bfs_snapshot_startup_t *startup,
                                 struct bfs_snapshot_pin *pin, bool published)
{
    if (startup && startup->volume_node) {
        if (published) RemDosEntry(startup->volume_node);
        FreeDosEntry(startup->volume_node);
    }
    if (startup && startup->device_node)
        FreeDosEntry((struct DosList *)startup->device_node);
    if (pin) FreeVec(pin);
    if (startup) FreeVec(startup);
}

static LONG AllocateSnapshotMount(const char *target, bfs_snapshot_startup_t **startup_out,
                                  struct bfs_snapshot_pin **pin_out)
{
    bfs_snapshot_startup_t *startup;
    struct bfs_snapshot_pin *pin;
    LONG error = ERROR_NO_FREE_STORE;

    startup = (bfs_snapshot_startup_t *)AllocVec(sizeof(*startup), MEMF_PUBLIC | MEMF_CLEAR);
    pin = (struct bfs_snapshot_pin *)AllocVec(sizeof(*pin), MEMF_PUBLIC | MEMF_CLEAR);
    if (!startup || !pin) goto fail;
    if (!CopySnapshotMountName(target, startup->mount_name)) {
        error = ERROR_INVALID_COMPONENT_NAME;
        goto fail;
    }
    CopySnapshotDeviceName(startup->mount_name, startup->device_name);
    startup->device_node = (struct DeviceNode *)MakeDosEntry(startup->device_name, DLT_DEVICE);
    if (!startup->device_node) goto fail;
    startup->volume_node = MakeDosEntry(startup->mount_name, DLT_VOLUME);
    if (!startup->volume_node) goto fail;
    startup->volume_node->dol_misc.dol_volume.dol_DiskType = BFS_SB_MAGIC;
    DateStamp(&startup->volume_node->dol_misc.dol_volume.dol_VolumeDate);
    *startup_out = startup;
    *pin_out = pin;
    return 0;
fail:
    if (error == ERROR_NO_FREE_STORE && IoErr()) error = IoErr();
    DiscardSnapshotMount(startup, pin, false);
    return error;
}

static LONG ConfigureSnapshotMount(struct bfs_handler *h, bfs_snapshot_startup_t *startup,
                                   const bfs_snapshot_record_t *record)
{
    const UBYTE *device_bstr = (const UBYTE *)BADDR(h->startup->fssm_Device);
    size_t words;

    uint32_t device_length = BstrLength(device_bstr, sizeof(startup->device_bstr));
    if (device_length == 0 || device_length >= sizeof(startup->device_bstr))
        return ERROR_BAD_NUMBER;
    memcpy(startup->device_bstr, device_bstr, (size_t)device_length + 1u); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    startup->magic = BFS_SNAPSHOT_STARTUP_MAGIC;
    startup->version = BFS_SNAPSHOT_STARTUP_VERSION;
    startup->size = sizeof(*startup);
    startup->pin = ++h->next_snapshot_pin;
    if (startup->pin == 0) startup->pin = ++h->next_snapshot_pin;
    startup->source_port = h->msgport;
    startup->record = *record;
    startup->fssm.fssm_Unit = h->startup->fssm_Unit;
    startup->fssm.fssm_Device = MKBADDR(startup->device_bstr);
    startup->fssm.fssm_Environ = MKBADDR(&startup->envec);
    startup->fssm.fssm_Flags = h->startup->fssm_Flags;
    words = sizeof(startup->envec) / sizeof(h->dosenvec->de_TableSize);
    if (h->dosenvec->de_TableSize < words)
        words = (size_t)h->dosenvec->de_TableSize + 1u;
    memcpy(&startup->envec, h->dosenvec,
           words * sizeof(h->dosenvec->de_TableSize)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    startup->device_node->dn_Startup = MKBADDR(&startup->fssm);
    startup->device_node->dn_GlobalVec = (BPTR)-1;
    startup->device_node->dn_StackSize = h->devnode->dn_StackSize ? h->devnode->dn_StackSize : 32768;
    startup->device_node->dn_Priority = h->devnode->dn_Priority;
    return 0;
}

static LONG PublishSnapshotVolume(bfs_snapshot_startup_t *startup)
{
    struct DosList *doslist = AttemptLockDosList(LDF_VOLUMES | LDF_WRITE);
    LONG error = 0;

    if (!doslist) return ERROR_OBJECT_IN_USE;
    /* AddDosEntry accepts duplicate volume names on AROS.  Do not let a
     * snapshot target shadow an existing live volume or another view. */
    if (FindDosEntry(doslist, startup->mount_name, LDF_VOLUMES)) {
        error = ERROR_OBJECT_EXISTS;
    } else if (AddDosEntry(startup->volume_node) == DOSFALSE) {
        error = IoErr() ? IoErr() : ERROR_OBJECT_EXISTS;
    }
    UnLockDosList(LDF_VOLUMES | LDF_WRITE);
    return error;
}

static LONG StartSnapshotWorker(bfs_snapshot_startup_t *startup)
{
    struct StandardPacket packet;
    struct MsgPort *reply = CreateMsgPort();
    struct Process *process;

    if (!reply) return ERROR_NO_FREE_STORE;
    process = CreateNewProcTags(NP_Entry, BFS_PACKET_PTR(EntryPoint),
                                NP_StackSize, startup->device_node->dn_StackSize < 131072 ?
                                              131072 : startup->device_node->dn_StackSize,
                                NP_Priority, startup->device_node->dn_Priority,
                                NP_Name, BFS_PACKET_PTR(startup->mount_name), TAG_DONE);
    if (!process) {
        LONG error = IoErr() ? IoErr() : ERROR_NO_FREE_STORE;
        DeleteMsgPort(reply);
        return error;
    }
    memset(&packet, 0, sizeof(packet));
    packet.sp_Msg.mn_Node.ln_Name = (char *)&packet.sp_Pkt;
    packet.sp_Msg.mn_Length = sizeof(packet);
    packet.sp_Pkt.dp_Link = &packet.sp_Msg;
    packet.sp_Pkt.dp_Port = reply;
    packet.sp_Pkt.dp_Type = ACTION_STARTUP;
    packet.sp_Pkt.dp_Arg3 = BFS_PACKET_PTR(MKBADDR(startup->device_node));
    packet.sp_Pkt.dp_Arg4 = BFS_PACKET_PTR(startup);
    packet.sp_Pkt.dp_Arg5 = BFS_SNAPSHOT_STARTUP_PACKET_MAGIC;
    PutMsg(&process->pr_MsgPort, &packet.sp_Msg);
    WaitPort(reply);
    (void)GetMsg(reply);
    DeleteMsgPort(reply);
    if (packet.sp_Pkt.dp_Res1 == DOSFALSE)
        return packet.sp_Pkt.dp_Res2 ? packet.sp_Pkt.dp_Res2 : ERROR_NOT_A_DOS_DISK;
    return 0;
}

static LONG StartSnapshotHandler(struct bfs_handler *h, const UBYTE *snapshot_bstr,
                                 const char *target)
{
    char snapshot_name[BFS_SNAPSHOT_NAME_MAX];
    uint32_t snapshot_id = 0;
    bfs_snapshot_record_t record;
    bfs_snapshot_startup_t *startup = NULL;
    struct bfs_snapshot_pin *pin = NULL;
    bfs_err_t err;
    LONG error;
    bool published = false;

    if (!h->startup || h->snapshot_startup) return ERROR_NOT_IMPLEMENTED;
    if (!CopySnapshotBstrName(snapshot_bstr, snapshot_name, sizeof(snapshot_name)))
        return ERROR_INVALID_COMPONENT_NAME;
    if (!h->fs.has_snapshots) return ERROR_OBJECT_NOT_FOUND;
    err = bfs_snapshot_find_by_name(&h->fs, snapshot_name, &snapshot_id, &record);
    if (err != BFS_OK) return Pfs4ToDosError(err);
    error = AllocateSnapshotMount(target, &startup, &pin);
    if (!error) error = ConfigureSnapshotMount(h, startup, &record);
    if (!error) {
        error = PublishSnapshotVolume(startup);
        published = error == 0;
    }
    if (!error) error = StartSnapshotWorker(startup);
    if (error) {
        DiscardSnapshotMount(startup, pin, published);
        return error;
    }
    pin->id = snapshot_id;
    pin->token = startup->pin;
    pin->startup = startup;
    pin->next = h->snapshot_pins;
    h->snapshot_pins = pin;
    return 0;
}

static void SetMountError(struct bfs_handler *h, bfs_err_t err,
                          const bfs_superblock_t *sb)
{
    char message[BFS_FORMAT_ERROR_MAX] = {0};
    if (err == BFS_ERR_UNSUPPORTED) bfs_sb_describe_unsupported(sb, message);
    if (strcmp(message, h->format_error) != 0) h->format_error_reported = false;
    /* Both arrays have BFS_FORMAT_ERROR_MAX bytes. */
    memcpy(h->format_error, message, sizeof(message)); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    h->mount_error = err;
    h->format_replaceable = err == BFS_ERR_UNSUPPORTED &&
                            bfs_amiga_bio_format_replaceable((amiga_bio_t *)(h + 1));
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

/* At least 64 nodes (bfs_cache_mount_slots), or the Mountlist's Buffers if
 * memory is too short for them. */
static bfs_err_t InitNodeCache(struct bfs_handler *h, amiga_bio_t *ab)
{
    uint32_t buffers = h->dosenvec->de_NumBuffers;
    bfs_err_t err = bfs_cache_init(&h->cache, (bfs_bio_t *)ab,
                                   bfs_cache_mount_slots(buffers, ab->base.block_size));
    if (err == BFS_ERR_NOMEM)
        err = bfs_cache_init(&h->cache, (bfs_bio_t *)ab, buffers);
    if (err == BFS_OK) {
        bfs_cache_set_node_write_retention(&h->cache, true);
        /* Nodes of the live transaction stay dirty until its commit. */
        bfs_cache_set_deferred_node_limit(&h->cache, h->cache.num_slots / 2);
    }
    return err;
}

/* Set the mount's limit for new names; it cannot fail on a mounted volume. */
static void ApplyNameLimit(struct bfs_handler *h)
{
    (void)bfs_fs_set_name_limit(&h->fs, h->long_names ? BFS_NAME_MAX : BFS_SHORT_NAME_MAX);
}

static bool TryRemountMedia(struct bfs_handler *h)
{
    /* A snapshot root is immutable and is selected by the startup record.
     * Never replace it with the live root after media-change handling. */
    if (h->snapshot_startup) return false;
    if (HandlerIsInUse(h)) return false;

    RemoveVolumeNode(h);
    bfs_cache_destroy(&h->cache);

    amiga_bio_t *ab = (amiga_bio_t *)(h + 1);

    bfs_superblock_t sb;
    bfs_err_t err = bfs_amiga_bio_probe_superblock(ab, &sb);
    SetMountError(h, err, &sb);
    if (err != BFS_OK) return false;

    err = InitNodeCache(h, ab);
    h->mount_error = err;
    if (err != BFS_OK) return false;

    err = bfs_fs_mount(&h->fs, &h->cache.bio);
    SetMountError(h, err, &h->fs.txn.sb);
    if (err != BFS_OK) return false;
    ApplyNameLimit(h);

    h->volnode = RegisterVolumeNode(h, (const char *)h->fs.txn.sb.volname);
    if (!h->volnode) {
        bfs_fs_abandon(&h->fs);
        bfs_cache_destroy(&h->cache);
        return false;
    }

    h->media_changed = false;
    return true;
}

static bool SnapshotRejectsPacket(LONG type)
{
    switch (type) {
    case ACTION_FINDOUTPUT:
    case ACTION_FINDUPDATE:
    case ACTION_WRITE:
    case ACTION_CREATE_DIR:
    case ACTION_DELETE_OBJECT:
    case ACTION_RENAME_OBJECT:
    case ACTION_SET_PROTECT:
    case ACTION_MAKE_LINK:
    case ACTION_SET_COMMENT:
    case ACTION_SET_FILE_SIZE:
    case ACTION_RENAME_DISK:
    case ACTION_FLUSH:
    case ACTION_WRITE_PROTECT:
    case ACTION_FORMAT:
    case ACTION_SET_DATE:
    case ACTION_SET_OWNER:
    case BFS_ACTION_SET_FILE_SIZE64:
    case BFS_ACTION_SNAPSHOT_CREATE:
    case BFS_ACTION_SNAPSHOT_DELETE:
    case BFS_ACTION_SNAPSHOT_LIST:
    case BFS_ACTION_SNAPSHOT_SHOW:
    case BFS_ACTION_SNAPSHOT_MOUNT:
        return true;
    default:
        return false;
    }
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

static void DisposeLock(bfs_lock_t *lock)
{
    if (lock->cursor) {
        bfs_btree_cursor_release(&lock->cursor->tree);
        FreeVec(lock->cursor);
    }
    FreeVec(lock);
}

static bool FreeLock(struct bfs_handler *h, bfs_lock_t *lock)
{
    if (!lock || !h->volnode) return false;

    BPTR *link = &h->volnode->dol_misc.dol_volume.dol_LockList;
    while (*link) {
        bfs_lock_t *current = (bfs_lock_t *)BADDR(*link);
        if (current == lock) {
            *link = current->fl.fl_Link;
            DisposeLock(current);
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

static bfs_err_t NameForObject(struct bfs_handler *h, uint32_t ino,
                               uint32_t parent_ino, char *name, uint8_t *name_len)
{
    if (ino == BFS_ROOT_INO) {
        uint8_t len = 0;
        const char *volname = h->snapshot_startup ? h->snapshot_startup->mount_name
                                                  : (const char *)h->fs.txn.sb.volname;
        while (len < BFS_VOLNAME_MAX && volname[len]) len++;
        /* The caller supplies BFS_NAME_MAX + 1, exceeding BFS_VOLNAME_MAX. */
        memcpy(name, volname, len); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        name[len] = 0;
        *name_len = len;
        return BFS_OK;
    }

    lock_name_ctx_t lookup = {
        .ino = ino,
        .name = name,
        .name_len = 0,
        .found = false,
    };
    bfs_err_t err = bfs_dir_scan(&h->fs.dir_tree, parent_ino,
                                 FindLockName, &lookup);
    if (err != BFS_OK) return err;
    if (!lookup.found) return BFS_ERR_NOTFOUND;
    *name_len = lookup.name_len;
    return BFS_OK;
}

static bfs_err_t NameForLock(struct bfs_handler *h, const bfs_lock_t *lock,
                             char *name, uint8_t *name_len)
{
    return NameForObject(h, lock->ino, lock->parent_ino, name, name_len);
}

/* ── BSTR / path helpers ──────────────────────────────────── */

/* A soft link met as a directory component of a path. dos.library then asks
 * ReadLink for the path that replaces it and retries (ERROR_IS_SOFT_LINK).
 * A handler-only result, mapped by Pfs4ToDosError. */
#define HANDLER_ERR_IS_SOFT_LINK ((bfs_err_t)-100)

/* Walk the directory components of a lock-relative path, leaving *parent at
 * the directory and *name at the final component. A soft link among the
 * directories stops the walk there with HANDLER_ERR_IS_SOFT_LINK: *parent is
 * its directory and *name points at the link's component. */
static bfs_err_t ResolveDirectories(struct bfs_handler *h, uint32_t *parent,
                                    const char **name, uint8_t *len)
{
    while (*len > 0) {
        uint8_t component = 0;
        while (component < *len && (*name)[component] != '/') component++;
        uint8_t remaining = *len - component;
        if (component && (remaining == 0 || remaining == 1)) break;

        uint32_t ino, type;
        bool dot_dot = component == 2 && (*name)[0] == '.' && (*name)[1] == '.';
        if (component == 0 && *parent == BFS_ROOT_INO) {
            ino = BFS_ROOT_INO;
        } else if (component == 0 || dot_dot) {
            /* A leading '/' names the parent directory, and so does a '..'
             * component between others; the root has no parent. */
            if (*parent == BFS_ROOT_INO) return BFS_ERR_NOTFOUND;
            bfs_err_t err = bfs_dir_parent_get(&h->fs.dir_tree, *parent, &ino);
            if (err != BFS_OK) return err;
        } else {
            bfs_err_t err = bfs_dir_lookup(&h->fs.dir_tree, *parent, *name,
                                           component, &ino, &type);
            if (err != BFS_OK) return err;
            if (type == BFS_INODE_SOFTLINK) return HANDLER_ERR_IS_SOFT_LINK;
            if (type != BFS_INODE_DIR) return BFS_ERR_INVAL;
        }
        *parent = ino;
        *name += component + 1;
        *len -= component + 1;
    }
    return BFS_OK;
}

/* Length of a NUL-terminated string in caller memory, or limit if none of
 * its first limit bytes is NUL. */
static uint32_t BoundedStringLength(const char *text, uint32_t limit)
{
    uint32_t length = 0;
    while (length < limit && text[length]) length++;
    return length;
}

/* Walk name (len bytes, NULL for none) relative to lock as
 * ResolveDirectories does. *last points into name at the final component
 * or, with HANDLER_ERR_IS_SOFT_LINK, at the soft link; *last_len counts the
 * bytes from there to the end of name. */
static bfs_err_t WalkPath(BPTR lock, const char *name, uint8_t len,
                          uint32_t *parent_out, const char **last,
                          uint8_t *last_len, struct bfs_handler *h)
{
    if (!parent_out || !last || !last_len || !h || !h->fs.mounted)
        return BFS_ERR_INVAL;

    bfs_lock_t *base_lock = (bfs_lock_t *)BADDR(lock);
    if (base_lock && !LockIsOwned(h, base_lock)) return BFS_ERR_INVAL;

    uint32_t parent_ino = LockIno(lock);
    bfs_err_t err = BFS_OK;
    if (!name) {
        len = 0;
    } else {
        /* Skip volume prefix (e.g. "VOL:") — resets to root */
        for (uint8_t i = 0; i < len; i++) {
            if (name[i] == ':') {
                name += i + 1;
                len -= i + 1;
                parent_ino = BFS_ROOT_INO;
                break;
            }
        }
        err = ResolveDirectories(h, &parent_ino, &name, &len);
    }
    *parent_out = parent_ino;
    *last = name;
    *last_len = len;
    return err;
}

/* Resolve name (len bytes, NULL for none) relative to lock into the parent
 * directory and the last component. */
static bfs_err_t ResolveName(BPTR lock, const char *name, uint8_t len,
                             char *namebuf, uint8_t *namelen_out,
                             uint32_t *parent_out,
                             struct bfs_handler *h)
{
    if (!namebuf || !namelen_out) return BFS_ERR_INVAL;
    const char *last;
    uint32_t parent_ino;
    bfs_err_t err = WalkPath(lock, name, len, &parent_ino, &last, &len, h);
    if (err != BFS_OK) return err;
    if (len && last[len - 1] == '/') len--;
    /* All callers supply BFS_NAME_MAX + 1 bytes; len is at most 255. */
    if (len) memcpy(namebuf, last, len); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    namebuf[len] = 0;
    *namelen_out = len;
    *parent_out = parent_ino;
    return BFS_OK;
}

static bfs_err_t ResolvePath(BPTR lock, BPTR bstr_name,
                             char *namebuf, uint8_t *namelen_out,
                             uint32_t *parent_out,
                             struct bfs_handler *h)
{
    const UBYTE *bstr = (const UBYTE *)BADDR(bstr_name);
    if (!bstr) return ResolveName(lock, NULL, 0, namebuf, namelen_out, parent_out, h);
    /* AROS_FAST_BSTR names are NUL-terminated; a bounded length also rejects
     * a malformed name before it is used. */
    uint32_t name_length = BstrLength(bstr, 256);
    if (name_length > 255) return BFS_ERR_INVAL;
    return ResolveName(lock, (const char *)BstrText(bstr), (uint8_t)name_length,
                       namebuf, namelen_out, parent_out, h);
}

/* ── BFS error to AmigaDOS error mapping ─────────────────── */

static LONG Pfs4ToDosError(bfs_err_t err)
{
    if (err == HANDLER_ERR_IS_SOFT_LINK) return ERROR_IS_SOFT_LINK;
    switch (err) {
    case BFS_OK:          return 0;
    case BFS_ERR_NOTFOUND: return ERROR_OBJECT_NOT_FOUND;
    case BFS_ERR_EXISTS:   return ERROR_OBJECT_EXISTS;
    case BFS_ERR_NOSPC:    return ERROR_DISK_FULL;
    case BFS_ERR_NOTEMPTY: return ERROR_DIRECTORY_NOT_EMPTY;
    case BFS_ERR_NOMEM:    return ERROR_NO_FREE_STORE;
    case BFS_ERR_INVAL:    return ERROR_BAD_NUMBER;
    case BFS_ERR_OVERFLOW: return ERROR_BAD_NUMBER;
    case BFS_ERR_NAME_TOO_LONG: return ERROR_INVALID_COMPONENT_NAME;
    /* Only the FIBF_WRITE check of bfs_file_write_checked returns it. */
    case BFS_ERR_PROTECTED: return ERROR_WRITE_PROTECTED;
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

static void SampleInodeStamp(void *context, bfs_inode_stamp_t *stamp)
{
    (void)context;
    struct DateStamp ds;
    DateStamp(&ds);
    stamp->days = (uint16_t)ds.ds_Days;
    stamp->mins = (uint16_t)ds.ds_Minute;
    stamp->ticks = (uint16_t)ds.ds_Tick;
}

static LONG MarkFileChanged(struct bfs_handler *h, uint32_t ino)
{
    h->dirty = true;
    h->notify_pending = true;
    bfs_inode_t inode;
    bfs_err_t err = bfs_inode_read(&h->fs.inode_tree, ino, &inode);
    if (err != BFS_OK) return Pfs4ToDosError(err);
    bfs_inode_stamp_t stamp;
    SampleInodeStamp(NULL, &stamp);
    bfs_inode_apply_stamp(&inode, &stamp, false);
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

#ifndef BFS_AROS
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
#endif

/* ── Directory enumeration ────────────────────────────────── */

/* Cleared memory is an initialized, empty tree cursor. */
static bfs_scan_cursor_t *CursorFor(bfs_lock_t *lk)
{
    if (!lk->cursor) lk->cursor = AllocVec(sizeof(*lk->cursor), MEMF_ANY | MEMF_CLEAR);
    return lk->cursor;
}

static void CursorRemember(bfs_lock_t *lk, uint32_t position,
                           const bfs_dir_pos_t *last, bool consumed_stop)
{
    if (!CursorFor(lk)) return; /* the next call counts from the start */
    lk->cursor->position = position;
    lk->cursor->last = *last;
    lk->cursor->consumed_stop = consumed_stop;
}

static void CursorClearConsumedStop(bfs_lock_t *lk)
{
    if (lk->cursor) lk->cursor->consumed_stop = false;
}

/* Scan the directory of lk from the entry after position. *skip tells the
 * callback how many entries to pass over first. */
static bfs_err_t ScanDirectoryFrom(struct bfs_handler *h, bfs_lock_t *lk,
                                   uint32_t position, bfs_dir_scan_pos_cb cb,
                                   void *ctx, uint32_t *skip,
                                   bool allow_exclusive_resume)
{
    bfs_scan_cursor_t *cursor = CursorFor(lk);
    bfs_btree_cursor_t *tree_cursor = cursor ? &cursor->tree : NULL;
    if (position > 0 && cursor && cursor->position == position) {
        *skip = 0;
        if (allow_exclusive_resume && cursor->consumed_stop) {
            bfs_err_t err = bfs_dir_scan_resume_pos(&h->fs.dir_tree, tree_cursor,
                                                    lk->ino, cb, ctx);
            if (err != BFS_ERR_AGAIN) return err;
        }
        return bfs_dir_scan_cursor_pos(&h->fs.dir_tree, tree_cursor, lk->ino,
                                       &cursor->last, cb, ctx);
    }
    *skip = position;
    return bfs_dir_scan_cursor_pos(&h->fs.dir_tree, tree_cursor, lk->ino, NULL, cb, ctx);
}

/* ── Directory scan context for EXAMINE_NEXT ──────────────── */

typedef struct {
    uint32_t skip_count; /* number of entries to skip (cursor position) */
    uint32_t seen;       /* entries seen so far */
    char *name_out;
    uint8_t name_len;
    uint32_t ino_out;
    uint32_t type_out;
    bfs_dir_pos_t pos_out;
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

static bool exam_next_pos_cb(const char *name, uint8_t name_len, uint32_t inode_nr,
                             uint32_t entry_type, const bfs_dir_pos_t *pos, void *ctx)
{
    ((exam_next_ctx_t *)ctx)->pos_out = *pos;
    return exam_next_cb(name, name_len, inode_nr, entry_type, ctx);
}

/* ── Fill FileInfoBlock ───────────────────────────────────── */

/* The DOS entry type of an inode type, as Examine, ExNext and ExAll report it. */
static LONG DosEntryType(uint32_t type)
{
    if (type == BFS_INODE_DIR) return ST_USERDIR;
    if (type == BFS_INODE_SOFTLINK) return ST_SOFTLINK;
    return ST_FILE;
}

static void FillFib(struct FileInfoBlock *fib, const char *name, uint8_t name_len,
                    uint32_t ino, uint32_t type, uint64_t size, uint32_t prot,
                    const bfs_inode_t *inode)
{
    memset(fib, 0, sizeof(*fib));
    fib->fib_DiskKey = ino;
    fib->fib_DirEntryType = DosEntryType(type);
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

#ifdef BFS_AROS
static void FillFib64(struct FileInfoBlock64 *destination,
                      const struct FileInfoBlock *source, uint64_t size)
{
    memset(destination, 0, sizeof(*destination));
    destination->fib_DiskKey = source->fib_DiskKey;
    destination->fib_DirEntryType = source->fib_DirEntryType;
    memcpy(destination->fib_FileName, source->fib_FileName,
           sizeof(destination->fib_FileName));
    destination->fib_Protection = source->fib_Protection;
    destination->fib_EntryType = source->fib_EntryType;
    destination->fib_Size = size;
    destination->fib_NumBlocks = size / 512 + (size % 512 != 0);
    destination->fib_Date = source->fib_Date;
    memcpy(destination->fib_Comment, source->fib_Comment,
           sizeof(destination->fib_Comment));
    destination->fib_OwnerUID = source->fib_OwnerUID;
    destination->fib_OwnerGID = source->fib_OwnerGID;
}
#else
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
#endif

/* Read an object's comment into a C string of BFS_COMMENT_BUFFER bytes. Only
 * an inode with HAS_COMMENT has one, so most objects need no directory
 * lookup. *length is 0 for an object without comment. */
#define BFS_COMMENT_BUFFER 80
static bfs_err_t ReadComment(struct bfs_handler *h, uint32_t ino,
                             const bfs_inode_t *inode,
                             char buffer[BFS_COMMENT_BUFFER], int *length)
{
    buffer[0] = 0;
    *length = 0;
    if (!(bfs_be32(inode->flags) & BFS_INODE_FLAG_HAS_COMMENT)) return BFS_OK;
    bfs_err_t err = bfs_fs_get_comment(&h->fs, ino, buffer, BFS_COMMENT_BUFFER);
    if (err != BFS_OK) return err;
    while (*length < BFS_COMMENT_BUFFER - 1 && buffer[*length]) (*length)++;
    return BFS_OK;
}

/* fib_Comment is a BSTR: a length byte and at most 79 characters. */
static bfs_err_t FillFibComment(struct bfs_handler *h, struct FileInfoBlock *fib,
                                uint32_t ino, const bfs_inode_t *inode)
{
    char comment[BFS_COMMENT_BUFFER];
    int length;
    bfs_err_t err = ReadComment(h, ino, inode, comment, &length);
    if (err != BFS_OK) return err;
    fib->fib_Comment[0] = (UBYTE)length;
    memcpy(&fib->fib_Comment[1], comment, length); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    return BFS_OK;
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

/* Notifications report that a change is visible, not that it is durable, so
 * they do not wait for the commit. */
static void SendPendingNotifications(struct bfs_handler *h)
{
    if (h->notify_pending) SendNotifications(h);
    h->notify_pending = false;
}

/* ── Commit policy ─────────────────────────────────────────── */

/* Commit the live transaction if it holds changes. */
static bfs_err_t CommitDirty(struct bfs_handler *h)
{
    if (!h->dirty || !h->fs.mounted) return BFS_OK;
    bfs_err_t err = bfs_fs_sync(&h->fs);
    if (err != BFS_OK) return err;
    h->dirty = false;
    h->commit_failed = false;
    return BFS_OK;
}

/* Control words are separated by blanks, commas or quotes. */
static bool ControlSeparator(UBYTE c)
{
    return c == ' ' || c == '\t' || c == ',' || c == '"' || c == 0;
}

static bool ControlWordMatches(const UBYTE *text, LONG length, const char *word)
{
    /* word is a string literal of the caller. */
    LONG word_length = (LONG)strlen(word); /* Flawfinder: ignore */
    for (LONG start = 0; start + word_length <= length; start++) {
        if (start > 0 && !ControlSeparator(text[start - 1])) continue;
        LONG i = 0;
        while (i < word_length) {
            UBYTE c = text[start + i];
            if (c >= 'a' && c <= 'z') c = (UBYTE)(c - 'a' + 'A');
            if (c != (UBYTE)word[i]) break;
            i++;
        }
        if (i == word_length &&
            (start + i == length || ControlSeparator(text[start + i])))
            return true;
    }
    return false;
}

/* Mountlist: Control = "COMMIT=SYNC" restores a commit at every close and
 * standalone metadata packet. DOS stores the Control string as a BSTR, which
 * native AROS keeps NUL-terminated; Mount accepts at most 255 characters. */
static bool ControlRequestsSyncCommits(const struct DosEnvec *env)
{
    if (!env || env->de_TableSize < DE_CONTROL || !env->de_Control) return false;
    const UBYTE *control = (const UBYTE *)BADDR(env->de_Control);
    return ControlWordMatches(BstrText(control), (LONG)BstrLength(control, 255),
                              "COMMIT=SYNC");
}

/* Mountlist: Control = "LONGNAMES" allows new names of up to 255 bytes. Examine
 * and ExNext still truncate such names to 107 bytes in fib_FileName. */
static bool ControlAllowsLongNames(const struct DosEnvec *env)
{
    if (!env || env->de_TableSize < DE_CONTROL || !env->de_Control) return false;
    const UBYTE *control = (const UBYTE *)BADDR(env->de_Control);
    return ControlWordMatches(BstrText(control), (LONG)BstrLength(control, 255),
                              "LONGNAMES");
}

static void OpenCommitTimer(struct bfs_handler *h)
{
    h->commit_port = CreateMsgPort();
    if (!h->commit_port) return;
    h->commit_timer = (struct timerequest *)CreateIORequest(h->commit_port,
                                                            sizeof(struct timerequest));
    if (!h->commit_timer) return;
    h->commit_timer_open = OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK,
                                      (struct IORequest *)h->commit_timer, 0) == 0;
}

static void CloseCommitTimer(struct bfs_handler *h)
{
    if (h->commit_timer_pending) {
        AbortIO((struct IORequest *)h->commit_timer);
        WaitIO((struct IORequest *)h->commit_timer);
        h->commit_timer_pending = false;
    }
    if (h->commit_timer_open) CloseDevice((struct IORequest *)h->commit_timer);
    h->commit_timer_open = false;
    if (h->commit_timer) DeleteIORequest((struct IORequest *)h->commit_timer);
    h->commit_timer = NULL;
    if (h->commit_port) DeleteMsgPort(h->commit_port);
    h->commit_port = NULL;
}

static ULONG CommitTimerMask(const struct bfs_handler *h)
{
    return h->commit_timer_open ? 1UL << h->commit_port->mp_SigBit : 0;
}

/* Arm the commit timer while delayed changes are outstanding. */
static void ArmCommitTimer(struct bfs_handler *h)
{
    if (!h->commit_timer_open || h->commit_timer_pending || !h->dirty ||
        h->sync_commits || !h->fs.mounted)
        return;
    h->commit_timer->tr_node.io_Command = TR_ADDREQUEST;
    h->commit_timer->tr_time.tv_secs = 0;
    h->commit_timer->tr_time.tv_micro = COMMIT_PERIOD_MICROS;
    SendIO((struct IORequest *)h->commit_timer);
    h->commit_timer_pending = true;
    h->commit_activity = false;
}

/* A timer period ended: commit after a quiet period or at the latest after
 * COMMIT_MAX_PERIODS. Errors stay latched in the core; ACTION_FLUSH and the
 * next packets report them. */
static void CommitTimerExpired(struct bfs_handler *h)
{
    if (!h->commit_timer_pending ||
        !CheckIO((struct IORequest *)h->commit_timer))
        return;
    WaitIO((struct IORequest *)h->commit_timer);
    h->commit_timer_pending = false;
    if (!h->dirty) {
        h->commit_periods = 0;
        return;
    }
    if (h->commit_periods < COMMIT_MAX_PERIODS) h->commit_periods++;
    bool due = h->commit_periods >= COMMIT_MAX_PERIODS ||
               (!h->commit_activity && !h->commit_failed);
    if (due && !h->sync_commits) {
        if (CommitDirty(h) == BFS_OK) {
            SendPendingNotifications(h);
        } else {
            h->commit_failed = true;
        }
        h->commit_periods = 0;
    }
}

/* PFS3 commits from a keyboard reset handler as well. Best effort: without
 * keyboard.device, a warm reboot loses the changes of the last second. */
static void AddCommitResetHandler(struct bfs_handler *h)
{
    h->reset_sig = AllocSignal(-1);
    if (h->reset_sig < 0) return;
    h->reset_int = (struct Interrupt *)AllocVec(sizeof(struct Interrupt), MEMF_CLEAR | MEMF_PUBLIC);
    if (!h->reset_int) return;
    h->reset_int->is_Node.ln_Type = NT_INTERRUPT;
    h->reset_int->is_Node.ln_Name = (char *)"BFS-Commit";
    h->reset_int->is_Data = h;
#ifdef BFS_AROS
    h->reset_int->is_Code = (void (*)(void))AROS_ASMSYMNAME(ResetHandler);
#else
    h->reset_int->is_Code = (void (*)(void))ResetHandler;
#endif
    h->reset_req = (struct IOStdReq *)CreateIORequest(h->devport, sizeof(struct IOStdReq));
    if (!h->reset_req) return;
    if (OpenDevice((CONST_STRPTR)"keyboard.device", 0,
                   (struct IORequest *)h->reset_req, 0) != 0) {
        DeleteIORequest((struct IORequest *)h->reset_req);
        h->reset_req = NULL;
        return;
    }
    h->reset_req->io_Command = KBD_ADDRESETHANDLER;
    h->reset_req->io_Data = h->reset_int;
    h->reset_req->io_Length = 0;
    h->reset_handler_added = DoIO((struct IORequest *)h->reset_req) == 0;
}

static void RemoveCommitResetHandler(struct bfs_handler *h)
{
    if (h->reset_handler_added) {
        h->reset_req->io_Command = KBD_REMRESETHANDLER;
        h->reset_req->io_Data = h->reset_int;
        h->reset_req->io_Length = 0;
        DoIO((struct IORequest *)h->reset_req);
        h->reset_handler_added = false;
    }
    if (h->reset_req) {
        CloseDevice((struct IORequest *)h->reset_req);
        DeleteIORequest((struct IORequest *)h->reset_req);
        h->reset_req = NULL;
    }
    if (h->reset_int) FreeVec(h->reset_int);
    h->reset_int = NULL;
    if (h->reset_sig >= 0) FreeSignal(h->reset_sig);
    h->reset_sig = -1;
}

/* The machine resets once every handler has answered. Packets can still
 * arrive before it does, so from now on each change is committed before the
 * reply that acknowledges it. */
static void CommitBeforeReset(struct bfs_handler *h)
{
    h->reset_pending = true;
    if (CommitDirty(h) == BFS_OK) SendPendingNotifications(h);
    h->reset_req->io_Command = KBD_RESETHANDLERDONE;
    h->reset_req->io_Data = h->reset_int;
    h->reset_req->io_Length = 0;
    DoIO((struct IORequest *)h->reset_req);
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
    bfs_lock_t *lock;
    uint32_t skip_count;
    uint32_t seen;
    uint32_t position;     /* entries consumed by the whole enumeration */
    struct ExAllData *last_ead;
    bool overflow;
    bfs_err_t err;
    /* The entry consumed last, for the lock's resume point. */
    bool have_last;
    bfs_dir_pos_t last;
    char skipped[BFS_NAME_MAX + 1]; /* NUL-terminated name for the pattern */
} exall_optimized_ctx_t;

/* Size of the fixed part of an ExAllData entry for each type. */
static LONG ExAllFixedSize(LONG type)
{
    switch (type) {
    case ED_NAME: return (LONG)offsetof(struct ExAllData, ed_Type);
    case ED_TYPE: return (LONG)offsetof(struct ExAllData, ed_Size);
    case ED_SIZE: return (LONG)offsetof(struct ExAllData, ed_Prot);
    case ED_PROTECTION: return (LONG)offsetof(struct ExAllData, ed_Days);
    case ED_DATE: return (LONG)offsetof(struct ExAllData, ed_Comment);
    case ED_COMMENT: return (LONG)offsetof(struct ExAllData, ed_OwnerUID);
    default: return (LONG)sizeof(struct ExAllData);
    }
}

/* Bytes an entry takes without its comment; a comment only adds to it. */
static LONG ExAllMinimumSize(LONG type, uint8_t name_len)
{
    LONG size = ExAllFixedSize(type) + name_len + 1;
    if (type >= ED_COMMENT) size += 1;
    /* Entries hold pointers: align to their width (4 on m68k, 8 on 64-bit
     * AROS). */
    return (size + (LONG)sizeof(APTR) - 1) & ~((LONG)sizeof(APTR) - 1);
}

/* The enumeration has consumed the entry at pos: written, or rejected by the
 * pattern. */
static void ExAllConsume(exall_optimized_ctx_t *ec, const bfs_dir_pos_t *pos)
{
    ec->position++;
    ec->eac->eac_LastKey = (ULONG)ec->position;
    ec->have_last = true;
    ec->last = *pos;
}

static bool exall_write_entry(exall_optimized_ctx_t *ec, const char *name,
                              uint8_t name_len, uint32_t inode_nr,
                              uint32_t entry_type, const bfs_dir_pos_t *entry_pos,
                              const bfs_inode_t *inode);

/* Stateful callback for single-pass linear directory scanning.
 * Manages skip_count, pattern matching, and user buffer overflow. */
#ifdef BFS_PERF_PROBE
static bool exall_optimized_cb_body(const char *name, uint8_t name_len,
                                    uint32_t inode_nr, uint32_t entry_type,
                                    const bfs_dir_pos_t *entry_pos, void *ctx);
static bool exall_optimized_cb(const char *name, uint8_t name_len,
                               uint32_t inode_nr, uint32_t entry_type,
                               const bfs_dir_pos_t *entry_pos, void *ctx)
{
    bfs_perf_detail_sample_t sample = bfs_perf_probe_detail_begin(
        BFS_PERF_DETAIL_SCOPE_DETAIL_EXALL_FILL);
    bool result = exall_optimized_cb_body(name, name_len, inode_nr, entry_type,
                                          entry_pos, ctx);
    bfs_perf_probe_detail_end(&sample);
    return result;
}
static bool exall_optimized_cb_body(const char *name, uint8_t name_len,
#else
static bool exall_optimized_cb(const char *name, uint8_t name_len,
#endif
                               uint32_t inode_nr, uint32_t entry_type,
                               const bfs_dir_pos_t *entry_pos, void *ctx)
{
    exall_optimized_ctx_t *ec = (exall_optimized_ctx_t *)ctx;

    if (ec->seen < ec->skip_count) {
        ec->seen++;
        return true;
    }

    /* Pattern match */
    if (ec->eac->eac_MatchString) {
        /* name_len <= BFS_NAME_MAX, and skipped holds BFS_NAME_MAX + 1. */
        memcpy(ec->skipped, name, name_len); ec->skipped[name_len] = 0; /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        if (!MatchPatternNoCase(ec->eac->eac_MatchString, ec->skipped)) {
            ExAllConsume(ec, entry_pos);
            return true;
        }
    }

    /* Read inode for metadata fields */
    bfs_inode_t inode;
    if (ec->type >= ED_SIZE) {
        ec->err = bfs_inode_read(&ec->h->fs.inode_tree, inode_nr, &inode);
        if (ec->err != BFS_OK) return false;
    }
    return exall_write_entry(ec, name, name_len, inode_nr, entry_type, entry_pos,
                             ec->type >= ED_SIZE ? &inode : NULL);
}

/* Write one entry, whose inode the caller has read when the requested fields
 * need it (inode is NULL otherwise). An entry that does not fit sets overflow
 * and is left for the next call. Returns false when the listing must stop. */
static bool exall_write_entry(exall_optimized_ctx_t *ec, const char *name,
                              uint8_t name_len, uint32_t inode_nr,
                              uint32_t entry_type, const bfs_dir_pos_t *entry_pos,
                              const bfs_inode_t *inode)
{
    uint64_t fsize = 0; uint32_t prot = 0;
    if (inode) {
        fsize = ((uint64_t)bfs_be32(inode->size_hi) << 32) | bfs_be32(inode->size_lo);
        prot = bfs_be32(inode->protection);
    }
    char cbuf[BFS_COMMENT_BUFFER];
    int cl = 0;
    if (ec->type >= ED_COMMENT) {
        /* ED_COMMENT implies ED_SIZE, so the inode has been read. */
        ec->err = ReadComment(ec->h, inode_nr, inode, cbuf, &cl);
        if (ec->err != BFS_OK) return false;
    }

    /* Only the fields up to the requested type, then the strings, as
     * dos.library lays out ExAllData. */
    LONG fixed_size = ExAllFixedSize(ec->type);
    LONG entry_size = fixed_size + name_len + 1;
    if (ec->type >= ED_COMMENT) entry_size += cl + 1;
    /* The same alignment as ExAllMinimumSize, which this never undercuts. */
    entry_size = (entry_size + (LONG)sizeof(APTR) - 1) & ~((LONG)sizeof(APTR) - 1);
    if ((size_t)(ec->end - ec->pos) < (size_t)entry_size) {
        ec->overflow = true;
        return false; /* stop scan */
    }

    struct ExAllData *ead = (struct ExAllData *)ec->pos;
    memset(ead, 0, fixed_size);
    UBYTE *str = ec->pos + fixed_size;
    /* entry_size above includes name_len + 1 bytes behind the fixed part. */
    memcpy(str, name, name_len); str[name_len] = 0; /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    ead->ed_Name = str; str += name_len + 1;
    if (ec->type >= ED_TYPE) ead->ed_Type = DosEntryType(entry_type);
    if (ec->type >= ED_SIZE) ead->ed_Size = fsize > INT32_MAX ? INT32_MAX : (ULONG)fsize;
    if (ec->type >= ED_PROTECTION) ead->ed_Prot = prot;
    if (ec->type >= ED_DATE && inode) {
        ead->ed_Days = bfs_be16(inode->modify_days);
        ead->ed_Mins = bfs_be16(inode->modify_mins);
        ead->ed_Ticks = bfs_be16(inode->modify_ticks);
    }
    if (ec->type >= ED_COMMENT) {
        memcpy(str, cbuf, cl); str[cl] = 0;
        ead->ed_Comment = str;
    }

    ead->ed_Next = NULL;
    if (ec->last_ead) ec->last_ead->ed_Next = ead;
    ec->last_ead = ead;
    ec->pos += entry_size;
    ec->eac->eac_Entries++;
    ExAllConsume(ec, entry_pos);
    return true;
}

/* ── Batched EXAMINE_ALL ──────────────────────────────────── */

/* ExAll gathers entries in directory order, as many as their smallest size
 * lets fit, reads the inodes the requested fields need in one ascending
 * batch, and then writes the entries in order. Neighbouring inodes share one
 * view of their leaf instead of one search each. */
#define EXALL_BATCH 64u

typedef struct {
    uint32_t ino;
    uint32_t type;
    bfs_dir_pos_t pos;
    uint16_t name_offset;
    uint8_t name_len;
    uint8_t read_index;  /* into inodes and results, when the inode is read */
    bool matched;        /* passes eac_MatchString */
} exall_pending_t;

typedef struct {
    exall_optimized_ctx_t *ec;
    uint32_t count;
    uint32_t names_used;
    LONG space;          /* bytes left once the gathered entries are written */
    bool full;           /* the next entry does not fit, comment or not */
    exall_pending_t entries[EXALL_BATCH];
    char names[EXALL_BATCH * (BFS_NAME_MAX + 1)];
    uint32_t inos[EXALL_BATCH];
    bfs_inode_t inodes[EXALL_BATCH];
    bfs_err_t results[EXALL_BATCH];
} exall_batch_t;

static bool exall_gather_cb(const char *name, uint8_t name_len, uint32_t inode_nr,
                            uint32_t entry_type, const bfs_dir_pos_t *entry_pos,
                            void *ctx)
{
    exall_batch_t *b = (exall_batch_t *)ctx;
    exall_optimized_ctx_t *ec = b->ec;
    if (ec->seen < ec->skip_count) {
        ec->seen++;
        return true;
    }
    if (b->count == EXALL_BATCH) return false;
    exall_pending_t *pending = &b->entries[b->count];
    char *stored = b->names + b->names_used;
    /* names holds BFS_NAME_MAX + 1 bytes per entry. */
    memcpy(stored, name, name_len); stored[name_len] = 0; /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    pending->matched = !ec->eac->eac_MatchString ||
                       MatchPatternNoCase(ec->eac->eac_MatchString, stored);
    if (pending->matched) {
        LONG size = ExAllMinimumSize(ec->type, name_len);
        if (size > b->space) {
            b->full = true;
            return false;
        }
        b->space -= size;
    }
    pending->ino = inode_nr;
    pending->type = entry_type;
    pending->pos = *entry_pos;
    pending->name_offset = (uint16_t)b->names_used;
    pending->name_len = name_len;
    b->names_used += (uint32_t)name_len + 1u;
    b->count++;
    return true;
}

/* Write gathered entry index; false when the listing must stop. */
#ifdef BFS_PERF_PROBE
static bool exall_fill_gathered_body(exall_batch_t *b, uint32_t index);
static bool exall_fill_gathered(exall_batch_t *b, uint32_t index)
{
    bfs_perf_detail_sample_t sample = bfs_perf_probe_detail_begin(
        BFS_PERF_DETAIL_SCOPE_DETAIL_EXALL_FILL);
    bool result = exall_fill_gathered_body(b, index);
    bfs_perf_probe_detail_end(&sample);
    return result;
}
static bool exall_fill_gathered_body(exall_batch_t *b, uint32_t index)
#else
static bool exall_fill_gathered(exall_batch_t *b, uint32_t index)
#endif
{
    exall_optimized_ctx_t *ec = b->ec;
    const exall_pending_t *pending = &b->entries[index];
    if (!pending->matched) {
        ExAllConsume(ec, &pending->pos);
        return true;
    }
    const bfs_inode_t *inode = NULL;
    if (ec->type >= ED_SIZE) {
        ec->err = b->results[pending->read_index];
        if (ec->err != BFS_OK) return false;
        inode = &b->inodes[pending->read_index];
    }
    return exall_write_entry(ec, b->names + pending->name_offset, pending->name_len,
                             pending->ino, pending->type, &pending->pos, inode);
}

/* Read the inodes of the gathered entries that the fields need, in
 * ascending inode order. */
static bfs_err_t ExAllReadInodes(struct bfs_handler *h, exall_batch_t *b)
{
    uint8_t order[EXALL_BATCH];
    uint32_t count = 0;
    for (uint32_t i = 0; i < b->count; i++) {
        if (!b->entries[i].matched) continue;
        /* Insertion sort by inode number; batches are small. */
        uint32_t at = count++;
        while (at > 0 && b->entries[order[at - 1]].ino > b->entries[i].ino) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = (uint8_t)i;
    }
    for (uint32_t k = 0; k < count; k++) {
        b->inos[k] = b->entries[order[k]].ino;
        b->entries[order[k]].read_index = (uint8_t)k;
    }
    return bfs_inode_read_sorted(&h->fs.inode_tree, b->inos, count, b->inodes, b->results);
}

/* Fill the caller's buffer from the lock's resume point, a batch at a time. */
static bfs_err_t ExAllBatched(struct bfs_handler *h, bfs_lock_t *lk,
                              exall_optimized_ctx_t *ec, exall_batch_t *b)
{
    for (;;) {
        b->ec = ec;
        b->count = 0;
        b->names_used = 0;
        b->full = false;
        b->space = (LONG)(ec->end - ec->pos);
        ec->seen = 0;
        bfs_err_t err = ScanDirectoryFrom(h, lk, ec->position, exall_gather_cb, b,
                                          &ec->skip_count, false);
        if (err != BFS_OK) return err;
        bool more = b->full || b->count == EXALL_BATCH;
        if (ec->type >= ED_SIZE) {
            err = ExAllReadInodes(h, b);
            if (err != BFS_OK) return err;
        }
        for (uint32_t i = 0; i < b->count; i++)
            if (!exall_fill_gathered(b, i)) return ec->err;
        if (b->full) {
            ec->overflow = true;
            return BFS_OK;
        }
        if (!more) return BFS_OK;
        /* The next batch resumes after the entry consumed last. */
        CursorRemember(lk, ec->position, &ec->last, false);
    }
}

/* A ReadLink path split around its soft link: the text before the link and
 * the text after it, starting with '/'. */
typedef struct {
    const char *prefix;
    uint32_t prefix_len;
    const char *rest;
    uint32_t rest_len;
} soft_link_request_t;

/* Write into buf the path that replaces a soft link, as PFS3 does: the
 * request's text before the link (prefix), the link's target, and the text
 * after the link (rest, starting with '/'). A target containing ':' is
 * absolute and drops the prefix, except that a target starting with ':'
 * keeps the prefix's volume or assign name. A target ending in '/' absorbs
 * the slash that starts rest; otherwise a single trailing '/' of rest is
 * dropped. Returns the length, -2 if size bytes cannot hold the path and its
 * terminator, or -1 with *error set. */
static LONG ComposeSoftLinkPath(struct bfs_handler *h, uint32_t ino,
                                const soft_link_request_t *request,
                                char *buf, LONG size, LONG *error)
{
    const char *prefix = request->prefix;
    uint32_t prefix_len = request->prefix_len;
    const char *rest = request->rest;
    uint32_t rest_len = request->rest_len;
    bfs_inode_t inode;
    bfs_err_t err = bfs_inode_read(&h->fs.inode_tree, ino, &inode);
    if (err != BFS_OK) { *error = Pfs4ToDosError(err); return -1; }
    uint64_t stored = ((uint64_t)bfs_be32(inode.size_hi) << 32) | bfs_be32(inode.size_lo);
    if (stored == 0 || stored > UINT16_MAX) { *error = ERROR_NOT_A_DOS_DISK; return -1; }
    uint32_t target_len = (uint32_t)stored;
    if ((uint64_t)target_len >= (uint64_t)size) return -2;

    bfs_file_t f;
    err = bfs_file_open(&f, &h->fs, ino);
    if (err != BFS_OK) { *error = Pfs4ToDosError(err); return -1; }
    int32_t n = bfs_file_read(&f, buf, target_len);
    if (n < 0) { *error = Pfs4ToDosError((bfs_err_t)n); return -1; }
    /* A stored target is non-empty and holds no NUL. */
    if ((uint32_t)n != target_len || BoundedStringLength(buf, target_len) != target_len) {
        *error = ERROR_NOT_A_DOS_DISK;
        return -1;
    }

    uint32_t keep = prefix_len;
    for (uint32_t i = 0; i < target_len; i++) {
        if (buf[i] != ':') continue;
        keep = 0;
        if (buf[0] == ':') {
            for (uint32_t j = 0; j < prefix_len; j++) {
                if (prefix[j] == ':') { keep = j; break; }
            }
        }
        break;
    }
    if (buf[target_len - 1] == '/') {
        if (rest_len) { rest++; rest_len--; }
    } else if (rest_len && rest[rest_len - 1] == '/' &&
               (rest_len == 1 || rest[rest_len - 2] != '/')) {
        rest_len--;
    }
    uint64_t total = (uint64_t)keep + target_len + rest_len;
    if (total >= (uint64_t)size) return -2;
    for (uint32_t i = target_len; i > 0; i--) buf[keep + i - 1] = buf[i - 1];
    /* total < size bounds every copy below. */
    if (keep) memcpy(buf, prefix, keep); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    if (rest_len) memcpy(buf + keep + target_len, rest, rest_len); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
    buf[total] = 0;
    return (LONG)total;
}

#ifdef BFS_PERF_PROBE
static void HandlePacketWork(struct DosPacket *pkt, struct bfs_handler *h)
#else
static void HandlePacket(struct DosPacket *pkt, struct bfs_handler *h)
#endif
{
    bfs_packet_word_t res1 = (pkt->dp_Type == ACTION_READ || pkt->dp_Type == ACTION_WRITE ||
                 pkt->dp_Type == ACTION_SEEK || pkt->dp_Type == ACTION_SET_FILE_SIZE ||
                 pkt->dp_Type == ACTION_READ_LINK) ?
                -1 : DOSFALSE;
    bfs_packet_word_t res2 = ERROR_ACTION_NOT_KNOWN;

    if (h->snapshot_startup && SnapshotRejectsPacket(pkt->dp_Type)) {
        res1 = DOSFALSE;
        res2 = ERROR_DISK_WRITE_PROTECTED;
        goto reply;
    }

#ifndef BFS_AROS
    if (pkt->dp_Type >= BFS_ACTION_CHANGE_FILE_POSITION64 &&
        pkt->dp_Type <= BFS_ACTION_GET_FILE_SIZE64) {
        if (pkt->dp_Res1 != BFS_DP64_INIT) goto reply;
        if (!h->fs.mounted) ReportFormatError(h, pkt->dp_Port);
        HandleDosPacket64((bfs_dos_packet64_t *)pkt, h);
        ReplyPacket(pkt, h);
        return;
    }
#endif

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

#ifdef BFS_PERF_PROBE
    case BFS_ACTION_PERF_RESET:
        bfs_perf_probe_reset();
        res1 = DOSTRUE;
        res2 = 0;
        break;

    case BFS_ACTION_PERF_READ: {
        bfs_perf_probe_snapshot_t *target =
            (bfs_perf_probe_snapshot_t *)pkt->dp_Arg1;
        if (!target) {
            res2 = ERROR_REQUIRED_ARG_MISSING;
            break;
        }
        if (pkt->dp_Arg2 != (LONG)sizeof(*target)) {
            res2 = ERROR_BAD_NUMBER;
            break;
        }
        bfs_perf_probe_snapshot_t snapshot = bfs_perf_probe_counters;
        snapshot.version = BFS_PERF_PROBE_VERSION;
        snapshot.size = (ULONG)sizeof(snapshot);
        *target = snapshot;
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }
#ifdef BFS_PERF_WRITE_DETAIL
    case BFS_ACTION_PERF_WRITE_READ: {
        bfs_write_probe_snapshot_t *target =
            (bfs_write_probe_snapshot_t *)pkt->dp_Arg1;
        if (!target) {
            res2 = ERROR_REQUIRED_ARG_MISSING;
            break;
        }
        if (pkt->dp_Arg2 != (LONG)sizeof(*target)) {
            res2 = ERROR_BAD_NUMBER;
            break;
        }
        *target = bfs_write_probe_counters;
        target->version = BFS_WRITE_PROBE_VERSION;
        target->size = (ULONG)sizeof(*target);
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }
#endif
#endif

    case BFS_ACTION_SNAPSHOT_CAPABILITY: {
        bfs_snapshot_capability_t *capability =
            (bfs_snapshot_capability_t *)pkt->dp_Arg1;
        if (!capability || pkt->dp_Arg2 < (LONG)sizeof(*capability)) {
            res2 = ERROR_BAD_NUMBER;
            break;
        }
        capability->version = BFS_SNAPSHOT_STARTUP_VERSION;
        capability->flags = h->snapshot_startup ? BFS_SNAPSHOT_CAP_MOUNTED_VIEW
                                                 : BFS_SNAPSHOT_CAP_MOUNT_SOURCE;
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    case BFS_ACTION_SNAPSHOT_MOUNT: {
        const UBYTE *snapshot_name = (const UBYTE *)BADDR(pkt->dp_Arg1);
        const char *target = (const char *)pkt->dp_Arg2;
        LONG error = StartSnapshotHandler(h, snapshot_name, target);
        if (error) {
            res2 = error;
            break;
        }
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    case BFS_ACTION_SNAPSHOT_RELEASE: {
        uint32_t token = (uint32_t)pkt->dp_Arg1;
        bfs_snapshot_startup_t *startup = (bfs_snapshot_startup_t *)pkt->dp_Arg2;
        struct bfs_snapshot_pin **previous = &h->snapshot_pins;
        while (*previous) {
            struct bfs_snapshot_pin *pin = *previous;
            if (pin->token == token && pin->startup == startup) {
                DestroySnapshotPin(h, pin, previous);
                res1 = DOSTRUE;
                res2 = 0;
                goto reply;
            }
            previous = &pin->next;
        }
        res2 = ERROR_OBJECT_NOT_FOUND;
        break;
    }

    case BFS_ACTION_FORMAT_ERROR: {
        char *buffer = (char *)pkt->dp_Arg1;
        if (!buffer || pkt->dp_Arg2 < BFS_FORMAT_ERROR_MAX) {
            res2 = ERROR_BAD_NUMBER;
            break;
        }
        /* Packet capacity was checked above; the source has exactly this size. */
        memcpy(buffer, h->format_error, BFS_FORMAT_ERROR_MAX); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
        res1 = h->format_error[0] ? DOSTRUE : DOSFALSE;
        res2 = h->format_error[0] && h->format_replaceable ? BFS_FORMAT_REPLACEABLE : 0;
        break;
    }

    case BFS_ACTION_CHECK: {
        ULONG *summary = (ULONG *)pkt->dp_Arg1;
        bfs_fsck_report_t report;
        bfs_err_t err;

        if (!summary || pkt->dp_Arg2 <
                        (LONG)(BFS_CHECK_REPORT_WORDS * sizeof(*summary))) {
            res2 = ERROR_BAD_NUMBER;
            break;
        }

        /* The handler processes packets serially. Scan a separate read-only
         * mount so CHECK observes the last committed state without changing
         * the live write transaction. Delayed changes are committed first so
         * the check covers everything accepted so far. */
        err = CommitDirty(h);
        if (err != BFS_OK) {
            res2 = Pfs4ToDosError(err);
            break;
        }
        SendPendingNotifications(h);
        /* A filesystem state is too large for the packet stack frame. */
        bfs_fs_t *checked = AllocVec(sizeof(*checked), MEMF_CLEAR);
        if (!checked) {
            res2 = ERROR_NO_FREE_STORE;
            break;
        }
        err = bfs_fs_mount_readonly(checked, &h->cache.bio);
        if (err == BFS_OK) {
            err = bfs_fs_check(checked, false, &report);
            if (bfs_fs_unmount(checked) != BFS_OK && err == BFS_OK)
                err = BFS_ERR_IO;
        }
        FreeVec(checked);
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
                    err = bfs_dir_parent_get(&h->fs.dir_tree, ino,
                                          &lock_parent);
                    if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
                }
            }
        } else {
            err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, namebuf, len,
                                  &ino, &type);
            if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
            /* A soft link is not locked itself; DOS follows it. */
            if (type == BFS_INODE_SOFTLINK) { res2 = ERROR_IS_SOFT_LINK; break; }
        }

        bfs_lock_t *lk = MakeLock(h, ino, type, pkt->dp_Arg3, lock_parent);
        if (!lk) { res2 = IoErr() ? IoErr() : ERROR_NO_FREE_STORE; break; }
        res1 = BFS_PACKET_PTR(MKBADDR(lk));
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
                err = bfs_fs_create_file_with_stamp(&h->fs, parent_ino, namebuf,
                                                    len, SampleInodeStamp,
                                                    NULL, &ino);
                if (err != BFS_OK) {
                    FreeVec(open_file); res2 = Pfs4ToDosError(err); break;
                }
                type = BFS_INODE_FILE;
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

        /* A soft link is not opened itself; DOS follows it, also to create
         * or truncate its target. */
        if (type == BFS_INODE_SOFTLINK) {
            FreeVec(open_file);
            res2 = ERROR_IS_SOFT_LINK;
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
        fh->fh_Arg1 = BFS_PACKET_PTR(&open_file->file);
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

        /* The core checks FIBF_WRITE on the inode it refreshes for the write,
         * so the packet needs no separate inode read. */
        int32_t n = bfs_file_write_checked(f, buf, (uint32_t)len,
                                          SampleInodeStamp, NULL, FIBF_ARCHIVE,
                                          FIBF_WRITE);
        if (n < 0) {
            res1 = -1;
            res2 = Pfs4ToDosError((bfs_err_t)n);
        } else {
            if (n > 0) {
                h->dirty = true;
                h->notify_pending = true;
            }
            res2 = 0;
            res1 = n;
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
        /* Delayed commits leave the change to the commit timer. */
        bfs_err_t err = h->sync_commits ? CommitDirty(h) : BFS_OK;
        if (err == BFS_OK) SendPendingNotifications(h);
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
        res1 = BFS_PACKET_PTR(MKBADDR(lk));
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
#ifdef BFS_AROS
        struct FileInfoBlock local_fib;
        bool wide_fib = pkt->dp_Type == BFS_ACTION_EXAMINE_OBJECT64;
        struct FileInfoBlock *fib = wide_fib ? &local_fib :
            (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
#else
        struct FileInfoBlock *fib = (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
#endif
        if (!lk || !pkt->dp_Arg2) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
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
#ifndef BFS_AROS
        if (pkt->dp_Type == BFS_ACTION_EXAMINE_OBJECT64) FillFib64(fib, size);
#endif
        err = FillFibComment(h, fib, lk->ino, &inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        /* Store scan index 0 in fib_DiskKey for EXAMINE_NEXT */
        fib->fib_DiskKey = 0;
#ifdef BFS_AROS
        if (wide_fib)
            FillFib64((struct FileInfoBlock64 *)BADDR(pkt->dp_Arg2), fib, size);
#endif
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── EXAMINE_NEXT ──────────────────────────────────────── */
    case BFS_ACTION_EXAMINE_NEXT64:
    case ACTION_EXAMINE_NEXT: {
        bfs_lock_t *lk = (bfs_lock_t *)BADDR(pkt->dp_Arg1);
#ifdef BFS_AROS
        struct FileInfoBlock local_fib;
        bool wide_fib = pkt->dp_Type == BFS_ACTION_EXAMINE_NEXT64;
        struct FileInfoBlock *fib = wide_fib ? &local_fib :
            (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
#else
        struct FileInfoBlock *fib = (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
#endif
        if (!lk || !pkt->dp_Arg2) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        if (!LockIsOwned(h, lk)) { res2 = ERROR_INVALID_LOCK; break; }

        char namebuf[BFS_NAME_MAX + 1];
#ifdef BFS_AROS
        /* A wide request keeps its position in the caller's FileInfoBlock64. */
        uint32_t position = wide_fib ?
            (uint32_t)((struct FileInfoBlock64 *)BADDR(pkt->dp_Arg2))->fib_DiskKey :
            (uint32_t)fib->fib_DiskKey;
#else
        uint32_t position = (uint32_t)fib->fib_DiskKey;
#endif
        exam_next_ctx_t ctx;
        ctx.seen = 0;
        ctx.name_out = namebuf;
        ctx.got_entry = false;

        bfs_err_t err = ScanDirectoryFrom(h, lk, position, exam_next_pos_cb, &ctx,
                                          &ctx.skip_count, true);
        CursorClearConsumedStop(lk);
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
#ifndef BFS_AROS
        if (pkt->dp_Type == BFS_ACTION_EXAMINE_NEXT64) FillFib64(fib, en_size);
#endif
        err = FillFibComment(h, fib, ctx.ino_out, &en_inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        CursorRemember(lk, position + 1, &ctx.pos_out, true);
        fib->fib_DiskKey = (LONG)(position + 1);
#ifdef BFS_AROS
        if (wide_fib)
            FillFib64((struct FileInfoBlock64 *)BADDR(pkt->dp_Arg2), fib, en_size);
#endif
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
            bfs_err_t err = bfs_dir_parent_get(&h->fs.dir_tree, par,
                                           &grandparent);
            if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        }
        bfs_lock_t *lk = MakeLock(h, par, BFS_INODE_DIR, SHARED_LOCK, grandparent);
        if (!lk) { res2 = IoErr() ? IoErr() : ERROR_NO_FREE_STORE; break; }
        res1 = BFS_PACKET_PTR(MKBADDR(lk));
        res2 = 0;
        break;
    }

    /* ── INHIBIT ───────────────────────────────────────────── */
    case ACTION_INHIBIT: {
        /* Whoever inhibits the volume may read or replace it directly. */
        bfs_err_t err = pkt->dp_Arg1 ? CommitDirty(h) : BFS_OK;
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        SendPendingNotifications(h);
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

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
            /* A soft link's target is a C string, not a BSTR (MakeLink
             * passes "a pointer to a null-terminated path string"). The
             * format stores at most UINT16_MAX bytes. */
            const char *target = (const char *)pkt->dp_Arg3;
            uint32_t target_len = target ? BoundedStringLength(target, UINT16_MAX + 1u) : 0;
            if (target_len == 0) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
            if (target_len > UINT16_MAX) { res2 = ERROR_LINE_TOO_LONG; break; }
            err = bfs_fs_make_softlink(&h->fs, parent_ino, namebuf, len,
                                       target, (uint16_t)target_len);
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
        /* dp_Arg1=lock, dp_Arg2=path (C string), dp_Arg3=buffer, dp_Arg4=size.
         * The soft link is the last component of path, or the directory
         * component at which another packet reported ERROR_IS_SOFT_LINK.
         * Res1 is the length of the path that replaces it, -1 on error, or
         * -2 if the buffer is too small; dos.library then retries with a
         * larger buffer. */
        const char *path = (const char *)pkt->dp_Arg2;
        if (!path) { res2 = ERROR_REQUIRED_ARG_MISSING; break; }
        uint32_t path_len = BoundedStringLength(path, BFS_NAME_MAX + 1);
        if (path_len > BFS_NAME_MAX) { res2 = ERROR_LINE_TOO_LONG; break; }
        char *buf = (char *)pkt->dp_Arg3;
        LONG bufsize = pkt->dp_Arg4;
        if (!buf || bufsize <= 0) { res2 = ERROR_BAD_NUMBER; break; }

        uint32_t parent_ino;
        const char *link;
        uint8_t remaining;
        bfs_err_t err = WalkPath((BPTR)pkt->dp_Arg1, path, (uint8_t)path_len,
                                 &parent_ino, &link, &remaining, h);
        uint8_t link_len = remaining;
        if (err == HANDLER_ERR_IS_SOFT_LINK) {
            link_len = 0;
            while (link_len < remaining && link[link_len] != '/') link_len++;
        } else if (err != BFS_OK) {
            res2 = Pfs4ToDosError(err);
            break;
        } else if (link_len && link[link_len - 1] == '/') {
            link_len--;
        }
        if (link_len == 0) { res2 = ERROR_OBJECT_WRONG_TYPE; break; }
        uint32_t ino, type;
        err = bfs_dir_lookup(&h->fs.dir_tree, parent_ino, link, link_len,
                             &ino, &type);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        if (type != BFS_INODE_SOFTLINK) { res2 = ERROR_OBJECT_WRONG_TYPE; break; }

        const char *rest = link + link_len;
        soft_link_request_t request = {
            path, (uint32_t)(link - path), rest, (uint32_t)(path + path_len - rest)
        };
        LONG error = 0;
        LONG n = ComposeSoftLinkPath(h, ino, &request, buf, bufsize, &error);
        res1 = n;
        res2 = n == -2 ? ERROR_LINE_TOO_LONG : error;
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
        uint32_t clen = BstrLength(bcomment, 80);
        if (clen > 79) { res2 = ERROR_COMMENT_TOO_BIG; break; }
        err = bfs_fs_set_comment(&h->fs, ino, (const char *)BstrText(bcomment), clen);
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
            res1 = BFS_PACKET_PTR(MKBADDR(lk));
        } else {
            bfs_lock_t *lk = MakeLock(h, src->ino, src->type, src->fl.fl_Access, src->parent_ino);
            if (!lk) { res2 = IoErr() ? IoErr() : ERROR_NO_FREE_STORE; break; }
            res1 = BFS_PACKET_PTR(MKBADDR(lk));
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
            .end = buffer + bufsize, .type = type, .lock = lk,
            .seen = 0, .position = (uint32_t)eac->eac_LastKey,
            .last_ead = NULL, .overflow = false, .err = BFS_OK,
            .have_last = false
        };

        CursorClearConsumedStop(lk);
        bfs_err_t err;
        exall_batch_t *batch = (exall_batch_t *)AllocVec(sizeof(*batch), MEMF_ANY);
        if (batch) {
            err = ExAllBatched(h, lk, &ectx, batch);
            FreeVec(batch);
        } else {
            /* Without the batch memory, entry by entry as before. */
            err = ScanDirectoryFrom(h, lk, ectx.position, exall_optimized_cb,
                                    &ectx, &ectx.skip_count, false);
        }
        if (ectx.have_last)
            CursorRemember(lk, ectx.position, &ectx.last, false);
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
    case BFS_ACTION_SNAPSHOT_CREATE: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        UBYTE *bname = (UBYTE *)BADDR(pkt->dp_Arg1);
        uint32_t nlen = BstrLength(bname, BFS_SNAPSHOT_NAME_MAX);
        if (nlen == 0 || nlen >= BFS_SNAPSHOT_NAME_MAX) {
            res2 = ERROR_INVALID_COMPONENT_NAME; break;
        }
        char name[BFS_NAME_BSTR_MAX];
        memcpy(name, BstrText(bname), nlen);
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
    case BFS_ACTION_SNAPSHOT_DELETE: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        UBYTE *bname = (UBYTE *)BADDR(pkt->dp_Arg1);
        uint32_t nlen = BstrLength(bname, BFS_SNAPSHOT_NAME_MAX);
        if (nlen == 0 || nlen >= BFS_SNAPSHOT_NAME_MAX) {
            res2 = ERROR_INVALID_COMPONENT_NAME; break;
        }
        char name[BFS_NAME_BSTR_MAX];
        memcpy(name, BstrText(bname), nlen);
        name[nlen] = 0;
        uint32_t id = 0;
        bfs_err_t err = bfs_snapshot_find_by_name(&h->fs, name, &id, NULL);
        if (err == BFS_OK && SnapshotPinExists(h, id)) {
            res2 = ERROR_OBJECT_IN_USE;
            break;
        }
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
    case BFS_ACTION_SNAPSHOT_LIST: {
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
    case BFS_ACTION_SNAPSHOT_SHOW: { /* list files in a snapshot */
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
        uint32_t nlen = BstrLength(bname, BFS_SNAPSHOT_NAME_MAX);
        if (nlen == 0 || nlen >= BFS_SNAPSHOT_NAME_MAX || !outbuf ||
            bufsize < BFS_SNAPSHOT_ENTRY_SIZE) {
            res2 = ERROR_BAD_NUMBER; break;
        }

        /* Find snapshot by name */
        char sname[BFS_NAME_BSTR_MAX]; memcpy(sname, BstrText(bname), nlen); sname[nlen] = 0;

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
        uint32_t nlen = BstrLength(bname, BFS_VOLNAME_MAX);
        if (nlen == 0 || nlen >= BFS_VOLNAME_MAX) {
            res2 = ERROR_INVALID_COMPONENT_NAME; break;
        }
        UBYTE *new_node_name = AllocateBstr(BstrText(bname), nlen);
        if (!new_node_name) { res2 = ERROR_NO_FREE_STORE; break; }

        memset(h->fs.txn.sb_new.volname, 0, BFS_VOLNAME_MAX);
        memcpy(h->fs.txn.sb_new.volname, BstrText(bname), nlen);
        bfs_err_t err = bfs_fs_sync(&h->fs);
        bool committed = memcmp(h->fs.txn.sb.volname, BstrText(bname), nlen) == 0 &&
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
        if (HandlerIsInUse(h) || (!h->snapshot_startup && HandlerHasSnapshotPins(h))) {
            res2 = ERROR_OBJECT_IN_USE;
            break;
        }
        bfs_err_t err = h->fs.mounted ? bfs_fs_unmount(&h->fs) : BFS_OK;
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        h->dirty = false;
        h->should_exit = true;
        res1 = DOSTRUE;
        res2 = 0;
        if (h->snapshot_startup) {
            /* This reply is deferred until the source-side pin has been
             * released. Preserve the completed packet result now. */
            pkt->dp_Res1 = res1;
            pkt->dp_Res2 = res2;
            h->shutdown_packet = pkt;
            return;
        }
        break;
    }

    /* ── ACTION_CURRENT_VOLUME ─────────────────────────────── */
    case ACTION_CURRENT_VOLUME:
        res1 = h->volnode ? BFS_PACKET_PTR(MKBADDR(h->volnode)) : 0;
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
            h->commit_failed = false;
            SendPendingNotifications(h);
        }
        res1 = (err == BFS_OK) ? DOSTRUE : DOSFALSE;
        res2 = Pfs4ToDosError(err);
        break;
    }

    /* ── BFS_ACTION_COMMIT_MODE ────────────────────────────── */
    case BFS_ACTION_COMMIT_MODE: {
        LONG mode = pkt->dp_Arg1;
        if (mode != BFS_COMMIT_MODE_QUERY && mode != BFS_COMMIT_MODE_DELAYED &&
            mode != BFS_COMMIT_MODE_SYNC) {
            res2 = ERROR_BAD_NUMBER;
            break;
        }
        if (mode == BFS_COMMIT_MODE_SYNC) {
            bfs_err_t err = CommitDirty(h);
            if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
            SendPendingNotifications(h);
            h->sync_commits = true;
        } else if (mode == BFS_COMMIT_MODE_DELAYED) {
            h->sync_commits = false;
        }
        res1 = h->sync_commits ? BFS_COMMIT_MODE_SYNC : BFS_COMMIT_MODE_DELAYED;
        res2 = 0;
        break;
    }

    /* ── ACTION_CHANGE_MODE ────────────────────────────────── */
    case ACTION_CHANGE_MODE:
        res2 = ERROR_ACTION_NOT_KNOWN;
        break;

    /* ── ACTION_WRITE_PROTECT ──────────────────────────────── */
    case ACTION_WRITE_PROTECT: {
        /* Changes accepted before protection must not reach the medium
         * later, so they are committed first. */
        bfs_err_t err = pkt->dp_Arg1 ? CommitDirty(h) : BFS_OK;
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        h->write_protected = (pkt->dp_Arg1 != 0);
        res1 = DOSTRUE;
        res2 = 0;
        break;
    }

    /* ── ACTION_FORMAT ─────────────────────────────────────── */
    case ACTION_FORMAT: {
        if (h->write_protected) { res2 = ERROR_DISK_WRITE_PROTECTED; break; }
        /* Formatting replaces older BFS formats, never a newer one. */
        if (h->mount_error == BFS_ERR_UNSUPPORTED && !h->format_replaceable) {
            ReportFormatError(h, pkt->dp_Port);
            res2 = Pfs4ToDosError(h->mount_error); break;
        }
        if (HandlerIsInUse(h)) { res2 = ERROR_OBJECT_IN_USE; break; }
        UBYTE *bname = (UBYTE *)BADDR(pkt->dp_Arg1);
        uint32_t nlen = BstrLength(bname, BFS_VOLNAME_MAX);
        if (nlen == 0 || nlen >= BFS_VOLNAME_MAX) {
            res2 = ERROR_INVALID_COMPONENT_NAME; break;
        }
        char volname[BFS_VOLNAME_MAX];
        memcpy(volname, BstrText(bname), nlen);
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
        err = InitNodeCache(h, ab);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }

        err = bfs_fs_format(&h->cache.bio, volname, 0);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        memset(&h->fs, 0, sizeof(h->fs));
        bfs_cache_invalidate(&h->cache);
        err = bfs_fs_mount(&h->fs, &h->cache.bio);
        SetMountError(h, err, &h->fs.txn.sb);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        ApplyNameLimit(h);
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
        fh->fh_Arg1 = BFS_PACKET_PTR(&open_file->file);
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
            bfs_err_t err = bfs_dir_parent_get(&h->fs.dir_tree, par_ino,
                                           &grandparent);
            if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        }

        bfs_lock_t *lk = MakeLock(h, par_ino, BFS_INODE_DIR, SHARED_LOCK,
                                  grandparent);
        if (!lk) { res2 = IoErr() ? IoErr() : ERROR_NO_FREE_STORE; break; }
        res1 = BFS_PACKET_PTR(MKBADDR(lk));
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
        res1 = BFS_PACKET_PTR(MKBADDR(lk));
        res2 = 0;
        break;
    }

    /* ── ACTION_EXAMINE_FH ─────────────────────────────────── */
    case BFS_ACTION_EXAMINE_FH64:
    case ACTION_EXAMINE_FH: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
#ifdef BFS_AROS
        struct FileInfoBlock local_fib;
        bool wide_fib = pkt->dp_Type == BFS_ACTION_EXAMINE_FH64;
        struct FileInfoBlock *fib = wide_fib ? &local_fib :
            (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
#else
        struct FileInfoBlock *fib = (struct FileInfoBlock *)BADDR(pkt->dp_Arg2);
#endif
        if (!FindOpenFile(h, f) || !pkt->dp_Arg2) {
            res2 = ERROR_REQUIRED_ARG_MISSING; break;
        }

        bfs_inode_t inode;
        bfs_err_t err = bfs_inode_read(&h->fs.inode_tree, f->inode_nr, &inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        uint64_t size = ((uint64_t)bfs_be32(inode.size_hi) << 32) | bfs_be32(inode.size_lo);
        uint32_t type = bfs_be32(inode.type);
        /* Objects in use cannot be renamed, so the parent recorded at open
         * still holds the name. */
        char name[BFS_NAME_MAX + 1];
        uint8_t name_len = 0;
        err = NameForObject(h, f->inode_nr, ((bfs_open_file_t *)f)->parent_ino,
                            name, &name_len);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        FillFib(fib, name, name_len, f->inode_nr, type, size, bfs_be32(inode.protection), &inode);
#ifndef BFS_AROS
        if (pkt->dp_Type == BFS_ACTION_EXAMINE_FH64) FillFib64(fib, size);
#endif
        err = FillFibComment(h, fib, f->inode_nr, &inode);
        if (err != BFS_OK) { res2 = Pfs4ToDosError(err); break; }
        fib->fib_DiskKey = 0;
#ifdef BFS_AROS
        if (wide_fib)
            FillFib64((struct FileInfoBlock64 *)BADDR(pkt->dp_Arg2), fib, size);
#endif
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

#ifdef BFS_AROS
    /* On 64-bit AROS, DosPacket already has pointer-sized arguments and
     * results; the OS4 packet overlay must not be used. */
    case BFS_ACTION_GET_FILE_POSITION64:
    case BFS_ACTION_GET_FILE_SIZE64:
    case BFS_ACTION_CHANGE_FILE_POSITION64:
    case BFS_ACTION_CHANGE_FILE_SIZE64: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        res1 = pkt->dp_Type == BFS_ACTION_GET_FILE_POSITION64 ||
               pkt->dp_Type == BFS_ACTION_GET_FILE_SIZE64 ? -1 : DOSFALSE;
        if (!FindOpenFile(h, f)) { res2 = ERROR_INVALID_LOCK; break; }
        if (pkt->dp_Type == BFS_ACTION_GET_FILE_POSITION64) {
            res1 = (bfs_packet_word_t)f->offset;
            res2 = 0;
        } else if (pkt->dp_Type == BFS_ACTION_GET_FILE_SIZE64) {
            res1 = (bfs_packet_word_t)f->size;
            res2 = 0;
        } else if (pkt->dp_Type == BFS_ACTION_CHANGE_FILE_SIZE64) {
            uint64_t size;
            res2 = ResizeFile(h, f, pkt->dp_Arg2, pkt->dp_Arg3, INT64_MAX, &size);
            if (!res2) res1 = DOSTRUE;
        } else {
            int mode = FileSeekMode(pkt->dp_Arg3);
            if (mode < 0) { res2 = ERROR_BAD_NUMBER; break; }
            int64_t position = bfs_file_seek(f, pkt->dp_Arg2, mode);
            res2 = position < 0 ? Pfs4ToDosError((bfs_err_t)position) : 0;
            if (!res2) res1 = DOSTRUE;
        }
        break;
    }

    case BFS_ACTION_SEEK64:
    case BFS_ACTION_SET_FILE_SIZE64: {
        bfs_file_t *f = (bfs_file_t *)pkt->dp_Arg1;
        res1 = -1;
        if (!FindOpenFile(h, f)) { res2 = ERROR_INVALID_LOCK; break; }
        if (pkt->dp_Type == BFS_ACTION_SET_FILE_SIZE64) {
            uint64_t size;
            res2 = ResizeFile(h, f, pkt->dp_Arg2, pkt->dp_Arg3, INT64_MAX, &size);
            if (!res2) res1 = (bfs_packet_word_t)size;
        } else {
            int mode = FileSeekMode(pkt->dp_Arg3);
            if (mode < 0) { res2 = ERROR_BAD_NUMBER; break; }
            uint64_t previous = f->offset;
            int64_t position = bfs_file_seek(f, pkt->dp_Arg2, mode);
            res2 = position < 0 ? Pfs4ToDosError((bfs_err_t)position) : 0;
            if (!res2) res1 = (bfs_packet_word_t)previous;
        }
        break;
    }
#else
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
#endif

    default:
        res2 = ERROR_ACTION_NOT_KNOWN;
        break;
    }

    /* Standalone metadata operations are complete here. In sync mode they
     * are committed; file mutations wait for ACTION_END so rapid writes to
     * one open handle share one transaction. */
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
        bfs_err_t sync_err = h->sync_commits ? CommitDirty(h) : BFS_OK;
        if (sync_err == BFS_OK) {
            SendPendingNotifications(h);
        } else {
            res1 = DOSFALSE;
            res2 = Pfs4ToDosError(sync_err);
        }
    }
    if (h->reset_pending && h->dirty && res2 == 0) {
        bfs_err_t reset_err = CommitDirty(h);
        if (reset_err != BFS_OK) {
            res1 = (pkt->dp_Type == ACTION_READ || pkt->dp_Type == ACTION_WRITE ||
                    pkt->dp_Type == ACTION_SEEK || pkt->dp_Type == ACTION_SET_FILE_SIZE ||
                    pkt->dp_Type == ACTION_READ_LINK) ? -1 : DOSFALSE;
            res2 = Pfs4ToDosError(reset_err);
        }
    }

reply:
    pkt->dp_Res1 = res1;
    pkt->dp_Res2 = res2;
    ReplyPacket(pkt, h);
}

#ifdef BFS_PERF_PROBE
static enum bfs_perf_cpu_scope PacketCpuScope(LONG packet_type)
{
    switch (packet_type) {
    case ACTION_FINDINPUT:
    case ACTION_FINDOUTPUT:
    case ACTION_FINDUPDATE:
        return BFS_PERF_CPU_SCOPE_PACKET_OPEN;
    case ACTION_READ:
        return BFS_PERF_CPU_SCOPE_PACKET_READ;
    case ACTION_WRITE:
        return BFS_PERF_CPU_SCOPE_PACKET_WRITE;
    case ACTION_END:
        return BFS_PERF_CPU_SCOPE_PACKET_END;
    case ACTION_DELETE_OBJECT:
        return BFS_PERF_CPU_SCOPE_PACKET_DELETE;
    case ACTION_FLUSH:
        return BFS_PERF_CPU_SCOPE_PACKET_FLUSH;
    default:
        return BFS_PERF_CPU_SCOPE_PACKET_OTHER;
    }
}

static void HandlePacket(struct DosPacket *pkt, struct bfs_handler *h)
{
    if (pkt->dp_Type == BFS_ACTION_PERF_RESET ||
        pkt->dp_Type == BFS_ACTION_PERF_READ
#ifdef BFS_PERF_WRITE_DETAIL
        || pkt->dp_Type == BFS_ACTION_PERF_WRITE_READ
#endif
        ) {
        HandlePacketWork(pkt, h);
        return;
    }

    enum bfs_perf_cpu_scope packet_scope = PacketCpuScope(pkt->dp_Type);
    struct EClockVal started = {0};
    bfs_perf_probe_begin(&started);
    HandlePacketWork(pkt, h);
    uint64_t ticks = bfs_perf_probe_elapsed(&started);
    bfs_perf_probe_cpu_scope_record(BFS_PERF_CPU_SCOPE_PACKET, ticks);
    bfs_perf_probe_cpu_scope_record(packet_scope, ticks);
}
#endif

/* ── Main handler entry ────────────────────────────────────── */

void EntryPointNoStack(void)
{
#ifndef BFS_AROS
    SysBase = *((struct ExecBase **)4);
#endif
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
    bfs_snapshot_startup_t *snapshot_startup = NULL;

#ifndef BFS_AROS
    SysBase = *((struct ExecBase **)4);
#endif

    myproc = (struct Process *)FindTask(NULL);

    /* Wait for startup packet */
    WaitPort(&myproc->pr_MsgPort);
    msg = GetMsg(&myproc->pr_MsgPort);
    pkt = (struct DosPacket *)msg->mn_Node.ln_Name;

    /* Allocate handler state */
    h = AllocMem(sizeof(struct bfs_handler) + sizeof(amiga_bio_t), MEMF_CLEAR);
    if (h) h->reset_sig = -1;
    if (!h) {
        pkt->dp_Res1 = DOSFALSE;
        pkt->dp_Res2 = ERROR_NO_FREE_STORE;
        goto fail_startup;
    }

    h->SysBase = SysBase;
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_fs = &h->fs;
#endif
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
    if ((ULONG)pkt->dp_Arg5 == BFS_SNAPSHOT_STARTUP_PACKET_MAGIC) {
        snapshot_startup = (bfs_snapshot_startup_t *)pkt->dp_Arg4;
        if (!SnapshotStartupIsValid(snapshot_startup, h->devnode)) {
            pkt->dp_Res1 = DOSFALSE;
            pkt->dp_Res2 = ERROR_BAD_NUMBER;
            goto fail_startup;
        }
        h->snapshot_startup = snapshot_startup;
        h->write_protected = true;
    }
    h->startup = fssm;
    h->dosenvec = (struct DosEnvec *)BADDR(fssm->fssm_Environ);
    h->long_names = ControlAllowsLongNames(h->dosenvec);

    /* Open device */
    h->devport = CreateMsgPort();
    if (!h->devport) {
        pkt->dp_Res1 = DOSFALSE;
        pkt->dp_Res2 = ERROR_NO_FREE_STORE;
        goto fail_startup;
    }
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_init(h->devport);
#endif
    h->request = (struct IOExtTD *)CreateIORequest(h->devport, sizeof(struct IOExtTD));
    if (!h->request) {
        pkt->dp_Res1 = DOSFALSE;
        pkt->dp_Res2 = ERROR_NO_FREE_STORE;
        goto fail_startup;
    }
    {
        UBYTE devname[108];
        UBYTE *bname = (UBYTE *)BADDR(fssm->fssm_Device);
        uint32_t device_length = BstrLength(bname, sizeof(devname));
        if (device_length == 0 || device_length >= sizeof(devname)) {
            pkt->dp_Res1 = DOSFALSE;
            pkt->dp_Res2 = ERROR_BAD_NUMBER;
            goto fail_startup;
        }
        memcpy(devname, BstrText(bname), device_length);
        devname[device_length] = 0;
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
    if (snapshot_startup)
        bfs_amiga_bio_set_readonly((amiga_bio_t *)(h + 1), true);

    /* Mount filesystem: read superblock to get block_size, switch, then mount */
    bfs_superblock_t sb;
    bfs_err_t mount_err = bfs_amiga_bio_probe_superblock((amiga_bio_t *)(h + 1), &sb);
    if (mount_err == BFS_OK) {
        mount_err = InitNodeCache(h, (amiga_bio_t *)(h + 1));
        if (mount_err == BFS_OK) {
            mount_err = snapshot_startup ? bfs_fs_mount_readonly(&h->fs, &h->cache.bio)
                                         : bfs_fs_mount(&h->fs, &h->cache.bio);
            if (mount_err == BFS_ERR_UNSUPPORTED) sb = h->fs.txn.sb;
            if (mount_err == BFS_OK) ApplyNameLimit(h);
            if (mount_err == BFS_OK && snapshot_startup) {
                mount_err = OpenSnapshotNamespace(h, &snapshot_startup->record);
                if (mount_err != BFS_OK) (void)bfs_fs_unmount(&h->fs);
            }
        }
    }
    /* Stay available for unformatted media, but retain incompatible-format
     * errors so ordinary packets and ACTION_FORMAT cannot overwrite it. */
    SetMountError(h, mount_err, &sb);
    if (mount_err != BFS_OK) {
        h->fs.bio = (bfs_bio_t *)(h + 1);
        if (snapshot_startup) {
            pkt->dp_Res1 = DOSFALSE;
            pkt->dp_Res2 = Pfs4ToDosError(mount_err);
            goto fail_startup;
        }
    }

    /* Set up message port for DOS packets */
    h->msgport = &myproc->pr_MsgPort;
    h->devnode->dn_Task = h->msgport;

    /* Register VolumeNode (only if mounted successfully) */
    if (mount_err == BFS_OK) {
        if (snapshot_startup) {
            /* The source handler has already inserted this volume entry.
             * Keeping it there makes a dynamically mounted volume visible
             * to the system DOS path resolver, while the worker supplies
             * the packet port and owns its lock list. */
            h->volnode = snapshot_startup->volume_node;
            h->volnode->dol_Task = h->msgport;
        } else {
            int vlen = 0;
            const char *volume_name = (const char *)h->fs.txn.sb.volname;
            while (vlen < BFS_VOLNAME_MAX && volume_name[vlen]) vlen++;
            char vname[BFS_VOLNAME_MAX + 1];
            /* vlen is bounded above by BFS_VOLNAME_MAX; leave room for NUL. */
            memcpy(vname, volume_name, vlen); /* Flawfinder: ignore */ // nosemgrep: c_buffer_rule-memcpy-CopyMemory
            vname[vlen] = 0;
            h->volnode = RegisterVolumeNode(h, vname);
            if (!h->volnode) {
                pkt->dp_Res1 = DOSFALSE;
                pkt->dp_Res2 = IoErr() ? IoErr() : ERROR_NO_FREE_STORE;
                goto fail_startup;
            }
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
#ifdef BFS_AROS
            h->diskchange_int->is_Code = (void (*)(void))AROS_ASMSYMNAME(DiskChangeHandler);
#else
            h->diskchange_int->is_Code = (void (*)(void))DiskChangeHandler;
#endif
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

    /* Live volumes commit delayed changes from a timer and before a reset.
     * Snapshot workers are read-only. */
    if (!snapshot_startup) {
        h->sync_commits = ControlRequestsSyncCommits(h->dosenvec);
        OpenCommitTimer(h);
        AddCommitResetHandler(h);
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
        ULONG reset_mask = h->reset_handler_added ? 1UL << h->reset_sig : 0;
        ULONG sigs = Wait((1UL << h->msgport->mp_SigBit) |
                          ((h->diskchange_sig >= 0) ? (1UL << h->diskchange_sig) : 0) |
                          CommitTimerMask(h) | reset_mask);

        if (sigs & CommitTimerMask(h)) CommitTimerExpired(h);
        if (sigs & reset_mask) CommitBeforeReset(h);

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
            h->commit_activity = true;
            if (h->should_exit) {
                running = FALSE;
                break;
            }
            if (h->media_changed && !HandlerIsInUse(h)) TryRemountMedia(h);
        }
        if (running) ArmCommitTimer(h);
    }

    /* Cleanup */
    RemoveCommitResetHandler(h);
    CloseCommitTimer(h);
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
    if (h->snapshot_startup) {
        /* Source-side pin cleanup owns this pre-registered entry. */
        h->volnode = NULL;
    } else {
        RemoveVolumeNode(h);
    }
    bfs_cache_destroy(&h->cache);
    bfs_amiga_bio_release((amiga_bio_t *)(h + 1));
#ifdef BFS_PERF_PROBE
    bfs_perf_probe_close();
#endif
    CloseDevice((struct IORequest *)h->request);
    DeleteIORequest((struct IORequest *)h->request);
    DeleteMsgPort(h->devport);
    if (h->snapshot_startup) ReleaseSnapshotPin(h);
    if (h->shutdown_packet) ReplyPacket(h->shutdown_packet, h);
    CloseLibrary((struct Library *)DOSBase);
    FreeMem(h, sizeof(struct bfs_handler) + sizeof(amiga_bio_t));
    return;

fail_startup:
    /* Complete local teardown before acknowledging failure.  The source
     * handler owns the private startup allocation and removes the temporary
     * DOS node after it receives this reply, so it must never race this
     * cleanup while it still dereferences either object. */
    if (h) {
        if (h->devnode) h->devnode->dn_Task = NULL;
        if (h->snapshot_startup) h->volnode = NULL;
        else RemoveVolumeNode(h);
        if (h->fs.mounted) bfs_fs_abandon(&h->fs);
        bfs_cache_destroy(&h->cache);
        bfs_amiga_bio_release((amiga_bio_t *)(h + 1));
#ifdef BFS_PERF_PROBE
        bfs_perf_probe_close();
#endif
        if (device_open) CloseDevice((struct IORequest *)h->request);
        if (h->request) DeleteIORequest((struct IORequest *)h->request);
        if (h->devport) DeleteMsgPort(h->devport);
        if (DOSBase) CloseLibrary((struct Library *)DOSBase);
        FreeMem(h, sizeof(struct bfs_handler) + sizeof(amiga_bio_t));
    }
    {
        struct MsgPort *replyport = pkt->dp_Port;
        pkt->dp_Link->mn_Node.ln_Name = (char *)pkt;
        pkt->dp_Link->mn_Node.ln_Succ = NULL;
        pkt->dp_Link->mn_Node.ln_Pred = NULL;
        pkt->dp_Port = &myproc->pr_MsgPort;
        PutMsg(replyport, pkt->dp_Link);
    }

}
