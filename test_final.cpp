/*
 * Final Test Suite  (C++17)
 *
 * Tests complete malloc + free + coalescing correctness and performance.
 * Tests 1-8 are the same checkpoint tests.
 * Tests 9-20 add free(), coalescing, and performance checks.
 *
 * Usage:
 *   ./test_final             — run all tests, print summary
 *   ./test_final <N>         — run only test N (1-indexed), exit 0=pass 1=fail
 *                              (used by the GitHub Classroom autograder)
 */

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <cstdint>
#include <cstddef>
#include "allocator.h"
#include "memlib.h"

// ─────────────────────────────────────────────
// Minimal test framework
// ─────────────────────────────────────────────

struct TestResult {
    std::string name;
    bool        passed = false;
    std::string failure_msg;
    std::string extra;   // optional metric (e.g. utilization %)
};

static std::vector<std::pair<std::string, std::function<TestResult()>>> g_tests;

static void register_test(const std::string &name,
                           std::function<TestResult()> fn) {
    g_tests.emplace_back(name, std::move(fn));
}

static TestResult pass(const std::string &name,
                       const std::string &extra = "") {
    return { name, true, "", extra };
}

static TestResult fail(const std::string &name,
                       const std::string &msg,
                       const std::string &extra = "") {
    return { name, false, msg, extra };
}

// ─────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────

static bool is_aligned(const void *ptr) {
    return (reinterpret_cast<uintptr_t>(ptr) % 8) == 0;
}

static bool reset_allocator() {
    mem_deinit();
    mem_init();
    return mm_init() == 0;
}

// ─────────────────────────────────────────────
// Tests 1-8  (mirrors checkpoint suite exactly)
// ─────────────────────────────────────────────

static TestResult test_single_alloc() {
    const std::string name = "Single allocation";
    void *ptr = mm_malloc(8);
    if (ptr == nullptr)
        return fail(name, "malloc returned nullptr — check mm_init and extend_heap");
    if (!is_aligned(ptr))
        return fail(name, "returned pointer is not 8-byte aligned");
    auto *p = static_cast<int *>(ptr);
    *p = 42;
    if (*p != 42)
        return fail(name, "cannot write/read from allocated memory — header may be corrupt");
    return pass(name);
}

static TestResult test_multiple_small_allocs() {
    const std::string name = "Multiple small allocations";
    constexpr int N = 10;
    void *ptrs[N];
    for (int i = 0; i < N; ++i) {
        ptrs[i] = mm_malloc(8);
        if (ptrs[i] == nullptr)
            return fail(name, "malloc returned nullptr — may have run out of heap space");
        if (!is_aligned(ptrs[i]))
            return fail(name, "returned pointer is not 8-byte aligned");
        *static_cast<int *>(ptrs[i]) = i * 100;
    }
    for (int i = 0; i < N; ++i) {
        if (*static_cast<int *>(ptrs[i]) != i * 100)
            return fail(name, "data corruption — a later allocation overwrote an earlier block");
    }
    return pass(name);
}

static TestResult test_various_sizes() {
    const std::string name = "Various allocation sizes";
    constexpr size_t sizes[] = { 1, 8, 16, 32, 64, 128, 256, 512, 1024 };
    constexpr int    N       = sizeof(sizes) / sizeof(sizes[0]);
    void *ptrs[N];
    for (int i = 0; i < N; ++i) {
        ptrs[i] = mm_malloc(sizes[i]);
        if (ptrs[i] == nullptr)
            return fail(name, "malloc returned nullptr — check size-rounding and extend_heap");
        if (!is_aligned(ptrs[i]))
            return fail(name, "returned pointer is not 8-byte aligned");
        std::memset(ptrs[i], i, sizes[i]);
    }
    for (int i = 0; i < N; ++i) {
        auto *p = static_cast<unsigned char *>(ptrs[i]);
        for (size_t j = 0; j < sizes[i]; ++j)
            if (p[j] != static_cast<unsigned char>(i))
                return fail(name, "data corruption — blocks are overlapping or too small");
    }
    return pass(name);
}

