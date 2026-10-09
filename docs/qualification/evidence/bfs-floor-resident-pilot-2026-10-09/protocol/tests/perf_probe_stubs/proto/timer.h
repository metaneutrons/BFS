/* SPDX-License-Identifier: MPL-2.0 */
/* Minimal test-only Timer prototypes used by src/amiga/perf_probe.c. */
#ifndef BFS_TEST_PROTO_TIMER_H
#define BFS_TEST_PROTO_TIMER_H

#include <devices/timer.h>

ULONG ReadEClock(struct EClockVal *value);

#endif
