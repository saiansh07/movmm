#ifndef ENGINE_H
#define ENGINE_H

#include "types.h"
#include "heap_allocator.h"
#include "mmu.h"
#include "paging.h"
#include "swap.h"
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <cstdint>
#include <unordered_map>

using namespace std;

struct EngineConfig {
    size_t ram_size_bytes = 4096;
    size_t page_size = 256;
    size_t tlb_capacity = 4;
    size_t swap_capacity_pages = 64;
    FitPolicy fit_policy = FitPolicy::FIRST_FIT;
    ReplacementAlgorithm replacement_algo = ReplacementAlgorithm::FIFO;
};

struct Event {
    size_t event_id;
    size_t step_number;
    std::string event_type;
    int process_id = 0;
    int virtual_address = 0;
    int page_number = 0;
    int frame_number = 0;
    uint32_t swap_slot = 0;
    bool success = true;
    std::string explanation;
    std::map<std::string, std::string> metadata;
};

struct EngineState {
    EngineConfig config;
    std::unique_ptr<HeapAllocator> heap = nullptr;
    std::unique_ptr<MMU> mmu = nullptr;
    std::unique_ptr<PagingManager> paging = nullptr;
    std::unique_ptr<SwapManager> swap = nullptr;
    size_t next_process_id = 1;
    size_t simulation_step = 0;
    uint64_t total_allocations = 0;
    uint64_t total_frees = 0;
    size_t active_allocations = 0;
    size_t allocated_bytes = 0;

    // Process registry
    std::unordered_map<ProcessId, std::vector<PageTableEntry>> process_page_tables;

    bool initialized = false;
};

struct EngineSnapshot {
    struct SnapshotMachine {
        EngineConfig config;
        size_t total_frames = 0;
        size_t available_frames = 0;
        size_t page_size = 0;
        size_t used_swap_slots = 0;
        size_t total_swap_slots = 0;
        size_t heap_utilization_percent = 0;
    } machine;

    struct SnapshotHeap {
        FragmentationStats stats;
        size_t total_blocks = 0;
        size_t free_blocks = 0;
    } heap;

    struct SnapshotMMU {
        PerformanceMetrics mmu_metrics;
        size_t tlb_capacity = 0;
    } mmu;

    struct SnapshotPaging {
        size_t total_frames = 0;
        size_t resident_pages = 0;
        size_t page_faults = 0;
        size_t replacements = 0;
        bool fifo_enabled = false;
        bool lru_enabled = false;
    } paging;

    struct SnapshotSwap {
        size_t total_swap_slots = 0;
        size_t used_swap_slots = 0;
        size_t page_outs = 0;
        size_t page_ins = 0;
        size_t total_bytes_swapped = 0;
    } swap;

    PagingManager::PageTables process_page_tables;
    struct SnapshotFrame {
        bool resident;
        ProcessId pid;
        PageNumber page_number;
        bool dirty;
    };
    std::vector<SnapshotFrame> frames;
    std::vector<TLBEntry> tlb_entries;
    std::vector<Event> recent_events;
};

class Engine {
public:
    Engine();
    explicit Engine(const EngineConfig& config);

    bool initialize(const EngineConfig& config);
    void reset();

    ProcessId create_process();
    bool terminate_process(ProcessId pid);

    bool allocate_memory(ProcessId pid, size_t size);
    bool free_memory(ProcessId pid, void* ptr);

    struct TranslationResult {
        PhysicalAddress physical_address = 0;
        bool tlb_hit = false;
        bool page_fault = false;
        bool protection_fault = false;
    };
    TranslationResult translate_address(ProcessId pid, VirtualAddress va);

    bool read_memory(ProcessId pid, VirtualAddress va, void* buffer, size_t size);
    bool write_memory(ProcessId pid, VirtualAddress va, const void* data, size_t size);

    bool set_allocation_policy(FitPolicy policy);
    bool set_replacement_policy(ReplacementAlgorithm algo);

    EngineState get_state() const;
    EngineSnapshot get_snapshot() const;

    const std::vector<Event>& get_recent_events() const;
    void clear_events();

    EngineConfig get_config() const;
    bool is_initialized() const;

private:
    EngineConfig config_;
    EngineState state_;
    std::vector<Event> event_history_;
    size_t max_event_history_ = 1000;
};

#endif
