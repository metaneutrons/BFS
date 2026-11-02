/* SPDX-License-Identifier: MPL-2.0 */
/* Shared by hitrate.c and hitrate-check.c: reading and naming the counters. */
#ifndef HITRATE_COMMON_H
#define HITRATE_COMMON_H

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include "hitrate.h"

static const char *const hr_kind_name[HR_KINDS] = {"exall", "exnext"};
static const char *const hr_test_name[HR_TESTS] = {"stamps", "counter"};
static const char *const hr_scope_name[HR_SCOPES] = {"kind", "any"};
static const char *const hr_class_name[HR_CLASSES] = {"new", "same_lock", "other_lock", "changed"};

static BOOL hr_read(const char *device, bfs_hitrate_report_t *report)
{
    struct MsgPort *port = DeviceProc(device);
    if (!port) return FALSE;
    report->magic = 0;
    if (!DoPkt(port, BFS_ACTION_HITRATE_READ, (LONG)report, (LONG)sizeof(*report), 0, 0, 0))
        return FALSE;
    return report->magic == BFS_HITRATE_MAGIC && report->version == BFS_HITRATE_VERSION &&
           report->size == (ULONG)sizeof(*report);
}

static BOOL hr_reset(const char *device)
{
    struct MsgPort *port = DeviceProc(device);
    if (!port) return FALSE;
    return DoPkt(port, BFS_ACTION_HITRATE_RESET, 0, 0, 0, 0, 0) != 0;
}

static ULONG hr_length(const char *value)
{
    ULONG length = 0;
    while (value[length]) length++;
    return length;
}

static void hr_emit(const char *value)
{
    Write(Output(), (APTR)value, hr_length(value));
}

static void hr_emit_number(unsigned long value)
{
    char digits[12];
    int count = 0;
    if (value == 0) digits[count++] = '0';
    while (value > 0) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }
    while (count > 0) {
        char c = digits[--count];
        Write(Output(), &c, 1);
    }
}

#endif
