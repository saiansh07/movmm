import json
import os
import socket
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from functools import partial
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root))
import uvicorn
from websockets.sync.client import connect
from cpp.python import config, main

def request(path, payload=None):
    body = json.dumps(payload).encode() if payload is not None else None
    req = urllib.request.Request(f"http://127.0.0.1:{port}" + path, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=5) as response:
        return response.read()

def command(path, **params):
    return json.loads(request("/api/v1/" + path, {"id": "phase5", "params": params}))

def receive(ws, condition):
    deadline = time.monotonic() + 5
    while True:
        message = json.loads(ws.recv(timeout=max(0, deadline - time.monotonic())))
        assert message["type"] in ("initial_state", "state_update"), message
        if condition(message["data"]):
            return message["data"]

with tempfile.TemporaryDirectory() as work, socket.socket() as listener:
    engine_binary = Path(sys.argv[1]).resolve()
    os.chdir(work)
    config.EngineProcess = partial(config.EngineProcess, str(engine_binary))
    listener.bind(("127.0.0.1", 0))
    port = listener.getsockname()[1]
    server = uvicorn.Server(uvicorn.Config(main.app, log_level="warning", timeout_graceful_shutdown=2))
    thread = threading.Thread(target=server.run, kwargs={"sockets": [listener]}, daemon=True)
    thread.start()
    try:
        deadline = time.monotonic() + 10
        while not server.started and thread.is_alive() and time.monotonic() < deadline:
            time.sleep(0.05)
        assert server.started
        assert b"Virtual Memory Lab" in request("/")
        assert b".workspace" in request("/frontend/styles.css")
        assert b"new WebSocket" in request("/frontend/app.js")
        for algorithm in ("fifo", "lru"):
            with connect(f"ws://127.0.0.1:{port}/ws/simulation", open_timeout=5) as ws:
                receive(ws, lambda state: "frames" in state)
                request("/api/v1/config", {"page_size": 256, "ram_size_bytes": 512, "swap_capacity_pages": 8, "replacement_algo": algorithm})
                pid = command("processes")["pid"]
                assert command("memory/allocate", pid=pid, size=64)["success"]
                assert command("memory/allocate", pid=pid, size=128)["success"]
                state = receive(ws, lambda state: state["heap"]["total_allocations"] == 2)
                assert state["heap"]["blocks"][0]["owner"] == pid
                assert command("memory/free", pid=pid, address=0)["success"]
                state = receive(ws, lambda state: state["heap"]["total_frees"] == 1)
                assert state["heap"]["external_fragmentation_bytes"] == 64
                stats = json.loads(request("/api/v1/statistics"))
                assert (stats["total_allocations"], stats["total_frees"], stats["active_allocations"]) == (2, 1, 1)
                request("/api/v1/memory/permissions", {"pid": pid, "virtual_address": 0, "writable": False})
                try:
                    command("memory/write", pid=pid, virtual_address=0, data=[42])
                except urllib.error.HTTPError as exc:
                    assert exc.code == 500 and b"Write failed" in exc.read()
                else:
                    raise AssertionError("Read-only write succeeded")
                protected = receive(ws, lambda state: state["processes"][0]["pages"][0]["writable"] is False)
                assert protected["paging"]["page_faults"] == 0
                request("/api/v1/memory/permissions", {"pid": pid, "virtual_address": 0, "writable": True})
                for invalid in (-1, 65536):
                    try:
                        command("memory/translate", pid=pid, virtual_address=invalid)
                    except urllib.error.HTTPError as exc:
                        assert b"Virtual address must be an integer" in exc.read()
                    else:
                        raise AssertionError("Out-of-range address accepted")
                assert command("memory/write", pid=pid, virtual_address=0, data=[42])["success"]
                assert command("memory/translate", pid=pid, virtual_address=256)["page_fault"]
                assert command("memory/translate", pid=pid, virtual_address=0)["tlb_hit"]
                assert command("memory/translate", pid=pid, virtual_address=512)["page_fault"]
                state = receive(ws, lambda state: state["paging"]["replacements"] == 1)
                page = state["processes"][0]["pages"][0]
                assert page["present"] == (algorithm == "lru")
                assert page["swap_slot"] == (None if algorithm == "lru" else 0)
                assert state["swap"]["page_outs"] == (0 if algorithm == "lru" else 1)
                assert command("memory/read", pid=pid, virtual_address=0, size=1)["bytes"] == [42]
                final = receive(ws, lambda state: state["events"][-1]["event_type"] == "memory_read")
                faults = 3 if algorithm == "lru" else 4
                assert final["paging"]["page_faults"] == faults
                assert final["paging"]["replacements"] == faults - 2
                assert final["paging"][algorithm + "_enabled"]
                assert final["mmu"]["tlb_lookups"] == 5
                assert final["mmu"]["tlb_hits"] == 5 - faults
                assert final["swap"]["page_ins"] == (0 if algorithm == "lru" else 1)
                rest = json.loads(request("/api/v1/state"))
                assert rest["frames"] == final["frames"]
                request(f"/api/v1/processes/{pid}", {})
                cleaned = receive(ws, lambda state: not state["processes"])
                assert cleaned["swap"]["used_slots"] == 0 and cleaned["paging"]["resident_pages"] == 0
    finally:
        server.should_exit = True
        thread.join(timeout=10)
        os.chdir(root)
print("PASS: permissions, invalid addresses, heap metrics, and distinct FIFO/LRU measurements against live engine over HTTP and WebSockets")
