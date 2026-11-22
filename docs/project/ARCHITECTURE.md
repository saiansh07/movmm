# MoVMM-Engine: Architecture

## Component diagram

```text
Browser (HTML, plain CSS, vanilla JavaScript)
    <-> HTTP REST and WebSocket
FastAPI backend (Python 3.14, Uvicorn)
    <-> JSON Lines over stdin/stdout
Persistent C++17 engine subprocess
    <-> fixed-size page reads and writes
swap.bin
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

### Browser Frontend (HTML + plain CSS + vanilla JavaScript)
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

## Communication flow

1. FastAPI starts the engine subprocess during application startup. It serves the dashboard at `/` and static assets under `/frontend/`.
2. The browser sends configuration to `POST /api/v1/config`. FastAPI forwards `initialize` to the CLI, which rebuilds the simulated machine.
3. The browser sends heap requests to `/api/v1/memory/allocate` and `/api/v1/memory/free`. Translation and byte access use `/api/v1/memory/translate`, `/api/v1/memory/read`, and `/api/v1/memory/write`. Page permissions use `/api/v1/memory/permissions`.
4. The engine decomposes virtual addresses into a page number and offset, looks up cached translations in the TLB, and handles demand faults. With full RAM, the active FIFO or LRU policy selects a victim. The engine writes dirty pages to `swap.bin` and restores backed pages on demand.
5. FastAPI requests `get_snapshot` for `GET /api/v1/state` and for the stream at `/ws/simulation`. The browser renders the snapshot and uses its counters to calculate rates. The engine supplies both eviction orders and machine-wide swap ownership.

## Engine interfaces

- [Engine controller](../../cpp/engine.h) coordinates state, policy, processes, and snapshots.
- [Heap allocator](../../cpp/heap_allocator.h) manages allocation, block splitting, coalescing, and fragmentation.
- [MMU](../../cpp/mmu.h) handles page-table translation and TLB lookup.
- [Paging manager](../../cpp/paging.h) manages frame residency and victim selection.
- [Swap manager](../../cpp/swap.h) allocates and releases disk slots and transfers page data.

The [backend API](../BACKEND_API.md) and [CLI protocol](../ENGINE_API.md) describe the request envelopes and response fields. The swap file is recreated on initialization or reset.

## Directory layout

```text
movmm/
  AGENTS.md
  CLAUDE.md
  run.ps1
  cpp/
    CMakeLists.txt
    engine.cpp, engine.h, engine_cli.cpp
    heap_allocator.cpp, heap_allocator.h
    mmu.cpp, mmu.h
    paging.cpp, paging.h
    swap.cpp, swap.h
    events.cpp, events.h, types.h
    json/
      json.hpp, LICENSE.MIT
    python/
      __init__.py
      config.py
      main.py
      requirements.txt
    tests/
      test_engine.cpp
      check_http_ws.py
  frontend/
    index.html
    styles.css
    app.js
  docs/
    README.md
    WINDOWS_SETUP.md
    BACKEND_API.md
    ENGINE_API.md
    project/
      ARCHITECTURE.md
      PROJECT_CONTEXT.md
      DEVELOPMENT_STATUS.md
      REPORT.md
      FINAL_REPORT.md
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

**Decision**: Use a **persistent JSON subprocess**. It provides cleaner Windows compatibility, avoids pybind11 complexities, and the JSON protocol naturally fits the FastAPI architecture. Can transition to pybind11 later if performance measurements show the need.

## State snapshots

The snapshot contains `machine`, `heap`, `mmu`, `paging`, `swap`, `processes`, `frames`, and `events`. FastAPI returns these fields alongside `success` on the REST endpoint and inside `data` in WebSocket messages. See the [snapshot field reference](../ENGINE_API.md#engine-state-snapshots).

## Error Handling

### C++ Engine Errors
- Allocation failures (out of memory, invalid size)
- Page table errors (invalid page number, protection violation)
- TLB miss handling with page fault
- Swap I/O errors (swap.bin not found, capacity exceeded)

### Backend Error Translation
- Translate engine failures into HTTP responses. See the backend API for route-specific behavior.
- Return structured JSON error responses
- Log errors to console/file for debugging

### Frontend Error Handling
- Display user-friendly error messages
- Graceful degradation when engine subprocess fails
- Retry logic for transient failures