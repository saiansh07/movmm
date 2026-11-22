#ifndef MMU_H
#define MMU_H

#include "types.h"
#include <vector>
#include <unordered_map>

class MMU {
public:
    MMU(size_t tlb_capacity, size_t page_size);

    struct Result {
        PhysicalAddress physical_address;
        bool tlb_hit;
        bool page_fault;
        bool protection_fault;
    };

    Result translate(ProcessId pid, VirtualAddress va, const std::vector<PageTableEntry>& page_table);
    void update_tlb(ProcessId pid, PageNumber pn, FrameNumber fn, bool dirty);
    void invalidate_tlb(ProcessId pid);
    void invalidate_tlb_entry(ProcessId pid, PageNumber pn);

    const std::vector<TLBEntry>& get_entries() const { return tlb; }
    PerformanceMetrics get_metrics() const { return metrics; }

private:
    size_t tlb_capacity;
    size_t page_size;
    std::vector<TLBEntry> tlb;
    uint64_t timer = 0;
    PerformanceMetrics metrics;
};

#endif
