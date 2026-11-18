#ifndef ROOMWISE_MODELS_H
#define ROOMWISE_MODELS_H

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <cctype>
#include "json.hpp"

namespace roomwise {

// Utility class for Time and Date calculations
class TimeUtil {
public:
    static std::tm toLocalTm(std::time_t t) {
        std::tm out = {};
#ifdef _WIN32
        localtime_s(&out, &t);
#else
        localtime_r(&t, &out);
#endif
        return out;
    }

    // Parse "HH:MM" or "H:MM" (24-hour) into minutes after midnight (e.g., "09:30" -> 570)
    // Strictly rejects malformed input such as "09x30", "09:30abc", "24:00", "-1:00"
    static int parseTimeToMinutes(const std::string& rawTime) {
        size_t first = rawTime.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return -1;
        size_t last = rawTime.find_last_not_of(" \t\r\n");
        std::string timeStr = rawTime.substr(first, last - first + 1);

        size_t colonPos = timeStr.find(':');
        if (colonPos == std::string::npos || colonPos == 0 || colonPos + 1 >= timeStr.size()) {
            return -1;
        }
        // Reject multiple colons
        if (timeStr.find(':', colonPos + 1) != std::string::npos) {
            return -1;
        }

        std::string hStr = timeStr.substr(0, colonPos);
        std::string mStr = timeStr.substr(colonPos + 1);
        if (hStr.size() > 2 || mStr.size() != 2) {
            return -1;
        }
        for (char c : hStr) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
        }
        for (char c : mStr) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
        }

        int hours = std::stoi(hStr);
        int minutes = std::stoi(mStr);
        if (hours >= 0 && hours < 24 && minutes >= 0 && minutes < 60) {
            return hours * 60 + minutes;
        }
        return -1;
    }

    // Convert minutes after midnight into 24-hour "HH:MM" format
    static std::string minutesToTimeString(int minutes) {
        if (minutes < 0) minutes = 0;
        int hours = (minutes / 60) % 24;
        int mins = minutes % 60;
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << hours << ":"
            << std::setfill('0') << std::setw(2) << mins;
        return oss.str();
    }

    // Convert minutes after midnight into 12-hour "hh:mm AM/PM" format
    static std::string minutesTo12HourString(int minutes) {
        if (minutes < 0) minutes = 0;
        int hours = (minutes / 60) % 24;
        int mins = minutes % 60;
        std::string period = (hours >= 12) ? "PM" : "AM";
        int displayHours = hours % 12;
        if (displayHours == 0) displayHours = 12;
        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(2) << displayHours << ":"
            << std::setfill('0') << std::setw(2) << mins << " " << period;
        return oss.str();
    }

    // Validate "YYYY-MM-DD" (or "YYYY/MM/DD") calendar date strictly
    static bool parseValidDate(const std::string& dateStr, int& year, int& month, int& day) {
        if (dateStr.size() != 10) return false;
        char sep1 = dateStr[4];
        char sep2 = dateStr[7];
        if (!((sep1 == '-' && sep2 == '-') || (sep1 == '/' && sep2 == '/'))) return false;
        for (size_t i = 0; i < 10; ++i) {
            if (i == 4 || i == 7) continue;
            if (!std::isdigit(static_cast<unsigned char>(dateStr[i]))) return false;
        }
        year = std::stoi(dateStr.substr(0, 4));
        month = std::stoi(dateStr.substr(5, 2));
        day = std::stoi(dateStr.substr(8, 2));
        if (year < 1970 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) {
            return false;
        }
        std::tm check = {};
        check.tm_year = year - 1900;
        check.tm_mon = month - 1;
        check.tm_mday = day;
        check.tm_hour = 12; // Midday avoids DST midnight rollover issues
        check.tm_isdst = -1;
        std::time_t t = std::mktime(&check);
        if (t == -1) return false;
        std::tm verify = toLocalTm(t);
        return (verify.tm_year == year - 1900 && verify.tm_mon == month - 1 && verify.tm_mday == day);
    }

    // Get Day of Week ("Monday", "Tuesday", etc.) from Date "YYYY-MM-DD"
    static std::string getDayOfWeekFromDate(const std::string& dateStr) {
        int year = 0, month = 0, day = 0;
        if (!parseValidDate(dateStr, year, month, day)) {
            return "";
        }

        std::tm time_in = {};
        time_in.tm_year = year - 1900;
        time_in.tm_mon = month - 1;
        time_in.tm_mday = day;
        time_in.tm_hour = 12;
        time_in.tm_isdst = -1;

        std::time_t time_temp = std::mktime(&time_in);
        if (time_temp == -1) {
            return "";
        }

        std::tm time_out = toLocalTm(time_temp);
        static const std::string days[] = {
            "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
        };
        if (time_out.tm_wday >= 0 && time_out.tm_wday <= 6) {
            return days[time_out.tm_wday];
        }
        return "";
    }

    // Strict day normalization: returns "" for unrecognized day strings
    static std::string normalizeDayNameStrict(const std::string& day) {
        size_t first = day.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = day.find_last_not_of(" \t\r\n");
        std::string d = day.substr(first, last - first + 1);
        std::transform(d.begin(), d.end(), d.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (d == "monday" || d == "mon") return "Monday";
        if (d == "tuesday" || d == "tue") return "Tuesday";
        if (d == "wednesday" || d == "wed") return "Wednesday";
        if (d == "thursday" || d == "thu") return "Thursday";
        if (d == "friday" || d == "fri") return "Friday";
        if (d == "saturday" || d == "sat") return "Saturday";
        if (d == "sunday" || d == "sun") return "Sunday";
        return "";
    }

    // Normalize day name to Title Case e.g., "monday" -> "Monday" (with fallback)
    static std::string normalizeDayName(const std::string& day) {
        std::string strict = normalizeDayNameStrict(day);
        return strict.empty() ? "Monday" : strict;
    }

    // Get current local system date as "YYYY-MM-DD"
    static std::string getCurrentDateString() {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm = toLocalTm(now_c);
        std::ostringstream oss;
        oss << (local_tm.tm_year + 1900) << "-"
            << std::setfill('0') << std::setw(2) << (local_tm.tm_mon + 1) << "-"
            << std::setfill('0') << std::setw(2) << local_tm.tm_mday;
        return oss.str();
    }

    // Get current minutes after midnight
    static int getCurrentMinutesNow() {
        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm = toLocalTm(now_c);
        return local_tm.tm_hour * 60 + local_tm.tm_min;
    }

    // Check if two time intervals [s1, e1] and [s2, e2] overlap
    // Strict overlap rule: s1 < e2 && e1 > s2
    // Adjacent intervals (e.g. 10:00 to 11:00 and 11:00 to 12:00) DO NOT overlap
    static bool intervalsOverlap(int start1, int end1, int start2, int end2) {
        return (start1 < end2) && (end1 > start2);
    }
};

