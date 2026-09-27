/* SPDX-License-Identifier: MPL-2.0 */
/* Create and inspect known trees for AmigaOS administration-command tests. */

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "snapshot_protocol.h"

#define NESTED_FILE_NAME       "cli-dir/nested-file"
#define NESTED_LINK_NAME       "cli-dir/nested-link"
#define NESTED_FILE_CONTENT    "snapshot fixture\n"
#define NESTED_FILE_SIZE       17
#define NESTED_FILE_COMMENT    "snapshot metadata"
#define NESTED_FILE_PROTECTION FIBF_ARCHIVE
#define NESTED_FILE_DAYS       1234
#define NESTED_FILE_MINUTES    56
#define NESTED_FILE_TICKS      78

static int text_length(const char *text)
{
    int length = 0;
    while (text[length]) length++;
    return length;
}

static int text_equal(const char *first, const char *second)
{
    int index = 0;
    while (first[index] && second[index] && first[index] == second[index]) index++;
    return first[index] == second[index];
}

static int append(char *destination, int capacity, const char *text)
{
    int length = text_length(destination);
    int index = 0;

    while (text[index]) {
        if (length + index + 1 >= capacity) return 0;
        destination[length + index] = text[index];
        index++;
    }
    destination[length + index] = 0;
    return 1;
}

static int path_for(char *path, int capacity, const char *volume, const char *name)
{
    path[0] = 0;
    return append(path, capacity, volume) && append(path, capacity, name);
}

static int write_file(const char *path, const char *contents, int size)
{
    BPTR file = Open(path, MODE_NEWFILE);
    int ok = file != 0;
    if (!file) return 0;
    if (Write(file, (APTR)contents, size) != size) ok = 0;
    if (!Close(file)) ok = 0;
    return ok;
}

static int create_fixture(const char *volume)
{
    char path[128];
    BPTR directory;
    BPTR target;
    struct DateStamp date;
    int ok = path_for(path, sizeof(path), volume, "cli-file") &&
             write_file(path, "fixture\n", 8);


    if (ok && !path_for(path, sizeof(path), volume, "cli-dir")) ok = 0;
    directory = ok ? CreateDir(path) : 0;
    if (!directory) ok = 0;
    else UnLock(directory);

    if (ok && !path_for(path, sizeof(path), volume, NESTED_FILE_NAME)) ok = 0;
    if (ok && !write_file(path, NESTED_FILE_CONTENT, NESTED_FILE_SIZE)) ok = 0;
    if (ok && !SetProtection(path, NESTED_FILE_PROTECTION)) ok = 0;
    if (ok && !SetComment(path, NESTED_FILE_COMMENT)) ok = 0;
    date.ds_Days = NESTED_FILE_DAYS;
    date.ds_Minute = NESTED_FILE_MINUTES;
    date.ds_Tick = NESTED_FILE_TICKS;
    if (ok && !SetFileDate(path, &date)) ok = 0;

    target = ok ? Lock(path, SHARED_LOCK) : 0;
    if (!target) ok = 0;
    if (ok && !path_for(path, sizeof(path), volume, NESTED_LINK_NAME)) ok = 0;
    if (ok && !MakeLink(path, (LONG)target, LINK_HARD)) ok = 0;
    if (target) UnLock(target);
    return ok;
}

static int packet_rejected(struct MsgPort *port, LONG action)
{
    return !DoPkt(port, action, 0, 0, 0, 0, 0) &&
           IoErr() == ERROR_DISK_WRITE_PROTECTED;
}

static int snapshot_packets_are_readonly(struct MsgPort *port)
{
    return packet_rejected(port, ACTION_FINDOUTPUT) &&
           packet_rejected(port, ACTION_FINDUPDATE) &&
           packet_rejected(port, ACTION_WRITE) &&
           packet_rejected(port, ACTION_CREATE_DIR) &&
           packet_rejected(port, ACTION_DELETE_OBJECT) &&
           packet_rejected(port, ACTION_RENAME_OBJECT) &&
           packet_rejected(port, ACTION_SET_PROTECT) &&
           packet_rejected(port, ACTION_MAKE_LINK) &&
           packet_rejected(port, ACTION_SET_COMMENT) &&
           packet_rejected(port, ACTION_SET_DATE) &&
           packet_rejected(port, ACTION_SET_FILE_SIZE) &&
           packet_rejected(port, ACTION_RENAME_DISK) &&
           packet_rejected(port, ACTION_SET_OWNER) &&
           packet_rejected(port, ACTION_FLUSH) &&
           packet_rejected(port, ACTION_WRITE_PROTECT) &&
           packet_rejected(port, ACTION_FORMAT) &&
           packet_rejected(port, BFS_ACTION_SNAPSHOT_CREATE) &&
           packet_rejected(port, BFS_ACTION_SNAPSHOT_DELETE) &&
           packet_rejected(port, BFS_ACTION_SNAPSHOT_LIST) &&
           packet_rejected(port, BFS_ACTION_SNAPSHOT_SHOW) &&
           packet_rejected(port, BFS_ACTION_SNAPSHOT_MOUNT);
}

static int snapshot_write_open_rejected(const char *volume)
{
    char path[128];
    BPTR file;

    if (!path_for(path, sizeof(path), volume, "cli-file")) return 0;
    file = Open(path, MODE_NEWFILE);
    if (file) {
        Close(file);
        return 0;
    }
    return IoErr() == ERROR_DISK_WRITE_PROTECTED;
}

