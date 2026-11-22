# MoVMM-Engine Phase 4 - Backend API Documentation

## Overview
This document describes the FastAPI backend implementation for MoVMM-Engine Phase 4. The backend communicates with a persistent C++ engine subprocess via JSON Lines protocol and provides RESTful and WebSocket endpoints for client interaction.

## Architecture

### Component Diagram
```
+----------------------+       JSON       +----------------------+
|  FastAPI Backend      | <------->      |  C++ Engine Subprocess|
|  (Python + Uvicorn)   |  REST API + WS  |  (C++17, MinGW-w64)  |
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
        |                               |   Engine   (persistent)
        +-------------------------------+-----------+
```

## REST API v1 Endpoints

### Base Path: `/api/v1`

### Health Endpoint
- **GET** `/api/v1/health`
- **Response**: `HealthResponse` (`status`: "ok", `engine_initialized`: bool, `version`: "0.1.0")
- **Purpose**: Verify backend and engine status

### Configuration Endpoint
- **POST** `/api/v1/config`
- **Request Body**: `{"ram_size_bytes": int, "page_size": int, "tlb_capacity": int, "swap_capacity_pages": int, "fit_policy": "first_fit"|"best_fit", "replacement_algo": "fifo"|"lru"}`
- **Response**: `ConfigResponse` with the configured values
- **Purpose**: Initialize or reconfigure the C++ engine

### Reset Endpoint
- **POST** `/api/v1/reset`
- **Response**: `{"status": "reset", "success": true}`
- **Purpose**: Reset the engine to initial state

### Process Management Endpoints

- **POST** `/api/v1/processes`
  - **Request**: `CreateProcessRequest` (`pid`: int, default 1)
  - **Response**: `{"pid": int, "success": bool}`
  - **Purpose**: Create a new simulated process

- **POST** `/api/v1/processes/{pid}`
  - **Path Parameter**: `pid` (int)
  - **Response**: `{"pid": int, "success": bool}`
  - **Purpose**: Terminate a simulated process and release resources

### Memory Operations Endpoints

- **POST** `/api/v1/memory/allocate`
  - **Request**: `AllocateRequest` (`pid`: int, `size`: int)
  - **Response**: `{"success": bool, "message": string}`
  - **Purpose**: Allocate memory blocks from the custom heap

- **POST** `/api/v1/memory/free`
  - **Request**: `FreeRequest` with `pid` and `address`. Use the byte offset of a block owned by the process from `heap.blocks` in the current state snapshot.
  - **Response**: `{"success": bool, "message": string}`
  - **Purpose**: Free memory blocks and coalesce free lists

- **POST** `/api/v1/memory/translate`
  - **Request**: `TranslateRequest` (`pid`: int, `virtual_address`: int, 16-bit VA)
  - **Response**: `{"success": true, "physical_address": int, "tlb_hit": bool, "page_fault": bool, "protection_fault": bool}`
  - **Purpose**: Translate virtual addresses through MMU and TLB

- **POST** `/api/v1/memory/read`
  - **Request**: `ReadMemoryRequest` (`pid`: int, `virtual_address`: int, `size`: int)
  - **Response**: `{"success": bool, "bytes_read": int, "bytes": [...]}`
  - **Purpose**: Read from a single virtual page with page-fault handling

- **POST** `/api/v1/memory/write`
  - **Request**: `WriteMemoryRequest` (`pid`: int, `virtual_address`: int, `data`: byte array)
  - **Response**: `{"success": bool, "message": string}`
  - **Purpose**: Write to a single virtual page with page-fault handling

### Page Permissions

- **POST** `/api/v1/memory/permissions`
  - **Request**: `{"pid": 1, "virtual_address": 0, "writable": false}`
  - Applies to the entire virtual page. Reads can fault the page into RAM. Writes to a read-only page are rejected and recorded in execution history.

### Policy Management Endpoint

