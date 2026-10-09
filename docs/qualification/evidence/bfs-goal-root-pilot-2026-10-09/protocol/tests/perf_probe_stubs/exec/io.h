/* SPDX-License-Identifier: MPL-2.0 */
/* Minimal test-only Exec I/O structures used by src/amiga/perf_probe.c. */
#ifndef BFS_TEST_EXEC_IO_H
#define BFS_TEST_EXEC_IO_H

struct Device {
    int unused;
};

struct MsgPort {
    int unused;
};

struct IORequest {
    struct Device *io_Device;
};

#endif
