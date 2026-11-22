# MoVMM-Engine: Project Context

## Project Abstract
MoVMM-Engine is a modular virtual memory management framework that simulates important memory-management mechanisms found in modern operating systems. This is a user-space simulation of virtual memory management that accurately models relevant mechanisms for educational experimentation. The project is built as a C++ simulation engine extended with a browser-based interactive interface.

## Team
- **Member 1 — Anirudh Goel** (992401030184): User-Space Heap Manager
- **Member 2 — Saiansh Singh** (992401030185): MMU and TLB
- **Member 3 — Avish Mittal** (992401030190): Demand Paging and Replacement
- **Member 4 — Vinayak Arora** (992401030193): Swap File Subsystem

## Functional Requirements
- FR-1: Dynamic block allocation using custom my_malloc() with explicit free lists
- FR-2: Block splitting and coalescing (split oversized free blocks, merge adjacent free blocks on deallocation)
- FR-3: Virtual address translation (16-bit virtual address decomposition into page numbers and offsets, physical frame mapping)
- FR-4: TLB cache management (configurable fixed-size TLB, hit/miss tracking, updates on misses)
- FR-5: Page-fault handling (detect non-resident pages, coordinate page loading into physical memory)
- FR-6: Page replacement (FIFO and LRU when physical memory is full)
- FR-7: Binary swap subsystem using persistent swap.bin as simulated secondary storage
- FR-8: Page-in and page-out operations (disk-backed page transfers, dirty-page writeback)

## Non-Functional Requirements
- Near O(1) routine TLB lookup
- Internal tracking overhead below 10% of simulated RAM where applicable and measurable
- Modular architecture with clearly defined header interfaces
- Configurable page sizes from 256 bytes to 4 KB
- No unnecessary dependency on host OS kernel abstractions
- Correctness and testability take priority over superficial performance claims

## Technology Stack
- **Core**: C++17+, CMake
- **Backend**: Python, FastAPI, Pydantic, Uvicorn
- **Frontend**: HTML5, plain CSS, Vanilla JavaScript (native, no React/Vue/Angular/Next.js/Vite/TypeScript/Redux)
- **Platform**: Windows 11 native

## Application Objectives
The finished application functions as a miniature OS memory-management laboratory allowing:
1. Opening a local browser page
2. Configuring a simulated computer (RAM, page size, TLB capacity, swap capacity)
3. Creating simulated processes
4. Issuing memory allocation and deallocation requests
5. Executing virtual memory accesses
6. Observing physical RAM filling up
7. Inspecting page tables and TLB contents
8. Observing page faults
9. Watching FIFO or LRU select pages for eviction
10. Observing page-in and page-out operations
11. Comparing algorithms using actual engine measurements

## Required Application Sections
- Overview
- Machine Configuration
- Memory Map
- Process Manager
- Heap Allocator
- MMU & TLB
- Paging Simulator
- Swap Manager
- Performance Analytics
- Execution History