// Floor representation
struct Floor {
    int floor_number = 1;
    std::string floor_name = "";
    std::string description = "";

    nlohmann::json toJson() const {
        return {
            {"floor_number", floor_number},
            {"floor_name", floor_name},
            {"description", description}
        };
    }

    static Floor fromJson(const nlohmann::json& j) {
        Floor f;
        if (j.contains("floor_number") && j["floor_number"].is_number_integer()) f.floor_number = j["floor_number"].get<int>();
        if (j.contains("floor_name") && j["floor_name"].is_string()) f.floor_name = j["floor_name"].get<std::string>();
        if (j.contains("description") && j["description"].is_string()) f.description = j["description"].get<std::string>();
        return f;
    }
};

// Classroom representation
struct Classroom {
    std::string room_number = "";
    int floor_number = 1;
    int capacity = 60;
    std::string room_type = "Lecture Hall";
    bool is_active = true;

    nlohmann::json toJson() const {
        return {
            {"room_number", room_number},
            {"floor_number", floor_number},
            {"capacity", capacity},
            {"room_type", room_type},
            {"is_active", is_active}
        };
    }

    static Classroom fromJson(const nlohmann::json& j) {
        Classroom c;
        if (j.contains("room_number") && j["room_number"].is_string()) c.room_number = j["room_number"].get<std::string>();
        if (j.contains("floor_number") && j["floor_number"].is_number_integer()) c.floor_number = j["floor_number"].get<int>();
        if (j.contains("capacity") && j["capacity"].is_number_integer()) c.capacity = j["capacity"].get<int>();
        if (j.contains("room_type") && j["room_type"].is_string()) c.room_type = j["room_type"].get<std::string>();
        if (j.contains("is_active") && j["is_active"].is_boolean()) c.is_active = j["is_active"].get<bool>();
        return c;
    }
};

// Schedule entry representation
struct ScheduleEntry {
    std::string id = "";
    std::string room_number = "";
    std::string section = "";
    std::string subject = "";
    std::string day = "";
    std::string start_time = "";
    std::string end_time = "";
    int start_minutes = -1;
    int end_minutes = -1;

    nlohmann::json toJson() const {
        return {
            {"id", id},
            {"room_number", room_number},
            {"section", section},
            {"subject", subject},
            {"day", day},
            {"start_time", start_time},
            {"end_time", end_time},
            {"start_minutes", start_minutes},
            {"end_minutes", end_minutes}
        };
    }