- **POST** `/api/v1/policies`
  - **Request**: `SetPolicyRequest` (`policy_type`: "allocation"|"replacement", `policy`: "first_fit"|"best_fit"|"lru"|"fifo")
  - **Response**: `{"success": bool, "message": string}`
  - **Purpose**: Change allocation (First-Fit/Best-Fit) or replacement (FIFO/LRU) policies

### State and Statistics Endpoints

- **GET** `/api/v1/state`
  - **Response**: Engine state snapshot with machine, heap, MMU and valid TLB entries, paging, swap, physical frames, process page tables, and recent events
  - **Purpose**: Get complete engine state

- **GET** `/api/v1/statistics`
  - **Response**: `{"total_allocations": int, "total_frees": int, "active_allocations": int, "tlb_hit_rate": float, "page_faults": int}`
  - **Purpose**: Get engine performance statistics

### Event Endpoint

- **GET** `/api/v1/events`
  - **Response**: `{"success": true, "events": [...]}` (array of recent events)
  - **Purpose**: Get the event history for UI visualization

### WebSocket Endpoint

- **WS** `/ws/simulation`
- **Purpose**: Real-time state updates and event broadcasting
- **Features**:
  - Clients receive `{"type":"initial_state","data":{...}}` on connection
  - Clients receive `{"type":"state_update","data":{...}}` when a snapshot changes. Recent engine events are included in `data.events`.
  - Supports clean disconnection
  - Reports actual C++ engine state
  - The backend polls the engine every 250 ms per connected client and skips identical snapshots.

## Python Backend Structure

```
cpp/python/
├── __init__.py         # Package init
├── config.py           # Engine process management
├── main.py             # FastAPI application
├── requirements.txt    # Python dependencies
```

### FastAPI Application Features
- Pydantic request/response validation
- Consistent JSON responses with meaningful HTTP status codes
- Error handling for validation failures, engine errors, unavailable processes
- WebSocket support for real-time updates
- Static file serving for future frontend
- `/docs` interface automatically generated by FastAPI

### Error Handling
- **422 Unprocessable Entity**: Pydantic validation failures
- **500 Internal Server Engine errors**
- **503 Service Unavailable**: Engine subprocess not running
- **All responses** include structured error details

### Integration Notes
- The C++ engine is the single source of truth
- The backend does not fabricate memory operations or maintain competing state
- All memory management, statistics, and event reporting originate from the C++ engine
- The persistent subprocess model ensures platform independence (Windows 11)
- Proper isolation between client interactions and engine state

## Windows 11 Execution

### run.ps1 Startup Script
The `run.ps1` script orchestrates the complete setup:

1. Verifies required tools (Python, g++, etc.)
2. Sets up or uses Python virtual environment
3. Installs missing Python dependencies
4. Compiles the C++ engine if needed
5. Starts the FastAPI server
6. Displays application URLs

**Application URLs**:
- HTTP: `http://127.0.0.1:8000`
- API Docs: `http://127.0.0.1:8000/docs`
- WebSocket WS: `ws://127.0.0.1:8000/ws/simulation`

### Requirements
- Python 3.8+
- FastAPI, Uvicorn, Pydantic
- C++17 compiler (MinGW-w64 g++ on Windows)
- No Docker, WSL, or Bash required

## Testing

### C++ Tests
- Compilation verification
- All Phase 2 unit tests passing
- Engine controller integration
- Memory read/write round trips
- Process isolation and cleanup
- FIFO/LRU eviction
- Dirty-page swap integrity
- Invalid memory access

### HTTP and WebSocket Integration Check

After building the engine and installing the backend requirements, run from the repository root:

```powershell
python cpp/tests/check_http_ws.py cpp/build/windows/engine_cli.exe
```

The check starts an isolated Uvicorn instance with a temporary swap file. It verifies served assets, process and heap operations, page permissions, address bounds, and FIFO/LRU measurements over HTTP and WebSockets.

## Model Boundaries

- The simulated heap is 4 KiB with configurable allocation policies.
- Read and write requests must fit within a single virtual page.
- Swap backs live pages on disk. Initialization and reset clear the previous simulation.
