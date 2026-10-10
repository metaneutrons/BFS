/* SPDX-License-Identifier: MPL-2.0 */
/*
 * BFS — Directory B+tree
 *
 * Single global directory tree of fixed 12-byte keys and 40-byte values (see
 * bfs_ondisk.h). An entry is a head record with the first 33 name bytes and up
 * to six continuation parts for longer names. Directories also own a parent
 * link record, and any inode can own a comment record.
 *
 * Case-insensitive lookup using Amiga international character folding. Keys
 * hold the FNV-1a hash of the folded name, not the name, so the tree order is
 * plain byte order; names are compared only within one hash.
 *
 * Every mutating call changes all of its records or none of them. When undoing
 * a partial change fails, the tree's sticky_err is set and the caller must not
 * publish the transaction.
 */

#ifndef BFS_DIR_H
#define BFS_DIR_H

#include "bfs_btree.h"
#include "bfs_alloc.h"
#include "bfs_ondisk.h"

/* Directory tree handle */
typedef struct {
    bfs_btree_t tree;
    /* An undo of a partial multi-record change failed; the live tree may hold
     * a half-done change. Reset only by bfs_dir_init. */
    // cppcheck-suppress unusedStructMember
    bfs_err_t sticky_err;
} bfs_dir_tree_t;

/* The place of an entry in its directory's key order. */
typedef struct {
    // cppcheck-suppress unusedStructMember
    uint32_t hash;
    // cppcheck-suppress unusedStructMember
    uint16_t ordinal;
} bfs_dir_pos_t;

/* Scan callback: called for each entry in a directory */
typedef bool (*bfs_dir_scan_cb)(const char *name, uint8_t name_len,
                                  uint32_t inode_nr, uint32_t entry_type,
                                  void *ctx);

/* As bfs_dir_scan_cb, with the entry's position for a later resume. */
typedef bool (*bfs_dir_scan_pos_cb)(const char *name, uint8_t name_len,
                                    uint32_t inode_nr, uint32_t entry_type,
                                    const bfs_dir_pos_t *pos, void *ctx);

/* Initialize the directory tree. root = BFS_BLK_NULL for empty. */
bfs_err_t bfs_dir_init(bfs_dir_tree_t *dt, bfs_bio_t *bio,
                   bfs_allocator_t *alloc, bfs_blk_t root, uint64_t txn_id);

/* Look up a name in a directory. Case-insensitive.
 * Returns BFS_OK and fills inode_nr, or BFS_ERR_NOTFOUND. */
bfs_err_t bfs_dir_lookup(bfs_dir_tree_t *dt, uint32_t parent_id,
                           const char *name, uint8_t name_len,
                           uint32_t *inode_nr_out, uint32_t *type_out);

/* bfs_dir_lookup that also returns the entry's position. */
bfs_err_t bfs_dir_lookup_pos(bfs_dir_tree_t *dt, uint32_t parent_id,
                             const char *name, uint8_t name_len,
                             uint32_t *inode_nr_out, uint32_t *type_out,
                             bfs_dir_pos_t *pos_out);

/* Insert a directory entry. Returns BFS_ERR_EXISTS if a case-equivalent name
 * exists, BFS_ERR_INVAL for "." and "..", and BFS_ERR_OVERFLOW if all 65,536
 * ordinals of the name's hash are taken. Owner 0 holds only the root "/". */
bfs_err_t bfs_dir_insert(bfs_dir_tree_t *dt, uint32_t parent_id,
                           const char *name, uint8_t name_len,
                           uint32_t inode_nr, uint32_t entry_type);

/* Insert an entry at the position it had before bfs_dir_remove, as an undo
 * does. pos->hash must be the name's hash; a taken ordinal returns
 * BFS_ERR_EXISTS. */
bfs_err_t bfs_dir_insert_at(bfs_dir_tree_t *dt, uint32_t parent_id,
                            const char *name, uint8_t name_len,
                            uint32_t inode_nr, uint32_t entry_type,
                            const bfs_dir_pos_t *pos);

/* Replace the value of an existing entry. The key and its stored name spelling
 * remain unchanged; optional outputs receive the displaced value. */
bfs_err_t bfs_dir_replace(bfs_dir_tree_t *dt, uint32_t parent_id,
                          const char *name, uint8_t name_len,
                          uint32_t inode_nr, uint32_t entry_type,
                          uint32_t *old_inode_out, uint32_t *old_type_out);

/* Change only the stored spelling of a case-equivalent entry. */
bfs_err_t bfs_dir_rekey_case(bfs_dir_tree_t *dt, uint32_t parent_id,
                             const char *old_name, uint8_t old_len,
                             const char *new_name, uint8_t new_len);

/* Remove a directory entry. */
bfs_err_t bfs_dir_remove(bfs_dir_tree_t *dt, uint32_t parent_id,
                           const char *name, uint8_t name_len);

/* Scan all entries in a directory (for ExAll/ExNext). Parent links and
 * comments are not entries and are not reported. */
bfs_err_t bfs_dir_scan(bfs_dir_tree_t *dt, uint32_t parent_id,
                         bfs_dir_scan_cb cb, void *ctx);

