#ifndef PAGING_H
#define PAGING_H

#include "types.h"
#include "swap.h"
#include <vector>
#include <list>

struct PhysicalFrame {
    bool resident = false;
    ProcessId pid = 0;
    PageNumber page_number = 0;
    bool dirty = false;
    uint64_t last_used = 0;
};

class PagingManager {
public:
    PagingManager(size_t total_frames, ReplacementAlgorithm algo, SwapManager& swap);

    PageFaultInfo handle_page_fault(ProcessId pid, PageNumber page_num, std::vector<PageTableEntry>& page_table);
    void access_page(FrameNumber frame_num, bool is_write);

    std::vector<PhysicalFrame>& get_frames() { return frames; }
    const std::vector<PhysicalFrame>& get_frames() const { return frames; }
    PerformanceMetrics get_metrics() const { return metrics; }

private:
    FrameNumber allocate_frame(ProcessId pid, PageNumber page_num, PageFaultInfo& fault_info, std::vector<PageTableEntry>& page_table);
    FrameNumber select_victim();

    size_t total_frames;
    ReplacementAlgorithm algo;
    SwapManager& swap;

    std::vector<PhysicalFrame> frames;
    std::list<FrameNumber> fifo_queue;
    uint64_t timer = 0;
    PerformanceMetrics metrics;
};

#endif