# MoVMM-Engine: Architecture

## Component Diagram

```text
+----------------------+       JSON       +----------------------+
|   Browser (HTML/JS)  | <------------> |  FastAPI Backend     |
|  (Tailwind CSS)      |  REST API + WS  |  (Python + Uvicorn) |
+----------------------+                +----------------------+
        ^                               |  ↑     ↑       ↑
        |                               |     |       |
        |   WebSocket / HTTP POST      |     |       |
        |------------------------------>|     |   JSON  |
        |   (configuration, workloads) |     |  exchange|
        | <-----------------------------|     |   (state)|
        |                               |     ↑     ↑
        |                               |   |   |     |
        |                               |   |   |     |
        |                               |   |   v     v
        |                               |   C++   C++ Subprocess
        |                               |   Engine   (pybind11 or
        |                               |   (C++17)   persistent JSON)
        |                               +-----------+
        |                                   |
        |                                   |  File-based
        |                                   |  handoff
        |                                   v
        |                            +----------------------+
        |                            |  swap.bin / logs     |
        |                            |  (binary/JSON files) |
        |                            +----------------------+
```

## Responsibilities

### C++ MoVMM-Engine
- Single source of truth for all simulated memory state
- Implement all memory-management logic:
  - User-space heap manager (my_malloc/my_free, free-list management, First-Fit/Best-Fit, block splitting, coalescing, fragmentation tracking)
  - MMU and TLB (page-table structures, 16-bit virtual address decomposition, virtual-to-physical translation, TLB lookup/updates, hit/miss tracking, address permissions/validation)
  - Demand paging and replacement (page-fault handling, physical-frame management, FIFO/LRU replacement, reference/dirty bits, fault/replacement statistics)
  - Swap file subsystem (persistent binary swap.bin, page-in/page-out, swap-space indexing, dirty-page writeback, disk I/O statistics)
- Expose C++ interfaces via headers for each module
- Compile to executable with CMake + MinGW-w64 g++ on Windows 11

### FastAPI Backend
- Serve the frontend directly (HTML files from designated directory)
- Accept configuration and workload commands via HTTP REST API
- Facilitate communication between browser and C++ engine
- Manage state snapshots and event reporting
- Run on Python + Uvicorn on Windows 11

### Browser Frontend (HTML + Tailwind CSS + Vanilla JS)
- User interface for machine configuration
- Memory map visualization
- Process manager interface
- Heap allocator explorer
- MMU & TLB explorer
- Paging simulator visualizer
- Swap manager visualizer
- Performance analytics dashboard
- Execution history panel
- Never independently fabricate memory operations or maintain competing simulation

## Communication Flow

### 1. Initial Setup
1. User opens browser to `http://localhost:8000`
2. Frontend loads configuration UI
3. Backend initializes C++ engine subprocess (or pybind11 context)
4. Engine initializes simulated state (RAM, page tables, TLB, heap, swap)
5. Engine state exported to frontend as initial JSON snapshot

### 2. Runtime Interaction
1. User modifies configuration (RAM size, page size, TLB capacity, swap capacity)
2. Frontend sends HTTP POST to `/configure` with new parameters
3. Backend forwards configuration to C++ engine
4. Engine reinitializes with new parameters
5. Frontend updates UI to reflect new state

### 3. Workload Execution
1. User issues memory allocation/deallocation requests via UI
2. Frontend sends HTTP POST to `/allocate` or `/deallocate`
3. Backend forwards to C++ engine
4. Engine performs operation, returns result (success/failure, fragmentation stats, new block addresses)
5. Backend returns result to frontend
6. UI updates: memory map, heap state, TLB contents, page tables

### 4. Memory Access
1. User issues virtual memory access (address translation)
2. Frontend sends HTTP POST to `/access` with 16-bit virtual address
3. C++ engine performs translation:
   - Decompose 16-bit VA into page number + offset
   - Look up page table entry
   - Check TLB (hit → return physical address; miss → page table walk)
   - Handle page fault if page not resident
   - If page fault: FIFO/LRU replacement if physical memory full
   - If swap involved: page-out dirty page to swap.bin, page-in from swap.bin
