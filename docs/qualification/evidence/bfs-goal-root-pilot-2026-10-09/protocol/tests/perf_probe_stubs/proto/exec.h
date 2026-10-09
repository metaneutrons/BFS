/* SPDX-License-Identifier: MPL-2.0 */
/* Minimal test-only Exec prototypes used by src/amiga/perf_probe.c. */
#ifndef BFS_TEST_PROTO_EXEC_H
#define BFS_TEST_PROTO_EXEC_H

#include <exec/io.h>
#include <exec/types.h>

struct IORequest *CreateIORequest(struct MsgPort *port, ULONG size);
void DeleteIORequest(struct IORequest *request);
LONG OpenDevice(const char *name, ULONG unit, struct IORequest *request,
                ULONG flags);
void CloseDevice(struct IORequest *request);

#endif
