# MoVMM-Engine on Windows 11

## Start the dashboard

Install Python 3.14 and a native MinGW-w64 C++ compiler. Put the compiler on PATH or install the MSYS2 UCRT64 toolchain at the default location. From the repository root, run:

```powershell
.\run.ps1
```

If PowerShell blocks local scripts, start this launcher with a process-scoped policy:

```powershell
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File .\run.ps1
```

The launcher creates or reuses the Python environment, installs backend dependencies, builds the C++ engine when needed, and starts FastAPI. Open `http://127.0.0.1:8000`. Stop an existing launcher with Ctrl+C before restarting it with code changes.

- **Run demo** or **Step** demonstrates TLB hits, page faults, dirty writeback, and restoration from swap.
- **Compare FIFO and LRU** runs the same reference sequence under both policies. It resets the machine twice and leaves the LRU run visible.
- **Make read-only** applies to the page containing the entered virtual address. The execution history records a protection fault if you then try to write to it.
- API documentation is available at `http://127.0.0.1:8000/docs`.

## Build and test with CMake

With CMake and MinGW-w64 on PATH, run:

```powershell
cmake -S cpp -B cpp/build/windows -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build cpp/build/windows --parallel
ctest --test-dir cpp/build/windows --output-on-failure
```

The native tests cover the heap, address translation, TLB, paging, replacement, swap data integrity, page permissions, and bounded event history. The repository includes the MIT-licensed nlohmann/json header needed to compile the CLI.

## HTTP and WebSocket integration check

After the launcher installs dependencies and the CMake build completes, run from the repository root:

```powershell
& "$env:USERPROFILE\movmm_venv\Scripts\python.exe" cpp/tests/check_http_ws.py cpp/build/windows/engine_cli.exe
```

This starts an isolated Uvicorn instance with a temporary swap file.
