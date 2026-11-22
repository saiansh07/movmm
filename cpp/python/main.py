"""MoVMM-Engine Phase 4 - FastAPI Backend"""

from contextlib import asynccontextmanager
import asyncio
from typing import Optional, Dict, Any
from pathlib import Path

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from starlette.concurrency import run_in_threadpool
from pydantic import BaseModel, Field, validator

from .config import get_engine_process, cleanup_engine

@asynccontextmanager
async def lifespan(app: FastAPI):
    app.state.engine = await run_in_threadpool(get_engine_process)
    try:
        yield
    finally:
        await run_in_threadpool(cleanup_engine)


app = FastAPI(
    title="MoVMM-Engine Phase 4 API",
    description="Virtual Memory Manager Integration",
    version="0.1.0",
    lifespan=lifespan,
    docs_url="/docs",
    redoc_url="/redoc"
)

frontend_path = Path(__file__).resolve().parents[2] / "frontend"
if frontend_path.is_dir():
    app.mount("/frontend", StaticFiles(directory=frontend_path), name="frontend")


# Pydantic models for request/response validation

class CommandRequest(BaseModel):
    """Base request model for engine commands."""
    id: str = Field(..., description="Request identifier")
    command: str = Field(..., description="Engine command name")
    params: Optional[Dict[str, Any]] = Field(default=None, description="Command parameters")

    @validator("command")
    def validate_command(cls, v):
        allowed = [
            "initialize", "reset", "create_process", "terminate_process",
            "allocate", "free", "translate", "read_memory", "write_memory",
            "set_policy", "get_state", "get_statistics", "get_events"
        ]
        if v not in allowed:
            raise ValueError(f"Invalid command: {v}")
        return v

class InitializeRequest(BaseModel):
    """Request for engine initialization."""
    id: str
    command: str = "initialize"
    params: Optional[Dict[str, Any]] = None

class ResetRequest(BaseModel):
    """Request for engine reset."""
    id: str
    command: str = "reset"
    params: Optional[Dict[str, Any]] = None

class CreateProcessRequest(BaseModel):
    """Request for process creation."""
    id: str
    command: str = "create_process"
    params: Dict[str, int] = Field(default={"pid": 1})

class TerminateProcessRequest(BaseModel):
    """Request for process termination."""
    id: str
    command: str = "terminate_process"
    params: Dict[str, int] = Field(..., description="pid: Process ID")

class AllocateRequest(BaseModel):
    """Request for memory allocation."""
    id: str
    command: str = "allocate"
    params: Dict[str, int] = Field(..., description="pid: Process ID, size: Allocation size")

class FreeRequest(BaseModel):
    """Request for memory freeing."""
    id: str
    command: str = "free"
    params: Dict[str, int] = Field(..., description="pid: Process ID, address: Memory address")

class TranslateRequest(BaseModel):
    """Request for address translation."""
    id: str
    command: str = "translate"
    params: Dict[str, int] = Field(..., description="pid: Process ID, virtual_address: 16-bit VA")

class ReadMemoryRequest(BaseModel):
    """Request for memory read."""
    id: str
    command: str = "read_memory"
    params: Dict[str, int] = Field(..., description="pid: Process ID, virtual_address: 16-bit VA, size: Bytes to read")

class WriteMemoryRequest(BaseModel):
    """Request for memory write."""
    id: str
    command: str = "write_memory"
    params: Dict[str, Any] = Field(..., description="pid: Process ID, virtual_address: 16-bit VA, data: Byte array")

class SetPolicyRequest(BaseModel):
    """Request for policy setting."""
    id: str
    command: str = "set_policy"
    params: Dict[str, str] = Field(..., description="policy_type: allocation|replacement, policy: first_fit|best_fit|lru|fifo")

class GetStateRequest(BaseModel):
    """Request for engine state."""
    id: str
    command: str = "get_state"  # or "get_snapshot"

