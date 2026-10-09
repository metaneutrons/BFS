/* SPDX-License-Identifier: MPL-2.0 */
/* Native AROS: the core's heap through exec. The handler starts as a DOS
 * process without a C runtime, so nothing has to initialise a C library. */
#include <exec/memory.h>
#include <proto/exec.h>
#include <stddef.h>

void *malloc(size_t size)
{
    return AllocVec(size ? size : 1, MEMF_ANY);
}

void *calloc(size_t count, size_t size)
{
    if (size && count > (size_t)-1 / size) return NULL;
    size_t total = count * size;
    return AllocVec(total ? total : 1, MEMF_ANY | MEMF_CLEAR);
}

void free(void *memory)
{
    if (memory) FreeVec(memory);
}
