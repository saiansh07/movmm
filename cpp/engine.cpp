#include "engine.h"
#include "heap_allocator.h"
#include "mmu.h"
#include "paging.h"
#include "swap.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

using namespace std;

Engine::Engine() : Engine(EngineConfig()) {}

Engine::Engine(const EngineConfig& config) { initialize(config); }

bool Engine::initialize(const EngineConfig& config) {
    if (config.page_size < 256 || config.page_size > 4096 ||
        (config.page_size & (config.page_size - 1)) != 0 ||
        config.ram_size_bytes % config.page_size != 0 ||
        config.ram_size_bytes / config.page_size == 0 ||
        config.ram_size_bytes / config.page_size > 65536) {
        throw invalid_argument("Invalid page size or physical frame count");
    }
    state_.paging.reset();
    state_.swap.reset();
    state_.process_page_tables.clear();
    config_ = config;

    // 1. Initialize Swap Manager first (dependencies for paging)
    state_.swap = make_unique<SwapManager>("swap.bin", config_.swap_capacity_pages, config_.page_size);

    // 2. Initialize Heap Allocator
    state_.heap = make_unique<HeapAllocator>(4096, config_.fit_policy);
    heap_allocations_.clear();

    // 3. Initialize MMU with TLB
    state_.mmu = make_unique<MMU>(config_.tlb_capacity, config_.page_size);

    // 4. Initialize Paging Manager
    size_t ram_pages = config_.ram_size_bytes / config_.page_size;
    state_.paging = make_unique<PagingManager>(ram_pages, config_.replacement_algo, *state_.swap, config_.page_size);

    state_.simulation_step = 0;
    state_.next_process_id = 1;
    state_.allocation_attempts = 0;
    state_.total_allocations = 0;
    state_.total_frees = 0;
    state_.active_allocations = 0;
    state_.allocated_bytes = 0;

    event_history_.clear();
    next_event_id_ = 1;

    // Redirect diagnostic output to stderr
    cerr << "Engine initialized:" << endl;
    cerr << "  RAM: " << config_.ram_size_bytes << " bytes (" << ram_pages << " pages)" << endl;
    cerr << "  Page size: " << config_.page_size << " bytes" << endl;
    cerr << "  TLB entries: " << config_.tlb_capacity << endl;
    cerr << "  Swap capacity: " << config_.swap_capacity_pages << " pages" << endl;
    cerr << "  Allocation policy: " << (config_.fit_policy == FitPolicy::FIRST_FIT ? "First-Fit" : "Best-Fit") << endl;
    cerr << "  Replacement policy: " << (config_.replacement_algo == ReplacementAlgorithm::FIFO ? "FIFO" : "LRU") << endl;

    return true;
}

void Engine::reset() {
    if (!initialize(config_)) {
        cerr << "Failed to reset engine" << endl;
        return;
    }
    cerr << "Engine reset" << endl;
}

ProcessId Engine::create_process() {
    ProcessId pid = state_.next_process_id++;

    // Initialize this process's page table
    state_.process_page_tables[pid] = vector<PageTableEntry>();

    state_.simulation_step++;
    Event ev;

    ev.step_number = state_.simulation_step;
    ev.event_type = "process_created";
    ev.process_id = static_cast<int>(pid);
    ev.success = true;
    ev.explanation = "Process created with ID " + to_string(pid);
    record_event(std::move(ev));
    cerr << "Process created: PID " << pid << endl;
    return pid;
}

