#ifndef TYPES_H
#define TYPES_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

// 16-bit Virtual Address & Physical Memory types
using VirtualAddress = uint16_t;
using PhysicalAddress = uint32_t;
using PageNumber = uint16_t;
using FrameNumber = uint16_t;
using ProcessId = uint32_t;

enum class FitPolicy {
    FIRST_FIT,
    BEST_FIT
};

enum class ReplacementAlgorithm {
    FIFO,
    LRU
};

struct VirtualAddress16 {
    VirtualAddress raw;
    PageNumber page_number;
    uint16_t offset;

    explicit VirtualAddress16(VirtualAddress va, uint16_t page_size = 256) {
        raw = va;
        uint16_t mask = page_size - 1;
        offset = va & mask;
        int shift = 0;
        uint16_t ps = page_size;
        while (ps > 1) {
            shift++;
            ps >>= 1;
        }
        page_number = va >> shift;
    }
};

struct PageTableEntry {
    bool valid = false;
    FrameNumber frame_number = 0;
    bool read_write = true;
    bool present = false;
    bool referenced = false;
    bool dirty = false;
    uint32_t swap_slot = 0;
};

struct TLBEntry {
    bool valid = false;
    ProcessId pid = 0;
    PageNumber page_number = 0;
    FrameNumber frame_number = 0;
    uint64_t last_accessed = 0;
    bool dirty = false;
};

struct AllocationBlock {
    size_t offset;
    size_t size;
    bool free;
    AllocationBlock* next;
    AllocationBlock* prev;
};

struct FragmentationStats {
    size_t total_heap_size = 0;
    size_t allocated_bytes = 0;
    size_t free_bytes = 0;
    size_t external_fragmentation_bytes = 0;
    size_t internal_fragmentation_bytes = 0;
    size_t free_blocks_count = 0;
    size_t allocated_blocks_count = 0;
};

struct PageFaultInfo {
    bool occurred = false;
    PageNumber fault_page = 0;
    FrameNumber allocated_frame = 0;
    bool victim_selected = false;
    FrameNumber victim_frame = 0;
    bool dirty_writeback = false;
    uint32_t swap_slot_used = 0;
    PageNumber evicted_page = 0;
};

struct SimulationConfig {
    size_t ram_size_bytes = 4096;
    size_t page_size = 256;
    size_t tlb_capacity = 4;
    size_t swap_capacity_pages = 64;
    FitPolicy fit_policy = FitPolicy::FIRST_FIT;
    ReplacementAlgorithm replacement_algo = ReplacementAlgorithm::FIFO;
};

struct PerformanceMetrics {
    uint64_t tlb_lookups = 0;
    uint64_t tlb_hits = 0;
    uint64_t tlb_misses = 0;
    uint64_t page_faults = 0;
    uint64_t page_ins = 0;
    uint64_t page_outs = 0;
    uint64_t allocations = 0;
    uint64_t frees = 0;

    double tlb_hit_rate() const {
        if (tlb_lookups == 0) return 0.0;
        return static_cast<double>(tlb_hits) / tlb_lookups * 100.0;
    }
};

#endif