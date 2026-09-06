/* SPDX-License-Identifier: MPL-2.0 */
/*
 * bfssnapshot — BFS Snapshot Management Tool for AmigaOS
 *
 * Commands:
 *   bfssnapshot DH1: CREATE <name>          Create a snapshot
 *   bfssnapshot DH1: DELETE <name>          Delete a snapshot
 *   bfssnapshot DH1: SNAPSHOTS             List all snapshots
 *   bfssnapshot DH1: DIR <snap>            Root directory listing
 *   bfssnapshot DH1: DIR <snap> DIRS       Only directories
 *   bfssnapshot DH1: DIR <snap> FILES      Only files
 *   bfssnapshot DH1: LIST <snap>           Detailed root listing
 *   bfssnapshot DH1: INFO                  Show version
 *
 * Version: 1.1 (2026-05-03)
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <dos/datetime.h>
#include <proto/exec.h>
#include <proto/dos.h>

#define VERSION   "1.1"
#define VSTRING   "bfssnapshot 1.1 (03.05.2026)"
#define VERSTAG   "\0$VER: bfssnapshot 1.1 (03.05.2026)"

#define ACTION_BFS_SNAPSHOT_CREATE  3000
#define ACTION_BFS_SNAPSHOT_DELETE  3001
#define ACTION_BFS_SNAPSHOT_LIST    3002
#define ACTION_BFS_SNAPSHOT_SHOW    3003

/* BFS_VOLNAME_MAX/BFS_SNAPSHOT_NAME_MAX (32) + BSTR length byte + NUL. Mirrors
 * BFS_NAME_BSTR_MAX in bfs_ondisk.h, which this standalone tool can't include. */
#define BFS_NAME_BSTR_MAX 34
#define BFS_SNAPSHOT_ENTRY_META_OFFSET 260

static const char version[] = VERSTAG;

static int tool_strlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void tool_memcpy(void *dst, const void *src, int len)
{
    UBYTE *d = (UBYTE *)dst;
    const UBYTE *s = (const UBYTE *)src;
    while (len-- > 0) *d++ = *s++;
}

static void put(const char *s) { Write(Output(), (APTR)s, tool_strlen(s)); }

static void putnum(LONG n)
{
    char buf[12], *p = buf + 11;
    ULONG magnitude;
    *p = 0;
    if (n < 0) {
        magnitude = (ULONG)(-(n + 1)) + 1;
    } else {
        magnitude = (ULONG)n;
    }
    do {
        *--p = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0);
    if (n < 0) *--p = '-';
    put(p);
}

static void putu64(unsigned long long n, int width)
{
    char buf[24], *p = buf + sizeof(buf) - 1;
    *p = 0;
    do {
        *--p = (char)('0' + (n % 10));
        n /= 10;
    } while (n != 0);
    while (width-- > tool_strlen(p)) put(" ");
    put(p);
}

static void putpad(const char *s, int width)
{
    int len = tool_strlen(s);
    put(s);
    int pad = width - len;
    while (pad-- > 0) put(" ");
}