static TestResult test_large_alloc() {
    const std::string name = "Large allocation (1 MB)";
    void *ptr = mm_malloc(1024 * 1024);
    if (ptr == nullptr)
        return fail(name, "malloc returned nullptr for 1 MB — check extend_heap loop");
    if (!is_aligned(ptr))
        return fail(name, "returned pointer is not 8-byte aligned");
    auto *p = static_cast<int *>(ptr);
    p[0] = 1; p[1000] = 2; p[262143] = 3;
    if (p[0] != 1 || p[1000] != 2 || p[262143] != 3)
        return fail(name, "data corruption in large allocation — block may be too small");
    return pass(name);
}

static TestResult test_zero_size() {
    const std::string name = "Zero-size allocation returns nullptr";
    if (mm_malloc(0) != nullptr)
        return fail(name, "malloc(0) should return nullptr per the spec");
    return pass(name);
}

static TestResult test_sequential_stress() {
    const std::string name = "Sequential allocations (100 blocks of 32 B)";
    constexpr int N = 100;
    void *ptrs[N];
    for (int i = 0; i < N; ++i) {
        ptrs[i] = mm_malloc(32);
        if (ptrs[i] == nullptr)
            return fail(name, "malloc failed — heap may not be growing correctly");
        auto *p = static_cast<int *>(ptrs[i]);
        p[0] = i; p[1] = i * 2;
    }
    for (int i = 0; i < N; ++i) {
        auto *p = static_cast<int *>(ptrs[i]);
        if (p[0] != i || p[1] != i * 2)
            return fail(name, "data corruption — blocks may be overlapping");
    }
    return pass(name);
}

static TestResult test_alternating_sizes() {
    const std::string name = "Alternating small (8 B) and large (512 B) allocations";
    constexpr int N = 20;
    void *ptrs[N];
    for (int i = 0; i < N; ++i) {
        size_t sz = (i % 2 == 0) ? 8 : 512;
        ptrs[i] = mm_malloc(sz);
        if (ptrs[i] == nullptr)
            return fail(name, "malloc returned nullptr — check alignment rounding");
        if (!is_aligned(ptrs[i]))
            return fail(name, "returned pointer is not 8-byte aligned");
        std::memset(ptrs[i], i, sz);
    }
    for (int i = 0; i < N; ++i) {
        size_t sz = (i % 2 == 0) ? 8 : 512;
        auto  *p  = static_cast<unsigned char *>(ptrs[i]);
        for (size_t j = 0; j < sz; ++j)
            if (p[j] != static_cast<unsigned char>(i))
                return fail(name, "data corruption — adjacent blocks may be overlapping");
    }
    return pass(name);
}

static TestResult test_very_large_alloc() {
    const std::string name = "Very large allocation (4 MB)";
    void *ptr = mm_malloc(4 * 1024 * 1024);
    if (ptr == nullptr)
        return fail(name, "malloc returned nullptr for 4 MB — extend_heap may not request enough");
    auto  *p         = static_cast<uint64_t *>(ptr);
    size_t num_words = (4 * 1024 * 1024) / sizeof(uint64_t);
    p[0]             = 0x123456789ABCDEF0ULL;
    p[num_words / 2] = 0xFEDCBA9876543210ULL;
    p[num_words - 1] = 0xAAAABBBBCCCCDDDDULL;
    if (p[0]             != 0x123456789ABCDEF0ULL ||
        p[num_words / 2] != 0xFEDCBA9876543210ULL ||
        p[num_words - 1] != 0xAAAABBBBCCCCDDDDULL)
        return fail(name, "data corruption in 4 MB block — block boundary may be wrong");
    return pass(name);
}

// ─────────────────────────────────────────────
// Tests 9-20  (free + coalescing + performance)
// ─────────────────────────────────────────────

