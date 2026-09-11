/* SPDX-License-Identifier: MPL-2.0 */
/* Create a small known tree for AmigaOS administration-command tests. */

#include <dos/dos.h>
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
    LONG arguments[1] = {0};
    const char *volume;
    char path[128];
    BPTR file;
    BPTR directory;
    int ok;

    process->pr_WindowPtr = (APTR)-1;
    parsed = ReadArgs("VOLUME/A", arguments, NULL);
    if (!parsed) {
        process->pr_WindowPtr = old_window;
        return 20;
    }
    volume = (const char *)arguments[0];
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
