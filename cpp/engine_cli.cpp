#include "engine.h"
#include <iostream>
#include <string>
#include <sstream>
#include <map>
#include <vector>

using namespace std;

string escape_json(const string& s) {
    ostringstream oss;
    for (auto c : s) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c; break;
        }
    }
    return oss.str();
}

string event_to_json(const Event& ev) {
    ostringstream oss;
    oss << "{"
        << "\"event_id\":" << ev.event_id << ","
        << "\"step_number\":" << ev.step_number << ","
        << "\"event_type\":\"" << escape_json(ev.event_type) << "\","
        << "\"process_id\":" << ev.process_id << ","
        << "\"virtual_address\":" << ev.virtual_address << ","
        << "\"page_number\":" << ev.page_number << ","
        << "\"frame_number\":" << ev.frame_number << ","
        << "\"swap_slot\":" << ev.swap_slot << ","
        << "\"success\":" << (ev.success ? "true" : "false") << ","
        << "\"explanation\":\"" << escape_json(ev.explanation) << "\""
        << "}";
    return oss.str();
}

string snapshot_to_json(const EngineSnapshot& snap) {
    ostringstream oss;
    oss << "{"
        << "\"machine\":{"
        << "\"ram_size_bytes\":" << snap.machine.config.ram_size_bytes << ","
        << "\"page_size\":" << snap.machine.page_size << ","
        << "\"total_frames\":" << snap.machine.total_frames << ","
        << "\"used_swap_slots\":" << snap.machine.used_swap_slots << ","
        << "\"total_swap_slots\":" << snap.machine.total_swap_slots << ","
        << "\"heap_utilization_percent\":" << snap.machine.heap_utilization_percent
        << "},"
        << "\"heap\":{"
        << "\"allocated_bytes\":" << snap.heap.stats.allocated_bytes << ","
        << "\"free_bytes\":" << snap.heap.stats.free_bytes << ","
        << "\"total_blocks\":" << snap.heap.total_blocks << ","
        << "\"free_blocks\":" << snap.heap.free_blocks
        << "},"
        << "\"mmu\":{"
        << "\"tlb_hits\":" << snap.mmu.mmu_metrics.tlb_hits << ","
        << "\"tlb_misses\":" << snap.mmu.mmu_metrics.tlb_misses << ","
        << "\"tlb_capacity\":" << snap.mmu.tlb_capacity
        << "},"
        << "\"paging\":{"
        << "\"page_faults\":" << snap.paging.page_faults << ","
        << "\"resident_pages\":" << snap.paging.resident_pages << ","
        << "\"fifo_enabled\":" << (snap.paging.fifo_enabled ? "true" : "false") << ","
        << "\"lru_enabled\":" << (snap.paging.lru_enabled ? "true" : "false")
        << "},"
        << "\"swap\":{"
        << "\"page_outs\":" << snap.swap.page_outs << ","
        << "\"page_ins\":" << snap.swap.page_ins << ","
        << "\"used_slots\":" << snap.swap.used_swap_slots
        << "},"
        << "\"recent_events\":[";

    for (size_t i = 0; i < snap.recent_events.size(); ++i) {
        oss << event_to_json(snap.recent_events[i]);
        if (i < snap.recent_events.size() - 1) oss << ",";
    }

    oss << "]}";
    return oss.str();
}

int main() {
    Engine engine;
    string line;
    cerr << "MoVMM Engine CLI started" << endl;

    while (getline(cin, line)) {
        if (line.empty()) continue;

        string req_id = "unknown";
        string command = "";

        size_t id_pos = line.find("\"id\":\"");
        if (id_pos != string::npos) {
            size_t id_end = line.find("\"", id_pos + 6);
            req_id = line.substr(id_pos + 6, id_end - (id_pos + 6));
        }

        size_t cmd_pos = line.find("\"command\":\"");
        if (cmd_pos != string::npos) {
            size_t cmd_end = line.find("\"", cmd_pos + 11);
            command = line.substr(cmd_pos + 11, cmd_end - (cmd_pos + 11));
        }

        if (command == "initialize") {
            engine.initialize(EngineConfig());
            cout << "{\"id\":\"" << req_id << "\",\"success\":true,\"data\":{\"status\":\"initialized\"}}" << endl;
        } else if (command == "get_state" || command == "get_snapshot") {
            cout << "{\"id\":\"" << req_id << "\",\"success\":true,\"data\":" << snapshot_to_json(engine.get_snapshot()) << "}" << endl;
        } else if (command == "create_process") {
            ProcessId pid = engine.create_process();
            cout << "{\"id\":\"" << req_id << "\",\"success\":true,\"data\":{\"pid\":" << pid << "}}" << endl;
        } else if (command == "reset") {
            engine.reset();
            cout << "{\"id\":\"" << req_id << "\",\"success\":true,\"data\":{\"status\":\"reset\"}}" << endl;
        } else {
            cout << "{\"id\":\"" << req_id << "\",\"success\":false,\"error\":\"Unknown command: " << command << "\"}" << endl;
        }
    }
    return 0;
}