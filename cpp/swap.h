#ifndef SWAP_H
#define SWAP_H

#include "types.h"
#include <fstream>
#include <string>
#include <vector>

class SwapManager {
public:
    SwapManager(const std::string& filename, size_t capacity_pages, size_t page_size);
    ~SwapManager();

    bool page_out(uint32_t slot, const void* data);
    bool page_in(uint32_t slot, void* data);

    uint32_t allocate_slot();
    void free_slot(uint32_t slot);

    size_t get_page_outs() const { return page_outs; }
    size_t get_page_ins() const { return page_ins; }
    size_t get_used_slots() const { return used_slots; }
    size_t get_capacity_pages() const { return capacity_pages; }

private:
    std::string filename;
    size_t capacity_pages;
    size_t page_size;
    std::fstream file;

    std::vector<bool> slot_bitmap;
    size_t used_slots = 0;
    size_t page_outs = 0;
    size_t page_ins = 0;
};

#endif