/* As bfs_dir_scan, but continue after the entry with this name in key
 * order. The entry need not exist any more, so an enumeration survives the
 * removal of the entry it stopped at; if it is gone, the scan continues after
 * every entry with its hash. */
bfs_err_t bfs_dir_scan_after(bfs_dir_tree_t *dt, uint32_t parent_id,
                             const char *name, uint8_t name_len,
                             bfs_dir_scan_cb cb, void *ctx);

/* bfs_dir_scan for name_len 0, bfs_dir_scan_after otherwise, resuming from
 * the cursor's leaf copy when it still covers the start (see
 * bfs_btree_scan_cursor). An enumeration keeps one cursor across calls. */
bfs_err_t bfs_dir_scan_cursor(bfs_dir_tree_t *dt, bfs_btree_cursor_t *cursor,
                              uint32_t parent_id, const char *name,
                              uint8_t name_len, bfs_dir_scan_cb cb, void *ctx);

/* Scan from the start (after == NULL) or after the entry at *after, which
 * need not exist any more: the scan continues with the next position. */
bfs_err_t bfs_dir_scan_cursor_pos(bfs_dir_tree_t *dt, bfs_btree_cursor_t *cursor,
                                  uint32_t parent_id, const bfs_dir_pos_t *after,
                                  bfs_dir_scan_pos_cb cb, void *ctx);

/* Resume exclusively after a current stopped entry for this parent. A stale
 * or missing cursor, unavailable BIO mutation epoch, or stop in another
 * parent returns BFS_ERR_AGAIN without calling cb. */
bfs_err_t bfs_dir_scan_resume(bfs_dir_tree_t *dt, bfs_btree_cursor_t *cursor,
                              uint32_t parent_id, bfs_dir_scan_cb cb, void *ctx);

/* bfs_dir_scan_resume with positions. */
bfs_err_t bfs_dir_scan_resume_pos(bfs_dir_tree_t *dt, bfs_btree_cursor_t *cursor,
                                  uint32_t parent_id, bfs_dir_scan_pos_cb cb,
                                  void *ctx);

/* Cheap pre-check for a scan: *maybe is false only if parent_id has no
 * entries; true means it may have some. */
bfs_err_t bfs_dir_may_have_entries(bfs_dir_tree_t *dt, uint32_t parent_id,
                                   bool *maybe);

/* The parent link of a directory other than the root. */
bfs_err_t bfs_dir_parent_get(bfs_dir_tree_t *dt, uint32_t dir_ino,
                             uint32_t *parent_out);
bfs_err_t bfs_dir_parent_insert(bfs_dir_tree_t *dt, uint32_t dir_ino,
                                uint32_t parent);
bfs_err_t bfs_dir_parent_replace(bfs_dir_tree_t *dt, uint32_t dir_ino,
                                 uint32_t parent, uint32_t *old_parent_out);
bfs_err_t bfs_dir_parent_remove(bfs_dir_tree_t *dt, uint32_t dir_ino);

/* The comment of an inode, 1 to BFS_DIR_COMMENT_MAX bytes. Get returns
 * BFS_ERR_NOTFOUND when there is none; insert returns BFS_ERR_EXISTS when
 * there is one. */
bfs_err_t bfs_dir_comment_get(bfs_dir_tree_t *dt, uint32_t ino,
                              char text[BFS_DIR_COMMENT_MAX], uint8_t *len_out);
bfs_err_t bfs_dir_comment_insert(bfs_dir_tree_t *dt, uint32_t ino,
                                 const char *text, uint8_t len);
bfs_err_t bfs_dir_comment_remove(bfs_dir_tree_t *dt, uint32_t ino);

/* Callbacks of bfs_dir_walk; each may be NULL. Returning false stops it. */
typedef struct {
    // cppcheck-suppress unusedStructMember
    bool (*entry)(uint32_t parent, const char *name, uint8_t name_len,
                  uint32_t inode_nr, uint32_t entry_type,
                  const bfs_dir_pos_t *pos, void *ctx);
    // cppcheck-suppress unusedStructMember
    bool (*parent_link)(uint32_t dir_ino, uint32_t parent, void *ctx);
    // cppcheck-suppress unusedStructMember
    bool (*comment)(uint32_t ino, const char *text, uint8_t len, void *ctx);
} bfs_dir_walk_ops_t;

/* Walk every record of the tree in key order, as a checker does. Entries and
 * comments are reported once complete. Records that do not form complete
 * entries or comments, such as orphaned or missing parts or a name whose hash
 * does not match, return BFS_ERR_CORRUPT. */
bfs_err_t bfs_dir_walk(bfs_dir_tree_t *dt, const bfs_dir_walk_ops_t *ops, void *ctx);

/* FNV-1a hash of case-folded name (exposed for testing) */
uint32_t bfs_dir_name_hash(const char *name, uint8_t len);

/* Case-fold a character (Amiga international) */
uint8_t bfs_intl_toupper(uint8_t c);

#endif /* BFS_DIR_H */