bool Engine::terminate_process(ProcessId pid) {
    if (pid == 0) {
        Event ev;

        ev.step_number = state_.simulation_step + 1;
        ev.event_type = "process_terminated";
        ev.process_id = 0;
        ev.success = false;
        ev.explanation = "Cannot terminate process ID 0";
        record_event(std::move(ev));
        return false;
    }

    // Validate process exists
    if (state_.process_page_tables.find(pid) == state_.process_page_tables.end()) {
        Event ev;

        ev.step_number = state_.simulation_step + 1;
        ev.event_type = "process_terminated";
        ev.process_id = static_cast<int>(pid);
        ev.success = false;
        ev.explanation = "Process not found: PID " + to_string(pid);
        record_event(std::move(ev));
        return false;
    }

    for (auto entry = heap_allocations_.begin(); entry != heap_allocations_.end();) {
        const auto allocation = entry++;
        if (allocation->second.owner == pid) free_heap_offset(pid, allocation->first);
    }
    state_.mmu->invalidate_tlb(pid);
    state_.paging->release_process(pid);
    for (const auto& page : state_.process_page_tables.at(pid)) {
        if (page.swap_slot != NO_SWAP_SLOT) state_.swap->free_slot(page.swap_slot);
    }
    state_.process_page_tables.erase(pid);

    state_.simulation_step++;
    Event ev;

    ev.step_number = state_.simulation_step;
    ev.event_type = "process_terminated";
    ev.process_id = static_cast<int>(pid);
    ev.success = true;
    ev.explanation = "Process terminated: PID " + to_string(pid);
    record_event(std::move(ev));
    cerr << "Process terminated: PID " << pid << endl;
    return true;
}

bool Engine::allocate_memory(ProcessId pid, size_t size) {
    ++state_.allocation_attempts;
    if (size == 0) {
        Event ev;

        ev.step_number = state_.simulation_step + 1;
        ev.event_type = "memory_allocated";
        ev.process_id = static_cast<int>(pid);
        ev.success = false;
        ev.explanation = "Allocation size cannot be zero";
        record_event(std::move(ev));
        return false;
    }

    // Validate process exists
    if (state_.process_page_tables.find(pid) == state_.process_page_tables.end()) {
        Event ev;

        ev.step_number = state_.simulation_step + 1;
        ev.event_type = "memory_allocated";
        ev.process_id = static_cast<int>(pid);
        ev.success = false;
        ev.explanation = "Process not found: PID " + to_string(pid);
        record_event(std::move(ev));
        return false;
    }

    state_.simulation_step++;
    void* ptr = state_.heap->my_malloc(size);

    bool success = (ptr != nullptr);

    Event ev;

    ev.step_number = state_.simulation_step;
    ev.event_type = "memory_allocated";
    ev.process_id = static_cast<int>(pid);
    ev.success = success;

    if (success) {
        const size_t offset = *state_.heap->offset_of(ptr);
        heap_allocations_.emplace(offset, HeapAllocation{pid, size});
        state_.total_allocations++;
        state_.active_allocations++;
        state_.allocated_bytes += size;
        ev.metadata["heap_offset"] = to_string(offset);
        ev.explanation = "Allocated " + to_string(size) + " bytes at heap offset " + to_string(offset);
    } else {
        ev.explanation = "Allocation failed: out of memory (" + to_string(size) + " bytes requested)";
    }
    cerr << (success ? "Allocated" : "Allocation failed") << ": " << ev.explanation << endl;
    record_event(std::move(ev));
    return success;
}

bool Engine::free_memory(ProcessId pid, void* ptr) {
    const auto offset = state_.heap->offset_of(ptr);
    return offset && free_heap_offset(pid, *offset);
}

bool Engine::free_heap_offset(ProcessId pid, size_t offset) {
    const auto allocation = heap_allocations_.find(offset);
    const bool owned = allocation != heap_allocations_.end() && allocation->second.owner == pid;
    Event ev;

    ev.step_number = ++state_.simulation_step;
    ev.event_type = "memory_freed";
    ev.process_id = static_cast<int>(pid);
    ev.metadata["heap_offset"] = to_string(offset);
    ev.success = owned;
    if (owned) {
        state_.heap->free_at(offset);
        state_.allocated_bytes -= allocation->second.requested_size;
        --state_.active_allocations;
        ++state_.total_frees;
        heap_allocations_.erase(allocation);
    }
    ev.explanation = owned ? "Freed block at heap offset " + to_string(offset)
                          : "No block owned by this process at heap offset " + to_string(offset);
    record_event(std::move(ev));
    return owned;
}

