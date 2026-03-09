/*
 * Memory Allocator Implementation (C++)
 *
 * This file implements malloc and free using an explicit free list.
 *
 * BLOCK STRUCTURE:
 * - Every block has a header and footer containing size and allocated bit
 * - Free blocks store next and prev pointers in the payload area
 * - Minimum block size on 64-bit systems is 24 bytes:
 *     header(4) + next ptr(8) + prev ptr(8) + footer(4) = 24 bytes
 *   The split threshold in place() accounts for this (see MIN_BLOCK_SIZE below)
 * - All blocks are 8-byte aligned
 *
 * FREE LIST STRUCTURE:
 * - Explicit doubly-linked list of free blocks
 * - LIFO policy (insert freed blocks at the head)
 * - nullptr-terminated (no sentinel node)
 * - free_listp points to the head of the list, or nullptr if empty
 *
 * C++ USAGE NOTES:
 * - Use modern C++ features where helpful (nullptr, references, constexpr)
 * - DO NOT use new/delete (infinite recursion -- they call malloc!)
 * - DO NOT use STL containers (they call malloc internally!)
 * - DO NOT use smart pointers
 * - Pointer arithmetic and casts are necessary for this low-level code
 */

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include "allocator.h"
#include "memlib.h"

/* ============================================
 * Constants
 *
 * Integer constants can and should be constexpr in C++ -- it gives
 * type safety and lets the compiler catch mistakes that #define cannot.
 *
 * The pointer-manipulating macros below (HDRP, FTRP, GET, PUT, etc.)
 * cannot be constexpr because they dereference runtime addresses.
 * That is why those remain as #define macros rather than constexpr.
 * ============================================ */

constexpr size_t WSIZE = 4;             /* Word size in bytes              */
constexpr size_t DSIZE = 8;             /* Double word size in bytes       */
constexpr size_t CHUNKSIZE = (1 << 12); /* Default heap extension size 4KB */

/*
 * Minimum free block size.
 * A free block must hold: header(4) + next ptr + prev ptr + footer(4).
 * On a 64-bit system sizeof(void*) == 8, so the minimum is 4+8+8+4 = 24 bytes.
 * We round up to the next multiple of DSIZE to preserve alignment → 24 bytes.
 * Use this as the split threshold in place() rather than 2*DSIZE (16), which
 * would be too small to store the two free-list pointers.
 */
constexpr size_t MIN_BLOCK_SIZE = DSIZE + 2 * sizeof(void *); /* 24 on 64-bit */

/* ============================================
 * Macros
 * ============================================ */

/* Pack a size and allocated bit into a single word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a 4-byte word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Extract size and allocated bit from a header/footer word at address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* Given a block payload pointer bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given a block payload pointer bp, compute payload pointer of adjacent blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))

/* ============================================
 * Free list pointer macros
 *
 * Free blocks store a next and prev pointer inside their payload:
 *
 *   [Header 4B][Next sizeof(void*)][Prev sizeof(void*)][...][Footer 4B]
 *              ^bp                  ^bp + sizeof(void*)
 *
 * GET_NEXT_FREE / GET_PREV_FREE dereference those memory locations,
 * which produces an lvalue (a location you can assign to). That is
 * why SET_NEXT_FREE / SET_PREV_FREE can write through those same
 * expressions: assigning to a dereferenced pointer writes to the
 * underlying memory. This is standard C++ -- not a trick.
 *
 * We use sizeof(void*) rather than DSIZE for the prev offset so the
 * code is correct on both 32-bit (sizeof(void*)==4) and 64-bit
 * (sizeof(void*)==8) platforms. On a 64-bit machine these happen to
 * be equal, but being explicit avoids a silent bug on 32-bit.
 * ============================================ */
#define GET_NEXT_FREE(bp) (*(void **)(bp))
#define GET_PREV_FREE(bp) (*(void **)((char *)(bp) + sizeof(void *)))
#define SET_NEXT_FREE(bp, val) (*(void **)(bp) = (val))
#define SET_PREV_FREE(bp, val) (*(void **)((char *)(bp) + sizeof(void *)) = (val))

/* ============================================
 * Global Variables
 * ============================================ */

/* Points to the payload of the prologue block (fixed anchor at heap start) */
static char *heap_listp = nullptr;

/* Points to the first block in the explicit free list, or nullptr if empty */
static char *free_listp = nullptr;

