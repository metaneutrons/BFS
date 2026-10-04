/* SPDX-License-Identifier: MPL-2.0 */
#ifndef BFS_DOS_NAME_H
#define BFS_DOS_NAME_H

/* Packet names are length-prefixed on m68k and NUL-terminated on native
 * AROS. FileInfoBlock names are a separate, length-prefixed packet ABI. */
static inline unsigned bfs_dos_name_length(const unsigned char *name,
                                            unsigned limit)
{
    if (!name) return limit;
#ifdef BFS_AROS
    unsigned length = 0;
    while (length < limit && name[length]) length++;
    return length;
#else
    return name[0];
#endif
}

static inline const unsigned char *bfs_dos_name_text(const unsigned char *name)
{
#ifdef BFS_AROS
    return name;
#else
    return name + 1;
#endif
}

static inline int bfs_dos_name_encode(unsigned char *destination,
                                      unsigned capacity,
                                      const unsigned char *text,
                                      unsigned length)
{
#ifdef BFS_AROS
    if (length >= capacity) return 0;
    for (unsigned i = 0; i < length; i++) destination[i] = text[i];
    destination[length] = 0;
#else
    if (length > 255 || length + 1 >= capacity) return 0;
    destination[0] = (unsigned char)length;
    for (unsigned i = 0; i < length; i++) destination[i + 1] = text[i];
    destination[length + 1] = 0;
#endif
    return 1;
}

#endif