class GetStatisticsRequest(BaseModel):
    """Request for statistics."""
    id: str
    command: str = "get_statistics"

class GetEventsRequest(BaseModel):
    """Request for events."""
    id: str
    command: str = "get_events"

# Response models
class CommandResponse(BaseModel):
    """Base command response."""
    id: str
    success: bool
    data: Optional[Dict[str, Any]] = None
    error: Optional[str] = None

class HealthResponse(BaseModel):
    """Health check response."""
    status: str = "ok"
    engine_initialized: bool
    version: str = "0.1.0"

class ConfigResponse(BaseModel):
    """Configuration response."""
    ram_size_bytes: int
    page_size: int
    tlb_capacity: int
    swap_capacity_pages: int
    fit_policy: str
    replacement_algo: str

# Endpoints

@app.get("/api/v1/health", response_model=HealthResponse)
async def health_check():
    """Health check endpoint."""
    try:
        proc = app.state.engine
        is_running = proc.is_running()
        return HealthResponse(
            status="ok",
            engine_initialized=is_running
        )
    except Exception as e:
        raise HTTPException(status_code=503, detail=str(e))

@app.post("/api/v1/config", response_model=ConfigResponse)
async def set_config(request: dict):
    """Configure the engine."""
    try:
        proc = app.state.engine
        command = "initialize"
        params = {
            "ram_size_bytes": request.get("ram_size_bytes", 4096),
            "page_size": request.get("page_size", 256),
            "tlb_capacity": request.get("tlb_capacity", 4),
            "swap_capacity_pages": request.get("swap_capacity_pages", 64),
            "fit_policy": request.get("fit_policy", "first_fit"),
            "replacement_algo": request.get("replacement_algo", "fifo")
        }

        page_size = params["page_size"]
        if type(page_size) is not int or page_size not in (256, 512, 1024, 2048, 4096):
            raise HTTPException(status_code=422, detail="Page size must be a power of two from 256 to 4096 bytes")
        ram_size = params["ram_size_bytes"]
        if type(ram_size) is not int or ram_size < page_size or ram_size % page_size:
            raise HTTPException(status_code=422, detail="RAM size must be a positive multiple of page size")
        response = await run_in_threadpool(proc.send_command, command, params)

        if response and response.get("success"):
            return ConfigResponse(
                ram_size_bytes=params["ram_size_bytes"],
                page_size=params["page_size"],
                tlb_capacity=params["tlb_capacity"],
                swap_capacity_pages=params["swap_capacity_pages"],
                fit_policy=params["fit_policy"],
                replacement_algo=params["replacement_algo"]
            )
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error"))
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/reset")
async def reset_engine():
    """Reset the engine state."""
    try:
        proc = app.state.engine
        response = await run_in_threadpool(proc.send_command, "reset")

        if response and response.get("success"):
            return {"status": "reset", "success": True}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/processes")
async def create_process(request: CreateProcessRequest):
    """Create a simulated process."""
    try:
        proc = app.state.engine
        response = await run_in_threadpool(proc.send_command, request.command, request.params)

        if response and response.get("success"):
            pid = response["data"].get("pid", 1)
            return {"pid": pid, "success": True}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/processes/{pid}")
async def terminate_process(pid: int):
    """Terminate a simulated process."""
    try:
        proc = app.state.engine
        response = await run_in_threadpool(proc.send_command, "terminate_process", {"pid": pid})

        if response and response.get("success"):
            return {"pid": pid, "success": True}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/memory/allocate")
async def allocate_memory(request: AllocateRequest):
    """Allocate memory for a process."""
    try:
        proc = app.state.engine
        response = await run_in_threadpool(proc.send_command, request.command, request.params)

        if response and response.get("success"):
            return {"success": True, "message": "Memory allocated"}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/memory/free")
async def free_memory(request: FreeRequest):
    """Free memory for a process."""
    try:
        proc = app.state.engine
        response = await run_in_threadpool(proc.send_command, request.command, request.params)

        if response and response.get("success"):
            return {"success": True, "message": "Memory freed"}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/memory/translate")
