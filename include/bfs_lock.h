/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Lightweight multi-platform lock abstraction
 *
 * Maps to POSIX Read-Write locks (pthread_rwlock_t) on Host platforms (macOS/Linux).
 * Maps to zero-overhead no-ops on AmigaOS/AROS, where the DOS handler processes
 * packets sequentially and no real concurrency exists.
 *
 * NOTE: bfs_fs_lock_t (this file) is the filesystem-wide reader/writer lock that
 * guards a bfs_fs_t. It is unrelated to bfs_lock_t in src/amiga/handler.c, which
 * is the AmigaOS DOS FileLock object (a per-open BPTR handle). Despite the
 * similar name, the two are different concepts — do not conflate them.
 */

#ifndef BFS_LOCK_H
#define BFS_LOCK_H

#if defined(BFS_HOST) && !defined(BFS_AMIGA)
#include <pthread.h>

typedef pthread_rwlock_t bfs_fs_lock_t;

static inline void bfs_lock_init(bfs_fs_lock_t *lock) {
    pthread_rwlock_init(lock, NULL);
}

static inline void bfs_lock_destroy(bfs_fs_lock_t *lock) {
    pthread_rwlock_destroy(lock);
}

static inline void bfs_lock_read(bfs_fs_lock_t *lock) {
    pthread_rwlock_rdlock(lock);
}

static inline void bfs_lock_write(bfs_fs_lock_t *lock) {
    pthread_rwlock_wrlock(lock);
}

static inline void bfs_lock_unlock(bfs_fs_lock_t *lock) {
    pthread_rwlock_unlock(lock);
}

#else

/* No-op implementations for AmigaOS/AROS/cooperative systems */
typedef int bfs_fs_lock_t;

static inline void bfs_lock_init(bfs_fs_lock_t *lock) { (void)lock; }
static inline void bfs_lock_destroy(bfs_fs_lock_t *lock) { (void)lock; }
static inline void bfs_lock_read(bfs_fs_lock_t *lock) { (void)lock; }
static inline void bfs_lock_write(bfs_fs_lock_t *lock) { (void)lock; }
static inline void bfs_lock_unlock(bfs_fs_lock_t *lock) { (void)lock; }

#endif

#endif /* BFS_LOCK_H */
