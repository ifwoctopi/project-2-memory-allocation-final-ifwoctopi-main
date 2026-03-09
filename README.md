[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/cBMVGwQD)
# Project 2: Memory Allocator

## Overview
In this project, you will implement your own version of `malloc` and `free` using an **explicit free list** with immediate coalescing. This project will deepen your understanding of memory management, pointer manipulation, and data structure implementation at a low level.

## Learning Objectives
- Understand how dynamic memory allocation works
- Implement a free list data structure
- Practice pointer arithmetic and bit manipulation
- Learn about memory fragmentation and coalescing
- Debug memory-related issues

## Timeline
- **Assigned:** Wednesday, February 18
- **Checkpoint Due:** Wednesday, February 25 (11:59 PM)
  - Must have `malloc` working and pass checkpoint tests
  - `free` can be a stub (no-op is fine)
- **Spring Break:** March 2-6
- **Final Due:** Friday, March 13 (11:59 PM)
  - Complete implementation with `malloc`, `free`, and coalescing
- **Tech Interviews:** Week of March 16-20

## Project Structure
```
malloc-project/
├── README.md           # This file
├── allocator.cpp         # Your implementation (EDIT THIS)
├── allocator.h         # Function prototypes
├── memlib.cpp            # Memory system helpers (DO NOT EDIT)
├── memlib.h            # Memory system interface
├── test_checkpoint.cpp   # Checkpoint tests
├── test_final.cpp        # Full test suite
├── Makefile            # Build configuration
└── .github/
    └── workflows/
        └── classroom.yml  # Autograder configuration
```

## Getting Started

### Language: C++
This project uses **C++17**. You can use modern C++ features, but with important restrictions (see below).

### 1. Clone Your Repository
```bash
git clone <your-repo-url>
cd malloc-project (or whatever I named it)
```

### 2. Build the Project
```bash
make
```

### 3. Run Checkpoint Tests
```bash
./test_checkpoint
```

### 4. Run Full Tests
```bash
./test_final
```

## C++ Usage Guidelines

### ✅ You CAN Use
- **Modern C++ syntax**: `nullptr` instead of NULL, `auto` for type inference
- **C++ casts**: `static_cast`, `reinterpret_cast` (clearer than C casts)
- **References**: Pass by reference where appropriate
- **constexpr**: For compile-time constants
- **Inline functions**: Small helper functions
- **Namespaces**: If you want to organize your code
- **C++ headers**: `<cstdio>`, `<cstring>`, etc.

### ❌ You CANNOT Use (Will Cause Failures!)
- **`new` / `delete`**: Will cause infinite recursion (they call malloc)
- **STL containers**: `vector`, `string`, `map`, `list`, etc. (they call malloc internally)
- **Smart pointers**: `unique_ptr`, `shared_ptr`, `weak_ptr` (they call new/delete)
- **`std::allocator`**: Any STL allocator (calls malloc)
- **Exception throwing with heap allocation**: May allocate memory

### Why These Restrictions?
You're implementing malloc itself. Using anything that allocates memory will:
1. Call your incomplete malloc → crash or infinite recursion
2. Corrupt your heap data structures
3. Make debugging a nightmare

### Example: Good vs Bad C++

```cpp
// ✅ GOOD - Modern C++ without forbidden features
void *mm_malloc(size_t size) {
    if (size == 0) return nullptr;  // Modern C++: nullptr
    
    size_t asize = (size <= 8) ? 16 : ((size + 15) & ~7);  // Bitwise alignment
    
    void *bp = find_fit(asize);
    if (bp != nullptr) {
        place(bp, asize);
        return bp;
    }
    
    return nullptr;
}

// ❌ BAD - Uses forbidden features
void *mm_malloc(size_t size) {
    std::vector<void*> blocks;  // Nope. Calls malloc internally
    auto ptr = new char[size];   // Nope. Calls malloc (infinite recursion)
    return ptr;
}
```

## Implementation Requirements

### Core Requirements (Required for Passing)
1. **`malloc(size_t size)`**
   - Allocate a block of at least `size` bytes
   - Return pointer to usable payload
   - Return NULL if allocation fails
   - Use explicit free list with first-fit or next-fit policy
   - Split blocks when necessary