Engine::TranslationResult Engine::translate_address(ProcessId pid, VirtualAddress va) {
    TranslationResult result = {0, false, false, false};

    // Validate process exists
    auto it = state_.process_page_tables.find(pid);
    if (it == state_.process_page_tables.end()) {
        result.page_fault = true;
        result.protection_fault = true;

        Event ev;

        ev.step_number = state_.simulation_step + 1;
        ev.event_type = "address_translated";
        ev.process_id = static_cast<int>(pid);
        ev.virtual_address = static_cast<int>(va);
        ev.success = false;
        ev.explanation = "Translation failed: process not found: PID " + to_string(pid);
        record_event(std::move(ev));

        state_.simulation_step++;
        return result;
    }

    state_.simulation_step++;

    // Get the process's page table
    auto& page_table = it->second;

    VirtualAddress16 addr(va, static_cast<uint16_t>(config_.page_size));
    if (addr.page_number >= page_table.size()) page_table.resize(addr.page_number + 1);
    const auto translated = state_.mmu->translate(pid, va, page_table);
    result = {translated.physical_address, translated.tlb_hit, translated.page_fault, translated.protection_fault};

    if (result.page_fault) {
        auto fault = state_.paging->handle_page_fault(pid, addr.page_number, page_table, &state_.process_page_tables);
        result.physical_address = fault.allocated_frame * config_.page_size + addr.offset;
        if (fault.victim_selected) {
            state_.mmu->invalidate_tlb_entry(fault.evicted_pid, fault.evicted_page);
        }
        state_.mmu->update_tlb(pid, addr.page_number, fault.allocated_frame, false);

        auto record = [&](const string& type, ProcessId owner, PageNumber page, FrameNumber frame,
                          uint32_t slot, const string& explanation) {
            Event event{};

            event.step_number = state_.simulation_step;
            event.event_type = type;
            event.process_id = static_cast<int>(owner);
            event.virtual_address = va;
            event.page_number = page;
            event.frame_number = frame;
            event.swap_slot = slot;
            event.explanation = explanation;
            record_event(std::move(event));
        };
        record("page_fault", pid, addr.page_number, fault.allocated_frame, 0, "Loaded a non-resident page");
        if (fault.victim_selected) {
            record("page_replaced", fault.evicted_pid, fault.evicted_page, fault.victim_frame, 0,
                   config_.replacement_algo == ReplacementAlgorithm::LRU ? "LRU evicted a page" : "FIFO evicted a page");
        }
        if (fault.dirty_writeback) {
            record("page_out", fault.evicted_pid, fault.evicted_page, fault.victim_frame,
                   fault.swap_slot_used, "Wrote a dirty page to swap");
        }
        if (fault.page_in) {
            record("page_in", pid, addr.page_number, fault.allocated_frame,
                   page_table[addr.page_number].swap_slot, "Restored a page from swap");
        }
    }
    auto& page = page_table[addr.page_number];
    page.referenced = true;
    state_.paging->access_page(page.frame_number, false);

    Event ev;

    ev.step_number = state_.simulation_step;
    ev.event_type = "address_translated";
    ev.process_id = static_cast<int>(pid);
    ev.virtual_address = static_cast<int>(va);
    ev.success = true;
    ev.explanation = "Virtual address " + to_string(va) + " translated to physical " + to_string(result.physical_address);
    record_event(std::move(ev));

    cerr << "Translated VA " << hex << va << " -> PA " << dec << result.physical_address
         << " (TLB hit: " << (result.tlb_hit ? "yes" : "no") << ")" << endl;
    return result;
}

bool Engine::read_memory(ProcessId pid, VirtualAddress va, void* buffer, size_t size) {
    TranslationResult result = translate_address(pid, va);

    const size_t offset = va % config_.page_size;
    if (!result.protection_fault && size <= config_.page_size - offset) {
        const auto& frame = state_.paging->get_frames().at(result.physical_address / config_.page_size);
        if (size) memcpy(buffer, frame.data.data() + offset, size);

        Event ev;

        ev.step_number = state_.simulation_step;
        ev.event_type = "memory_read";
        ev.process_id = static_cast<int>(pid);
        ev.virtual_address = static_cast<int>(va);
        ev.success = true;
        ev.explanation = "Read " + to_string(size) + " bytes from VA " + to_string(va) + " -> PA " + to_string(result.physical_address);
        record_event(std::move(ev));
        return true;
    }

    Event ev;

    ev.step_number = state_.simulation_step;
    ev.event_type = "memory_read";
    ev.process_id = static_cast<int>(pid);
    ev.virtual_address = static_cast<int>(va);
    ev.success = false;
    ev.explanation = "Read failed at VA " + to_string(va);
    record_event(std::move(ev));
    return false;
}

