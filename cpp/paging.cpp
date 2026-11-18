#include "paging.h"

using namespace std;

PagingManager::PagingManager(size_t total_frames, ReplacementAlgorithm algo, SwapManager& swap)
    : total_frames(total_frames), algo(algo), swap(swap) {
    frames.resize(total_frames);
    // Initialize each frame's data storage
    for (auto& frame : frames) {
        frame.data.resize(256); // Default page size, will be resized if needed
    }
}

PageFaultInfo PagingManager::handle_page_fault(ProcessId pid, PageNumber page_num, vector<PageTableEntry>& page_table) {
    PageFaultInfo fault;
    fault.occurred = true;
    fault.fault_page = page_num;
    timer++;
    metrics.page_faults++;

    FrameNumber fn = allocate_frame(pid, page_num, fault, page_table);

    // Update page table entry for the faulted page
    page_table[page_num].present = true;
    page_table[page_num].frame_number = fn;
    page_table[page_num].valid = true;

    return fault;
}

FrameNumber PagingManager::allocate_frame(ProcessId pid, PageNumber page_num, PageFaultInfo& fault, vector<PageTableEntry>& page_table) {
    FrameNumber fn = 0;
    bool found = false;

    for (size_t i = 0; i < frames.size(); ++i) {
        if (!frames[i].resident) {
            fn = static_cast<FrameNumber>(i);
            found = true;
            break;
        }
    }

    if (!found) {
        // Need replacement - evict a victim
        fn = select_victim();
        PhysicalFrame& victim = frames[fn];

        // Invalidate the victim's page table entry
        if (victim.page_number != 0 && victim.page_number != page_num && victim.page_number < page_table.size()) {
            page_table[victim.page_number].present = false;
            page_table[victim.page_number].valid = false;
            page_table[victim.page_number].frame_number = 0;
            fault.victim_selected = true;
            fault.victim_frame = fn;
            fault.evicted_page = victim.page_number;
        } else {
            fault.victim_selected = false;
            fault.victim_frame = fn;
            fault.evicted_page = 0;
        }

        // Write dirty page to swap
        if (victim.dirty) {
            uint32_t slot = swap.allocate_slot();
            // Write actual frame data to swap
            swap.page_out(slot, victim.data.data());
            fault.dirty_writeback = true;
            fault.swap_slot_used = slot;
            metrics.page_outs++;
        }
    }

    // Allocate the frame
    frames[fn].resident = true;
    frames[fn].pid = pid;
    frames[fn].page_number = page_num;
    frames[fn].dirty = false;
    frames[fn].last_used = timer;
    // Initialize/clear data for new page
    frames[fn].data.assign(frames[fn].data.size(), 0);
    fifo_queue.push_back(fn);

    return fn;
}

FrameNumber PagingManager::select_victim() {
    if (algo == ReplacementAlgorithm::FIFO) {
        while (!fifo_queue.empty()) {
            FrameNumber victim = fifo_queue.front();
            fifo_queue.pop_front();
            if (frames[victim].resident) {
                return victim;
            }
        }
        return 0;
    } else {
        FrameNumber victim = 0;
        uint64_t oldest = timer + 1;
        for (size_t i = 0; i < frames.size(); ++i) {
            if (frames[i].resident && frames[i].last_used < oldest) {
                oldest = frames[i].last_used;
                victim = static_cast<FrameNumber>(i);
            }
        }
        return victim;
    }
}