// Test 9 — free() must not crash on a valid pointer
static TestResult test_basic_free() {
    const std::string name = "Basic free does not crash";
    void *ptr = mm_malloc(64);
    if (ptr == nullptr)
        return fail(name, "malloc returned nullptr before we could test free");
    mm_free(ptr);   // should not crash or hang
    return pass(name);
}

// Test 10 — freed block must be reused before heap grows
static TestResult test_free_and_reuse() {
    const std::string name = "Freed block is reused (heap does not grow)";

    void *first = mm_malloc(64);
    if (first == nullptr)
        return fail(name, "malloc failed on first allocation");

    size_t heap_before = mem_heapsize();
    mm_free(first);
    void *second = mm_malloc(64);
    if (second == nullptr)
        return fail(name, "malloc returned nullptr after free — block may not be in free list");

    size_t heap_after = mem_heapsize();
    // A correct implementation reuses the freed block and grows by 0 bytes.
    // Allow 32 bytes of slack for alignment rounding only.
    // More growth means the freed block was not added to the free list.
    if (heap_after > heap_before + 32)
        return fail(name, "heap grew after free+malloc — freed block is not being reused");

    return pass(name);
}

// Test 11 — free multiple blocks, then reallocate all
static TestResult test_multiple_free_realloc() {
    const std::string name = "Free N blocks then reallocate N blocks";
    constexpr int N = 10;
    void *ptrs[N];

    for (int i = 0; i < N; ++i) {
        ptrs[i] = mm_malloc(32);
        if (ptrs[i] == nullptr)
            return fail(name, "malloc failed during initial allocation phase");
        *static_cast<int *>(ptrs[i]) = i;
    }
    for (int i = 0; i < N; ++i)
        mm_free(ptrs[i]);

    // All N slots were freed — we should be able to allocate them again
    for (int i = 0; i < N; ++i) {
        ptrs[i] = mm_malloc(32);
        if (ptrs[i] == nullptr)
            return fail(name, "malloc failed after freeing all blocks — coalescing or free list may be broken");
    }
    return pass(name);
}

// Test 12 — backward coalescing (merge with previous free block)
// Layout: [a:free][b:freeing][c:alloc]
// When b is freed, its prev neighbor (a) is already free → Case 3 (backward merge)
static TestResult test_coalesce_backward() {
    const std::string name = "Backward coalescing (merge with previous free block)";

    void *a = mm_malloc(64);
    void *b = mm_malloc(64);
    void *c = mm_malloc(64);
    if (!a || !b || !c)
        return fail(name, "malloc failed during setup");

    size_t heap_before = mem_heapsize();
    mm_free(a);   // free a first so it is b's free neighbor when b is freed
    mm_free(b);   // b's prev (a) is free → should merge backward with a

    // The two freed 64-byte blocks should merge into ~128 bytes
    void *big = mm_malloc(100);
    if (big == nullptr)
        return fail(name, "could not fit 100 B after freeing two adjacent blocks — backward coalescing may be missing");

    size_t heap_after = mem_heapsize();
    if (heap_after > heap_before + 256)
        return fail(name, "heap grew more than expected — coalesced space may not be reused");

    mm_free(c);
    mm_free(big);
    return pass(name);
}

// Test 13 — forward coalescing (merge with next free block)
// Layout: [a:freeing][b:free][c:alloc]
// When a is freed, its next neighbor (b) is already free → Case 2 (forward merge)
static TestResult test_coalesce_forward() {
    const std::string name = "Forward coalescing (merge with next free block)";

    void *a = mm_malloc(64);
    void *b = mm_malloc(64);
    void *c = mm_malloc(64);
    if (!a || !b || !c)
        return fail(name, "malloc failed during setup");

    size_t heap_before = mem_heapsize();
    mm_free(b);   // free b first so it is a's free neighbor when a is freed
    mm_free(a);   // a's next (b) is free → should merge forward with b

    void *big = mm_malloc(100);
    if (big == nullptr)
        return fail(name, "could not fit 100 B after freeing two blocks — forward coalescing may be missing");

    size_t heap_after = mem_heapsize();
    if (heap_after > heap_before + 256)
        return fail(name, "heap grew more than expected — forward coalescing may not update size correctly");

    mm_free(c);
    mm_free(big);
    return pass(name);
}

