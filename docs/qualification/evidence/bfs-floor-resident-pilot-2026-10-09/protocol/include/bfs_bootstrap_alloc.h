/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Bootstrap bump allocator
 *
 * Simple sequential allocator for early B+tree testing.
 */

#ifndef BFS_BOOTSTRAP_ALLOC_H
#define BFS_BOOTSTRAP_ALLOC_H

#include "bfs_btree.h"

typedef struct bootstrap_alloc bootstrap_alloc_t;

/* Create a bootstrap allocator that hands out blocks [start, max). */
bootstrap_alloc_t *bootstrap_create(bfs_blk_t start, bfs_blk_t max);

#endif /* BFS_BOOTSTRAP_ALLOC_H */
