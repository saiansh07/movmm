#ifndef PAGING_H
#define PAGING_H

#include "types.h"
#include "swap.h"
#include <vector>
#include <list>
#include <unordered_map>

struct PhysicalFrame {
    bool resident = false;
    ProcessId pid = 0;
    PageNumber page_number = 0;
    bool dirty = false;
    uint64_t last_used = 0;
    // Store actual page data for read/write operations
    std::vector<uint8_t> data;
};

class PagingManager {
public:
    using PageTables = std::unordered_map<ProcessId, std::vector<PageTableEntry>>;

    PagingManager(size_t total_frames, ReplacementAlgorithm algo, SwapManager& swap, size_t page_size = 256);

    PageFaultInfo handle_page_fault(ProcessId pid, PageNumber page_num, std::vector<PageTableEntry>& page_table, PageTables* tables = nullptr);
    void release_process(ProcessId pid);
    void set_algorithm(ReplacementAlgorithm algorithm) { algo = algorithm; }
    size_t get_replacements() const { return replacements; }
    void access_page(FrameNumber frame_num, bool is_write);

    std::vector<PhysicalFrame>& get_frames() { return frames; }
    const std::vector<PhysicalFrame>& get_frames() const { return frames; }
    PerformanceMetrics get_metrics() const { return metrics; }

private:
    FrameNumber allocate_frame(ProcessId pid, PageNumber page_num, PageFaultInfo& fault, std::vector<PageTableEntry>& page_table, PageTables* tables);
    FrameNumber select_victim();

    size_t total_frames;
    ReplacementAlgorithm algo;
    SwapManager& swap;

    std::vector<PhysicalFrame> frames;
    std::list<FrameNumber> fifo_queue; // For FIFO replacement
    uint64_t timer = 0;
    size_t replacements = 0;
    PerformanceMetrics metrics;
};

#endif