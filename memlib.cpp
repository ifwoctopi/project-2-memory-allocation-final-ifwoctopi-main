#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <unistd.h>
#include <cstring>
#include "memlib.h"

/* Private global variables */
#define MAX_HEAP (8 * 1024 * 1024)  /* 8 MB max heap size */

static char *mem_heap;      /* Pointer to first byte of heap */
static char *mem_brk;       /* Pointer to last byte of heap plus 1 */
static char *mem_max_addr;  /* Max legal heap address plus 1 */

/* 
 * mem_init - Initialize the memory system
 * DO NOT MODIFY THIS FUNCTION
 */
void mem_init(void) {
    mem_heap = (char *)malloc(MAX_HEAP);
    if (mem_heap == NULL) {
        fprintf(stderr, "mem_init: malloc failed\n");
        exit(1);
    }
    mem_brk = mem_heap;
    mem_max_addr = mem_heap + MAX_HEAP;
}

/*
 * mem_deinit - Free the memory system
 * DO NOT MODIFY THIS FUNCTION
 */
void mem_deinit(void) {
    free(mem_heap);
}

/*
 * mem_sbrk - Simple model of the sbrk function. Extends the heap 
 *            by incr bytes and returns the start address of the new area.
 *            Returns (void *)-1 on error.
 * DO NOT MODIFY THIS FUNCTION
 */
void *mem_sbrk(int incr) {
    char *old_brk = mem_brk;

    if ((incr < 0) || ((mem_brk + incr) > mem_max_addr)) {
        fprintf(stderr, "ERROR: mem_sbrk failed. Ran out of memory...\n");
        return (void *)-1;
    }
    
    mem_brk += incr;
    return (void *)old_brk;
}

/*
 * mem_heap_lo - Return address of the first heap byte
 * DO NOT MODIFY THIS FUNCTION
 */
void *mem_heap_lo(void) {
    return (void *)mem_heap;
}

/* 
 * mem_heap_hi - Return address of last heap byte
 * DO NOT MODIFY THIS FUNCTION
 */
void *mem_heap_hi(void) {
    return (void *)(mem_brk - 1);
}

/*
 * mem_heapsize - Return the heap size in bytes
 * DO NOT MODIFY THIS FUNCTION
 */
size_t mem_heapsize(void) {
    return (size_t)(mem_brk - mem_heap);
}

/*
 * mem_pagesize - Return the page size of the system
 * DO NOT MODIFY THIS FUNCTION
 */
size_t mem_pagesize(void) {
    return (size_t)getpagesize();
}
