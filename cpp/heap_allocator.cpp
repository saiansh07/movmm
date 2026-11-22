#include "heap_allocator.h"
#include <algorithm>
#include <cstring>

using namespace std;

HeapAllocator::HeapAllocator(size_t heap_size, FitPolicy policy)
    : total_size(heap_size), policy(policy), allocated_bytes(0) {
    heap_start = ::operator new(heap_size);

    head = new AllocationBlock();
    head->offset = 0;
    head->size = heap_size;
    head->free = true;
    head->next = nullptr;
    head->prev = nullptr;
}

HeapAllocator::~HeapAllocator() {
    AllocationBlock* current = head;
    while (current) {
        AllocationBlock* next = current->next;
        delete current;
        current = next;
    }
    ::operator delete(heap_start);
}

void* HeapAllocator::my_malloc(size_t size) {
    if (size == 0) return nullptr;

    AllocationBlock* best_block = nullptr;
    AllocationBlock* current = head;

    while (current) {
        if (current->free && current->size >= size) {
            if (policy == FitPolicy::FIRST_FIT) {
                best_block = current;
                break;
            } else if (policy == FitPolicy::BEST_FIT) {
                if (!best_block || current->size < best_block->size) {
                    best_block = current;
                }
            }
        }
        current = current->next;
    }

    if (!best_block) return nullptr;

    split_block(best_block, size);
    best_block->free = false;
    allocated_bytes += best_block->size;

    return static_cast<char*>(heap_start) + best_block->offset;
}

void HeapAllocator::my_free(void* ptr) {
    if (auto offset = offset_of(ptr)) free_at(*offset);
}

optional<size_t> HeapAllocator::offset_of(const void* ptr) const {
    const auto address = reinterpret_cast<uintptr_t>(ptr);
    const auto base = reinterpret_cast<uintptr_t>(heap_start);
    if (address < base || address - base >= total_size) return nullopt;
    return address - base;
}

void HeapAllocator::free_at(size_t offset) {
    AllocationBlock* current = head;

    while (current) {
        if (current->offset == offset) {
            if (current->free) {
                return;
            }
            current->free = true;
            allocated_bytes -= current->size;
            coalesce();
            return;
        }
        current = current->next;
    }
}

vector<HeapAllocator::Block> HeapAllocator::get_blocks() const {
    vector<Block> blocks;
    for (const AllocationBlock* block = head; block; block = block->next) {
        blocks.push_back({block->offset, block->size, block->free});
    }
    return blocks;
}

void HeapAllocator::split_block(AllocationBlock* block, size_t size) {
    const size_t min_split_size = 8;

    if (block->size >= size + min_split_size) {
        AllocationBlock* new_block = new AllocationBlock();
        new_block->offset = block->offset + size;
        new_block->size = block->size - size;
        new_block->free = true;
        new_block->next = block->next;
        new_block->prev = block;

        if (block->next) {
            block->next->prev = new_block;
        }
        block->next = new_block;
        block->size = size;
    }
}

void HeapAllocator::coalesce() {
    AllocationBlock* current = head;
    while (current && current->next) {
        if (current->free && current->next->free) {
            AllocationBlock* to_merge = current->next;
            current->size += to_merge->size;
            current->next = to_merge->next;
            if (to_merge->next) {
                to_merge->next->prev = current;
            }
            delete to_merge;
        } else {
            current = current->next;
        }
    }
}

FragmentationStats HeapAllocator::get_fragmentation_stats() const {
    FragmentationStats stats = {0};
    stats.total_heap_size = total_size;
    stats.allocated_bytes = allocated_bytes;
    stats.free_bytes = total_size - allocated_bytes;

    size_t free_blocks = 0;
    size_t allocated_blocks = 0;
    size_t largest_free_block = 0;

    AllocationBlock* current = head;
    while (current) {
        if (current->free) {
            free_blocks++;
            largest_free_block = max(largest_free_block, current->size);
        } else {
            allocated_blocks++;
        }
        current = current->next;
    }

    stats.free_blocks_count = free_blocks;
    stats.allocated_blocks_count = allocated_blocks;
    stats.external_fragmentation_bytes = stats.free_bytes - largest_free_block;

    return stats;
}
