"""MoVMM-Engine Phase 4 - FastAPI Backend"""

import json
import asyncio
from typing import Optional, Dict, Any, List
from pathlib import Path

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field, validator

from .config import get_engine_process, cleanup_engine, EngineProcess

# Create FastAPI app
app = FastAPI(
    title="MoVMM-Engine Phase 4 API",
    description="Virtual Memory Manager Integration",
    version="0.1.0",
    docs_url="/docs",
    redoc_url="/redoc"
)

# Mount static files (future frontend)
app.mount("/frontend", StaticFiles(directory=Path(__file__).parent.parent.parent / "frontend"), name="frontend")

# Engine process management
engine_process = get_engine_process()

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
    params: Dict[str, int] = Field(..., description={"pid": "Process ID"})

class AllocateRequest(BaseModel):
    """Request for memory allocation."""
    id: str
    command: str = "allocate"
    params: Dict[str, int] = Field(..., description={"pid": "Process ID", "size": "Allocation size"})

class FreeRequest(BaseModel):
    """Request for memory freeing."""
    id: str
    command: str = "free"
    params: Dict[str, int] = Field(..., description={"pid": "Process ID", "address": "Memory address"})

class TranslateRequest(BaseModel):
    """Request for address translation."""
    id: str
    command: str = "translate"
    params: Dict[str, int] = Field(..., description={"pid": "Process ID", "virtual_address": "16-bit VA"})

class ReadMemoryRequest(BaseModel):
    """Request for memory read."""
    id: str
    command: str = "read_memory"
    params: Dict[str, int] = Field(..., description={"pid": "Process ID", "virtual_address": "16-bit VA", "size": "Bytes to read"})

class WriteMemoryRequest(BaseModel):
    """Request for memory write."""
    id: str
    command: str = "write_memory"
    params: Dict[str, Any] = Field(..., description={"pid": "Process ID", "virtual_address": "16-bit VA", "data": "Byte array"})

class SetPolicyRequest(BaseModel):
    """Request for policy setting."""
    id: str
    command: str = "set_policy"
    params: Dict[str, str] = Field(..., description={"policy_type": "allocation|replacement", "policy": "first_fit|best_fit|lru|fifo"})

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
        proc = engine_process
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
        proc = engine_process
        command = "initialize"
        params = {
            "ram_size_bytes": request.get("ram_size_bytes", 4096),
            "page_size": request.get("page_size", 256),
            "tlb_capacity": request.get("tlb_capacity", 4),
            "swap_capacity_pages": request.get("swap_capacity_pages", 64),
            "fit_policy": request.get("fit_policy", "first_fit"),
            "replacement_algo": request.get("replacement_algo", "fifo")
        }

        response = proc.send_command(command, params)

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
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/reset")
async def reset_engine():
    """Reset the engine state."""
    try:
        proc = engine_process
        response = proc.send_command("reset")

        if response and response.get("success"):
            return {"status": "reset", "success": True}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/processes")
async def create_process(request: CreateProcessRequest):
    """Create a simulated process."""
    try:
        proc = engine_process
        response = proc.send_command(request.command, request.params)

        if response and response.get("success"):
            pid = response["data"].get("pid", 1)
            return {"pid": pid, "success": True}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/processes/{pid}")
async def terminate_process(pid: int):
    """Terminate a simulated process."""
    try:
        proc = engine_process
        response = proc.send_command("terminate_process", {"pid": pid})

        if response and response.get("success"):
            return {"pid": pid, "success": True}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/memory/allocate")
async def allocate_memory(request: AllocateRequest):
    """Allocate memory for a process."""
    try:
        proc = engine_process
        response = proc.send_command(request.command, request.params)

        if response and response.get("success"):
            return {"success": True, "message": "Memory allocated"}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/memory/free")
async def free_memory(request: FreeRequest):
    """Free memory for a process."""
    try:
        proc = engine_process
        response = proc.send_command(request.command, request.params)

        if response and response.get("success"):
            return {"success": True, "message": "Memory freed"}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/memory/translate")
async def translate_address(request: TranslateRequest):
    """Translate a virtual address."""
    try:
        proc = engine_process
        response = proc.send_command(request.command, request.params)

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
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/memory/read")
async def read_memory(request: ReadMemoryRequest):
    """Read from simulated memory."""
    try:
        proc = engine_process
        response = proc.send_command(request.command, request.params)

        if response and response.get("success"):
            return {"success": True, "bytes_read": response.get("data", {}).get("bytes_read", 0)}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/memory/write")
async def write_memory(request: WriteMemoryRequest):
    """Write to simulated memory."""
    try:
        proc = engine_process
        response = proc.send_command(request.command, request.params)

        if response and response.get("success"):
            return {"success": True, "message": "Memory written"}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/v1/policies")
