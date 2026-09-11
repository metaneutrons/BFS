/* SPDX-License-Identifier: MPL-2.0 */

#include <exec/types.h>
#include <proto/dos.h>

#include "bfs_command.h"

static int text_length(const char *text)
{
    int length = 0;
    while (text[length]) length++;
    return length;
}

void bfs_put(const char *text)
{
    Write(Output(), (APTR)text, text_length(text));
}

void bfs_putnum(LONG value)
{
    char buffer[12], *cursor = buffer + sizeof(buffer) - 1;
    ULONG magnitude;

    *cursor = 0;
    if (value < 0) magnitude = (ULONG)(-(value + 1)) + 1;
    else magnitude = (ULONG)value;
    do {
        *--cursor = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0);
    if (value < 0) *--cursor = '-';
    bfs_put(cursor);
}

void bfs_putu64(unsigned long long value, int width)
{
    char buffer[24], *cursor = buffer + sizeof(buffer) - 1;

    *cursor = 0;
    do {
        *--cursor = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0);
    while (width-- > text_length(cursor)) bfs_put(" ");
    bfs_put(cursor);
}

void bfs_putpad(const char *text, int width)
{
    int padding = width - text_length(text);
    bfs_put(text);
    while (padding-- > 0) bfs_put(" ");
}

void bfs_copy(void *destination, const void *source, int length)
{
    UBYTE *out = (UBYTE *)destination;
    const UBYTE *in = (const UBYTE *)source;

    while (length-- > 0) *out++ = *in++;
}

int bfs_equal_nocase(const char *left, const char *right)
{
    while (*left && *right) {
        char a = (*left >= 'a' && *left <= 'z') ? *left - 32 : *left;
        char b = (*right >= 'a' && *right <= 'z') ? *right - 32 : *right;
        if (a != b) return 0;
        left++;
        right++;
    }
    return *left == *right;
}

int bfs_build_name_bstr(UBYTE *destination, const char *name)
{
    int length = text_length(name);

    if (length < 1 || length >= 32) return 0;
    destination[0] = (UBYTE)length;
    bfs_copy(destination + 1, name, length);
    return 1;
}

UBYTE *bfs_bstr_bytes(bfs_bstr_t *bstr)
{
    ULONG address = (ULONG)(APTR)bstr->storage;

    /* MKBADDR shifts two low bits; stack byte arrays do not guarantee them. */
    return (UBYTE *)((address + 3) & ~3UL);
}

int bfs_contains_slash(const char *text)
{
    while (*text) {
        if (*text++ == '/') return 1;
    }
    return 0;
}
