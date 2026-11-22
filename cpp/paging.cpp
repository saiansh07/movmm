#include "paging.h"
#include <algorithm>
#include <stdexcept>

using namespace std;

PagingManager::PagingManager(size_t total_frames, ReplacementAlgorithm algo, SwapManager& swap, size_t page_size)
    : total_frames(total_frames), algo(algo), swap(swap), frames(total_frames) {
    for (auto& frame : frames) {
        frame.data.resize(page_size);
    }
}

PageFaultInfo PagingManager::handle_page_fault(ProcessId pid, PageNumber page_num,
                                              vector<PageTableEntry>& page_table, PageTables* tables) {
    PageFaultInfo fault;
    fault.occurred = true;
    fault.fault_page = page_num;
    metrics.page_faults++;
    fault.allocated_frame = allocate_frame(pid, page_num, fault, page_table, tables);

    auto& entry = page_table[page_num];
    entry.present = true;
    entry.frame_number = fault.allocated_frame;
    entry.valid = true;
    return fault;
}

FrameNumber PagingManager::allocate_frame(ProcessId pid, PageNumber page_num, PageFaultInfo& fault,
                                          vector<PageTableEntry>& page_table, PageTables* tables) {
    if (frames.empty()) throw runtime_error("No physical frames configured");
    size_t index = 0;
    while (index < frames.size() && frames[index].resident) ++index;
    if (index == frames.size()) index = select_victim();
    auto fn = static_cast<FrameNumber>(index);
    auto& frame = frames[fn];
    auto& incoming = page_table[page_num];
    vector<uint8_t> data(frame.data.size(), 0);

    if (incoming.swap_slot != NO_SWAP_SLOT) {
        if (!swap.page_in(incoming.swap_slot, data.data())) {
            throw runtime_error("Could not read page from swap");
        }
        fault.page_in = true;
    }

    if (frame.resident) {
        PageTableEntry* victim = nullptr;
        if (tables) {
            victim = &tables->at(frame.pid).at(frame.page_number);
        } else if (frame.pid == pid && frame.page_number < page_table.size()) {
            victim = &page_table[frame.page_number];
        }

        if (frame.dirty) {
            auto slot = victim ? victim->swap_slot : NO_SWAP_SLOT;
            const bool new_slot = slot == NO_SWAP_SLOT;
            if (new_slot) slot = swap.allocate_slot();
            if (slot == NO_SWAP_SLOT) throw runtime_error("Swap space is full");
            if (!swap.page_out(slot, frame.data.data())) {
                if (new_slot) swap.free_slot(slot);
                throw runtime_error("Could not write page to swap");
            }
            if (victim) victim->swap_slot = slot;
            fault.dirty_writeback = true;
            fault.swap_slot_used = slot;
        }

        if (victim) {
            victim->present = false;
            victim->referenced = false;
            victim->dirty = false;
        }

        fault.victim_selected = true;
        fault.victim_frame = fn;
        fault.evicted_pid = frame.pid;
        fault.evicted_page = frame.page_number;
        ++replacements;
        fifo_queue.remove(fn);
    }

    frame.resident = true;
    frame.pid = pid;
    frame.page_number = page_num;
    frame.dirty = false;
    frame.last_used = ++timer;
    frame.data = std::move(data);
    fifo_queue.push_back(fn);
    return fn;
}

bool PagingManager::used_before(FrameNumber left, FrameNumber right) const {
    const auto left_use = frames[left].last_used;
    const auto right_use = frames[right].last_used;
    return left_use < right_use || (left_use == right_use && left < right);
}

std::vector<FrameNumber> PagingManager::eviction_order(ReplacementAlgorithm algorithm) const {
    std::vector<FrameNumber> order(fifo_queue.begin(), fifo_queue.end());
    if (algorithm == ReplacementAlgorithm::LRU) {
        std::sort(order.begin(), order.end(), [this](FrameNumber left, FrameNumber right) {
            return used_before(left, right);
        });
    }
    return order;
}

FrameNumber PagingManager::select_victim() const {
    if (algo == ReplacementAlgorithm::FIFO) return fifo_queue.front();
    return *std::min_element(fifo_queue.begin(), fifo_queue.end(), [this](FrameNumber left, FrameNumber right) {
        return used_before(left, right);
    });
}

void PagingManager::access_page(FrameNumber frame_num, bool is_write) {
    auto& frame = frames.at(frame_num);
    frame.last_used = ++timer;
    frame.dirty = frame.dirty || is_write;
}

void PagingManager::release_process(ProcessId pid) {
    for (size_t i = 0; i < frames.size(); ++i) {
        auto& frame = frames[i];
        if (frame.resident && frame.pid == pid) {
            frame.resident = false;
            fifo_queue.remove(static_cast<FrameNumber>(i));
        }
    }
}