async def set_policies(request: SetPolicyRequest):
    """Set allocation and replacement policies."""
    try:
        proc = engine_process
        policy_type = request.params.get("policy_type", "allocation")
        policy = request.params.get("policy", "first_fit")

        response = proc.send_command("set_policy", {
            "policy_type": policy_type,
            "policy": policy
        })

        if response and response.get("success"):
            return {"success": True, "message": "Policy updated"}
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/v1/state")
async def get_state():
    """Get engine state snapshot."""
    try:
        proc = engine_process
        response = proc.send_command("get_snapshot")

        if response and response.get("success"):
            data = response.get("data", {})
            return {
                "success": True,
                "machine": data.get("machine", {}),
                "heap": data.get("heap", {}),
                "mmu": data.get("mmu", {}),
                "paging": data.get("paging", {}),
                "swap": data.get("swap", {})
            }
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/v1/statistics")
async def get_statistics():
    """Get engine statistics."""
    try:
        proc = engine_process
        response = proc.send_command("get_statistics")

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
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/v1/events")
async def get_events():
    """Get engine events."""
    try:
        proc = engine_process
        response = proc.send_command("get_events")

        if response and response.get("success"):
            data = response.get("data", {})
            return {
                "success": True,
                "events": data.get("events", [])
            }
        else:
            raise HTTPException(status_code=500, detail=response.get("error", "Unknown error") if response else "No response")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

# WebSocket endpoint for real-time updates
@app.websocket("/ws/simulation")
async def websocket_simulation(websocket: WebSocket):
    """WebSocket endpoint for simulation state updates."""
    await websocket.accept()

    try:
        # Subscribe to engine events
        proc = engine_process

        # Send initial state
        response = proc.send_command("get_snapshot")
        if response and response.get("success"):
            data = response.get("data", {})
            await websocket.send_json({
                "type": "initial_state",
                "data": data
            })

        # Listen for engine output
        while True:
            # Check if process is still running
            if not proc.is_running():
                break

            # Try to read a line from engine stdout (non-blocking with timeout)
            try:
                line = proc.process.stdout.readline(1.0) if hasattr(proc.process, 'readline') else ""
                if line.strip():
                    # Parse and forward to client
                    try:
                        msg = json.loads(line.strip())
                        # Route different message types
                        if msg.get("type") == "state_update":
                            await websocket.send_json(msg.get("data", {}))
                        elif msg.get("type") == "event":
                            await websocket.send_json({"event": msg.get("data", {})})
                        else:
                            await websocket.send_json(msg)
                    except json.JSONDecodeError:
                        await websocket.send_text(line)
            except Exception:
                break

    except WebSocketDisconnect:
        pass
    finally:
        pass

# HTML landing page
@app.get("/")
async def landing_page():
    """Minimal diagnostic landing page."""
    return HTMLResponse(html="""
    <!DOCTYPE html>
    <html>
    <head>
        <title>MoVMM-Engine Phase 4</title>
        <style>
            body {font-family: Arial, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px;}
            h1 {color: #333;}
            .endpoint {margin: 10px 0; padding: 10px; background: #f5f5f5; border-radius: 5px;}
            a {color: #0066cc; text-decoration: none;}
            a:hover {text-decoration: underline;}
        </style>
    </head>
    <body>
        <h1>MoVMM-Engine Phase 4</h1>
        <p>Virtual Memory Manager FastAPI Backend</p>
        <p>Application URL: <a href="http://127.0.0.1:8000">http://127.0.0.1:8000</a></p>
        <p>API Documentation: <a href="http://127.0.0.1:8000/docs">http://127.0.0.1:8000/docs</a></p>

        <h2>Endpoints</h2>
        <div class="endpoint"><strong>GET</strong> <code>/api/v1/health</code> - Health check</div>
        <div class="endpoint"><strong>POST</strong> <code>/api/v1/config</code> - Configure engine</div>
        <div class="endpoint"><strong>POST</strong> <code>/api/v1/reset</code> - Reset engine</div>
        <div class="endpoint"><strong>POST</strong> <code>/api/v1/processes</code> - Create process</div>
        <div class="endpoint"><strong>DELETE</strong> <code>/api/v1/processes/{pid}</code> - Terminate process</div>
        <div class="endpoint"><strong>POST</strong> <code>/api/v1/memory/allocate</code> - Allocate memory</div>
        <div class="endpoint"><strong>POST</strong> <code>/api/v1/memory/free</code> - Free memory</div>
        <div class="endpoint"><strong>POST</strong> <code>/api/v1/memory/translate</code> - Translate address</div>
        <div class="endpoint"><strong>POST</strong> <code>/api/v1/memory/read</code> - Read memory</div>
        <div class="endpoint"><strong>POST</strong> <code>/api/v1/memory/write</code> - Write memory</div>
        <div class="endpoint"><strong>POST</strong> <code>/api/v1/policies</code> - Set policies</div>
        <div class="endpoint"><strong>GET</strong> <code>/api/v1/state</code> - Get engine state</div>
        <div class="endpoint"><strong>GET</strong> <code>/api/v1/statistics</code> - Get statistics</div>
        <div class="endpoint"><strong>GET</strong> <code>/api/v1/events</code> - Get events</div>
        <div class="endpoint"><strong>GET</strong> <code>/ws/simulation</code> - WebSocket updates</div>
        <div class="endpoint"><strong>GET</strong> <code>/</code> - This landing page</div>
    </body>
    </html>
    """)

# Startup event
@app.on_event("startup")
async def startup_event():
    Write-Host "FastAPI application starting..." -ForegroundColor Cyan

# Shutdown event
@app.on_event("shutdown")
async def shutdown_event():
    cleanup_engine()
    Write-Host "FastAPI application shutting down..." -ForegroundColor Cyan