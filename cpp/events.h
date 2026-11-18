#ifndef EVENTS_H
#define EVENTS_H

#include <string>
#include <vector>
#include <map>
#include <cstdint>

struct Event {
    size_t event_id;
    size_t step_number;
    std::string event_type;
    int process_id = 0;
    int virtual_address = 0;
    int page_number = 0;
    int frame_number = 0;
    uint32_t swap_slot = 0;
    bool success = true;
    std::string explanation;
    std::map<std::string, std::string> metadata;
};

class EventReporting {
public:
    EventReporting(size_t max_history = 1000);

    void record_event(const Event& event);
    const std::vector<Event>& get_recent_events() const;
    std::vector<Event> get_events_by_type(const std::string& type) const;
    void clear();
    size_t count() const;
    std::string to_json_lines() const;

private:
    std::vector<Event> event_history_;
    size_t max_history_;
};

#endif