// Test 14 — bidirectional coalescing (merge with both neighbors)
static TestResult test_coalesce_both() {
    const std::string name = "Bidirectional coalescing (merge with both neighbors)";

    void *a = mm_malloc(64);
    void *b = mm_malloc(64);
    void *c = mm_malloc(64);
    void *d = mm_malloc(64);
    if (!a || !b || !c || !d)
        return fail(name, "malloc failed during setup");

    size_t heap_before = mem_heapsize();
    mm_free(a);
    mm_free(c);
    mm_free(b);   // 'b' is between two free blocks — should merge all three

    void *big = mm_malloc(160);
    if (big == nullptr)
        return fail(name, "could not fit 160 B after freeing three adjacent blocks — bidirectional coalescing may be missing");

    size_t heap_after = mem_heapsize();
    if (heap_after > heap_before + 256)
        return fail(name, "heap grew more than expected — both-direction coalesce may be incomplete");

    mm_free(d);
    mm_free(big);
    return pass(name);
}

// Test 15 — free(nullptr) must be a no-op
static TestResult test_free_nullptr() {
    const std::string name = "free(nullptr) is a no-op";
    mm_free(nullptr);   // must not crash, segfault, or corrupt the heap
    return pass(name);
}

// Test 16 — alternating malloc/free (each block freed before next is allocated)
static TestResult test_alloc_free_alternating() {
    const std::string name = "Alternating malloc/free (50 iterations)";
    for (int i = 0; i < 50; ++i) {
        void *ptr = mm_malloc(64);
        if (ptr == nullptr)
            return fail(name, "malloc failed — freed block may not be going back into the free list");
        *static_cast<int *>(ptr) = i;
        if (*static_cast<int *>(ptr) != i)
            return fail(name, "data corruption immediately after allocation");
        mm_free(ptr);
    }
    return pass(name);
}

// Test 17 — interleaved alloc/free with data-integrity check
static TestResult test_interleaved_pattern() {
    const std::string name = "Interleaved alloc/free with data integrity";
    constexpr int SLOTS = 50;
    void *ptrs[SLOTS] = {};
    bool  live[SLOTS] = {};

    for (int op = 0; op < 150; ++op) {
        int idx = op % SLOTS;
        if (!live[idx]) {
            size_t sz = static_cast<size_t>(16 + (op % 128));
            ptrs[idx] = mm_malloc(sz);
            if (ptrs[idx] == nullptr)
                return fail(name, "malloc returned nullptr — free list or extend_heap may be broken");
            *static_cast<int *>(ptrs[idx]) = idx;
            live[idx] = true;
        } else {
            if (*static_cast<int *>(ptrs[idx]) != idx)
                return fail(name, "data corruption detected before free — blocks may be overlapping");
            mm_free(ptrs[idx]);
            ptrs[idx] = nullptr;
            live[idx] = false;
        }
    }
    // Clean up remaining live blocks
    for (int i = 0; i < SLOTS; ++i)
        if (live[i]) mm_free(ptrs[i]);

    return pass(name);
}

// Test 18 — binary-tree allocation pattern (alloc all leaves, free, re-alloc)
static TestResult test_binary_tree_pattern() {
    const std::string name = "Binary-tree allocation pattern";
    constexpr int NODES = 127;   // 7-level complete binary tree
    void *nodes[NODES];

    for (int i = 0; i < NODES; ++i) {
        nodes[i] = mm_malloc(24);
        if (nodes[i] == nullptr)
            return fail(name, "malloc failed while building tree — heap may be too fragmented");
    }
    // Free all leaves (indices 63-126)
    for (int i = 63; i < NODES; ++i)
        mm_free(nodes[i]);

    // Re-allocate the same leaves
    for (int i = 63; i < NODES; ++i) {
        nodes[i] = mm_malloc(24);
        if (nodes[i] == nullptr)
            return fail(name, "malloc failed when re-allocating leaf nodes — freed blocks may not be reusable");
    }
    // Clean up
    for (int i = 0; i < NODES; ++i)
        mm_free(nodes[i]);

    return pass(name);
}

