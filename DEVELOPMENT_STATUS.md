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

- The tracking-overhead target in PROJECT_CONTEXT.md has no measured result. Engine metadata overhead depends on the simulated RAM size and workload.
- Swap is disk-backed during a simulation. Initialization and reset recreate the swap file. The dashboard does not restore a previous simulation after a restart.
- The dashboard uses HTML, vanilla JavaScript, and plain CSS. Browser visual verification remains separate from the HTTP and WebSocket checks.
