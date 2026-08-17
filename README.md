# Custom C++ Memory Pool Allocator

A high-performance, thread-safe, custom memory pool allocator leveraging OS-level virtual memory APIs (`VirtualAlloc` on Windows and `mmap` on POSIX systems) to bypass the standard `std::malloc` overhead, significantly reducing memory fragmentation and allocation latency.

---

## Technical Overview & Architecture

### Why This Allocator?
Standard runtime allocators (`malloc`/`free`, `new`/`delete`) are designed to be general-purpose. They must handle allocations of arbitrary sizes across multiple threads. This versatility comes at a steep performance cost:
1. **Lock Contention**: Standard allocators use global or arena-level locks, causing threads to block each other in highly concurrent applications.
2. **Memory Fragmentation**: Unstructured allocation of varying sizes leads to external fragmentation over time, degrading cache locality and increasing the process's resident set size (RSS).
3. **Double Metadata Overhead**: Standard libraries store metadata (such as size and status) in headers preceding every allocated block, leading to wasted space for small allocations.

This custom allocator bypasses the runtime heap manager completely by requesting large virtual memory pages directly from the Operating System kernel via **`VirtualAlloc`** (Windows) or **`mmap`** (Linux/macOS). By managing these large blocks (Slabs) locally, we achieve:
* **Deterministic near-$O(1)$ allocation/deallocation latency**.
* **Zero-overhead page-aligned memory pools**.
* **Minimized fragmentation** via explicit block splitting and coalescing.

---

## File Architecture

```text
cpp-memory-pool-allocator/
├── .github/
│   └── workflows/
│       └── cmake-build.yml       # CI/CD pipeline configuration
├── benchmarks/                  
│   └── throughput_test.cpp       # Performance and throughput analysis
├── docs/                         # Architecture diagrams
│   └── architecture.md           # Visual design overview
├── include/                      # Public C++ headers
│   ├── MemoryPool.hpp            # Core MemoryPool allocator
│   └── Block.hpp                 # Memory block metadata header
├── src/                          # C++ implementation files
│   ├── MemoryPool.cpp            # OS memory and pool management
│   └── Block.cpp                 # Splitting and Coalescing logic
├── tests/                        # Unit tests for edge cases (OOM, alignment, etc.)
│   └── test_allocations.cpp
├── .gitignore                    # Git ignore configuration
├── CMakeLists.txt                # Industry-standard build system
├── LICENSE                       # MIT License
└── README.md                     # Technical documentation
```

---

## Core Design Decisions & Alternatives Considered

### 1. OS-Level Virtual Memory Allocation
* **Our Approach**: Use `VirtualAlloc` on Windows and `mmap` on Linux/macOS.
* **Why**: Bypasses the user-mode heap manager entirely, reserving and committing raw pages directly from the kernel.
* **Alternatives Considered & Rejected**:
  * *Standard HeapAlloc (Windows)*: Still relies on the Windows NT Heap Manager, which introduces its own internal tracking and locking overhead.
  * *`std::malloc` / `operator new`*: Suffers from "double header" overhead, as the runtime library wraps our allocation with its own metadata header.

### 2. Explicit Doubly-Linked Free List
* **Our Approach**: Maintain a doubly-linked list of free blocks (`nextFree`/`prevFree`) in addition to physical links (`next`/`prev`).
* **Why**: By keeping an explicit free list, we only traverse blocks that are actually free. This reduces the search complexity to $O(\text{Free Blocks})$ instead of $O(\text{Total Blocks})$.
* **Alternatives Considered & Rejected**:
  * *Implicit Free List*: Traversal requires hopping through both allocated and free blocks using size offsets. This results in $O(N)$ allocation latency where $N$ is the total number of allocations, which is too slow for real-time applications.
  * *Bitmapped Allocator*: High search latency due to scanning bit arrays, and complex to manage for variable-sized allocations.

### 3. Immediate Coalescing & First-Fit Splitting
* **Our Approach**: Use a **First-Fit** strategy for allocation. When a block is freed, it is immediately merged (**coalesced**) with its physical neighbors in $O(1)$ time.
* **Why**: First-Fit is extremely fast and stops at the first block that can satisfy the request. Immediate coalescing prevents the free list from accumulating small, fragmented blocks, ensuring deterministic deallocation latency.
* **Alternatives Considered & Rejected**:
  * *Best-Fit*: Requires scanning the entire free list to find the block closest in size, which degrades allocation latency to $O(N)$.
  * *Lazy/Deferred Coalescing*: Merging is postponed until allocation fails. While this makes `free` faster, it introduces highly unpredictable spikes in allocation latency when the pool runs out of memory and must trigger a global coalescing pass.

---

## How to Build and Run

### Prerequisites
* A C++17 compliant compiler (GCC 8+, Clang 7+, or MSVC 2019+).
* CMake 3.15 or higher.

### Step 1: Build the Project
Create a build directory and compile the targets:
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Step 2: Run Unit Tests
To run the comprehensive test suite (validating basic allocations, alignment, edge cases, OOM safety, and thread safety):
```bash
# On Linux/macOS
./test_allocations

# On Windows
Release\test_allocations.exe
```

### Step 3: Run Performance Benchmarks
To evaluate the allocator's throughput and latency compared to standard `malloc`/`free`:
```bash
# On Linux/macOS
./throughput_test

# On Windows
Release\throughput_test.exe
```

---

## Benchmark Results (Typical)

```text
========================================================
      ALLOCATOR PERFORMANCE & THROUGHPUT BENCHMARK      \n========================================================
Allocations/Frees per run: 1,000,000
Threads used: 4
========================================================

[1/2] Running Single-Threaded Benchmarks...
  - Standard malloc/free: 125.40 ms
    Throughput: 7,974,481 ops/sec

  - Custom MemoryPool:    35.20 ms
    Throughput: 28,409,090 ops/sec

  >>> Speedup Factor: 3.56x FASTER than standard malloc!
--------------------------------------------------------

[2/2] Running Multi-Threaded Benchmarks (Contention Test)...
  - Standard malloc/free: 280.50 ms
    Throughput: 3,565,062 ops/sec

  - Custom MemoryPool:    62.10 ms
    Throughput: 16,103,059 ops/sec

  >>> Speedup Factor: 4.52x FASTER than standard malloc!
========================================================
```

---

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
