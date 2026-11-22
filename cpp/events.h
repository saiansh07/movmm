#ifndef EVENTS_H
#define EVENTS_H

#include <string>
#include <vector>
#include <map>
#include <cstdint>

using namespace std;

struct Event {
    size_t event_id;
    size_t step_number;
    string event_type;
    int process_id = 0;
    int virtual_address = 0;
    int page_number = 0;
    int frame_number = 0;
    uint32_t swap_slot = 0;
    bool success = true;
    string explanation;
    map<string, string> metadata;
};

class EventReporting {
public:
    EventReporting(size_t max_history = 1000);

    void record_event(const Event& event);
    const vector<Event>& get_recent_events() const;
    vector<Event> get_events_by_type(const string& type) const;
    void clear();
    size_t count() const;
    string to_json_lines() const;

private:
    vector<Event> event_history_;
    size_t max_history_;
};

#endif