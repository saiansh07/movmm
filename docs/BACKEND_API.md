# MoVMM-Engine backend API

FastAPI serves the dashboard at `/`, its assets under `/frontend/`, and interactive API documentation at `/docs`. The backend forwards commands to the C++ subprocess using the [engine protocol](ENGINE_API.md).

## REST API

All paths below are relative to `/api/v1`. Use JSON for request bodies. Process creation, memory allocation, free, translation, read, write, and policy changes accept an envelope with a string `id`, an optional `command` that defaults to the endpoint's operation, and `params`. For example:

```json
{"id":"write-1","params":{"pid":1,"virtual_address":256,"data":[42]}}
```

| Method and path | Request | Successful response |
|---|---|---|
| GET `/health` | No body | `status`, `engine_initialized` (subprocess running), `version` |
| POST `/config` | Flat object with optional configuration fields | Configuration fields |
| POST `/reset` | No body | `success`, `status: "reset"` |
| POST `/processes` | Envelope with `id`. The engine assigns the PID. | `success`, `pid` |
| POST `/processes/{pid}` | Path parameter only | `success`, `pid` |
| POST `/memory/allocate` | Envelope with `pid`, `size` | `success`, `message` |
| POST `/memory/free` | Envelope with `pid`, `address`. The address is the byte offset of a block owned by the process, from `heap.blocks`. | `success`, `message` |
| POST `/memory/translate` | Envelope with `pid`, `virtual_address` | `success`, `physical_address`, `virtual_page`, `page_offset`, `physical_frame`, `tlb_hit`, `page_fault`, `protection_fault` |
| POST `/memory/read` | Envelope with `pid`, `virtual_address`, `size` | `success`, `bytes_read`, `bytes` |
| POST `/memory/write` | Envelope with `pid`, `virtual_address`, `data` (byte array) | `success`, `message` |
| POST `/memory/permissions` | Flat object, shown below | `{"success":true}` |
| POST `/policies` | Envelope with `policy_type` and `policy` | `success`, `message` |
| GET `/state` | No body | `success: true` plus the top-level fields of the engine snapshot |
| GET `/statistics` | No body | `success`, `total_allocations`, `total_frees`, `active_allocations`, `tlb_hit_rate` (percent), `page_faults` |
| GET `/events` | No body | `success`, `events` |

Configuration defaults are `ram_size_bytes: 4096`, `page_size: 256`, `tlb_capacity: 4`, `swap_capacity_pages: 64`, `fit_policy: "first_fit"`, and `replacement_algo: "fifo"`. Page sizes are 256, 512, 1024, 2048, or 4096 bytes. RAM must be a positive multiple of page size. Reset clears simulation state and keeps the current configuration.

For `/policies`, use `allocation` with `first_fit` or `best_fit`, or `replacement` with `fifo` or `lru`. Virtual addresses are integers from 0 through 65535. Reads and writes must fit within a single virtual page.

### Page permissions

Send a flat object to `POST /api/v1/memory/permissions`:

```json
{"pid":1,"virtual_address":0,"writable":false}
```

The backend supplies the engine request ID and forwards these fields as `params` to `set_page_permission`. The permission applies to the entire page containing the address. A read can bring a read-only page into RAM. A rejected write records a `protection_fault` event and currently returns HTTP 500 with `{"detail":"Write failed"}`.

The permission route returns HTTP 404 if the process is missing and HTTP 503 for other engine failures. Request validation failures return HTTP 422. Most other routes report engine failures as HTTP 500 with a JSON `detail` field.

### Snapshots and WebSocket

`GET /api/v1/state` and the WebSocket at `/ws/simulation` use the same [engine snapshot fields](ENGINE_API.md#engine-state-snapshots). The WebSocket sends `type: "initial_state"` on connection and `type: "state_update"` when the snapshot changes, with the snapshot under `data`. Each connection polls approximately every 250 ms and skips unchanged snapshots. An engine failure sends `type: "error"` with an `error` field and closes the connection with code 1011.

- `paging.fifo_order` and `paging.lru_order` contain resident frame numbers in eviction order. The first frame is the next victim under that policy when RAM is full.
- `swap.slots` lists every slot as `slot`, `pid`, `page`, and `resident`. Free slots have null `pid` and `page` and `resident: false`. An occupied slot can still back a page in RAM, in which case `resident` is true.

## Windows setup and checks

Use Python 3.14 and follow the [Windows setup guide](WINDOWS_SETUP.md) to launch the app and build the C++ tests. After the launcher installs dependencies, run the isolated integration check from the repository root:

```powershell
& "$env:USERPROFILE\movmm_venv\Scripts\python.exe" cpp/tests/check_http_ws.py cpp/build/windows/engine_cli.exe
```

The check starts its own Uvicorn instance and uses a temporary swap file. It verifies served assets, heap operations, page permissions, address bounds, eviction order, swap ownership, and FIFO/LRU measurements.
