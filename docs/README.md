# MoVMM-Engine

MoVMM-Engine is a Windows 11 virtual memory simulator for the Operating Systems Lab (24B11CS312) at Jaypee Institute of Information Technology, Noida. A C++17 engine owns the simulated state. FastAPI sends commands over JSON lines and streams engine snapshots to a vanilla JavaScript dashboard over WebSockets.

## Quick start

Install Python 3.14 and MinGW-w64. Add the compiler to PATH or use the default MSYS2 UCRT64 installation. From the repository root, run:

```powershell
.\run.ps1
```

Open [the dashboard](http://127.0.0.1:8000/). The launcher installs Python dependencies, builds the engine when needed, and starts FastAPI. See the [Windows setup guide](WINDOWS_SETUP.md) if PowerShell blocks the script.

## Try the simulator

- Select **Run demo** to run the guided workload or **Step** to execute one memory access at a time. Both use the applied machine settings by default. Choose **Two-frame paging preset** to load the original demonstration. The workload panel lists events that the selected settings cannot produce.
- Inspect the physical and virtual memory maps, page tables, FIFO/LRU eviction order, and the PID and page backing each swap slot.
- Select **Compare FIFO and LRU** to run the same workload under both policies. Each run resets the simulated machine.
- Create a process to allocate and free heap blocks, or mark a page read-only and observe a rejected write in Execution History.

## Build and test

With CMake and MinGW-w64 on PATH, run:

```powershell
cmake -S cpp -B cpp/build/windows -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build cpp/build/windows --parallel
ctest --test-dir cpp/build/windows --output-on-failure
```

After the launcher installs dependencies and the CMake build completes, run the isolated HTTP and WebSocket checks from the repository root using the launcher's virtual environment:

```powershell
& "$env:USERPROFILE\movmm_venv\Scripts\python.exe" cpp/tests/check_http_ws.py cpp/build/windows/engine_cli.exe
```

## Project documentation

- [Project context and requirements](project/PROJECT_CONTEXT.md)
- [Architecture](project/ARCHITECTURE.md)
- [Development status and known limits](project/DEVELOPMENT_STATUS.md)
- [Project instructions](../CLAUDE.md)
- [Backend API](BACKEND_API.md) and [engine protocol](ENGINE_API.md)
- [Agent instructions](../AGENTS.md)