async def translate_address(request: TranslateRequest):
    """Translate a virtual address."""
    try:
        proc = app.state.engine
        response = await run_in_threadpool(proc.send_command, request.command, request.params)

        if response and response.get("success"):
            data = response.get("data", {})
            return {
                "success": True,
                "physical_address": data.get("physical_address", 0),
                "tlb_hit": data.get("tlb_hit", False),
                "page_fault": data.get("page_fault", False),
                "protection_fault": data.get("protection_fault", False)
            }
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/memory/read")
async def read_memory(request: ReadMemoryRequest):
    """Read from simulated memory."""
    try:
        proc = app.state.engine
        response = await run_in_threadpool(proc.send_command, request.command, request.params)

        if response and response.get("success"):
            return {"success": True, **response["data"]}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/memory/write")
async def write_memory(request: WriteMemoryRequest):
    """Write to simulated memory."""
    try:
        proc = app.state.engine
        response = await run_in_threadpool(proc.send_command, request.command, request.params)

        if response and response.get("success"):
            return {"success": True, "message": "Memory written"}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/policies")
async def set_policies(request: SetPolicyRequest):
    """Set allocation and replacement policies."""
    try:
        proc = app.state.engine
        policy_type = request.params.get("policy_type", "allocation")
        policy = request.params.get("policy", "first_fit")

        response = await run_in_threadpool(proc.send_command, "set_policy", {
            "policy_type": policy_type,
            "policy": policy
        })

        if response and response.get("success"):
            return {"success": True, "message": "Policy updated"}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/v1/state")
async def get_state():
    """Get engine state snapshot."""
    try:
        proc = app.state.engine
        response = await run_in_threadpool(proc.send_command, "get_snapshot")

        if response and response.get("success"):
            return {"success": True, **response["data"]}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/v1/statistics")
async def get_statistics():
    """Get engine statistics."""
    try:
        proc = app.state.engine
        response = await run_in_threadpool(proc.send_command, "get_statistics")

        if response and response.get("success"):
            data = response.get("data", {})
            return {
                "success": True,
                "total_allocations": data.get("total_allocations", 0),
                "active_allocations": data.get("active_allocations", 0),
                "tlb_hit_rate": data.get("tlb_hit_rate", 0.0),
                "page_faults": data.get("page_faults", 0)
            }
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/v1/events")
async def get_events():
    """Get engine events."""
    try:
        proc = app.state.engine
        response = await run_in_threadpool(proc.send_command, "get_events")

        if response and response.get("success"):
            data = response.get("data", {})
            return {
                "success": True,
                "events": data.get("events", [])
            }
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

# WebSocket endpoint for real-time updates
@app.websocket("/ws/simulation")
async def websocket_simulation(websocket: WebSocket):
    """Stream changed engine snapshots and their recent events."""
    await websocket.accept()
    proc = websocket.app.state.engine
    previous = None

    try:
        while True:
            response = await run_in_threadpool(proc.send_command, "get_snapshot")
            if not response.get("success"):
                await websocket.send_json({"type": "error", "error": response["error"]})
                await websocket.close(code=1011)
                return

            snapshot = response["data"]
            if snapshot != previous:
                await websocket.send_json({
                    "type": "initial_state" if previous is None else "state_update",
                    "data": snapshot,
                })
                previous = snapshot

            # The CLI only writes replies. Poll for changes while listening for
            # disconnects, including clients that never send a message.
            try:
                message = await asyncio.wait_for(websocket.receive(), timeout=0.25)
                if message["type"] == "websocket.disconnect":
                    return
            except asyncio.TimeoutError:
                pass
    except (WebSocketDisconnect, OSError):
        pass


@app.get("/", include_in_schema=False)
async def dashboard():
    return FileResponse(frontend_path / "index.html")
