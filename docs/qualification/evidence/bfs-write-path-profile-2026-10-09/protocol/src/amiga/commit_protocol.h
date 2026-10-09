/* SPDX-License-Identifier: MPL-2.0 */
/* DOS-packet contract for the BFS commit policy. See docs/amiga-packets.md. */

#ifndef BFS_AMIGA_COMMIT_PROTOCOL_H
#define BFS_AMIGA_COMMIT_PROTOCOL_H

#define BFS_ACTION_COMMIT_MODE   3012

/* Argument 1 of BFS_ACTION_COMMIT_MODE; result 1 reports the active mode. */
#define BFS_COMMIT_MODE_QUERY    0
#define BFS_COMMIT_MODE_DELAYED  1
#define BFS_COMMIT_MODE_SYNC     2

#endif /* BFS_AMIGA_COMMIT_PROTOCOL_H */
