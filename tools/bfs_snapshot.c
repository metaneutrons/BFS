/* SPDX-License-Identifier: MPL-2.0 */
/* Snapshot subcommands for the AmigaOS BFS administration command. */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/datetime.h>
#include <proto/dos.h>

#include "bfs_command.h"

#define ACTION_BFS_SNAPSHOT_CREATE 3000
#define ACTION_BFS_SNAPSHOT_DELETE 3001
#define ACTION_BFS_SNAPSHOT_LIST   3002
#define ACTION_BFS_SNAPSHOT_SHOW   3003

static void put_protection(ULONG protection)
{
    static const char letters[] = "hsparwed";
    char text[9];
    int index;

    for (index = 0; index < 8; index++) {
        ULONG bit = 1UL << (7 - index);
        int enabled = (index < 4) ? ((protection & bit) != 0)
                                  : ((protection & bit) == 0);
        text[index] = enabled ? letters[index] : '-';
    }
    text[8] = 0;
    bfs_put(text);
}

static void put_date(ULONG days)
{
    struct DateStamp stamp;
    struct DateTime date_time;
    char buffer[32];

    stamp.ds_Days = days;
    stamp.ds_Minute = 0;
    stamp.ds_Tick = 0;
    date_time.dat_Stamp = stamp;
    date_time.dat_Format = FORMAT_DEF;
    date_time.dat_Flags = 0;
    date_time.dat_StrDay = NULL;
    date_time.dat_StrDate = (STRPTR)buffer;
    date_time.dat_StrTime = NULL;
    if (DateToStr(&date_time)) bfs_put(buffer);
    else bfs_putnum((LONG)days);
}

static int snapshot_create_or_delete(struct MsgPort *port, const char *operation,
                                     const char *name, const char *option)
{
    bfs_bstr_t bstr = {0};
    const char *label = bfs_equal_nocase(operation, "create") ? "CREATE" : "DELETE";
    LONG action;

    if (!name || !name[0]) {
        bfs_put(label);
        bfs_put(" requires a name.\n");
        return 10;
    }
    if (option) {
        bfs_put(label);
        bfs_put(" does not accept an option.\n");
        return 10;
    }
    if (bfs_equal_nocase(operation, "create") && bfs_contains_slash(name)) {
        bfs_put("Snapshot names cannot contain '/'.\n");
        return 10;
    }
    if (!bfs_build_name_bstr(bfs_bstr_bytes(&bstr), name)) {
        bfs_put("Snapshot names must contain 1 to 31 characters.\n");
        return 10;
    }
    action = bfs_equal_nocase(operation, "create") ? ACTION_BFS_SNAPSHOT_CREATE
                                                    : ACTION_BFS_SNAPSHOT_DELETE;
    bfs_put(bfs_equal_nocase(operation, "create") ? "Creating snapshot \""
                                                   : "Deleting snapshot \"");
    bfs_put(name);
    bfs_put("\"...\n");
    if (DoPkt(port, action, (LONG)MKBADDR(bfs_bstr_bytes(&bstr)), 0, 0, 0, 0)) {
        bfs_put(bfs_equal_nocase(operation, "create") ? "Snapshot created.\n"
                                                       : "Snapshot deleted.\n");
        return 0;
    }
    PrintFault(IoErr(), "bfs");
    return 20;
}

static int snapshot_list(struct MsgPort *port, const char *drive, const char *name,
                         const char *option)
{
    char buffer[64];
    ULONG last_id = 0;
    int count = 0;

    if (name || option) {
        bfs_put("LIST accepts only a drive.\n");
        return 10;
    }
    bfs_put("Snapshots on ");
    bfs_put(drive);
    bfs_put("\n");
    while (1) {
        LONG result = DoPkt(port, ACTION_BFS_SNAPSHOT_LIST, (LONG)buffer,
                            (LONG)sizeof(buffer), (LONG)last_id, 0, 0);
        ULONG next_id;
        ULONG snapshot_id, timestamp;
        char *snapshot_name;
        int length = 0;

        if (!result) {
            LONG error = IoErr();
            if (error != 0) {
                PrintFault(error, "bfs");
                return 20;
            }
            break;
        }
        next_id = (ULONG)IoErr();
        snapshot_name = buffer;
        while (length < (int)sizeof(buffer) - 9 && snapshot_name[length]) length++;
        if (length == (int)sizeof(buffer) - 9 || next_id <= last_id) {
            bfs_put("Invalid snapshot list response.\n");
            return 20;
        }
        bfs_copy(&snapshot_id, buffer + length + 1, sizeof(snapshot_id));
        bfs_copy(&timestamp, buffer + length + 5, sizeof(timestamp));
        if (snapshot_id != next_id) {
            bfs_put("Invalid snapshot list cursor.\n");
            return 20;
        }
        last_id = next_id;
        bfs_put("  #");
        bfs_putu64(snapshot_id, 0);
        bfs_put("  ");
        bfs_putpad(snapshot_name, 20);
        if (timestamp > 0) put_date(timestamp);
        else bfs_put("(no date)");
        bfs_put("\n");
        count++;
    }
    if (count == 0) bfs_put("  (none)\n");
    else {
        bfs_putnum(count);
        bfs_put(count == 1 ? " snapshot\n" : " snapshots\n");
    }
    return 0;
}

