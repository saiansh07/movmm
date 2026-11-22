# MoVMM-Engine CLI protocol

FastAPI starts one persistent C++ subprocess and sends one JSON object per line on stdin. The CLI writes one JSON reply per request on stdout and sends diagnostics to stderr.

## Request and response envelopes

Use a string request ID. The CLI echoes it in the response. Omit `params` for commands that take no parameters.

```json
{"id":"req-1","command":"create_process"}
{"id":"req-1","success":true,"data":{"pid":1}}
{"id":"req-2","command":"set_page_permission","params":{"pid":1,"virtual_address":0,"writable":false}}
{"id":"req-2","success":true}
```

Replies always contain `id` and `success`. A successful reply contains `data` only when the command returns a payload. Failures include an `error` string. A malformed JSON request returns `id: "unknown"`, `success: false`, `data: null`, and a parse error. A missing or nonstring request ID is reported as `"unknown"`.

## Supported commands

| Command | Fields inside `params` | Successful `data` payload |
|---|---|---|
| `initialize` | Optional `ram_size_bytes`, `page_size`, `tlb_capacity`, `swap_capacity_pages`, `fit_policy`, `replacement_algo` | `status: "initialized"` and `config` (RAM size in bytes) |
| `reset` | None | `status: "reset"` |
| `create_process` | None. The engine assigns the PID. | `pid` |
| `terminate_process` | `pid` | None |
| `allocate` | `pid`, `size` | None |
| `free` | `pid`, `address` (owned heap block offset in bytes) | None |
| `translate` | `pid`, `virtual_address` | `physical_address`, `virtual_page`, `page_offset`, `physical_frame`, `tlb_hit`, `page_fault`, `protection_fault` |
| `read_memory` | `pid`, `virtual_address`, `size` | `bytes_read`, `bytes` |
| `write_memory` | `pid`, `virtual_address`, `data` (byte array) | None |
| `set_page_permission` | `pid`, `virtual_address`, `writable` (boolean) | None |
| `set_policy` | `policy_type`, `policy` | None |
| `get_snapshot` or `get_state` | None | Engine state snapshot |
| `get_statistics` | None | `total_allocations`, `total_frees`, `active_allocations`, `tlb_hit_rate` (percent), `page_faults` |
| `get_events` | None | Object with an `events` array |

Virtual addresses are integers from 0 through 65535. Reads and writes must fit within one virtual page. `translate` can return `success: true` with `protection_fault: true` and `physical_frame: null`. A handled demand fault is indicated by `page_fault: true`. The permissions command changes the page containing the address and returns `success: false` with `error: "Process not found"` for an unknown PID. A later write to a read-only page fails and records a `protection_fault` event.

For `set_policy`, use `allocation` with `first_fit` or `best_fit`, or `replacement` with `fifo` or `lru`.

## Engine State Snapshots

- **`machine`** contains `ram_size_bytes`, `page_size`, `total_frames`, `used_swap_slots`, `total_swap_slots`, `heap_utilization_percent`, and `fit_policy`.
- **`heap`** contains `allocated_bytes`, `free_bytes`, `total_blocks`, `free_blocks`, `allocation_attempts`, `total_allocations`, `total_frees`, `external_fragmentation_bytes`, `internal_fragmentation_bytes`, and `blocks`. Blocks have byte `offset`, byte `size`, and a process `owner` (null for a free block). External fragmentation is total free bytes minus the largest contiguous free block. Internal fragmentation is the excess bytes reserved in live allocations.
- **`mmu`** contains `tlb_lookups`, `tlb_hits`, `tlb_misses`, `tlb_capacity`, and valid `entries` with `slot`, `pid`, `page`, `frame`, and `dirty`.
- **`paging`** contains `page_faults`, `resident_pages`, `replacements`, `fifo_enabled`, `lru_enabled`, `fifo_order`, and `lru_order`. Both order arrays list resident frame numbers from first to last eviction candidate. The first is the next victim when RAM is full under that policy.
- **`swap`** contains `page_outs`, `page_ins`, `used_slots`, and `slots`. Each slot has a numeric `slot` index, owner `pid`, virtual `page`, and `resident` flag. A free slot has null `pid` and `page` and `resident: false`. A slot that backs a resident page remains occupied and has `resident: true`.
- **`processes`** contains `pid` and `pages` for each process. Page entries have `page`, `present`, `frame` (null when nonresident), `swap_slot` (null without backing), `referenced`, `dirty`, and `writable`.
- **`frames`** lists all physical frames with `frame`, `resident`, `pid`, `page`, and `dirty`. Free frames have null `pid` and `page`.
- **`events`** contains recent events in the same form as `get_events`.

## Event reporting

The CLI retains up to 1,000 events. The snapshot and `get_events` expose `event_id`, `step_number`, `event_type`, `process_id`, `virtual_address`, `page_number`, `frame_number`, `swap_slot`, `success`, and `explanation`. Event IDs increase within a simulation and restart when it is initialized or reset.

See the [backend API](BACKEND_API.md) for the HTTP and WebSocket forms of these responses.
