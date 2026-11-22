#include "engine.h"
#include "json/json.hpp"
#include <iostream>
#include <string>
#include <sstream>

using namespace std;
using json = nlohmann::json;

static VirtualAddress parse_virtual_address(const json& value) {
    if (!value.is_number_integer() || value < 0 || value > UINT16_MAX) {
        throw std::invalid_argument("Virtual address must be an integer from 0 to 65535");
    }
    return value.get<VirtualAddress>();
}

int main() {
    Engine engine;
    string line;

    // No diagnostic output to stdout - all to stderr
    cerr << "MoVMM Engine CLI started" << endl;

    while (getline(cin, line)) {
        if (line.empty()) continue;

        json request;
        try {
            request = json::parse(line);
        } catch (json::parse_error& e) {
            json response = {
                {"id", "unknown"},
                {"success", false},
                {"data", nullptr},
                {"error", "JSON parse error: " + string(e.what())}
            };
            cout << response.dump() << endl;
            continue;
        }

        string req_id = "unknown";
        string command = "";

        if (request.contains("id") && request["id"].is_string()) {
            req_id = request["id"].get<string>();
        }

        if (request.contains("command") && request["command"].is_string()) {
            command = request["command"].get<string>();
        }

        json response;
        response["id"] = req_id;

        try {
            if (command == "initialize") {
                EngineConfig config;
                if (request.contains("params") && request["params"].is_object()) {
                    auto& params = request["params"];
                    if (params.contains("ram_size_bytes")) config.ram_size_bytes = params["ram_size_bytes"].get<size_t>();
                    if (params.contains("page_size")) config.page_size = params["page_size"].get<size_t>();
                    if (params.contains("tlb_capacity")) config.tlb_capacity = params["tlb_capacity"].get<size_t>();
                    if (params.contains("swap_capacity_pages")) config.swap_capacity_pages = params["swap_capacity_pages"].get<size_t>();
                    if (params.contains("fit_policy")) {
                        string fp = params["fit_policy"].get<string>();
                        config.fit_policy = (fp == "best_fit") ? FitPolicy::BEST_FIT : FitPolicy::FIRST_FIT;
                    }
                    if (params.contains("replacement_algo")) {
                        string ra = params["replacement_algo"].get<string>();
                        config.replacement_algo = (ra == "lru") ? ReplacementAlgorithm::LRU : ReplacementAlgorithm::FIFO;
                    }
                }
                engine.initialize(config);
                response["success"] = true;
                response["data"] = {{"status", "initialized"}, {"config", config.ram_size_bytes}};
            }
            else if (command == "reset") {
                engine.reset();
                response["success"] = true;
                response["data"] = {{"status", "reset"}};
            }
            else if (command == "create_process") {
                ProcessId pid = engine.create_process();
                response["success"] = true;
                response["data"] = {{"pid", static_cast<int>(pid)}};
            }
            else if (command == "terminate_process") {
                if (!request.contains("params") || !request["params"].contains("pid")) {
                    throw runtime_error("Missing pid parameter");
                }
                ProcessId pid = request["params"]["pid"].get<ProcessId>();
                bool success = engine.terminate_process(pid);
                response["success"] = success;
                if (!success) {
                    response["error"] = "Failed to terminate process";
                }
            }
            else if (command == "allocate") {
                if (!request.contains("params") || !request["params"].contains("pid") || !request["params"].contains("size")) {
                    throw runtime_error("Missing pid or size parameter");
                }
                ProcessId pid = request["params"]["pid"].get<ProcessId>();
                size_t size = request["params"]["size"].get<size_t>();
                bool success = engine.allocate_memory(pid, size);
                response["success"] = success;
                if (!success) {
                    response["error"] = "Allocation failed";
                }
            }
            else if (command == "free") {
                if (!request.contains("params") || !request["params"].contains("pid") || !request["params"].contains("address")) {
                    throw runtime_error("Missing pid or address parameter");
                }
                ProcessId pid = request["params"]["pid"].get<ProcessId>();
                size_t offset = request["params"]["address"].get<size_t>();
                bool success = engine.free_heap_offset(pid, offset);
                response["success"] = success;
                if (!success) {
                    response["error"] = "No allocation owned by this process at that heap offset";
                }
            }
            else if (command == "translate") {
                if (!request.contains("params") || !request["params"].contains("pid") || !request["params"].contains("virtual_address")) {
                    throw runtime_error("Missing pid or virtual_address parameter");
                }
                ProcessId pid = request["params"]["pid"].get<ProcessId>();
                VirtualAddress va = parse_virtual_address(request["params"]["virtual_address"]);
                auto result = engine.translate_address(pid, va);
                const auto page_size = engine.get_config().page_size;
                const VirtualAddress16 address(va, static_cast<uint16_t>(page_size));
                response["success"] = true;
                response["data"] = {
                    {"physical_address", result.physical_address},
                    {"virtual_page", address.page_number},
                    {"page_offset", address.offset},
                    {"physical_frame", result.protection_fault ? json(nullptr) : json(result.physical_address / page_size)},
                    {"tlb_hit", result.tlb_hit},
                    {"page_fault", result.page_fault},
                    {"protection_fault", result.protection_fault}
                };
            }
            else if (command == "read_memory") {
                if (!request.contains("params") || !request["params"].contains("pid") || !request["params"].contains("virtual_address") || !request["params"].contains("size")) {
                    throw runtime_error("Missing parameters");
                }
                ProcessId pid = request["params"]["pid"].get<ProcessId>();
                VirtualAddress va = parse_virtual_address(request["params"]["virtual_address"]);
                size_t size = request["params"]["size"].get<size_t>();

                vector<uint8_t> buffer(size);
                bool success = engine.read_memory(pid, va, buffer.data(), size);
                response["success"] = success;
                if (success) {
                    response["data"] = {{"bytes_read", size}, {"bytes", buffer}};
                } else {
                    response["error"] = "Read failed";
                }
            }
            else if (command == "write_memory") {
                if (!request.contains("params") || !request["params"].contains("pid") || !request["params"].contains("virtual_address") || !request["params"].contains("data")) {
                    throw runtime_error("Missing parameters");
                }
                ProcessId pid = request["params"]["pid"].get<ProcessId>();
                VirtualAddress va = parse_virtual_address(request["params"]["virtual_address"]);
                auto data_arr = request["params"]["data"];
                vector<uint8_t> data;
                for (auto& v : data_arr) data.push_back(static_cast<uint8_t>(v.get<int>()));

                bool success = engine.write_memory(pid, va, data.data(), data.size());
                response["success"] = success;
                if (!success) {
                    response["error"] = "Write failed";
                }
            }
            else if (command == "set_page_permission") {
                const auto& params = request.at("params");
                const auto pid = params.at("pid").get<ProcessId>();
                const auto va = parse_virtual_address(params.at("virtual_address"));
                const bool writable = params.at("writable").get<bool>();
                response["success"] = engine.set_page_writable(pid, va, writable);
                if (!response["success"].get<bool>()) response["error"] = "Process not found";
            }
            else if (command == "set_policy") {
                if (!request.contains("params") || !request["params"].contains("policy_type") || !request["params"].contains("policy")) {
                    throw runtime_error("Missing policy_type or policy parameter");
                }
                string policy_type = request["params"]["policy_type"].get<string>();
                string policy = request["params"]["policy"].get<string>();
                bool success = false;
                if (policy_type == "allocation") {
                    success = engine.set_allocation_policy(policy == "best_fit" ? FitPolicy::BEST_FIT : FitPolicy::FIRST_FIT);
                } else if (policy_type == "replacement") {
                    success = engine.set_replacement_policy(policy == "lru" ? ReplacementAlgorithm::LRU : ReplacementAlgorithm::FIFO);
                }
                response["success"] = success;
                if (!success) {
                    response["error"] = "Failed to set policy";
                }
            }
            else if (command == "get_state" || command == "get_snapshot") {
                auto snapshot = engine.get_snapshot();

                json data;
                // Machine state
                data["machine"] = {
                    {"ram_size_bytes", snapshot.machine.config.ram_size_bytes},
                    {"page_size", snapshot.machine.page_size},
                    {"total_frames", snapshot.machine.total_frames},
                    {"used_swap_slots", snapshot.machine.used_swap_slots},
                    {"total_swap_slots", snapshot.machine.total_swap_slots},
                    {"heap_utilization_percent", snapshot.machine.heap_utilization_percent}
                };

                // Heap state
                data["heap"] = {
                    {"allocated_bytes", snapshot.heap.stats.allocated_bytes},
                    {"free_bytes", snapshot.heap.stats.free_bytes},
                    {"total_blocks", snapshot.heap.total_blocks},
                    {"free_blocks", snapshot.heap.free_blocks},
                    {"total_allocations", snapshot.heap.total_allocations},
                    {"allocation_attempts", snapshot.heap.allocation_attempts},
                    {"total_frees", snapshot.heap.total_frees},
                    {"external_fragmentation_bytes", snapshot.heap.stats.external_fragmentation_bytes},
                    {"internal_fragmentation_bytes", snapshot.heap.stats.internal_fragmentation_bytes},
                    {"blocks", json::array()}
                };
                for (const auto& block : snapshot.heap.blocks) {
                    data["heap"]["blocks"].push_back({
                        {"offset", block.offset}, {"size", block.size},
                        {"owner", block.owner ? json(block.owner) : json(nullptr)}
                    });
                }

                // MMU state
                data["mmu"] = {
                    {"tlb_lookups", snapshot.mmu.mmu_metrics.tlb_lookups},
                    {"tlb_hits", snapshot.mmu.mmu_metrics.tlb_hits},
                    {"tlb_misses", snapshot.mmu.mmu_metrics.tlb_misses},
                    {"tlb_capacity", snapshot.mmu.tlb_capacity}
                };

                // Paging state
                data["paging"] = {
                    {"page_faults", snapshot.paging.page_faults},
                    {"resident_pages", snapshot.paging.resident_pages},
                    {"replacements", snapshot.paging.replacements},
                    {"fifo_enabled", snapshot.paging.fifo_enabled},
                    {"lru_enabled", snapshot.paging.lru_enabled}
                };

                // Swap state
                data["swap"] = {
                    {"page_outs", snapshot.swap.page_outs},
                    {"page_ins", snapshot.swap.page_ins},
                    {"used_slots", snapshot.swap.used_swap_slots}
                };

                data["machine"]["fit_policy"] = snapshot.machine.config.fit_policy == FitPolicy::BEST_FIT ? "best_fit" : "first_fit";
                data["processes"] = json::array();
                for (const auto& [pid, table] : snapshot.process_page_tables) {
                    json pages = json::array();
                    for (size_t vpn = 0; vpn < table.size(); ++vpn) {
                        const auto& entry = table[vpn];
                        if (!entry.valid) continue;
                        pages.push_back({
                            {"page", vpn}, {"present", entry.present},
                            {"frame", entry.present ? json(entry.frame_number) : json(nullptr)},
                            {"swap_slot", entry.swap_slot == NO_SWAP_SLOT ? json(nullptr) : json(entry.swap_slot)},
                            {"referenced", entry.referenced}, {"dirty", entry.dirty}, {"writable", entry.read_write}
                        });
                    }
                    data["processes"].push_back({{"pid", pid}, {"pages", pages}});
                }

                data["frames"] = json::array();
                for (size_t index = 0; index < snapshot.frames.size(); ++index) {
                    const auto& frame = snapshot.frames[index];
                    data["frames"].push_back({
                        {"frame", index}, {"resident", frame.resident},
                        {"pid", frame.resident ? json(frame.pid) : json(nullptr)},
                        {"page", frame.resident ? json(frame.page_number) : json(nullptr)},
                        {"dirty", frame.resident && frame.dirty}
                    });
                }

                data["mmu"]["entries"] = json::array();
                for (size_t index = 0; index < snapshot.tlb_entries.size(); ++index) {
                    const auto& entry = snapshot.tlb_entries[index];
                    if (entry.valid) {
                        data["mmu"]["entries"].push_back({
                            {"slot", index}, {"pid", entry.pid}, {"page", entry.page_number},
                            {"frame", entry.frame_number}, {"dirty", entry.dirty}
                        });
                    }
                }

                // Recent events
                json events_json = json::array();
                for (const auto& ev : snapshot.recent_events) {
                    events_json.push_back({
                        {"event_id", ev.event_id},
                        {"step_number", ev.step_number},
                        {"event_type", ev.event_type},
                        {"process_id", ev.process_id},
                        {"virtual_address", ev.virtual_address},
                        {"page_number", ev.page_number},
                        {"frame_number", ev.frame_number},
                        {"swap_slot", ev.swap_slot},
                        {"success", ev.success},
                        {"explanation", ev.explanation}
                    });
                }
                data["events"] = events_json;

                response["success"] = true;
                response["data"] = data;
            }
            else if (command == "get_statistics") {
                auto snapshot = engine.get_snapshot();
                json data;
                data["total_allocations"] = snapshot.heap.total_allocations;
                data["total_frees"] = snapshot.heap.total_frees;
                data["active_allocations"] = snapshot.heap.total_blocks - snapshot.heap.free_blocks;
                data["tlb_hit_rate"] = snapshot.mmu.mmu_metrics.tlb_hits + snapshot.mmu.mmu_metrics.tlb_misses > 0
                    ? (double)snapshot.mmu.mmu_metrics.tlb_hits / (snapshot.mmu.mmu_metrics.tlb_hits + snapshot.mmu.mmu_metrics.tlb_misses) * 100.0
                    : 0.0;
                data["page_faults"] = snapshot.paging.page_faults;
                response["success"] = true;
                response["data"] = data;
            }
            else if (command == "get_events") {
                auto events = engine.get_recent_events();
                json events_json = json::array();
                for (const auto& ev : events) {
                    events_json.push_back({
                        {"event_id", ev.event_id},
                        {"step_number", ev.step_number},
                        {"event_type", ev.event_type},
                        {"process_id", ev.process_id},
                        {"virtual_address", ev.virtual_address},
                        {"page_number", ev.page_number},
                        {"frame_number", ev.frame_number},
                        {"swap_slot", ev.swap_slot},
                        {"success", ev.success},
                        {"explanation", ev.explanation}
                    });
                }
                response["success"] = true;
                response["data"] = {{"events", events_json}};
            }
            else {
                response["success"] = false;
                response["error"] = "Unknown command: " + command;
            }
        } catch (const exception& e) {
            response["success"] = false;
            response["error"] = string(e.what());
        }

        cout << response.dump() << endl;
    }

    return 0;
}