bool Engine::write_memory(ProcessId pid, VirtualAddress va, const void* data, size_t size) {
    const VirtualAddress16 address(va, static_cast<uint16_t>(config_.page_size));
    const auto process = state_.process_page_tables.find(pid);
    if (process != state_.process_page_tables.end() && address.page_number < process->second.size() &&
        !process->second[address.page_number].read_write) {
        Event event{};
        event.step_number = ++state_.simulation_step;
        event.event_type = "protection_fault";
        event.process_id = static_cast<int>(pid);
        event.virtual_address = va;
        event.page_number = address.page_number;
        event.success = false;
        event.explanation = "Write denied for a read-only page";
        record_event(std::move(event));
        return false;
    }
    TranslationResult result = translate_address(pid, va);

    const size_t offset = va % config_.page_size;
    if (!result.protection_fault && size <= config_.page_size - offset) {
        VirtualAddress16 addr(va, static_cast<uint16_t>(config_.page_size));
        auto& page = state_.process_page_tables.at(pid)[addr.page_number];
        auto& frame = state_.paging->get_frames().at(page.frame_number);
        if (size) memcpy(frame.data.data() + offset, data, size);
        page.dirty = true;
        state_.paging->access_page(page.frame_number, true);
        state_.mmu->update_tlb(pid, addr.page_number, page.frame_number, true);

        Event ev;

        ev.step_number = state_.simulation_step;
        ev.event_type = "memory_write";
        ev.process_id = static_cast<int>(pid);
        ev.virtual_address = static_cast<int>(va);
        ev.success = true;
        ev.explanation = "Wrote " + to_string(size) + " bytes to VA " + to_string(va) + " -> PA " + to_string(result.physical_address);
        record_event(std::move(ev));
        return true;
    }

    Event ev;

    ev.step_number = state_.simulation_step;
    ev.event_type = "memory_write";
    ev.process_id = static_cast<int>(pid);
    ev.virtual_address = static_cast<int>(va);
    ev.success = false;
    ev.explanation = "Write failed at VA " + to_string(va);
    record_event(std::move(ev));
    return false;
}

bool Engine::set_page_writable(ProcessId pid, VirtualAddress va, bool writable) {
    const auto process = state_.process_page_tables.find(pid);
    if (process == state_.process_page_tables.end()) return false;
    const VirtualAddress16 address(va, static_cast<uint16_t>(config_.page_size));
    auto& table = process->second;
    if (address.page_number >= table.size()) table.resize(address.page_number + 1);
    auto& page = table[address.page_number];
    page.valid = true;
    page.read_write = writable;
    state_.mmu->invalidate_tlb_entry(pid, address.page_number);
    Event event{};
    event.step_number = ++state_.simulation_step;
    event.event_type = "permission_changed";
    event.process_id = static_cast<int>(pid);
    event.virtual_address = va;
    event.page_number = address.page_number;
    event.explanation = writable ? "Page allows reads and writes" : "Page is read-only";
    record_event(std::move(event));
    return true;
}

bool Engine::set_allocation_policy(FitPolicy policy) {
    config_.fit_policy = policy;
    if (state_.heap) {
        state_.heap->set_policy(policy);
        return true;
    }
    return false;
}

bool Engine::set_replacement_policy(ReplacementAlgorithm algo) {
    config_.replacement_algo = algo;
    if (state_.paging) {
        state_.paging->set_algorithm(algo);
        return true;
    }
    return false;
}

EngineState Engine::get_state() const {
    EngineState result;
    result.config = config_;
    result.allocation_attempts = state_.allocation_attempts;
    result.total_allocations = state_.total_allocations;
    result.total_frees = state_.total_frees;
    result.active_allocations = state_.active_allocations;
    result.allocated_bytes = state_.allocated_bytes;
    result.next_process_id = state_.next_process_id;
    result.simulation_step = state_.simulation_step;

    // Copy process page tables
    result.process_page_tables = state_.process_page_tables;

    return result;
}

