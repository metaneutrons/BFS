/* SPDX-License-Identifier: MPL-2.0 */
/*
 * bfssnapshot — BFS Snapshot Management Tool for AmigaOS
 *
 * Commands:
 *   bfssnapshot DH1: CREATE <name>          Create a snapshot
 *   bfssnapshot DH1: DELETE <name>          Delete a snapshot
 *   bfssnapshot DH1: SNAPSHOTS             List all snapshots
 *   bfssnapshot DH1: DIR <snap>[/path]     Dir-style listing
 *   bfssnapshot DH1: DIR <snap> ALL        Recursive listing
 *   bfssnapshot DH1: DIR <snap> DIRS       Only directories
 *   bfssnapshot DH1: DIR <snap> FILES      Only files
 *   bfssnapshot DH1: LIST <snap>[/path]    Detailed listing (sizes, dates, protection)
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
#define ACTION_BFS_SNAPSHOT_MOUNT   3004
#define ACTION_BFS_SNAPSHOT_UNMOUNT 3005

/* BFS_VOLNAME_MAX/BFS_SNAPSHOT_NAME_MAX (32) + BSTR length byte + NUL. Mirrors
 * BFS_NAME_BSTR_MAX in bfs_ondisk.h, which this standalone tool can't include. */
#define BFS_NAME_BSTR_MAX 34

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
    char buf[12], *p = buf + 11; *p = 0;
    if (n == 0) { put("0"); return; }
    while (n > 0) { *--p = '0' + (n % 10); n /= 10; }
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

/* Parse "snapname/path" from name argument */
static void parse_snappath(const char *name, char *snapname, int snapmax,
                            char *path, int pathmax)
{
    int si = 0, pi = 0;
    const char *p = name;
    while (*p && *p != '/' && si < snapmax - 1) snapname[si++] = *p++;
    snapname[si] = 0;
    if (*p == '/') p++;
    while (*p && pi < pathmax - 1) path[pi++] = *p++;
    path[pi] = 0;
}

