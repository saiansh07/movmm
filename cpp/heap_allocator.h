#ifndef HEAP_ALLOCATOR_H
#define HEAP_ALLOCATOR_H

#include "types.h"
#include <vector>
#include <cstddef>
#include <iostream>

class HeapAllocator {
public:
    HeapAllocator(size_t heap_size, FitPolicy policy);
    ~HeapAllocator();

    void* my_malloc(size_t size);
    void my_free(void* ptr);

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
