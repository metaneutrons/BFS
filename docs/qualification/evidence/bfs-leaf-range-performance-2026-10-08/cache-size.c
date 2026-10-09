/* SPDX-License-Identifier: MPL-2.0 */
/* Compiler-emitted constants, not a runtime resident-memory measurement. */
#include "bfs_cache.h"
const unsigned long cache_descriptor_bytes = sizeof(bfs_cache_slot_t);
const unsigned long cache_object_bytes = sizeof(bfs_cache_t);
const unsigned long cache_scratch_slots = BFS_CACHE_SCRATCH_SLOTS;