/* Build packed BSTR: "snapname\0path" */
static void build_pktname(UBYTE *pktbuf, const char *snapname, const char *path)
{
    int slen = tool_strlen(snapname);
    int plen = tool_strlen(path);
    pktbuf[0] = (UBYTE)(slen + 1 + plen);
    tool_memcpy(pktbuf + 1, snapname, slen);
    pktbuf[1 + slen] = 0;
    tool_memcpy(pktbuf + 2 + slen, path, plen);
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
        put("  DIR <snap>[/path]     Directory listing\n");
        put("  LIST <snap>[/path]    Detailed listing\n");
        put("  MOUNT <snap> <vol>    Mount snapshot read-only\n");
        put("  UNMOUNT <vol:>        Unmount snapshot volume\n");
        put("  INFO                  Show version\n\n");
        put("DIR options: ALL, DIRS, FILES\n");
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
        else {
            UBYTE bstr[BFS_NAME_BSTR_MAX] = {0};
            int nlen = 0; while (name[nlen] && nlen < 32) { bstr[nlen+1] = name[nlen]; nlen++; }
            bstr[0] = nlen;
            put("Creating snapshot \""); put(name); put("\"...\n");
            if (DoPkt(port, ACTION_BFS_SNAPSHOT_CREATE, (LONG)MKBADDR(bstr), 0, 0, 0, 0))
                put("Snapshot created.\n");
            else { put("Failed.\n"); rc = 20; }
        }

    } else if (str_eq_nocase(cmd, "DELETE")) {
        if (!name || !name[0]) { put("DELETE requires a name.\n"); rc = 10; }
        else {
            UBYTE bstr[BFS_NAME_BSTR_MAX] = {0};
            int nlen = 0; while (name[nlen] && nlen < 32) { bstr[nlen+1] = name[nlen]; nlen++; }
            bstr[0] = nlen;
            put("Deleting snapshot \""); put(name); put("\"...\n");
            if (DoPkt(port, ACTION_BFS_SNAPSHOT_DELETE, (LONG)MKBADDR(bstr), 0, 0, 0, 0))
                put("Snapshot deleted.\n");
            else { put("Snapshot not found.\n"); rc = 20; }
        }

    } else if (str_eq_nocase(cmd, "SNAPSHOTS")) {
        put("Snapshots on "); put(drive); put(":\n");
        char sbuf[64];
        ULONG last_id = 0;
        int scount = 0;
        while (1) {
            LONG res = DoPkt(port, ACTION_BFS_SNAPSHOT_LIST,
                             (LONG)sbuf, (LONG)sizeof(sbuf), (LONG)last_id, 0, 0);
            if (!res) break;
            last_id = (ULONG)IoErr();
            /* Parse: name\0 + uint32 id + uint32 timestamp */
            char *sname = sbuf;
            int nlen = 0; while (sname[nlen]) nlen++;
            ULONG sid, ts;
            tool_memcpy(&sid, sbuf + nlen + 1, sizeof(sid));
            tool_memcpy(&ts, sbuf + nlen + 5, sizeof(ts));
            put("  #"); putnum((LONG)sid); put("  ");
            putpad(sname, 20);
            if (ts > 0) { put_date(ts); }
            else { put("(no date)"); }
            put("\n");
            scount++;
            if (scount > 256) break;
        }
        if (scount == 0) put("  (none)\n");
        else { putnum(scount); put(scount == 1 ? " snapshot\n" : " snapshots\n"); }

    } else if (str_eq_nocase(cmd, "DIR") || str_eq_nocase(cmd, "LIST")) {
        int detailed = str_eq_nocase(cmd, "LIST");
        int show_dirs = 1, show_files = 1, recursive = 0;
        if (opt) {
            if (str_eq_nocase(opt, "ALL")) recursive = 1;
            else if (str_eq_nocase(opt, "DIRS")) show_files = 0;
            else if (str_eq_nocase(opt, "FILES")) show_dirs = 0;
        }
        (void)recursive;

        if (!name || !name[0]) {
            put(detailed ? "LIST" : "DIR");
            put(" requires a snapshot name.\n"); rc = 10;
        } else {
            char snapname[BFS_NAME_BSTR_MAX], path[256];
            parse_snappath(name, snapname, 34, path, 256);

            UBYTE pktbuf[290];
            build_pktname(pktbuf, snapname, path);

            /* Header */
            put("Directory \""); put(snapname); put(":");
            if (path[0]) put(path);
            put("\" on "); put(drive); put("\n");

            char buf[300];
            ULONG last_key = 0;
            int count = 0;
            while (1) {
                LONG res = DoPkt(port, ACTION_BFS_SNAPSHOT_SHOW,
                                 (LONG)MKBADDR(pktbuf), (LONG)buf,
                                 (LONG)sizeof(buf), (LONG)last_key, 0);
                if (!res) break;

                char type = buf[0]; /* 'D' or 'F' */
                char *ename = buf + 2;
                int elen = 0; while (ename[elen] && ename[elen] != '\n') elen++;
                ename[elen] = 0;

                /* Filter */
                if (type == 'D' && !show_dirs) goto next;
                if (type == 'F' && !show_files) goto next;

                if (detailed) {
                    /* LIST format: name  size  protection  date */
                    putpad(ename, 26);
                    if (type == 'D') put("   Dir ");
                    else put("      "); /* TODO: get size from handler */
                    put("----rwed"); /* TODO: get real protection */
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
                last_key = (ULONG)IoErr();
                if (count > 10000) break;
            }
            putnum(count);
            put(count == 1 ? " entry\n" : " entries\n");
        }

    } else if (str_eq_nocase(cmd, "MOUNT")) {
        /* bfssnapshot DH1: MOUNT snapname VOLNAME: */
        if (!name || !name[0] || !opt || !opt[0]) {
            put("Usage: bfssnapshot DH1: MOUNT <snapshot> <volname>\n");
            put("Example: bfssnapshot DH1: MOUNT daily SNAP0\n");
            rc = 10;
        } else {
            UBYTE bsnap[BFS_NAME_BSTR_MAX] = {0}, bvol[BFS_NAME_BSTR_MAX] = {0};
            int nlen = 0; while (name[nlen] && nlen < 32) { bsnap[nlen+1] = name[nlen]; nlen++; }
            bsnap[0] = nlen;
            int vlen = 0; while (opt[vlen] && opt[vlen] != ':' && vlen < 32) { bvol[vlen+1] = opt[vlen]; vlen++; }
            bvol[0] = vlen;
            put("Mounting snapshot \""); put(name);
            put("\" as "); put(opt); put("...\n");
            if (DoPkt(port, ACTION_BFS_SNAPSHOT_MOUNT, (LONG)MKBADDR(bsnap), (LONG)MKBADDR(bvol), 0, 0, 0)) {
                put("Mounted. Use \""); put(opt); put(":\" to access.\n");
            } else { put("Mount failed.\n"); rc = 20; }
        }

    } else if (str_eq_nocase(cmd, "UNMOUNT")) {
        if (!name || !name[0]) { put("UNMOUNT requires a volume name.\n"); rc = 10; }
        else {
            UBYTE bvol[BFS_NAME_BSTR_MAX] = {0};
            int vlen = 0; while (name[vlen] && name[vlen] != ':' && vlen < 32) { bvol[vlen+1] = name[vlen]; vlen++; }
            bvol[0] = vlen;
            if (DoPkt(port, ACTION_BFS_SNAPSHOT_UNMOUNT, (LONG)MKBADDR(bvol), 0, 0, 0, 0))
                put("Unmounted.\n");
            else { put("Not found.\n"); rc = 20; }
        }

    } else if (str_eq_nocase(cmd, "INFO")) {
        put(VSTRING "\n");
        put("Drive: "); put(drive); put("\n");

    } else {
        put("Unknown command: "); put(cmd);
        put("\nValid: CREATE, DELETE, SNAPSHOTS, DIR, LIST, MOUNT, UNMOUNT, INFO\n");
        rc = 10;
    }

    FreeArgs(rdargs);
    me->pr_WindowPtr = oldwin;
    return rc;
}