static int snapshot_directory(struct MsgPort *port, const char *drive, int detailed,
                              const char *name, const char *option)
{
    bfs_bstr_t request = {0};
    char buffer[300];
    ULONG last_key = 0;
    int count = 0;
    int show_directories = 1;
    int show_files = 1;

    if (option) {
        if (bfs_equal_nocase(option, "dirs")) show_files = 0;
        else if (bfs_equal_nocase(option, "files")) show_directories = 0;
        else {
            bfs_put("Valid DIR and INSPECT options are DIRS and FILES.\n");
            return 10;
        }
    }
    if (!name || !name[0]) {
        bfs_put(detailed ? "INSPECT" : "DIR");
        bfs_put(" requires a snapshot name.\n");
        return 10;
    }
    if (bfs_contains_slash(name)) {
        bfs_put("Snapshot listings currently support the snapshot root only.\n");
        return 10;
    }
    if (!bfs_build_name_bstr(bfs_bstr_bytes(&request), name)) {
        bfs_put("Snapshot names must contain 1 to 31 characters.\n");
        return 10;
    }

    bfs_put("Directory \"");
    bfs_put(name);
    bfs_put(":\" on ");
    bfs_put(drive);
    bfs_put("\n");
    while (1) {
        LONG result = DoPkt(port, ACTION_BFS_SNAPSHOT_SHOW, (LONG)MKBADDR(bfs_bstr_bytes(&request)),
                            (LONG)buffer, (LONG)sizeof(buffer), (LONG)last_key, 0);
        ULONG next_key;
        char type;
        UBYTE entry_length;
        char *entry_name;

        if (!result) {
            LONG error = IoErr();
            if (error != 0) {
                PrintFault(error, "bfs");
                return 20;
            }
            break;
        }
        type = buffer[0];
        entry_length = (UBYTE)buffer[1];
        if (type != 'D' && type != 'F') {
            bfs_put("Invalid snapshot entry response.\n");
            return 20;
        }
        entry_name = buffer + 2;
        entry_name[entry_length] = 0;
        next_key = (ULONG)IoErr();
        if (next_key <= last_key) {
            bfs_put("Invalid snapshot listing cursor.\n");
            return 20;
        }
        last_key = next_key;
        if ((type == 'D' && !show_directories) || (type == 'F' && !show_files)) continue;

        if (detailed) {
            ULONG size_hi, size_lo, protection, days;
            bfs_putpad(entry_name, 26);
            bfs_copy(&size_hi, buffer + BFS_SNAPSHOT_ENTRY_META_OFFSET, 4);
            bfs_copy(&size_lo, buffer + BFS_SNAPSHOT_ENTRY_META_OFFSET + 4, 4);
            bfs_copy(&protection, buffer + BFS_SNAPSHOT_ENTRY_META_OFFSET + 8, 4);
            bfs_copy(&days, buffer + BFS_SNAPSHOT_ENTRY_META_OFFSET + 12, 4);
            if (type == 'D') bfs_put("       Dir  ");
            else bfs_putu64(((unsigned long long)size_hi << 32) | size_lo, 10);
            bfs_put("  ");
            put_protection(protection);
            bfs_put("  ");
            put_date(days);
            bfs_put("\n");
        } else {
            bfs_put("  ");
            bfs_putpad(entry_name, 24);
            if (type == 'D') bfs_put("(dir)");
            bfs_put("\n");
        }
        count++;
    }
    bfs_putnum(count);
    bfs_put(count == 1 ? " entry\n" : " entries\n");
    return 0;
}

int bfs_snapshot_command(const char *operation, const char *drive,
                         const char *name, const char *option)
{
    struct MsgPort *port = DeviceProc(drive);
    int result;

    if (!port) {
        bfs_put("Cannot find handler for ");
        bfs_put(drive);
        bfs_put("\n");
        return 20;
    }
    if (bfs_equal_nocase(operation, "create") || bfs_equal_nocase(operation, "delete")) {
        result = snapshot_create_or_delete(port, operation, name, option);
    } else if (bfs_equal_nocase(operation, "list")) {
        result = snapshot_list(port, drive, name, option);
    } else if (bfs_equal_nocase(operation, "dir")) {
        result = snapshot_directory(port, drive, 0, name, option);
    } else if (bfs_equal_nocase(operation, "inspect")) {
        result = snapshot_directory(port, drive, 1, name, option);
    } else {
        bfs_put("Unknown snapshot command: ");
        bfs_put(operation);
        bfs_put("\nValid: CREATE, DELETE, LIST, DIR, INSPECT\n");
        result = 10;
    }
    return result;
}