    static ScheduleEntry fromJson(const nlohmann::json& j) {
        ScheduleEntry s;
        if (j.contains("id") && j["id"].is_string()) s.id = j["id"].get<std::string>();
        if (j.contains("room_number") && j["room_number"].is_string()) s.room_number = j["room_number"].get<std::string>();
        if (j.contains("section") && j["section"].is_string()) s.section = j["section"].get<std::string>();
        if (j.contains("subject") && j["subject"].is_string()) s.subject = j["subject"].get<std::string>();
        if (j.contains("day") && j["day"].is_string()) s.day = j["day"].get<std::string>();
        if (j.contains("start_time") && j["start_time"].is_string()) s.start_time = j["start_time"].get<std::string>();
        if (j.contains("end_time") && j["end_time"].is_string()) s.end_time = j["end_time"].get<std::string>();
        s.start_minutes = TimeUtil::parseTimeToMinutes(s.start_time);
        s.end_minutes = TimeUtil::parseTimeToMinutes(s.end_time);
        return s;
    }
};

// Available or Occupied slot in daily schedule
struct AvailableSlot {
    std::string start_time = "";
    std::string end_time = "";
    int start_minutes = 0;
    int end_minutes = 0;
    std::string status = "AVAILABLE"; // "AVAILABLE", "OCCUPIED"
    std::string section = "";
    std::string subject = "";

    nlohmann::json toJson() const {
        return {
            {"start_time", start_time},
            {"end_time", end_time},
            {"start_time_12h", TimeUtil::minutesTo12HourString(start_minutes)},
            {"end_time_12h", TimeUtil::minutesTo12HourString(end_minutes)},
            {"start_minutes", start_minutes},
            {"end_minutes", end_minutes},
            {"status", status},
            {"section", section},
            {"subject", subject}
        };
    }
};

// Summary status of a classroom at a given moment or query
struct RoomStatusResult {
    std::string room_number = "";
    int floor_number = 1;
    std::string floor_name = "";
    std::string room_type = "";
    int capacity = 60;
    std::string status = "UNAVAILABLE"; // "AVAILABLE", "OCCUPIED", "UNAVAILABLE"
    std::string status_label = "";
    std::string current_class = "";
    std::string current_subject = "";
    std::string current_time_slot = "";
    std::string next_available_slot = "";
    std::string next_class = "";
    std::string previous_class = "";
    bool has_schedule_data = false;
    std::vector<AvailableSlot> daily_slots;

    nlohmann::json toJson() const {
        nlohmann::json slotsArr = nlohmann::json::array();
        for (const auto& slot : daily_slots) {
            slotsArr.push_back(slot.toJson());
        }
        return {
            {"room_number", room_number},
            {"floor_number", floor_number},
            {"floor_name", floor_name},
            {"room_type", room_type},
            {"capacity", capacity},
            {"status", status},
            {"status_label", status_label},
            {"current_class", current_class},
            {"current_subject", current_subject},
            {"current_time_slot", current_time_slot},
            {"next_available_slot", next_available_slot},
            {"next_class", next_class},
            {"previous_class", previous_class},
            {"has_schedule_data", has_schedule_data},
            {"daily_slots", slotsArr}
        };
    }
};

// Settings model (never holds a hardcoded password)
struct Settings {
    std::string college_name = "Apex Institute of Technology";
    std::string building_name = "Sir C.V. Raman Academic Block";
    int total_floors = 9;
    std::string operating_start = "08:00";
    std::string operating_end = "18:00";
    int operating_start_minutes = 480;
    int operating_end_minutes = 1080;
    std::string admin_password_hash = ""; // Optional salted SHA-256 hash when overridden via Settings

    nlohmann::json toJson(bool include_hash = false) const {
        nlohmann::json j = {
            {"college_name", college_name},
            {"building_name", building_name},
            {"total_floors", total_floors},
            {"operating_start", operating_start},
            {"operating_end", operating_end},
            {"operating_start_minutes", operating_start_minutes},
            {"operating_end_minutes", operating_end_minutes}
        };
        if (include_hash && !admin_password_hash.empty()) {
            j["admin_password_hash"] = admin_password_hash;
        }
        return j;
    }

    static Settings fromJson(const nlohmann::json& j) {
        Settings s;
        if (j.contains("college_name") && j["college_name"].is_string()) s.college_name = j["college_name"].get<std::string>();
        if (j.contains("building_name") && j["building_name"].is_string()) s.building_name = j["building_name"].get<std::string>();
        if (j.contains("total_floors") && j["total_floors"].is_number_integer()) s.total_floors = j["total_floors"].get<int>();
        if (j.contains("operating_start") && j["operating_start"].is_string()) s.operating_start = j["operating_start"].get<std::string>();
        if (j.contains("operating_end") && j["operating_end"].is_string()) s.operating_end = j["operating_end"].get<std::string>();
        if (j.contains("admin_password_hash") && j["admin_password_hash"].is_string()) {
            s.admin_password_hash = j["admin_password_hash"].get<std::string>();
        }
        s.operating_start_minutes = TimeUtil::parseTimeToMinutes(s.operating_start);
        s.operating_end_minutes = TimeUtil::parseTimeToMinutes(s.operating_end);
        return s;
    }
};

} // namespace roomwise

#endif // ROOMWISE_MODELS_H
