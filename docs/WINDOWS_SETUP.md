# MoVMM-Engine Phase 4 - Windows 11 Setup

## Overview
This document describes how to set up and run MoVMM-Engine Phase 4 on Windows 11. The setup is designed to be self-contained with no requirement for Linux, WSL, Docker, or Bash.

## Prerequisites

### Hardware
- Windows 11 Home Single Language (or Professional)
- Minimum 4GB RAM (8GB recommended)
- .NET Framework not required

### Software
- **Python 3.8+** from https://python.org
- **Visual C++ Redistributable** (may be needed for some packages)
- **Git for Windows** (optional, for repository operations)

### Development Tools (if modifying source)
- **MSYS2 g++ 15.2.0** from https://msys2.org/ (or Git for Windows build tools)
- **CMake** (optional, for build system)

## Step-by-Step Setup

### 1. Install Python
1. Download Python 3.8+ from https://python.org
2. During installation, check "Add Python to PATH"
3. Restart any open terminals

### 2. Install Git (optional)
1. Download Git for Windows from https://git-scm.com/
2. Run the installer with default options
3. Restart any open terminals

### 3. Set Up Python Virtual Environment
```powershell
# Navigate to project directory
cd C:\Users\sai\Documents\osproj

# Create virtual environment
python -m venv .movmm_venv

# Activate virtual environment
& .\.movmm_venv\Scripts\Activate.ps1

# Verify activation
python --version
```

### 3. Install Python Dependencies
```powershell
# Install from requirements.txt
python -m pip install --upgrade pip
python -m pip install -r cpp\python\requirements.txt
```

### 4. Compile C++ Engine (if needed)
The C++ engine CLI (`engine_cli.exe`) must be compiled before running the FastAPI backend.

```powershell
# Using MSYS2 g++
& "C:\msys64\ucrt64\bin\g++.exe" -std=c++17 -Icpp -Icpp/json cpp\engine_cli.cpp -o engine_cli.exe

# Or using Git for Windows g++
& "C:\Program Files\Git\bin\g++.exe" -std=c++17 -Icpp -Icpp/json cpp\engine_cli.cpp -o engine_cli.exe
```

### 5. Run the Application
```powershell
# Using the provided startup script
& .\run.ps1
```

Or manually:
```powershell
# Start the FastAPI server
python -m uvicorn.main:main --host 127.0.0.1 --port 8000 --app-path cpp\python\main.py
```

## Application URLs

Once running, access these URLs:

- **HTTP Application**: `http://127.0.0.1:8000`
- **API Documentation**: `http://127.0.0.1:8000/docs`
- **WebSocket Endpoint**: `ws://127.0.0.1:8000/ws/simulation`

## Troubleshooting

### Common Issues

1. **Python not found**
   - Ensure Python is installed and added to PATH
   - Restart the terminal after installation

2. **g++ not found**
   - Install MSYS2 from https://msys2.org/
   - Or install the Visual C++ Build Tools from Microsoft
   - Or pre-compile engine_cli.exe and place it in the project root

3. **Module not found**
   - Ensure the virtual environment is activated
   - Run `python -m pip install -r cpp\python\requirements.txt`

4. **Engine subprocess errors**
   - Ensure engine_cli.exe is compiled and in the project root
   - Check that stderr is not being confused with stdout
   - Verify the engine initialize command was received

5. **Port already in use**
   - Change the port in run.ps1 or specify a different port
   - `python -m uvicorn.main:main --port 8001 --app-path cpp\python\main.py`

## Directory Structure (Post-Setup)

```
C:\Users\sai\Documents\osproj\
├── run.ps1                  # Startup script
├── engine_cli.exe            # Compiled C++ engine CLI
├── cpp/
│   ├── heap_allocator.cpp   # Heap allocator implementation
│   ├── mmu.cpp              # MMU and TLB implementation
│   ├── paging.cpp           # Demand paging and replacement
│   ├── swap.cpp             # Binary swap file manager
│   ├── engine.cpp           # Unified engine controller
│   ├── events.cpp           # Event reporting system
│   ├── engine_cli.cpp       # JSON subprocess interface
│   ├── CMakeLists.txt       # Build configuration
│   └── python/
│       ├── __init__.py      # Package init
│       ├── config.py        # Engine process management
│       ├── main.py          # FastAPI application
│       └── requirements.txt # Python dependencies
├── docs/
│   ├── ENGINE_API.md        # Engine API protocol
│   ├── BACKEND_API.md       # Backend API documentation
│   └── WINDOWS_SETUP.md     # This document
├── .gitignore              # Git ignore rules
└── .claude/                # Claude Code configuration
```

## Development Workflow

### Modifying the C++ Engine
1. Edit source files in `cpp/`
2. Recompile: `g++ -std=c++17 -Icpp -Icpp/json cpp\engine_cli.cpp -o engine_cli.exe`
3. The FastAPI backend will use the new engine automatically on restart

### Modifying the Python Backend
1. Edit files in `cpp/python/`
2. Run `python -m pip install -r cpp\python\requirements.txt` to update dependencies
3. Restart the FastAPI server

### Adding New API Endpoints
1. Add Pydantic models to `cpp/python/main.py`
2. Implement the endpoint function
3. Add the route decorator `@app.get(...)` or `@app.post(...)`
4. Test with `pytest` or manual `curl` requests

## Phase Completion

Phase 4 is considered complete when:
- The C++ engine compiles successfully on Windows 11
- The FastAPI server starts without errors
- All REST endpoints return genuine engine results
- WebSocket connections function and report actual C++ state
- The application launches successfully via `run.ps1`
- Documentation accurately describes the implementation
- Changes are committed and pushed to `origin/main`