/* ============================================
 * Helper Function Prototypes
 * ============================================ */

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void add_to_free_list(void *bp);
static void remove_from_free_list(void *bp);

/* ============================================
 * Main Allocator Functions
 * ============================================ */

/*
 * mm_init - Initialize the memory allocator.
 *
 * TODO: Implement this function.
 *
 * Creates the initial empty heap with a prologue and epilogue block.
 * These sentinel blocks prevent special-case code in coalesce():
 * the prologue ensures we never coalesce past the heap start, and
 * the epilogue ensures we never coalesce past the heap end.
 *
 * Required heap layout after mm_init:
 *
 *   Offset:  0      4      8      12
 *            +------+------+------+------+
 *   Content: | Pad  |ProHdr|ProFtr|EpiHdr|
 *            |  0   | 8|1  | 8|1  |  0|1 |
 *            +------+------+------+------+
 *                          ^
 *                      heap_listp points HERE
 *                      (prologue payload: between header and footer)
 *
 * Prologue: size=DSIZE (8), allocated=1
 * Epilogue: size=0,         allocated=1
 *
 * Steps:
 * 1. Call mem_sbrk(4 * WSIZE). Return -1 immediately if it fails.
 * 2. Write four words at the returned address:
 *      [0]: padding word, value 0
 *      [1]: prologue header, PACK(DSIZE, 1)
 *      [2]: prologue footer, PACK(DSIZE, 1)
 *      [3]: epilogue header, PACK(0, 1)
 * 3. Set heap_listp to point to the prologue payload:
 *      heap_listp = <returned address> + 2*WSIZE
 *    This places heap_listp between the prologue header and footer.
 * 4. Set free_listp = nullptr  (no free blocks yet).
 * 5. Call extend_heap(CHUNKSIZE / WSIZE). Return -1 if it fails.
 *
 * Return: 0 on success, -1 on error.
 */
int mm_init(void)
{
    /* TODO: Request 4 words from the memory system */
    /* Hint: if ((heap_listp = (char*)mem_sbrk(4 * WSIZE)) == (char*)-1) return -1; */

    /* TODO: Write the four initial words */
    /* Hint: PUT(heap_listp, 0);                              padding       */
    /* Hint: PUT(heap_listp + WSIZE,     PACK(DSIZE, 1));    prologue hdr  */
    /* Hint: PUT(heap_listp + 2*WSIZE,   PACK(DSIZE, 1));    prologue ftr  */
    /* Hint: PUT(heap_listp + 3*WSIZE,   PACK(0, 1));        epilogue hdr  */

    /* TODO: Advance heap_listp to the prologue payload */
    /* Hint: heap_listp += (2 * WSIZE); */

    /* TODO: Initialize the free list to empty */
    /* Hint: free_listp = nullptr; */

    /* TODO: Extend the heap with an initial free block */
    /* Hint: if (extend_heap(CHUNKSIZE / WSIZE) == nullptr) return -1; */

    /* TODO: Remove this line and return 0 once fully implemented */

    if ((heap_listp = (char *)mem_sbrk(4 * WSIZE)) == (char *)-1)
        return -1;
    PUT(heap_listp, 0);                          // padding
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1));     // prologue hdr
    PUT(heap_listp + 2 * WSIZE, PACK(DSIZE, 1)); // prologue ftr
    PUT(heap_listp + 3 * WSIZE, PACK(0, 1));     // epilogue hdr
    heap_listp += (2 * WSIZE);
    free_listp = nullptr;
    if (extend_heap(CHUNKSIZE / WSIZE) == nullptr)
        return -1;
    return 0;
}

/*
 * mm_malloc - Allocate a block with at least size bytes of payload.
 *
 * TODO: Implement this function.
 *
 * Steps:
 * 1. Return nullptr for size == 0.
 * 2. Compute the adjusted size asize that includes header+footer overhead
 *    and satisfies alignment:
 *      if size <= DSIZE:  asize = 2 * DSIZE         (minimum block)
 *      else:              asize = DSIZE * ((size + DSIZE + DSIZE-1) / DSIZE)
 * 3. Search free list: bp = find_fit(asize).
 *    If found, call place(bp, asize) and return bp.
 * 4. If not found, extend heap by max(asize, CHUNKSIZE), place, return.
 *    Return nullptr if extend_heap fails.
 *
 * Return: pointer to allocated payload, or nullptr on failure.
 */
