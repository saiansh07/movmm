# MoVMM-Engine Phase 4 - Engine API Protocol

## Overview
The MoVMM-Engine C++ core communicates with the FastAPI backend via a persistent JSON Lines subprocess protocol. This document defines the supported commands, request/response schemas, and interaction semantics.

## Persistent Subprocess Protocol

### Connection
- One persistent C++ engine subprocess is started by the FastAPI backend
- Communication via stdin (commands) and stdout (responses)
- stderr is kept separate from the protocol
- Request IDs correlate requests with responses

### Request Format (JSON Lines)
```json
{"id":"req-1","command":"get_state","params":{}}
```

### Response Format (JSON Lines)
```json
{"id":"req-1","success":true,"data":{...},"error":null}
```

### Supported Commands

| Command | Parameters | Response Data |
|---------|-----------|--------------|
| `initialize` | `ram_size_bytes`, `page_size`, `tlb_capacity`, `swap_capacity_pages`, `fit_policy`, `replacement_algo` | `{"status": "initialized", "config": ...}` |
| `reset` | (none) | `{"status": "reset"}` |
| `create_process` | `pid` (Process ID) | `{"pid": <int>}` |
| `terminate_process` | `pid` (Process ID) | `{"success": true/false}` |
| `allocate` | `pid`, `size` | `{"success": true/false}` |
| `free` | `pid`, `address` (owned heap block offset in bytes) | `{"success": true/false}` |
| `translate` | `pid`, `virtual_address` | `{"physical_address": int, "tlb_hit": bool, "page_fault": bool, "protection_fault": bool}` |
| `read_memory` | `pid`, `virtual_address`, `size` | `{"bytes_read": int}` |
| `write_memory` | `pid`, `virtual_address`, `data` (array) | `{"success": true/false}` |
| `set_policy` | `policy_type` (allocation/replacement), `policy` (first_fit/best_fit/lru/fifo) | `{"success": true/false}` |
| `get_state` | (none) | Full engine snapshot |
| `get_statistics` | (none) | Statistics object |
| `get_events` | (none) | Array of recent events |

### Error Handling
- Malformed JSON: `{"success": false, "error": "JSON parse error: ...}`
- Unknown command: `{"success": false, "error": "Unknown command: ...}`
- Missing parameters: `{"success": false, "error": "Missing parameter: ...}`
- Engine errors: `{"success": false, "error": "<engine error message>"}`
- Request ID is always preserved in the response for correlation

## Engine State Snapshots

### Machine State
- `ram_size_bytes`: Total RAM in bytes
- `page_size`: Page size in bytes
- `total_frames`: Total physical frames
- `used_swap_slots`: Used swap slots
- `total_swap_slots`: Total swap slots
- `heap_utilization_percent`: Percentage of heap used

### Heap State
- `allocated_bytes`: Total allocated bytes
- `free_bytes`: Free heap bytes
- `total_blocks`: Total blocks
- `free_blocks`: Free blocks
- `blocks`: Blocks in heap order with byte `offset`, byte `size`, and `owner` PID. Free blocks have a null owner.
- `total_allocations` and `total_frees`: Successful operations since initialization or reset. Process termination counts each block it releases.
- `external_fragmentation_bytes`: Total free bytes minus the largest contiguous free block.
- `internal_fragmentation_bytes`: Bytes reserved in live blocks beyond the sizes requested.

### MMU State
- `tlb_hits`: TLB hit count
- `tlb_misses`: TLB miss count
- `tlb_capacity`: TLB entry count

### Paging State
- `page_faults`: Page fault count
- `resident_pages`: Resident page count
- `fifo_enabled`: FIFO replacement active
- `lru_enabled`: LRU replacement active

### Swap State
- `page_outs`: Page-out count
- `page_ins`: Page-in count
- `used_slots`: Used swap slots

## Event Reporting

Events are generated for all meaningful operations and maintained in a bounded history (max 1000 events). Each event contains:
- `event_id`: Unique monotonically increasing ID
- `step_number`: Monotonic simulation step
- `event_type`: One of the supported event categories
- `process_id`: Associated process ID (when applicable)
- `virtual_address`: Virtual address (when applicable)
- `page_number`: Page number (when applicable)
- `frame_number`: Frame number (when applicable)
- `swap_slot`: Swap slot (when applicable)
- `success`: Operation success flag
- `explanation`: Human-readable explanation

## JSON Serialization
- All communication uses JSON Lines format (one JSON object per line)
- No diagnostic output on stdout; diagnostics go to stderr
- Request IDs enable correlation between requests and responses
- Proper handling of malformed input and unknown commands

## Integration Notes
- The C++ engine is the single source of truth for all simulated memory state
- The FastAPI backend does not fabricate memory operations or maintain competing state
- All memory management, statistics, and event reporting originate from the C++ engine
- The subprocess model ensures process isolation and platform independence (Windows 11)
