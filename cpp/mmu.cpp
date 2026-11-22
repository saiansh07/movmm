#include "mmu.h"
#include <algorithm>

using namespace std;

MMU::MMU(size_t tlb_capacity, size_t page_size)
    : tlb_capacity(tlb_capacity), page_size(page_size) {
    tlb.resize(tlb_capacity);
}

MMU::Result MMU::translate(ProcessId pid, VirtualAddress va, const vector<PageTableEntry>& page_table) {
    Result res = {0, false, false, false};
    VirtualAddress16 addr(va, static_cast<uint16_t>(page_size));
    metrics.tlb_lookups++;
    timer++;

    for (auto& entry : tlb) {
        if (entry.valid && entry.pid == pid && entry.page_number == addr.page_number) {
            res.physical_address = (entry.frame_number * page_size) + addr.offset;
            res.tlb_hit = true;
            entry.last_accessed = timer;
            metrics.tlb_hits++;
            return res;
        }
    }

    metrics.tlb_misses++;
    if (addr.page_number >= page_table.size()) {
        res.protection_fault = true;
        return res;
    }
    const auto& pte = page_table[addr.page_number];
    if (!pte.present) {
        res.page_fault = true;
        metrics.page_faults++;
        return res;
    }
    res.physical_address = (pte.frame_number * page_size) + addr.offset;
    update_tlb(pid, addr.page_number, pte.frame_number, pte.dirty);
    return res;
}

void MMU::update_tlb(ProcessId pid, PageNumber pn, FrameNumber fn, bool dirty) {
    if (tlb.empty()) return;
    for (auto& entry : tlb) {
        if (entry.valid && entry.pid == pid && entry.page_number == pn) {
            entry.frame_number = fn;
            entry.dirty = dirty;
            entry.last_accessed = timer;
            return;
        }
    }
    size_t victim_idx = 0;
    uint64_t min_time = timer + 1;
    for (size_t i = 0; i < tlb.size(); ++i) {
        if (!tlb[i].valid) {
            victim_idx = i;
            break;
        }
        if (tlb[i].last_accessed < min_time) {
            min_time = tlb[i].last_accessed;
            victim_idx = i;
        }
    }
    tlb[victim_idx].valid = true;
    tlb[victim_idx].pid = pid;
    tlb[victim_idx].page_number = pn;
    tlb[victim_idx].frame_number = fn;
    tlb[victim_idx].last_accessed = timer;
    tlb[victim_idx].dirty = dirty;
}

void MMU::invalidate_tlb(ProcessId pid) {
    for (auto& entry : tlb) {
        if (entry.pid == pid) {
            entry.valid = false;
        }
    }
}

void MMU::invalidate_tlb_entry(ProcessId pid, PageNumber pn) {
    for (auto& entry : tlb) {
        if (entry.valid && entry.pid == pid && entry.page_number == pn) {
            entry.valid = false;
        }
    }
}
