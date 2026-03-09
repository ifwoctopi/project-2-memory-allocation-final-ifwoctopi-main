#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>

/* Initialize the allocator - called once before any malloc/free calls */
int mm_init(void);

/* Allocate a block of at least size bytes */
void *mm_malloc(size_t size);

/* Free a previously allocated block */
void mm_free(void *ptr);

/* Optional: Resize a previously allocated block (extra credit) */
void *mm_realloc(void *ptr, size_t size);

/* Optional: Check heap consistency (useful for debugging) */
int mm_check(void);

#endif /* ALLOCATOR_H */