4. Engine returns physical address, hit/miss status, page fault info, any swap operation
5. Frontend updates: TLB hit/miss counter, page table entry, memory map

### 5. State Snapshots & Event Reporting
1. Periodic or on-demand state export from C++ engine
2. Backend collects snapshots and makes available via API
3. Frontend visualizes: memory map, page tables, TLB contents, heap allocation, swap usage
4. Performance metrics collected and displayed: page fault rate, TLB hit rate, swap operations, allocation success rate

## Interfaces

### C++ Header Interfaces (planned structure)

#### heap_allocator.h
- `void* my_malloc(size_t size)` — allocate block from custom heap
- `void my_free(void* ptr)` — free block, coalesce adjacent free blocks
- `struct Block { void* addr; size_t size; bool free; }` — free-list block structure
- `enum AllocResult { ALLOC_OK, ALLOC_OUT_OF_MEMORY, ALLOC_INVALID_SIZE }` — allocation results
- `FragmentationStats get_fragmentation_stats()` — internal fragmentation tracking

#### mmu.h
- `struct VirtualAddress16 { uint16_t va; uint16_t page_number; uint16_t offset; }` — 16-bit VA decomposition
- `struct PageTableEntry { bool valid; uint16_t physical_frame; bool read_write; bool present; }` — PTE structure
- `struct TranslationResult { uint16_t physical_address; bool tlb_hit; bool page_fault; }` — translation outcome
- `TranslationResult translate(uint16_t virtual_address)` — translate VA to PA
- `void update_tlb(uint16_t physical_frame, bool accessed_dirty)` — TLB update

#### paging.h
- `struct FrameInfo { bool resident; bool dirty; uint16_t swap_slot; }` — physical frame state
- `enum ReplacementAlgorithm { FIFO, LRU }` — replacement algorithms
- `struct PageFaultInfo { bool occurred; uint16_t victim_frame; uint16_t swapped_slot; bool dirty_writeback; }` — fault details
- `PageFaultInfo handle_page_fault(uint16_t virtual_page, ReplacementAlgorithm algo)` — fault handling
- `Metrics get_performance_metrics()` — fault/miss/replacement statistics

#### swap.h
- `struct SwapStats { size_t total_swap; size_t used_swap; size_t page_outs; size_t page_ins; }` — swap statistics
- `SwapResult initialize_swap(const char* path, size_t capacity_pages)` — swap initialization
- `SwapResult page_out(uint16_t frame, uint16_t slot)` — write page to swap.bin
- `SwapResult page_in(uint16_t frame, uint16_t slot)` — read page from swap.bin
- `SwapStats get_stats()` — current swap usage

## Communication Mechanisms

### JSON-Based API (FastAPI → C++ Subprocess)
Engine control and state queries via JSON over subprocess stdin/stdout or named pipes:
- `{"type": "configure", "ram_pages": N, "page_size": S, "tlb_entries": E, "swap_capacity": M}`
- `{"type": "allocate", "size": S}` → `{"type": "allocation_result", "success": B, "address": A, "block_id": ID, "fragmentation": F}`
- `{"type": "deallocate", "address": A}` → `{"type": "deallocation_result", "success": B}`
- `{"type": "translate", "virtual_address": VA}` → `{"type": "translation_result", "physical_address": PA, "tlb_hit": B, "page_fault": B, "algorithm": A}`
- `{"type": "access", "virtual_address": VA}` → comprehensive access result
- `{"type": "get_state"}` → full simulated state snapshot
- `{"type": "get_metrics"}` → performance metrics

### File-Based Handoff (swap.bin)
- Persistent binary file for swap storage
- Page-out/in operations read/write fixed-size pages
- Used when physical memory is full and replacement is needed

## Proposed Directory Structure