// Test 19 — utilization (payload / heap size)
static TestResult test_utilization() {
    const std::string name = "Heap utilization >= 60%";
    constexpr int N = 100;
    void *ptrs[N];
    size_t total_payload = 0;

    for (int i = 0; i < N; ++i) {
        size_t sz = static_cast<size_t>(16 + i * 8);
        ptrs[i] = mm_malloc(sz);
        if (ptrs[i] == nullptr)
            return fail(name, "malloc returned nullptr during utilization test");
        total_payload += sz;
    }

    size_t heap_size  = mem_heapsize();
    double utilization = 100.0 * static_cast<double>(total_payload)
                                / static_cast<double>(heap_size);

    std::string metric = std::to_string(static_cast<int>(utilization)) + "%";

    if (utilization < 50.0)
        return fail(name,
            "utilization is very low — check that blocks are being split and that "
            "overhead is not excessive",
            metric);

    if (utilization < 60.0)
        return fail(name,
            "utilization is below the 60% target — review your splitting logic",
            metric);

    return pass(name, metric);
}

// Test 20 — throughput (operations per second)
static TestResult test_throughput() {
    const std::string name = "Throughput >= 1000 Kops/sec";
    constexpr int OPS   = 10000;
    constexpr int SLOTS = 1000;
    void *ptrs[SLOTS]  = {};
    bool  live[SLOTS]  = {};

    // Warm-up pass (not timed) — avoids cold-start skew on CI/CD runners
    for (int i = 0; i < 200; ++i) { void *p = mm_malloc(64); mm_free(p); }

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < OPS; ++i) {
        int idx = i % SLOTS;
        if (!live[idx]) {
            ptrs[idx] = mm_malloc(static_cast<size_t>(16 + (i % 128)));
            live[idx] = (ptrs[idx] != nullptr);
        } else {
            mm_free(ptrs[idx]);
            ptrs[idx] = nullptr;
            live[idx] = false;
        }
    }

    auto end      = std::chrono::high_resolution_clock::now();
    double secs   = std::chrono::duration<double>(end - start).count();
    double kops   = static_cast<double>(OPS) / secs / 1000.0;

    std::string metric = std::to_string(static_cast<int>(kops)) + " Kops/s";

    if (kops < 200.0)
        return fail(name,
            "throughput is very low — are you walking the entire heap instead of the free list?",
            metric);

    if (kops < 1000.0)
        return fail(name,
            "throughput is below target — ensure find_fit traverses the free list, not the entire heap",
            metric);

    return pass(name, metric);
}

// ─────────────────────────────────────────────
// Registration + main
// ─────────────────────────────────────────────

static void register_all() {
    // Checkpoint tests (1-8)
    register_test("Single allocation",                           test_single_alloc);
    register_test("Multiple small allocations",                  test_multiple_small_allocs);
    register_test("Various allocation sizes",                    test_various_sizes);
    register_test("Large allocation (1 MB)",                     test_large_alloc);
    register_test("Zero-size allocation returns nullptr",        test_zero_size);
    register_test("Sequential allocations (100 blocks of 32 B)", test_sequential_stress);
    register_test("Alternating small and large allocations",    test_alternating_sizes);
    register_test("Very large allocation (4 MB)",               test_very_large_alloc);
    // Final tests (9-20)
    register_test("Basic free does not crash",                   test_basic_free);
    register_test("Freed block is reused",                       test_free_and_reuse);
    register_test("Free N then reallocate N",                    test_multiple_free_realloc);
    register_test("Backward coalescing",                        test_coalesce_backward);
    register_test("Forward coalescing",                          test_coalesce_forward);
    register_test("Bidirectional coalescing",                    test_coalesce_both);
    register_test("free(nullptr) is a no-op",                    test_free_nullptr);
    register_test("Alternating malloc/free (50 iterations)",    test_alloc_free_alternating);
    register_test("Interleaved alloc/free with integrity",      test_interleaved_pattern);
    register_test("Binary-tree allocation pattern",             test_binary_tree_pattern);
    register_test("Heap utilization >= 60%",                    test_utilization);
    register_test("Throughput >= 1000 Kops/sec",                test_throughput);
}

