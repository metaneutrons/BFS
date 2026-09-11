/* SPDX-License-Identifier: MPL-2.0 */
/* Create a small known tree for AmigaOS administration-command tests. */

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/rdargs.h>
#include <proto/dos.h>
#include <proto/exec.h>

static int text_length(const char *text)
{
    int length = 0;
    while (text[length]) length++;
    return length;
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

int main(void)
{
    struct Process *process = (struct Process *)FindTask(NULL);
    APTR old_window = process->pr_WindowPtr;
    struct RDArgs *parsed;
    LONG arguments[2] = {0, 0};
    const char *volume;
    char path[128];
    BPTR file;
    BPTR directory;
    int ok;

    process->pr_WindowPtr = (APTR)-1;
    parsed = ReadArgs("VOLUME/A,PROBE/S", arguments, NULL);
    if (!parsed) {
        process->pr_WindowPtr = old_window;
        return 20;
    }
    volume = (const char *)arguments[0];
    if (arguments[1]) {
        ULONG storage[8] = {0};
        UBYTE *name = (UBYTE *)storage;
        struct MsgPort *port = DeviceProc(volume);
        if (!port) ok = 0;
        else {
            name[0] = 8;
            name[1] = 'c'; name[2] = 'l'; name[3] = 'i'; name[4] = '-';
            name[5] = 'f'; name[6] = 'i'; name[7] = 'l'; name[8] = 'e';
            BPTR lock = DoPkt(port, ACTION_LOCATE_OBJECT, 0,
                              (LONG)MKBADDR(name), SHARED_LOCK, 0, 0);
            ok = lock != 0;
            if (lock) UnLock(lock);
            lock = ok ? Lock((STRPTR)volume, SHARED_LOCK) : 0;
            ok = lock != 0;
            if (lock) {
                struct FileInfoBlock fib;
                ok = Examine(lock, &fib) != DOSFALSE;
                if (ok) ok = ExNext(lock, &fib) != DOSFALSE;
                UnLock(lock);
            }
            if (ok && path_for(path, sizeof(path), volume, "cli-file")) {
                lock = Lock(path, SHARED_LOCK);
                ok = lock != 0;
                if (lock) {
                    struct FileInfoBlock fib;
                    ok = Examine(lock, &fib) != DOSFALSE;
                    UnLock(lock);
                }
                if (ok) {
                    char contents[8];
                    file = Open(path, MODE_OLDFILE);
                    ok = file != 0;
                    if (file) {
                        ok = Read(file, contents, sizeof(contents)) == sizeof(contents);
                        if (ok && (contents[0] != 'f' || contents[1] != 'i' ||
                                   contents[2] != 'x' || contents[3] != 't' ||
                                   contents[4] != 'u' || contents[5] != 'r' ||
                                   contents[6] != 'e' || contents[7] != '\n'))
                            ok = 0;
                        if (!Close(file)) ok = 0;
                    }
                }
                if (ok) {
                    file = Open(path, MODE_NEWFILE);
                    if (file) {
                        Close(file);
                        ok = 0;
                    } else if (IoErr() != ERROR_DISK_WRITE_PROTECTED) {
                        ok = 0;
                    }
                }
                if (ok && (DoPkt(port, ACTION_FORMAT, (LONG)MKBADDR(name),
                                 0, 0, 0, 0) ||
                           IoErr() != ERROR_DISK_WRITE_PROTECTED))
                    ok = 0;
            } else {
                ok = 0;
            }
        }
        if (ok) Write(Output(), "PROBE OK\n", 9);
        FreeArgs(parsed);
        process->pr_WindowPtr = old_window;
        return ok ? 0 : 20;
    }
    ok = path_for(path, sizeof(path), volume, "cli-file");
    file = ok ? Open(path, MODE_NEWFILE) : 0;
    if (!file) ok = 0;
    else {
        if (Write(file, "fixture\n", 8) != 8) ok = 0;
        if (!Close(file)) ok = 0;
    }
    if (ok && !path_for(path, sizeof(path), volume, "cli-dir")) ok = 0;
    directory = ok ? CreateDir(path) : 0;
    if (!directory) ok = 0;
    else UnLock(directory);
    FreeArgs(parsed);
    process->pr_WindowPtr = old_window;
    return ok ? 0 : 20;
}