void *mm_malloc(size_t size)
{
    size_t asize;      /* Adjusted block size (overhead + alignment)     */
    size_t extendsize; /* Amount to extend heap if no free block fits    */
    char *bp;

    /* TODO: Reject zero-size request */
    if (size == 0)
        return nullptr;
    /* TODO: Compute adjusted block size */
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + DSIZE + DSIZE - 1) / DSIZE);

    /* TODO: Search free list for a fit */
    if ((bp = (char *)find_fit(asize)) != nullptr)
    {
        place(bp, asize);
        return bp;
    }

    /* TODO: No fit found -- extend heap */
    extendsize = (asize > CHUNKSIZE) ? asize : CHUNKSIZE;
    if ((bp = (char *)extend_heap(extendsize / WSIZE)) == nullptr)
        return nullptr;
    place(bp, asize);

    return bp;
}

/*
 * mm_free - Free a previously allocated block.
 *
 * TODO: Implement this function. (NOT required for checkpoint.)
 *
 * Steps:
 * 1. Return immediately if ptr == nullptr.
 * 2. Read the block size from the header.
 * 3. Clear the allocated bit in both header and footer.
 * 4. Call coalesce(ptr).
 *
 * IMPORTANT: Do NOT call add_to_free_list() here.
 * coalesce() handles adding the final merged block to the free list.
 * Calling add_to_free_list() in both places would insert the block
 * twice and silently corrupt the list.
 *
 * Return: nothing.
 */
void mm_free(void *ptr)
{
    /* TODO: Guard against nullptr */
    if (ptr == nullptr)
        return;

    /* TODO: Read block size */
    /* Hint: size_t size = GET_SIZE(HDRP(ptr)); */
        size_t size = GET_SIZE(HDRP(ptr));


    /* TODO: Clear allocated bit in header and footer */
    /* Hint: PUT(HDRP(ptr), PACK(size, 0)); */
    /* Hint: PUT(FTRP(ptr), PACK(size, 0)); */
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));

    /* TODO: Coalesce and add to free list */
    /* Hint: coalesce(ptr); */
    coalesce(ptr);
}

/*
 * mm_realloc - Resize a previously allocated block. (OPTIONAL -- extra credit)
 *
 * A correct naive implementation is provided. For extra credit, replace it
 * with an in-place version that avoids an unnecessary copy when the next
 * block is free and the combined size is sufficient.
 *
 * Return: pointer to resized block, or nullptr on failure.
 */
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == nullptr)
        return mm_malloc(size);
    if (size == 0)
    {
        mm_free(ptr);
        return nullptr;
    }

    void *newptr = mm_malloc(size);
    if (newptr == nullptr)
        return nullptr;

    size_t copy_size = GET_SIZE(HDRP(ptr)) - DSIZE; /* payload only: subtract header + footer */
    if (size < copy_size)
        copy_size = size;
    memcpy(newptr, ptr, copy_size);
    mm_free(ptr);
    return newptr;
}

/* ============================================
 * Helper Functions
 * ============================================ */

/*
 * extend_heap - Extend the heap by (words * WSIZE) bytes.
 *
 * TODO: Implement this function.
 *
 * Steps:
 * 1. Round words up to an even number:
 *      size = (words % 2) ? (words+1)*WSIZE : words*WSIZE;
 * 2. Call mem_sbrk(size). Return nullptr if it fails.
 *    mem_sbrk returns the OLD break pointer. Because the header
 *    sits 4 bytes before the payload, that old break is exactly bp.
 * 3. Write new free block header:  PUT(HDRP(bp), PACK(size, 0))
 *    Write new free block footer:  PUT(FTRP(bp), PACK(size, 0))
 * 4. Write new epilogue past the block: PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1))
 * 5. Return coalesce(bp) -- do NOT call add_to_free_list directly.
 *
 * Return: pointer to the new free block (possibly merged), or nullptr.
 */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    /* TODO: Round up to even number of words */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

    /* TODO: Request memory from system */
    if ((bp = (char *)mem_sbrk(size)) == (char *)-1)
        return nullptr;

    /* TODO: Initialize free block header and footer */
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    /* TODO: Place new epilogue */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    /* TODO: Coalesce with previous block if free and return */
    return coalesce(bp);
}