EngineSnapshot Engine::get_snapshot() const {
    EngineSnapshot snapshot;

    // Machine state
    snapshot.machine.config = config_;
    snapshot.machine.total_frames = state_.paging ? static_cast<size_t>(config_.ram_size_bytes / config_.page_size) : 0;
    snapshot.machine.page_size = config_.page_size;
    snapshot.machine.used_swap_slots = state_.swap ? state_.swap->get_used_slots() : 0;
    snapshot.machine.total_swap_slots = state_.swap ? state_.swap->get_capacity_pages() : 0;
    if (state_.heap) {
        auto hs = state_.heap->get_fragmentation_stats();
        snapshot.machine.heap_utilization_percent = static_cast<size_t>((static_cast<double>(hs.allocated_bytes) / static_cast<double>(hs.total_heap_size)) * 100.0);
    }

    // Heap state
    if (state_.heap) {
        snapshot.heap.stats = state_.heap->get_fragmentation_stats();
        snapshot.heap.stats.internal_fragmentation_bytes =
            snapshot.heap.stats.allocated_bytes - state_.allocated_bytes;
        snapshot.heap.allocation_attempts = state_.allocation_attempts;
        snapshot.heap.total_allocations = state_.total_allocations;
        snapshot.heap.total_frees = state_.total_frees;
        // Count actual blocks from the heap instead of hardcoding
        // We'll just use the counts from the stats
        snapshot.heap.total_blocks = snapshot.heap.stats.allocated_blocks_count + snapshot.heap.stats.free_blocks_count;
        snapshot.heap.free_blocks = snapshot.heap.stats.free_blocks_count;
        for (const auto& block : state_.heap->get_blocks()) {
            const ProcessId owner = block.free ? 0 : heap_allocations_.at(block.offset).owner;
            snapshot.heap.blocks.push_back({block.offset, block.size, owner});
        }
    }

    // MMU state
    if (state_.mmu) {
        snapshot.mmu.mmu_metrics = state_.mmu->get_metrics();
        snapshot.mmu.tlb_capacity = config_.tlb_capacity;
    }

    // Paging state
    if (state_.paging) {
        snapshot.paging.total_frames = snapshot.machine.total_frames;
        size_t resident = 0;
        if (state_.paging) {
            const auto& frames = state_.paging->get_frames();
            resident = count_if(frames.begin(), frames.end(), [](const PhysicalFrame& f) { return f.resident; });
        }
        snapshot.paging.resident_pages = resident;
        snapshot.paging.page_faults = state_.paging->get_metrics().page_faults;
        snapshot.paging.fifo_enabled = config_.replacement_algo == ReplacementAlgorithm::FIFO;
        snapshot.paging.lru_enabled = config_.replacement_algo == ReplacementAlgorithm::LRU;
    }

    // Swap state
    if (state_.swap) {
        snapshot.swap.total_swap_slots = state_.swap->get_capacity_pages();
        snapshot.swap.used_swap_slots = state_.swap->get_used_slots();
        snapshot.swap.page_outs = state_.swap->get_page_outs();
        snapshot.swap.page_ins = state_.swap->get_page_ins();
    }

    snapshot.process_page_tables = state_.process_page_tables;
    for (const auto& frame : state_.paging->get_frames()) {
        snapshot.frames.push_back({frame.resident, frame.pid, frame.page_number, frame.dirty});
    }
    snapshot.tlb_entries = state_.mmu->get_entries();
    snapshot.paging.replacements = state_.paging->get_replacements();
    // Recent events
    snapshot.recent_events = vector<Event>(event_history_.begin(), event_history_.end());

    return snapshot;
}

void Engine::record_event(Event event) {
    event.event_id = next_event_id_++;
    if (event_history_.size() == max_event_history_) {
        event_history_.erase(event_history_.begin());
    }
    event_history_.push_back(std::move(event));
}

const vector<Event>& Engine::get_recent_events() const {
    return event_history_;
}

void Engine::clear_events() {
    event_history_.clear();
}

EngineConfig Engine::get_config() const {
    return config_;
}