static int snapshot_metadata_matches(BPTR file)
{
    struct FileInfoBlock fib;

    return file && Examine(file, &fib) && fib.fib_DirEntryType == ST_FILE &&
           fib.fib_Size == NESTED_FILE_SIZE &&
           fib.fib_Protection == NESTED_FILE_PROTECTION &&
           fib.fib_Date.ds_Days == NESTED_FILE_DAYS &&
           fib.fib_Date.ds_Minute == NESTED_FILE_MINUTES &&
           fib.fib_Date.ds_Tick == NESTED_FILE_TICKS &&
           text_equal(fib.fib_Comment, NESTED_FILE_COMMENT);
}

static int snapshot_file_matches(const char *volume)
{
    char path[128];
    char contents[NESTED_FILE_SIZE + 1];
    BPTR root = 0, directory = 0, file = 0, link = 0;
    struct FileInfoBlock fib;
    struct InfoData info;
    struct MsgPort *port = DeviceProc((STRPTR)volume);
    int file_seen = 0, link_seen = 0, ok = port != NULL;

    root = ok ? Lock((STRPTR)volume, SHARED_LOCK) : 0;
    if (!root) ok = 0;
    if (ok && (!Info(root, &info) || info.id_DiskState != ID_WRITE_PROTECTED)) ok = 0;
    if (ok && !snapshot_packets_are_readonly(port)) ok = 0;
    if (ok && !snapshot_write_open_rejected(volume)) ok = 0;

    if (ok && !path_for(path, sizeof(path), volume, "cli-dir")) ok = 0;
    directory = ok ? Lock(path, SHARED_LOCK) : 0;
    if (!directory) ok = 0;
    if (ok && (!Examine(directory, &fib) || fib.fib_DirEntryType < 0)) ok = 0;
    while (ok && ExNext(directory, &fib)) {
        if (text_equal(fib.fib_FileName, "nested-file")) file_seen = 1;
        if (text_equal(fib.fib_FileName, "nested-link")) link_seen = 1;
    }
    if (ok && (!file_seen || !link_seen)) ok = 0;

    if (ok && !path_for(path, sizeof(path), volume, NESTED_FILE_NAME)) ok = 0;
    file = ok ? Open(path, MODE_OLDFILE) : 0;
    if (!file) ok = 0;
    if (ok && Read(file, contents, NESTED_FILE_SIZE) != NESTED_FILE_SIZE) ok = 0;
    contents[NESTED_FILE_SIZE] = 0;
    if (ok && !text_equal(contents, NESTED_FILE_CONTENT)) ok = 0;
    if (file && !Close(file)) ok = 0;
    file = 0;

    file = ok ? Lock(path, SHARED_LOCK) : 0;
    if (!file) ok = 0;
    if (ok && !snapshot_metadata_matches(file)) ok = 0;
    if (ok && !path_for(path, sizeof(path), volume, NESTED_LINK_NAME)) ok = 0;
    link = ok ? Lock(path, SHARED_LOCK) : 0;
    if (!link) ok = 0;
    if (ok && SameLock(file, link) != LOCK_SAME) ok = 0;
    if (link) UnLock(link);
    if (file) UnLock(file);

    if (directory) UnLock(directory);
    if (root) UnLock(root);
    return ok;
}

static int snapshot_busy(const char *volume, const char *snapshot_name)
{
    UBYTE name[40];
    struct MsgPort *port = DeviceProc((STRPTR)volume);
    int length = text_length(snapshot_name);
    int index;

    if (!port || length == 0 || length > 31) return 0;
    name[0] = (UBYTE)length;
    for (index = 0; index < length; index++) name[index + 1] = snapshot_name[index];
    return !DoPkt(port, BFS_ACTION_SNAPSHOT_DELETE, (LONG)MKBADDR(name), 0, 0, 0, 0) &&
           IoErr() == ERROR_OBJECT_IN_USE &&
           !DoPkt(port, ACTION_DIE, 0, 0, 0, 0, 0) &&
           IoErr() == ERROR_OBJECT_IN_USE;
}

static int snapshot_target_is_absent(const char *volume)
{
    BPTR lock;
    if (DeviceProc((STRPTR)volume)) return 0;
    lock = Lock((STRPTR)volume, SHARED_LOCK);
    if (lock) {
        UnLock(lock);
        return 0;
    }
    return 1;
}

int main(void)
{
    struct Process *process = (struct Process *)FindTask(NULL);
    APTR old_window = process->pr_WindowPtr;
    struct RDArgs *parsed;
    LONG arguments[4] = {0, 0, 0, 0};
    const char *volume;
    int ok;

    process->pr_WindowPtr = (APTR)-1;
    parsed = ReadArgs("VOLUME/A,PROBE/S,EXPECTBUSY/K,EXPECTABSENT/S", arguments, NULL);
    if (!parsed) {
        process->pr_WindowPtr = old_window;
        return 20;
    }
    volume = (const char *)arguments[0];
    if (arguments[1]) {
        ok = snapshot_file_matches(volume);
        if (ok) Write(Output(), "PROBE OK\n", 9);
    } else if (arguments[2]) {
        ok = snapshot_busy(volume, (const char *)arguments[2]);
        if (ok) Write(Output(), "BUSY OK\n", 8);
    } else if (arguments[3]) {
        ok = snapshot_target_is_absent(volume);
        if (ok) Write(Output(), "ABSENT OK\n", 10);
    } else {
        ok = create_fixture(volume);
        if (ok) Write(Output(), "FIXTURE OK\n", 11);
    }
    FreeArgs(parsed);
    process->pr_WindowPtr = old_window;
    return ok ? 0 : 20;
}
