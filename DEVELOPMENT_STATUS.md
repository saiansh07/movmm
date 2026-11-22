# MoVMM-Engine development status

## Implemented

- Native C++17 engine with a process-owned heap, block splitting and coalescing, First-Fit and Best-Fit, and fragmentation metrics.
- 16-bit virtual addressing, configurable pages, page permissions, indexed TLB lookup, FIFO and LRU replacement, and binary swap with dirty-page writeback.
- FastAPI command bridge and WebSocket snapshots served to the dashboard.
- Dashboard for configuration, processes, heap blocks, physical frames, virtual pages, page tables, TLB, paging, swap, analytics, and execution history.
- Guided paging workload and a FIFO/LRU comparison driven by engine measurements.

## Windows verification

The native MinGW-w64 CMake build and CTest suite cover the engine subsystems and their interaction. The HTTP and WebSocket integration checks exercise live engine state, page protection, heap metrics, and a reference sequence with different FIFO and LRU results. See [Windows setup](docs/WINDOWS_SETUP.md) for commands.

## Scope and remaining verification

- A Windows MinGW-w64 lower-bound measurement with 16 resident frames, one process, 4 TLB entries, and 64 swap slots counted 1,128 bytes of tracking data. That was 27.54% of RAM with 256-byte pages and 1.72% with 4 KiB pages. It counts frame records, page-table entries, TLB entries, FIFO frame indices, and the swap bitmap. Container nodes, spare capacity, allocator bookkeeping, event history, and transient snapshots are outside the measurement, so the 4 KiB result cannot establish compliance with the 10% target.
- Swap is disk-backed during a simulation. Initialization and reset recreate the swap file. The dashboard does not restore a previous simulation after a restart.
- The dashboard uses HTML, vanilla JavaScript, and plain CSS. The eviction order, swap-slot ownership, policy comparison, and write rejection were visually verified in Edge against the native engine and FastAPI.
