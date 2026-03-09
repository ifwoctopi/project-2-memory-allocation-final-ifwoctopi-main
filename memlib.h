#ifndef MEMLIB_H
#define MEMLIB_H

#include <cstddef>  /* size_t — C++ style header */

/* Memory system interface - DO NOT MODIFY */

/* Initialize the memory system */
void mem_init(void);

/* Deinitialize the memory system */
void mem_deinit(void);

/*
 * Extend the heap by incr bytes and return the start of the new area.
 * Returns (void *)-1 on error.
 *
 * Note: incr is typed as int. Negative values are rejected, and the
 * 8 MB heap cap means the practical maximum is well within int range.
 * Passing a large size_t that truncates to a negative int will be
 * caught by the bounds check inside mem_sbrk and return (void *)-1.
 */
void *mem_sbrk(int incr);

/* Return address of first byte in heap */
void *mem_heap_lo(void);

/* Return address of last byte in heap */
void *mem_heap_hi(void);

/* Return current heap size in bytes */
size_t mem_heapsize(void);

/* Return system page size in bytes */
size_t mem_pagesize(void);

#endif /* MEMLIB_H */