static int str_eq_nocase(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

static int build_name_bstr(UBYTE *bstr, const char *name)
{
    int len = tool_strlen(name);
    if (len < 1 || len >= 32) return 0;
    bstr[0] = (UBYTE)len;
    tool_memcpy(&bstr[1], name, len);
    return 1;
}

static int contains_slash(const char *name)
{
    while (*name) {
        if (*name++ == '/') return 1;
    }
    return 0;
}

static void put_protection(ULONG protection)
{
    static const char letters[] = "hsparwed";
    char text[9];
    for (int i = 0; i < 8; i++) {
        ULONG bit = 1UL << (7 - i);
        int enabled = (i < 4) ? ((protection & bit) != 0)
                              : ((protection & bit) == 0);
        text[i] = enabled ? letters[i] : '-';
    }
    text[8] = 0;
    put(text);
}
/* Format AmigaOS days-since-1978 using system locale */
static void put_date(ULONG days)
{
    struct DateStamp ds;
    ds.ds_Days = days;
    ds.ds_Minute = 0;
    ds.ds_Tick = 0;
    char buf[32];
    struct DateTime dt;
    dt.dat_Stamp = ds;
    dt.dat_Format = FORMAT_DEF; /* system default format */
    dt.dat_Flags = 0;
    dt.dat_StrDay = NULL;
    dt.dat_StrDate = (STRPTR)buf;
    dt.dat_StrTime = NULL;
    if (DateToStr(&dt)) {
        put(buf);
    } else {
        /* Fallback */
        putnum((LONG)days);
    }
}

int main(void)
{
    (void)version;
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR oldwin = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;

    struct RDArgs *rdargs;
    LONG args[4] = {0, 0, 0, 0};

    rdargs = ReadArgs("DRIVE/A,CMD/A,NAME,OPT", args, NULL);
    if (!rdargs) {
        put(VSTRING "\n\n");
        put("Usage: bfssnapshot DRIVE CMD [NAME] [OPT]\n\n");
        put("Commands:\n");
        put("  CREATE <name>         Create a snapshot\n");
        put("  DELETE <name>         Delete a snapshot\n");
        put("  SNAPSHOTS             List all snapshots\n");
        put("  DIR <snap>            Root directory listing\n");
        put("  LIST <snap>           Detailed root listing\n");
        put("  INFO                  Show version\n\n");
        put("DIR/LIST options: DIRS, FILES\n");
        me->pr_WindowPtr = oldwin;
        return 5;
    }

    const char *drive = (const char *)args[0];
    const char *cmd = (const char *)args[1];
    const char *name = (const char *)args[2];
    const char *opt = (const char *)args[3];

    struct MsgPort *port = DeviceProc(drive);
    if (!port) {
        put("Cannot find handler for "); put(drive); put("\n");
        FreeArgs(rdargs); me->pr_WindowPtr = oldwin; return 20;
    }

    LONG rc = 0;

    if (str_eq_nocase(cmd, "CREATE")) {
        if (!name || !name[0]) { put("CREATE requires a name.\n"); rc = 10; }
        else if (opt) { put("CREATE does not accept an option.\n"); rc = 10; }
        else if (contains_slash(name)) { put("Snapshot names cannot contain '/'.\n"); rc = 10; }
        else {
            UBYTE bstr[BFS_NAME_BSTR_MAX] = {0};
            if (!build_name_bstr(bstr, name)) {
                put("Snapshot names must contain 1 to 31 characters.\n"); rc = 10;
            } else {
                put("Creating snapshot \""); put(name); put("\"...\n");
                if (DoPkt(port, ACTION_BFS_SNAPSHOT_CREATE, (LONG)MKBADDR(bstr), 0, 0, 0, 0))
                    put("Snapshot created.\n");
                else { PrintFault(IoErr(), "bfssnapshot"); rc = 20; }
            }
        }

    } else if (str_eq_nocase(cmd, "DELETE")) {
        if (!name || !name[0]) { put("DELETE requires a name.\n"); rc = 10; }
        else if (opt) { put("DELETE does not accept an option.\n"); rc = 10; }
        else {
            UBYTE bstr[BFS_NAME_BSTR_MAX] = {0};
            if (!build_name_bstr(bstr, name)) {
                put("Snapshot names must contain 1 to 31 characters.\n"); rc = 10;
            } else {
                put("Deleting snapshot \""); put(name); put("\"...\n");
                if (DoPkt(port, ACTION_BFS_SNAPSHOT_DELETE, (LONG)MKBADDR(bstr), 0, 0, 0, 0))
                    put("Snapshot deleted.\n");
                else { PrintFault(IoErr(), "bfssnapshot"); rc = 20; }
            }
        }

    } else if (str_eq_nocase(cmd, "SNAPSHOTS")) {
        if (name || opt) {
            put("SNAPSHOTS does not accept a name or option.\n");
            rc = 10;
            goto done;
        }
        put("Snapshots on "); put(drive); put(":\n");
        char sbuf[64];
        ULONG last_id = 0;
        int scount = 0;
        while (1) {
            LONG res = DoPkt(port, ACTION_BFS_SNAPSHOT_LIST,
                             (LONG)sbuf, (LONG)sizeof(sbuf), (LONG)last_id, 0, 0);
            if (!res) {
                LONG list_error = IoErr();
                if (list_error != 0) { PrintFault(list_error, "bfssnapshot"); rc = 20; }
                break;
            }
            ULONG next_id = (ULONG)IoErr();
            /* Parse: name\0 + uint32 id + uint32 timestamp */
            char *sname = sbuf;
            int nlen = 0;
            while (nlen < (int)sizeof(sbuf) - 9 && sname[nlen]) nlen++;
            if (nlen == (int)sizeof(sbuf) - 9 || next_id <= last_id) {
                put("Invalid snapshot list response.\n");
                rc = 20;
                break;
            }
            ULONG sid, ts;
            tool_memcpy(&sid, sbuf + nlen + 1, sizeof(sid));
            tool_memcpy(&ts, sbuf + nlen + 5, sizeof(ts));
            if (sid != next_id) {
                put("Invalid snapshot list cursor.\n");
                rc = 20;
                break;
            }
            last_id = next_id;
            put("  #"); putu64(sid, 0); put("  ");
            putpad(sname, 20);
            if (ts > 0) { put_date(ts); }
            else { put("(no date)"); }
            put("\n");
            scount++;
        }
        if (scount == 0) put("  (none)\n");
        else { putnum(scount); put(scount == 1 ? " snapshot\n" : " snapshots\n"); }

    } else if (str_eq_nocase(cmd, "DIR") || str_eq_nocase(cmd, "LIST")) {
        int detailed = str_eq_nocase(cmd, "LIST");
        int show_dirs = 1, show_files = 1;
        if (opt) {
            if (str_eq_nocase(opt, "DIRS")) show_files = 0;
            else if (str_eq_nocase(opt, "FILES")) show_dirs = 0;
            else {
                put("Valid DIR/LIST options are DIRS and FILES.\n");
                FreeArgs(rdargs); me->pr_WindowPtr = oldwin; return 10;
            }
        }

        if (!name || !name[0]) {
            put(detailed ? "LIST" : "DIR");
            put(" requires a snapshot name.\n"); rc = 10;
        } else if (contains_slash(name)) {
            put("Snapshot listings currently support the snapshot root only.\n");
            rc = 10;
        } else {
            UBYTE pktbuf[BFS_NAME_BSTR_MAX] = {0};
            if (!build_name_bstr(pktbuf, name)) {
                put("Snapshot names must contain 1 to 31 characters.\n");
                FreeArgs(rdargs); me->pr_WindowPtr = oldwin; return 10;
            }

            /* Header */
            put("Directory \""); put(name); put(":\" on "); put(drive); put("\n");

            char buf[300];
            ULONG last_key = 0;
            int count = 0;
            while (1) {
                LONG res = DoPkt(port, ACTION_BFS_SNAPSHOT_SHOW,
                                 (LONG)MKBADDR(pktbuf), (LONG)buf,
                                 (LONG)sizeof(buf), (LONG)last_key, 0);
                if (!res) {
                    if (IoErr() != 0) { PrintFault(IoErr(), "bfssnapshot"); rc = 20; }
                    break;
                }

                char type = buf[0]; /* 'D' or 'F' */
                UBYTE elen = (UBYTE)buf[1];
                if (type != 'D' && type != 'F') {
                    put("Invalid snapshot entry response.\n");
                    rc = 20;
                    break;
                }
                char *ename = buf + 2;
                ename[elen] = 0;

                /* Filter */
                if (type == 'D' && !show_dirs) goto next;
                if (type == 'F' && !show_files) goto next;

                if (detailed) {
                    putpad(ename, 26);
                    ULONG size_hi, size_lo, protection, days;
                    tool_memcpy(&size_hi, buf + BFS_SNAPSHOT_ENTRY_META_OFFSET, 4);
                    tool_memcpy(&size_lo, buf + BFS_SNAPSHOT_ENTRY_META_OFFSET + 4, 4);
                    tool_memcpy(&protection, buf + BFS_SNAPSHOT_ENTRY_META_OFFSET + 8, 4);
                    tool_memcpy(&days, buf + BFS_SNAPSHOT_ENTRY_META_OFFSET + 12, 4);
                    if (type == 'D') put("       Dir  ");
                    else putu64(((unsigned long long)size_hi << 32) | size_lo, 10);
                    put("  "); put_protection(protection); put("  "); put_date(days);
                    put("\n");
                } else {
                    /* DIR format: name  (dir) */
                    put("  ");
                    putpad(ename, 24);
                    if (type == 'D') put("(dir)");
                    put("\n");
                }
                count++;
next:
                {
                    ULONG next_key = (ULONG)IoErr();
                    if (next_key <= last_key) {
                        put("Invalid snapshot listing cursor.\n");
                        rc = 20;
                        break;
                    }
                    last_key = next_key;
                }
            }
            putnum(count);
            put(count == 1 ? " entry\n" : " entries\n");
        }

    } else if (str_eq_nocase(cmd, "INFO")) {
        if (name || opt) {
            put("INFO does not accept a name or option.\n");
            rc = 10;
        } else {
            put(VSTRING "\n");
            put("Drive: "); put(drive); put("\n");
        }

    } else {
        put("Unknown command: "); put(cmd);
        put("\nValid: CREATE, DELETE, SNAPSHOTS, DIR, LIST, INFO\n");
        rc = 10;
    }

done:
    FreeArgs(rdargs);
    me->pr_WindowPtr = oldwin;
    return rc;
}
