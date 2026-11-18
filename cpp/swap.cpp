#include "swap.h"
#include <iostream>

using namespace std;

SwapManager::SwapManager(const string& filename, size_t capacity_pages, size_t page_size)
    : filename(filename), capacity_pages(capacity_pages), page_size(page_size), slot_bitmap(capacity_pages, false) {

    file.open(filename, ios::in | ios::out | ios::binary | ios::trunc);
    if (!file.is_open()) {
        cerr << "Failed to open swap file: " << filename << endl;
        return;
    }

    vector<char> zero_page(page_size, 0);
    for (size_t i = 0; i < capacity_pages; ++i) {
        file.write(zero_page.data(), page_size);
    }
    file.flush();
}

SwapManager::~SwapManager() {
    if (file.is_open()) {
        file.close();
    }
}

bool SwapManager::page_out(uint32_t slot, const void* data) {
    if (slot >= capacity_pages || !file.is_open()) return false;

    file.seekp(slot * page_size);
    file.write(reinterpret_cast<const char*>(data), page_size);

    if (file.fail()) {
        file.clear();
        return false;
    }

    page_outs++;
    return true;
}

bool SwapManager::page_in(uint32_t slot, void* data) {
    if (slot >= capacity_pages || !file.is_open()) return false;

    file.seekg(slot * page_size);
    file.read(reinterpret_cast<char*>(data), page_size);

    if (file.fail()) {
        file.clear();
        return false;
    }

    page_ins++;
    return true;
}

uint32_t SwapManager::allocate_slot() {
    for (size_t i = 0; i < capacity_pages; ++i) {
        if (!slot_bitmap[i]) {
            slot_bitmap[i] = true;
            used_slots++;
            return static_cast<uint32_t>(i);
        }
    }
    return 0xFFFFFFFF;
}

void SwapManager::free_slot(uint32_t slot) {
    if (slot < capacity_pages && slot_bitmap[slot]) {
        slot_bitmap[slot] = false;
        used_slots--;
    }
}