```
movmm/
├── .gitignore             # Windows build outputs, Python envs, swap.bin, logs
├── docs/                  # Additional documentation
│   ├── project/              # Project context, architecture, status, and instructions
│   └── ...
├── cpp/                   # C++ engine source
│   ├── CMakeLists.txt
│   ├── engine.cpp         # Main engine entry point
│   ├── heap_allocator.h   # User-space heap manager
│   ├── heap_allocator.cpp
│   ├── mmu.h              # MMU and TLB
│   ├── mmu.cpp
│   ├── paging.h           # Demand paging and replacement
│   ├── paging.cpp
│   ├── swap.h             # Swap file subsystem
│   ├── swap.cpp
│   ├── types.h            # Shared types (VirtualAddress16, PageTableEntry, etc.)
│   └── tests/             # Unit tests for each module
├── python/                # FastAPI backend
│   ├── main.py            # FastAPI app, frontend serving, API endpoints
│   ├── engine_interface.py # C++ subprocess management or pybind11 bindings
│   ├── models.py          # Pydantic models for API requests/responses
│   ├── config.py          # Configuration defaults
│   └── requirements.txt
├── frontend/              # Browser frontend
│   ├── index.html         # Main page
│   ├── styles.css         # Custom Tailwind overrides
│   ├── app.js             # Main application logic
│   ├── config-ui.html/htm # Configuration dialog
│   ├── memory-map.html/htm # Memory map visualization
│   ├── process-manager.html/htm # Process manager
│   ├── heap-allocator.html/htm # Heap allocator explorer
│   ├── mmu-tlb.html/htm   # MMU & TLB explorer
│   ├── paging-simulator.html/htm # Paging simulator
│   ├── swap-manager.html/htm # Swap manager
│   ├── performance-analytics.html/htm # Analytics dashboard
│   ├── execution-history.html/htm # Execution history
│   └── assets/            # SVGs, icons, etc.
└── tests/                 # Integration tests
```

## Integration Strategy Evaluation

### pybind11 vs Persistent JSON Subprocess

**pybind11 (C++ bindings to Python)**:
- Pros: Direct C++ → Python function calls, zero-copy data transfer, natural C++ class exposure
- Cons: Requires pybind11 installation, GIL considerations, Python initialization overhead, harder to isolate engine state, Windows pybind11 binary availability
- Best when: Tight integration needed, frequent communication, low-latency required

**Persistent JSON Subprocess**:
- Pros: Language isolation, easy inter-process communication, straightforward Windows compatibility, can use same C++ binary without pybind11, natural fit for FastAPI → subprocess model
- Cons: Serialization overhead, slightly higher latency, needs well-defined message protocol
- Best when: Windows-first deployment, simpler integration path, clear protocol boundaries

**Decision**: Start with **persistent JSON subprocess** approach for Phase 2-3. It provides cleaner Windows compatibility, avoids pybind11 complexities, and the JSON protocol naturally fits the FastAPI architecture. Can transition to pybind11 later if performance measurements show the need.

## State Snapshots

Engine exports state on request (via `/get_state` API endpoint):
```json
{
  "ram": { ... },              // Physical memory state
  "page_tables": [...],        // Page table entries for all processes
  "tlb": [...],                // TLB contents (tag, physical_frame, valid)
  "heap": { ... },             // Heap allocation state
  "processes": [...],          // Process information
  "swap": { ... },             // Swap file state
  "metrics": { ... }           // Performance metrics (page faults, TLB hits, etc.)
}
```

## Error Handling

### C++ Engine Errors
- Allocation failures (out of memory, invalid size)
- Page table errors (invalid page number, protection violation)
- TLB miss handling with page fault
- Swap I/O errors (swap.bin not found, capacity exceeded)

### Backend Error Translation
- Map C++ error codes to HTTP status codes
- Return structured JSON error responses
- Log errors to console/file for debugging
- Never expose raw C++ internals to frontend

### Frontend Error Handling
- Display user-friendly error messages
- Graceful degradation when engine subprocess fails
- Retry logic for transient failures