/*
 * coalesce - Merge bp with any adjacent free blocks, then add to free list.
 *
 * TODO: Implement this function. (NOT required for checkpoint.)
 *
 * Always call this immediately after marking a block free -- never call
 * add_to_free_list() directly from mm_free().
 *
 * Four cases based on neighbor allocation status:
 *   Case 1: prev alloc,  next alloc  -- no merge
 *   Case 2: prev alloc,  next free   -- merge with next
 *   Case 3: prev free,   next alloc  -- merge with prev
 *   Case 4: prev free,   next free   -- merge with both
 *
 * For every block you absorb, call remove_from_free_list() BEFORE
 * updating any sizes. Changing sizes first corrupts the list because
 * removal relies on reading correct size/pointer fields.
 *
 * In cases 3 and 4, set bp = PREV_BLKP(bp) after merging so that bp
 * refers to the start of the combined block when add_to_free_list is called.
 *
 * Hints:
 *   int prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
 *   int next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
 *   size_t size    = GET_SIZE(HDRP(bp));
 *
 * Return: pointer to the (possibly enlarged) free block.
 */
static void *coalesce(void *bp)
{
    /* TODO: Check neighbor allocation status */
    /* TODO: Get current block size */
    int prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    int next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    /* TODO: Case 1 -- both allocated, nothing to merge */
    if (prev_alloc && next_alloc)
    {
        add_to_free_list(bp);
        return bp;
    }

    /* TODO: Case 2 -- merge with next */
    if (prev_alloc && !next_alloc)
    {
        size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_from_free_list(NEXT_BLKP(bp));
        size += next_size;
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        add_to_free_list(bp);
        return bp;
    }

    /* TODO: Case 3 -- merge with prev */
    if (!prev_alloc && next_alloc)
    {
        size_t prev_size = GET_SIZE(HDRP(PREV_BLKP(bp)));
        void *prev_bp = PREV_BLKP(bp);
        remove_from_free_list(prev_bp);
        size += prev_size;
        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bp = prev_bp;
        add_to_free_list(bp);
        return bp;
    }
    /* IMPORTANT: Save PREV_BLKP(bp) before writing anything. PREV_BLKP reads  */
    /* bp's footer to find the previous block; once we write to FTRP(bp) that   */
    /* pointer would be wrong. Always resolve PREV_BLKP first.                  */
    /* Hint: void *prev_bp = PREV_BLKP(bp);                    */
    /* Hint: remove_from_free_list(prev_bp);                   */
    /* Hint: size += GET_SIZE(HDRP(prev_bp));                  */
    /* Hint: PUT(HDRP(prev_bp), PACK(size, 0));                */
    /* Hint: PUT(FTRP(bp),      PACK(size, 0));   // bp's footer is now merged footer */
    /* Hint: bp = prev_bp;                                     */

    /* TODO: Case 4 -- merge with both */
    if (!prev_alloc && !next_alloc)
    {
        size_t prev_size = GET_SIZE(HDRP(PREV_BLKP(bp)));
        size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(bp)));
        void *prev_bp = PREV_BLKP(bp);
        remove_from_free_list(prev_bp);
        remove_from_free_list(NEXT_BLKP(bp));
        size += prev_size + next_size;
        PUT(HDRP(prev_bp), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = prev_bp;
        add_to_free_list(bp);
        return bp;
    }
    /* Combine the logic from cases 2 and 3:                           */
    /* - Save next_size and prev_bp before any writes                  */
    /* - remove_from_free_list for both neighbors                      */
    /* - size += next_size + GET_SIZE(HDRP(prev_bp))                  */
    /* - Update HDRP(prev_bp) and FTRP(NEXT_BLKP(prev_bp)) (the far   */
    /*   footer, which is bp's original footer), then bp = prev_bp    */

    /* TODO: Add merged block to free list */
    /* Hint: add_to_free_list(bp); */

    return bp;
}

/*
 * find_fit - Return the first free block >= asize bytes, or nullptr.
 *
 * TODO: Implement this function.
 *
 * Traverse the explicit free list from free_listp using GET_NEXT_FREE.
 * Return the first block whose header reports a size >= asize.
 *
 * Hint:
 *   for (void *bp = free_listp; bp != nullptr; bp = GET_NEXT_FREE(bp))
 *       if (GET_SIZE(HDRP(bp)) >= asize) return bp;
 */
