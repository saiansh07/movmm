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
    config_ = config;

    // 1. Initialize Swap Manager first (dependencies for paging)
    state_.swap = make_unique<SwapManager>("swap.bin", config_.swap_capacity_pages, config_.page_size);

    // 2. Initialize Heap Allocator
    state_.heap = make_unique<HeapAllocator>(4096, config_.fit_policy);

    // 3. Initialize MMU with TLB
    state_.mmu = make_unique<MMU>(config_.tlb_capacity, config_.page_size);

    // 4. Initialize Paging Manager
    size_t ram_pages = config_.ram_size_bytes / config_.page_size;
    state_.paging = make_unique<PagingManager>(ram_pages, config_.replacement_algo, *state_.swap);

    state_.simulation_step = 0;
    state_.next_process_id = 1;
    state_.total_allocations = 0;
    state_.total_frees = 0;
    state_.active_allocations = 0;
    state_.allocated_bytes = 0;

    event_history_.clear();

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
    ev.event_id = event_history_.size() + 1;
    ev.step_number = state_.simulation_step;
    ev.event_type = "process_created";
    ev.process_id = static_cast<int>(pid);
    ev.success = true;
    ev.explanation = "Process created with ID " + to_string(pid);
    event_history_.push_back(ev);
    cerr << "Process created: PID " << pid << endl;
    return pid;
}

bool Engine::terminate_process(ProcessId pid) {
    if (pid == 0) {
        Event ev;
        ev.event_id = event_history_.size() + 1;
        ev.step_number = state_.simulation_step + 1;
        ev.event_type = "process_terminated";
        ev.process_id = 0;
        ev.success = false;
        ev.explanation = "Cannot terminate process ID 0";
        event_history_.push_back(ev);
        return false;
    }

    // Validate process exists
    if (state_.process_page_tables.find(pid) == state_.process_page_tables.end()) {
        Event ev;
        ev.event_id = event_history_.size() + 1;
        ev.step_number = state_.simulation_step + 1;
        ev.event_type = "process_terminated";
        ev.process_id = static_cast<int>(pid);
        ev.success = false;
        ev.explanation = "Process not found: PID " + to_string(pid);
        event_history_.push_back(ev);
        return false;
    }

    // Release all resources owned by this process
    // 1. Invalidate TLB entries for this process
    if (state_.mmu) {
        state_.mmu->invalidate_tlb(pid);
    }

    // 2. Free frames belonging to this process (evict and write back to swap if dirty)
    if (state_.paging) {
        auto frames = state_.paging->get_frames();
        for (size_t i = 0; i < frames.size(); ++i) {
            if (frames[i].resident && frames[i].pid == pid) {
                // This frame belongs to the process being terminated
                // Evict it - if dirty, write back to swap
                if (frames[i].dirty) {
                    uint32_t slot = state_.swap->allocate_slot();
                    state_.swap->page_out(slot, nullptr); // In a real impl, we'd pass actual frame data
                    // Note: this is a simplification - actual frame data storage would be needed
                }
                // Clear the frame
                frames[i].resident = false;
                frames[i].pid = 0;
                frames[i].page_number = 0;
                frames[i].dirty = false;
                frames[i].last_used = 0;
            }
        }
    }

    // 3. Remove page table for this process
    state_.process_page_tables.erase(pid);

    state_.simulation_step++;
    Event ev;
    ev.event_id = event_history_.size() + 1;
    ev.step_number = state_.simulation_step;
    ev.event_type = "process_terminated";
    ev.process_id = static_cast<int>(pid);
    ev.success = true;
    ev.explanation = "Process terminated: PID " + to_string(pid);
    event_history_.push_back(ev);
    cerr << "Process terminated: PID " << pid << endl;
    return true;
}

bool Engine::allocate_memory(ProcessId pid, size_t size) {
    if (size == 0) {
        Event ev;
        ev.event_id = event_history_.size() + 1;
        ev.step_number = state_.simulation_step + 1;
        ev.event_type = "memory_allocated";
        ev.process_id = static_cast<int>(pid);
        ev.success = false;
        ev.explanation = "Allocation size cannot be zero";
        event_history_.push_back(ev);
        return false;
    }

    // Validate process exists
    if (state_.process_page_tables.find(pid) == state_.process_page_tables.end()) {
        Event ev;
        ev.event_id = event_history_.size() + 1;
        ev.step_number = state_.simulation_step + 1;
        ev.event_type = "memory_allocated";
        ev.process_id = static_cast<int>(pid);
        ev.success = false;
        ev.explanation = "Process not found: PID " + to_string(pid);
        event_history_.push_back(ev);
        return false;
    }

    state_.simulation_step++;
    void* ptr = state_.heap->my_malloc(size);

    bool success = (ptr != nullptr);

    Event ev;
    ev.event_id = event_history_.size() + 1;
    ev.step_number = state_.simulation_step;
    ev.event_type = "memory_allocated";
    ev.process_id = static_cast<int>(pid);
    ev.virtual_address = success ? static_cast<int>(reinterpret_cast<uintptr_t>(ptr)) : 0;
    ev.success = success;

    if (success) {
        state_.total_allocations++;
        state_.active_allocations++;
        state_.allocated_bytes += size;
        ev.explanation = "Allocated " + to_string(size) + " bytes at address " + to_string(reinterpret_cast<uintptr_t>(ptr));
    } else {
        ev.explanation = "Allocation failed: out of memory (" + to_string(size) + " bytes requested)";
    }
    event_history_.push_back(ev);

    cerr << (success ? "Allocated" : "Allocation failed") << ": " << ev.explanation << endl;
    return success;
}