int main(int argc, char *argv[]) {
    register_all();

    // ── Single-test mode (used by GitHub Classroom autograder) ──────────────
    // Each autograder invocation is a fresh process, so no reset_allocator()
    // is needed here. State never bleeds between tests in the autograder.
    if (argc == 2) {
        int n = std::stoi(argv[1]);
        if (n < 1 || n > static_cast<int>(g_tests.size())) {
            std::cerr << "Test number out of range (1-" << g_tests.size() << ")\n";
            return 2;
        }
        mem_init();
        if (mm_init() != 0) {
            std::cerr << "FAIL: mm_init() returned non-zero\n";
            return 1;
        }
        auto &[tname, fn] = g_tests[n - 1];
        TestResult r = fn();
        if (r.passed) {
            std::cout << "PASS: " << tname;
            if (!r.extra.empty()) std::cout << "  (" << r.extra << ")";
            std::cout << "\n";
            mem_deinit();
            return 0;
        } else {
            std::cout << "FAIL: " << tname << "\n";
            std::cout << "  Hint: " << r.failure_msg;
            if (!r.extra.empty()) std::cout << "  (" << r.extra << ")";
            std::cout << "\n";
            mem_deinit();
            return 1;
        }
    }

    // ── Full-suite mode ──────────────────────────────────────────────────────
    std::cout << "============================================\n";
    std::cout << "  FINAL TEST SUITE\n";
    std::cout << "  malloc + free + coalescing + performance\n";
    std::cout << "============================================\n";

    int passed = 0;
    int total  = static_cast<int>(g_tests.size());

    auto print_section = [](const std::string &title) {
        std::cout << "\n  --- " << title << " ---\n";
    };

    print_section("Checkpoint Tests (1-8)");

    for (int i = 0; i < total; ++i) {
        if (i == 8)  print_section("Free & Coalescing Tests (9-18)");
        if (i == 18) print_section("Performance Tests (19-20)");

        if (!reset_allocator()) {
            std::cout << "  [" << std::setw(2) << (i + 1) << "] "
                      << g_tests[i].first << "\n"
                      << "       FAIL: mm_init() returned non-zero\n";
            continue;
        }

        std::cout << "  [" << std::setw(2) << (i + 1) << "] "
                  << std::left << std::setw(48) << g_tests[i].first;

        TestResult r = g_tests[i].second();
        if (r.passed) {
            std::cout << "  PASS";
            if (!r.extra.empty()) std::cout << "  (" << r.extra << ")";
            std::cout << "\n";
            ++passed;
        } else {
            std::cout << "  FAIL";
            if (!r.extra.empty()) std::cout << "  (" << r.extra << ")";
            std::cout << "\n";
            std::cout << "       Hint: " << r.failure_msg << "\n";
        }
    }

    std::cout << "\n============================================\n";
    std::cout << "  Result: " << passed << "/" << total << " tests passed\n";
    std::cout << "============================================\n";

    if (passed == total) {
        std::cout << "\nAll tests passed — excellent work!\n";
    } else {
        if (passed >= 8)
            std::cout << "\nCheckpoint tests pass but free/coalescing needs work.\n";
        else
            std::cout << "\nFocus on malloc first, then tackle free and coalescing.\n";
        std::cout << "Run  ./test_final <N>  to isolate a single failing test.\n";
    }

    mem_deinit();
    return (passed == total) ? 0 : 1;
}
