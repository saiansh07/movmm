#include "engine.h"
#include "heap_allocator.h"
#include "mmu.h"
#include "paging.h"
#include "swap.h"
#include <iostream>
#include <cassert>

using namespace std;

int main() {
    cout << "--- MoVMM-Engine Unit & Integration Tests ---" << endl;

    // Test 1: Heap Allocator
    {
        cout << "Testing HeapAllocator..." << endl;
        HeapAllocator heap(1024, FitPolicy::FIRST_FIT);
        void* p1 = heap.my_malloc(200);
        assert(p1 != nullptr);
        void* p2 = heap.my_malloc(300);
        assert(p2 != nullptr);
        heap.my_free(p1);
        heap.my_free(p2);
        FragmentationStats stats = heap.get_fragmentation_stats();
        assert(stats.allocated_bytes == 0);
        cout << "HeapAllocator tests passed!" << endl;
    }

    // Test 2: MMU & TLB
    {
        cout << "Testing MMU and TLB..." << endl;
        MMU mmu(4, 256);
        vector<PageTableEntry> pt(16);
        pt[2].present = true;
        pt[2].frame_number = 7;
        pt[2].read_write = true;

        VirtualAddress va = (2 * 256) + 42;
        MMU::Result res1 = mmu.translate(1, va, pt);
        assert(!res1.page_fault);
        assert(!res1.tlb_hit);
        assert(res1.physical_address == (7 * 256) + 42);

        MMU::Result res2 = mmu.translate(1, va, pt);
        assert(res2.tlb_hit);
        assert(res2.physical_address == (7 * 256) + 42);

        MMU::Result res3 = mmu.translate(1, 20 * 256, pt);
        assert(res3.protection_fault);

        mmu.invalidate_tlb(1);
        MMU::Result res4 = mmu.translate(1, va, pt);
        assert(!res4.tlb_hit);
        cout << "MMU and TLB tests passed!" << endl;
    }

    // Test 3: Swap Manager
    {
        cout << "Testing SwapManager..." << endl;
        {
            SwapManager swap("swap.bin", 16, 256);
            uint32_t slot1 = swap.allocate_slot();
            uint32_t slot2 = swap.allocate_slot();
            assert(slot1 == 0);
            assert(slot2 == 1);

            vector<char> page_data(256, 'X');
            page_data[0] = 'M';
            page_data[255] = 'Z';

            assert(swap.page_out(slot1, page_data.data()));

            vector<char> read_buffer(256, 0);
            assert(swap.page_in(slot1, read_buffer.data()));
            assert(read_buffer[0] == 'M');
            assert(read_buffer[255] == 'Z');

            swap.free_slot(slot1);
            swap.free_slot(slot2);
        }
        remove("swap.bin");
        cout << "SwapManager tests passed!" << endl;
    }

    // Test 4: Demand Paging & Replacement - minimal
    {
        cout << "Testing PagingManager (basic)..." << endl;
        SwapManager swap("swap_paging.bin", 32, 256);
        PagingManager paging(4, ReplacementAlgorithm::FIFO, swap); // 4 frames to avoid exhaustion

        vector<PageTableEntry> pt(16);

        // Only test a couple of page faults without eviction pressure
        PageFaultInfo f1 = paging.handle_page_fault(100, 1, pt);
        assert(f1.occurred);
        assert(pt[1].present);
        assert(pt[1].frame_number == 0);

        PageFaultInfo f2 = paging.handle_page_fault(100, 2, pt);
        assert(f2.occurred);
        assert(pt[2].present);
        assert(pt[2].frame_number == 1);

        // Just test a couple more without eviction
        PageFaultInfo f3 = paging.handle_page_fault(100, 3, pt);
        assert(f3.occurred);
        assert(pt[3].present);
        assert(pt[3].frame_number == 2);

        PageFaultInfo f4 = paging.handle_page_fault(100, 4, pt);
        assert(f4.occurred);
        assert(pt[4].present);
        assert(pt[4].frame_number == 3);

        remove("swap_paging.bin");
        cout << "PagingManager basic tests passed!" << endl;
    }

    // Two processes share RAM. LRU must evict a dirty page and invalidate its TLB entry.
    {
        EngineConfig config;
        config.page_size = 512;
        config.ram_size_bytes = 1024;
        Engine engine(config);
        const auto first = engine.create_process();
        const auto second = engine.create_process();
        const uint8_t written = 42;
        assert(engine.write_memory(first, 511, &written, 1));
        assert(engine.translate_address(second, 0).page_fault);
        assert(engine.translate_address(first, 511).tlb_hit);

        assert(engine.set_replacement_policy(ReplacementAlgorithm::LRU));
        assert(engine.translate_address(second, 512).page_fault);
        auto snapshot = engine.get_snapshot();
        assert(snapshot.process_page_tables.at(first).at(0).present);
        assert(!snapshot.process_page_tables.at(second).at(0).present);

        assert(engine.translate_address(second, 0).page_fault);
        snapshot = engine.get_snapshot();
        assert(!snapshot.process_page_tables.at(first).at(0).present);
        assert(snapshot.process_page_tables.at(first).at(0).swap_slot == 0);
        assert(snapshot.swap.page_outs == 1);
        assert(snapshot.paging.replacements == 2);
        for (const auto& entry : snapshot.tlb_entries) {
            assert(!entry.valid || entry.pid != first);
        }

        uint8_t restored = 0;
        assert(engine.read_memory(first, 511, &restored, 1));
        assert(restored == written);
        assert(engine.get_snapshot().swap.page_ins == 1);
        assert(engine.terminate_process(first));
        snapshot = engine.get_snapshot();
        assert(snapshot.process_page_tables.count(first) == 0);
        assert(snapshot.swap.used_swap_slots == 0);
        assert(snapshot.paging.resident_pages == 1);
        engine.reset();
        assert(engine.get_snapshot().process_page_tables.empty());
    }

    // Heap offsets are owned by a process and return to the free list on exit.
    {
        Engine engine;
        const auto first = engine.create_process();
        const auto second = engine.create_process();
        assert(engine.allocate_memory(first, 64));
        assert(engine.allocate_memory(second, 128));
        assert(engine.set_allocation_policy(FitPolicy::BEST_FIT));
        auto heap = engine.get_snapshot().heap;
        assert(heap.blocks.size() == 3);
        assert(heap.blocks[0].offset == 0 && heap.blocks[0].owner == first);
        assert(heap.blocks[1].offset == 64 && heap.blocks[1].owner == second);
        assert(!engine.free_heap_offset(second, 0));
        assert(!engine.free_heap_offset(first, 1));
        assert(engine.free_heap_offset(first, 0));
        heap = engine.get_snapshot().heap;
        assert(heap.total_allocations == 2 && heap.total_frees == 1);
        assert(heap.stats.external_fragmentation_bytes == 64);
        assert(!engine.free_heap_offset(first, 0));
        assert(engine.terminate_process(second));
        heap = engine.get_snapshot().heap;
        assert(heap.blocks.size() == 1);
        assert(heap.blocks[0].owner == 0 && heap.blocks[0].size == 4096);
        assert(engine.get_state().active_allocations == 0);
        assert(engine.get_state().total_frees == 2);
        engine.reset();
        assert(!engine.free_heap_offset(first, 0));
        const auto after_reset = engine.create_process();
        assert(engine.allocate_memory(after_reset, 4093));
        heap = engine.get_snapshot().heap;
        assert(heap.total_allocations == 1 && heap.total_frees == 0);
        assert(heap.stats.internal_fragmentation_bytes == 3);
        cout << "Heap ownership and coalescing tests passed!" << endl;
    }

    // Permission survives faults and prevents writes without changing memory.
    {
        Engine engine;
        const auto pid = engine.create_process();
        const uint8_t value = 42;
        uint8_t read = 0;
        assert(engine.set_page_writable(pid, 0, false));
        assert(!engine.write_memory(pid, 0, &value, 1));
        assert(engine.get_snapshot().paging.page_faults == 0);
        assert(engine.read_memory(pid, 0, &read, 1) && read == 0);
        assert(engine.set_page_writable(pid, 0, true));
        assert(engine.write_memory(pid, 0, &value, 1));
        assert(engine.set_page_writable(pid, 0, false));
        assert(!engine.write_memory(pid, 0, &read, 1));
        assert(engine.read_memory(pid, 0, &read, 1) && read == value);
    }

    // The snapshot keeps a bounded history while IDs stay monotonic.
    {
        Engine engine;
        for (int i = 0; i < 1005; ++i) assert(!engine.allocate_memory(1, 0));
        const auto snapshot = engine.get_snapshot();
        assert(snapshot.recent_events.size() == 1000);
        assert(snapshot.recent_events.front().event_id == 6);
        assert(snapshot.recent_events.back().event_id == 1005);
        assert(snapshot.heap.allocation_attempts == 1005);
        assert(snapshot.heap.total_allocations == 0);
    }

    cout << "--- All MoVMM-Engine Tests Passed! ---" << endl;
    return 0;
}