bool Engine::free_memory(ProcessId pid, void* ptr) {
    if (!ptr) {
        Event ev;
        ev.event_id = event_history_.size() + 1;
        ev.step_number = state_.simulation_step + 1;
        ev.event_type = "memory_freed";
        ev.process_id = static_cast<int>(pid);
        ev.success = false;
        ev.explanation = "Cannot free null pointer";
        event_history_.push_back(ev);
        return false;
    }

    // Validate process exists
    if (state_.process_page_tables.find(pid) == state_.process_page_tables.end()) {
        Event ev;
        ev.event_id = event_history_.size() + 1;
        ev.step_number = state_.simulation_step + 1;
        ev.event_type = "memory_freed";
        ev.process_id = static_cast<int>(pid);
        ev.success = false;
        ev.explanation = "Process not found: PID " + to_string(pid);
        event_history_.push_back(ev);
        return false;
    }

    state_.simulation_step++;
    state_.heap->my_free(ptr);

    Event ev;
    ev.event_id = event_history_.size() + 1;
    ev.step_number = state_.simulation_step;
    ev.event_type = "memory_freed";
    ev.process_id = static_cast<int>(pid);
    ev.success = true;
    ev.explanation = "Freed memory at address " + to_string(reinterpret_cast<uintptr_t>(ptr));
    event_history_.push_back(ev);

    if (state_.active_allocations > 0) state_.active_allocations--;
    cerr << "Freed memory at address " << reinterpret_cast<uintptr_t>(ptr) << endl;
    return true;
}

Engine::TranslationResult Engine::translate_address(ProcessId pid, VirtualAddress va) {
    TranslationResult result = {0, false, false, false};

    // Validate process exists
    auto it = state_.process_page_tables.find(pid);
    if (it == state_.process_page_tables.end()) {
        result.page_fault = true;
        result.protection_fault = true;

        Event ev;
        ev.event_id = event_history_.size() + 1;
        ev.step_number = state_.simulation_step + 1;
        ev.event_type = "address_translated";
        ev.process_id = static_cast<int>(pid);
        ev.virtual_address = static_cast<int>(va);
        ev.success = false;
        ev.explanation = "Translation failed: process not found: PID " + to_string(pid);
        event_history_.push_back(ev);

        state_.simulation_step++;
        return result;
    }

    state_.simulation_step++;

    // Get the process's page table
    auto& page_table = it->second;

    // Use MMU to translate with the process's page table
    MMU::Result mmu_result = state_.mmu->translate(pid, va, page_table);
    result.physical_address = mmu_result.physical_address;
    result.tlb_hit = mmu_result.tlb_hit;
    result.page_fault = mmu_result.page_fault;
    result.protection_fault = mmu_result.protection_fault;

    // Handle page fault if occurred
    if (result.page_fault) {
        // The virtual address decomposition
        VirtualAddress16 addr(va, static_cast<uint16_t>(config_.page_size));
        PageNumber page_num = addr.page_number;

        // Ensure page table has enough entries
        if (page_num >= page_table.size()) {
            page_table.resize(page_num + 1);
        }

        // Handle page fault through paging manager
        PageFaultInfo fault = state_.paging->handle_page_fault(pid, page_num, page_table);

        // If a frame was allocated, update result
        if (fault.victim_selected) {
            // Frame was evicted - handle dirty writeback
            // (handled in paging manager)
        }

        // Page table entry updated by handle_page_fault
        // Retry translation
        MMU::Result mmu_result2 = state_.mmu->translate(pid, va, page_table);
        result.physical_address = mmu_result2.physical_address;
        result.tlb_hit = mmu_result2.tlb_hit;
        result.page_fault = mmu_result2.page_fault;
        result.protection_fault = mmu_result2.protection_fault;

        Event ev;
        ev.event_id = event_history_.size() + 1;
        ev.step_number = state_.simulation_step;
        ev.event_type = "page_fault";
        ev.process_id = static_cast<int>(pid);
        ev.virtual_address = static_cast<int>(va);
        ev.page_number = static_cast<int>(page_num);
        ev.success = !fault.victim_selected || fault.allocated_frame != 0;
        ev.explanation = "Page fault handled for page " + to_string(page_num);
        event_history_.push_back(ev);
    }

    Event ev;
    ev.event_id = event_history_.size() + 1;
    ev.step_number = state_.simulation_step;
    ev.event_type = "address_translated";
    ev.process_id = static_cast<int>(pid);
    ev.virtual_address = static_cast<int>(va);
    ev.success = true;
    ev.explanation = "Virtual address " + to_string(va) + " translated to physical " + to_string(result.physical_address);
    event_history_.push_back(ev);

    cerr << "Translated VA " << hex << va << " -> PA " << dec << result.physical_address
         << " (TLB hit: " << (result.tlb_hit ? "yes" : "no") << ")" << endl;
    return result;
}