2. **`free(void *ptr)`**
   - Free the block pointed to by `ptr`
   - Add block back to free list
   - Implement immediate bidirectional coalescing

3. **Block Structure**
   - Minimum block size: 24 bytes (header + footer + free list pointers)
   - 8-byte alignment for all blocks
   - Header and footer contain size and allocated bit

4. **Performance Targets**
   - **Utilization:** ≥ 60% (average across all tests)
   - **Throughput:** ≥ 5000 Kops/sec (not strict, but aim for reasonable speed)

### Checkpoint Requirements (Due Feb 25)
- Implement `malloc` with block splitting
- Maintain explicit free list
- Pass all checkpoint tests
- `free` can be a stub (doesn't need to work yet)

### Extra Credit Opportunities (Optional)
- **Address-ordered free list** (+5%): Maintain free list in address order instead of LIFO
- **Realloc** (+5%): Implement efficient `realloc` function
- **High utilization** (+5%): Achieve ≥ 75% utilization across all tests
- **Very high utilization** (+10%): Achieve ≥ 80% utilization across all tests

## Memory System Interface

You interact with the heap through these helper functions (provided in `memlib.c`):

```c
void *mem_sbrk(int incr);     // Extend heap by incr bytes, return old brk
void *mem_heap_lo(void);      // Return address of first byte in heap
void *mem_heap_hi(void);      // Return address of last byte in heap
size_t mem_heapsize(void);    // Return current heap size in bytes
size_t mem_pagesize(void);    // Return system page size
```

**Important:** 
- The heap size is limited to 8 MB
- `mem_sbrk()` returns `(void *)-1` on failure
- You must initialize the heap in your `mm_init()` function

**Notes:**
- A = 1 (allocated), A = 0 (free)
- Size includes header and footer
- Size must be multiple of 8 (alignment requirement)
- Minimum block size is 24 bytes

## Testing and Grading

### Checkpoint Tests (25% of project grade)
- 8 tests focusing on `malloc` correctness
- Must pass all to receive checkpoint credit
- Partial credit for passing subset of tests

### Final Tests (45% of project grade)
- All checkpoint tests plus 12 additional tests
- Tests include:
  - Simple allocations
  - Random allocation patterns
  - Reallocation patterns
  - Binary tree allocation
  - Heavy fragmentation scenarios
- **Correctness:** must pass all tests
- **Performance:** utilization ≥ 50% soft floor


## Development Tips

### Debugging Strategies
1. **Start simple:** Get basic malloc working before optimizing
2. **Test early, test often:** Run tests after each major change
3. **Use helper functions:** Create `heap_checker()` to validate consistency
4. **Print debugging:** Add `#ifdef DEBUG` blocks for detailed logging
5. **Draw pictures:** Sketch out block layouts on paper
6. **Use gdb:** Set breakpoints and inspect memory

### Common Pitfalls
- **Forgetting alignment:** All blocks must be 8-byte aligned
- **Off-by-one errors:** Be careful with pointer arithmetic
- **Forgetting to coalesce:** Always coalesce after freeing
- **Not checking for nullptr:** Handle failed allocations properly
- **Double-free bugs:** Freeing the same block twice causes corruption
- **Using forbidden C++ features:** Never use new/delete/STL in this project

### C++ Specific Pitfalls
- **Accidentally using `std::string`:** Use char* arrays instead
- **Accidentally using `std::vector`:** Use manual arrays or linked lists
- **Using `auto` with allocations:** Be explicit about types when calling malloc

### Recommended Development Order
1. Implement `mm_init()` - set up initial heap
2. Implement `extend_heap()` - grow heap when needed
3. Implement `find_fit()` - search free list
4. Implement `place()` - allocate block and split if needed
5. Implement `malloc()` - tie it all together
6. **Test checkpoint** ← Stop here for checkpoint
7. Implement `coalesce()` - merge adjacent free blocks
8. Implement `free()` - add to free list and coalesce
9. Test and optimize

## Resources

### Useful Reading
- [CMU Malloc Lab Writeup](http://csapp.cs.cmu.edu/3e/malloclab.pdf)


## Submission
- Push your code to your GitHub repository
- The autograder runs automatically on every push
- Your last push before the deadline is your submission
- **Checkpoint:** Pushed by Feb 25, 11:59 PM
- **Final:** Pushed by Mar 13, 11:59 PM