static void *find_fit(size_t asize)
{
    /* TODO: Walk free list */
    for (void *bp = free_listp; bp != nullptr; bp = GET_NEXT_FREE(bp))
    {
        if (GET_SIZE(HDRP(bp)) >= asize)
            return bp;
    }

    return nullptr; /* No fit found */
}

/*
 * place - Allocate asize bytes at bp, splitting if the remainder is usable.
 *
 * TODO: Implement this function.
 *
 * Steps:
 * 1. Read csize = GET_SIZE(HDRP(bp)).
 * 2. remove_from_free_list(bp).
 * 3. If (csize - asize) >= MIN_BLOCK_SIZE:
 *      - Write allocated header+footer for first asize bytes.
 *      - Advance bp to NEXT_BLKP(bp).
 *      - Write free header+footer for remaining (csize-asize) bytes.
 *      - add_to_free_list(bp) for the leftover.
 *    Else:
 *      - Write allocated header+footer using full csize.
 *    Note: use MIN_BLOCK_SIZE (not 2*DSIZE) as the threshold. On 64-bit systems
 *    a remainder of only 16 bytes cannot hold the two free-list pointers.
 *
 * Return: nothing.
 */
static void place(void *bp, size_t asize)
{
    /* TODO: Read current block size and remove from free list */
    size_t csize = GET_SIZE(HDRP(bp));
    remove_from_free_list(bp);
    if ((csize - asize) >= MIN_BLOCK_SIZE)
    {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void *next_bp = NEXT_BLKP(bp);
        PUT(HDRP(next_bp), PACK(csize - asize, 0));
        PUT(FTRP(next_bp), PACK(csize - asize, 0));
        add_to_free_list(next_bp);
    }
    else
    {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/*
 * add_to_free_list - Insert bp at the head of the free list (LIFO).
 *
 * TODO: Implement this function.
 *
 * Steps:
 * 1. SET_NEXT_FREE(bp, free_listp)      -- bp's next = old head
 * 2. SET_PREV_FREE(bp, nullptr)          -- bp has no predecessor
 * 3. If free_listp != nullptr:
 *      SET_PREV_FREE(free_listp, bp)    -- old head's prev = bp
 * 4. free_listp = (char*)bp            -- bp is new head
 *
 * Return: nothing.
 */
static void add_to_free_list(void *bp)
{
    /* TODO: Insert bp at head of list */
    SET_NEXT_FREE(bp, free_listp);
    SET_PREV_FREE(bp, nullptr);
    if (free_listp != nullptr)
    {
        SET_PREV_FREE(free_listp, bp);
    }
    free_listp = (char *)bp;
}

/*
 * remove_from_free_list - Unlink bp from the free list.
 *
 * TODO: Implement this function.
 *
 * Steps:
 * 1. void *prev = GET_PREV_FREE(bp);
 *    void *next = GET_NEXT_FREE(bp);
 * 2. If prev == nullptr: free_listp = (char*)next   (bp was head)
 *    Else:               SET_NEXT_FREE(prev, next)
 * 3. If next != nullptr: SET_PREV_FREE(next, prev)
 *
 * Return: nothing.
 */
static void remove_from_free_list(void *bp)
{
    /* TODO: Unlink bp */
    void *prev = GET_PREV_FREE(bp);
    void *next = GET_NEXT_FREE(bp);
    if (prev == nullptr)
    {
        free_listp = (char *)next;
    }
    else
    {
        SET_NEXT_FREE(prev, next);
    }
    if (next != nullptr)
    {
        SET_PREV_FREE(next, prev);
    }
}

/*
 * mm_check - Heap consistency checker. (Optional but strongly recommended.)
 *
 * Suggested checks:
 *   1. Every block in the free list is marked free.
 *   2. No two adjacent free blocks exist (escaped coalescing).
 *   3. Every free block in the heap appears in the free list.
 *   4. Free list is doubly-linked consistently (node->next->prev == node).
 *   5. No block extends outside heap bounds.
 *   6. Header and footer of each block agree on size and alloc bit.
 *
 * Call mm_check() after every malloc/free during development.
 * Remove calls (or guard with #ifdef DEBUG) before final submission.
 *
 * Return: 0 if consistent, non-zero on any error.
 */
int mm_check(void)
{
    /* TODO: Optional -- implement consistency checks here */
    return 0;
}
