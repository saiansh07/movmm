#ifndef HEAP_ALLOCATOR_H
#define HEAP_ALLOCATOR_H

#include "types.h"
#include <vector>
#include <cstddef>
#include <iostream>
#include <optional>

class HeapAllocator {
public:
    struct Block {
        size_t offset;
        size_t size;
        bool free;
    };

    HeapAllocator(size_t heap_size, FitPolicy policy);
    ~HeapAllocator();

    void* my_malloc(size_t size);
    void my_free(void* ptr);
    std::optional<size_t> offset_of(const void* ptr) const;
    void free_at(size_t offset);
    std::vector<Block> get_blocks() const;
    void set_policy(FitPolicy new_policy) { policy = new_policy; }

    FragmentationStats get_fragmentation_stats() const;

private:
    void split_block(AllocationBlock* block, size_t size);
    void coalesce();

    size_t total_size;
    void* heap_start;
    AllocationBlock* head;
    FitPolicy policy;

    size_t allocated_bytes;
};

#endif
