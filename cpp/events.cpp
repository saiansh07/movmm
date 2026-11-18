#include "events.h"
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace std;

EventReporting::EventReporting(size_t max_history) : max_history_(max_history) {}

void EventReporting::record_event(const Event& event) {
    if (event_history_.size() >= max_history_) {
        event_history_.erase(event_history_.begin());
    }
    event_history_.push_back(event);
}

const vector<Event>& EventReporting::get_recent_events() const {
    return event_history_;
}

vector<Event> EventReporting::get_events_by_type(const string& type) const {
    vector<Event> result;
    for (const auto& event : event_history_) {
        if (event.event_type == type) {
            result.push_back(event);
        }
    }
    return result;
}

void EventReporting::clear() {
    event_history_.clear();
}

size_t EventReporting::count() const {
    return event_history_.size();
}

string EventReporting::to_json_lines() const {
    ostringstream oss;
    for (size_t i = 0; i < event_history_.size(); ++i) {
        const auto& evt = event_history_[i];
        oss << "{\"event_id\":" << evt.event_id
            << ",\"step_number\":" << evt.step_number
            << ",\"event_type\":\"" << evt.event_type << "\"";

        if (evt.process_id != 0) {
            oss << ",\"process_id\":" << evt.process_id;
        }
        if (evt.virtual_address != 0) {
            oss << ",\"virtual_address\":" << evt.virtual_address;
        }
        if (evt.page_number != 0) {
            oss << ",\"page_number\":" << evt.page_number;
        }
        if (evt.frame_number != 0) {
            oss << ",\"frame_number\":" << evt.frame_number;
        }
        if (evt.swap_slot != 0) {
            oss << ",\"swap_slot\":" << evt.swap_slot;
        }
        oss << ",\"success\":" << (evt.success ? "true" : "false");

        if (!evt.explanation.empty()) {
            oss << ",\"explanation\":\"" << evt.explanation << "\"";
        }

        if (!evt.metadata.empty()) {
            oss << ",\"metadata\":{";
            bool first = true;
            for (const auto& [key, value] : evt.metadata) {
                if (!first) oss << ",";
                oss << "\"" << key << "\":\"" << value << "\"";
                first = false;
            }
            oss << "}";
        }

        oss << "}";
        if (i < event_history_.size() - 1) {
            oss << "\n";
        }
    }
    return oss.str();
}