bool Engine::read_memory(ProcessId pid, VirtualAddress va, void* buffer, size_t size) {
    TranslationResult result = translate_address(pid, va);

    if (!result.page_fault && result.physical_address != 0) {
        // Simulate reading from physical memory at the computed address
        // For simplicity, we'll generate deterministic data based on address and process
        if (buffer && size > 0) {
            uint8_t* buf = static_cast<uint8_t*>(buffer);
            for (size_t i = 0; i < size; ++i) {
                buf[i] = static_cast<uint8_t>((result.physical_address + pid + i) & 0xFF);
            }
        }

        Event ev;
        ev.event_id = event_history_.size() + 1;
        ev.step_number = state_.simulation_step;
        ev.event_type = "memory_read";
        ev.process_id = static_cast<int>(pid);
        ev.virtual_address = static_cast<int>(va);
        ev.success = true;
        ev.explanation = "Read " + to_string(size) + " bytes from VA " + to_string(va) + " -> PA " + to_string(result.physical_address);
        event_history_.push_back(ev);
        return true;
    }

    Event ev;
    ev.event_id = event_history_.size() + 1;
    ev.step_number = state_.simulation_step;
    ev.event_type = "memory_read";
    ev.process_id = static_cast<int>(pid);
    ev.virtual_address = static_cast<int>(va);
    ev.success = false;
    ev.explanation = "Read failed: page fault on VA " + to_string(va);
    event_history_.push_back(ev);
    return false;
}

bool Engine::write_memory(ProcessId pid, VirtualAddress va, const void* data, size_t size) {
    TranslationResult result = translate_address(pid, va);

    if (!result.page_fault && result.physical_address != 0) {
        // Simulate writing to physical memory
        // Mark the page as dirty in the page table
        VirtualAddress16 addr(va, static_cast<uint16_t>(config_.page_size));
        PageNumber page_num = addr.page_number;

        auto it = state_.process_page_tables.find(pid);
        if (it != state_.process_page_tables.end() && page_num < it->second.size()) {
            it->second[page_num].dirty = true;
        }

        Event ev;
        ev.event_id = event_history_.size() + 1;
        ev.step_number = state_.simulation_step;
        ev.event_type = "memory_write";
        ev.process_id = static_cast<int>(pid);
        ev.virtual_address = static_cast<int>(va);
        ev.success = true;
        ev.explanation = "Wrote " + to_string(size) + " bytes to VA " + to_string(va) + " -> PA " + to_string(result.physical_address);
        event_history_.push_back(ev);
        return true;
    }

    Event ev;
    ev.event_id = event_history_.size() + 1;
    ev.step_number = state_.simulation_step;
    ev.event_type = "memory_write";
    ev.process_id = static_cast<int>(pid);
    ev.virtual_address = static_cast<int>(va);
    ev.success = false;
    ev.explanation = "Write failed: page fault on VA " + to_string(va);
    event_history_.push_back(ev);
    return false;
}

bool Engine::set_allocation_policy(FitPolicy policy) {
    config_.fit_policy = policy;
    if (state_.heap) {
        state_.heap = make_unique<HeapAllocator>(4096, policy);
        return true;
    }
    return false;
}

bool Engine::set_replacement_policy(ReplacementAlgorithm algo) {
    config_.replacement_algo = algo;
    if (state_.paging) {
        size_t ram_pages = config_.ram_size_bytes / config_.page_size;
        state_.paging = make_unique<PagingManager>(ram_pages, algo, *state_.swap);
        return true;
    }
    return false;
}

EngineState Engine::get_state() const {
    EngineState result;
    result.config = config_;
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
        // Count actual blocks from the heap instead of hardcoding
        // We'll just use the counts from the stats
        snapshot.heap.total_blocks = snapshot.heap.stats.allocated_blocks_count + snapshot.heap.stats.free_blocks_count;
        snapshot.heap.free_blocks = snapshot.heap.stats.free_blocks_count;
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
            auto frames = state_.paging->get_frames();
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

    // Recent events
    snapshot.recent_events = vector<Event>(event_history_.begin(), event_history_.end());

    return snapshot;
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