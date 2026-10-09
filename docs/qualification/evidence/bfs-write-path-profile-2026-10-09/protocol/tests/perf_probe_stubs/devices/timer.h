/* SPDX-License-Identifier: MPL-2.0 */
/* Minimal test-only Timer device declarations used by the host probe build. */
#ifndef BFS_TEST_DEVICES_TIMER_H
#define BFS_TEST_DEVICES_TIMER_H

#include <exec/io.h>
#include <exec/types.h>

#define TIMERNAME "timer.device"
#define UNIT_ECLOCK 3u

struct EClockVal {
    ULONG ev_hi;
    ULONG ev_lo;
};

struct timerequest {
    struct IORequest tr_node;
};

#endif
