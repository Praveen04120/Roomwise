// Roomwise C++17 Serverless / CLI Request Dispatcher
// Executes the exact C++ Storage and Scheduler engines for stateless/serverless API invocations.

#include <iostream>
#include <sstream>
#include <string>
#include <regex>
#include <algorithm>
#include <cstdlib>
#include "json.hpp"
#include "models.h"
#include "storage.h"
#include "scheduler.h"

using namespace roomwise;
using json = nlohmann::json;

static std::string toLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string getHeaderInsensitive(const json& headers, const std::string& name) {
    if (!headers.is_object()) return "";
    std::string target = toLowerCopy(name);
    for (auto& kv : headers.items()) {
        if (toLowerCopy(kv.key()) == target && kv.value().is_string()) {
            return kv.value().get<std::string>();
        }
    }
    return "";
}

static std::string extractTokenFromJson(const json& headers) {
    std::string xToken = getHeaderInsensitive(headers, "x-admin-token");
    if (!xToken.empty()) return xToken;
    std::string auth = getHeaderInsensitive(headers, "authorization");
    if (auth.rfind("Bearer ", 0) == 0 || auth.rfind("bearer ", 0) == 0) {
        return auth.substr(7);
    }
    return "";
}

static std::string getQueryParam(const json& query, const std::string& key, const std::string& def = "") {
    if (!query.is_object() || !query.contains(key)) return def;
    if (query[key].is_string()) return query[key].get<std::string>();
    return def;
}

static bool hasQueryParam(const json& query, const std::string& key) {
    return query.is_object() && query.contains(key) && query[key].is_string() && !query[key].get<std::string>().empty();
}

static void emitResponse(int status, const json& payload, bool mutated = false) {
    json out = {
        {"status", status},
        {"content_type", "application/json"},
        {"mutated", mutated},
        {"body", payload}
    };
    std::cout << out.dump() << std::endl;
}

static bool requireAuth(const json& headers, Storage& storage) {
    std::string token = extractTokenFromJson(headers);
    if (token.empty() || !storage.isValidSession(token)) {
        emitResponse(401, {
            {"success", false},
            {"error", "Unauthorized: Valid admin session token required."}
        }, false);
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    std::string dataDir = "data";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) {
            dataDir = argv[++i];
        }
    }

    // Silence stdout logs from Storage during CLI JSON mode by redirecting cout temporarily
    std::streambuf* origCout = std::cout.rdbuf();
    std::ostringstream nullBuf;
    std::cout.rdbuf(nullBuf.rdbuf());

    Storage& storage = Storage::getInstance();
    bool ok = storage.initialize(dataDir);
    std::cout.rdbuf(origCout);

    if (!ok) {
        emitResponse(500, {{"error", "Failed to initialize storage"}});
        return 0;
    }

    Scheduler scheduler(storage);

    std::ostringstream stdinBuf;
    stdinBuf << std::cin.rdbuf();
    std::string rawInput = stdinBuf.str();

    json req;
    try {
        req = json::parse(rawInput);
    } catch (...) {
        emitResponse(400, {{"error", "Invalid request envelope JSON"}});
        return 0;
    }

    std::string method = req.value("method", "GET");
    std::string path = req.value("path", "/");
    json query = req.contains("query") ? req["query"] : json::object();
    json headers = req.contains("headers") ? req["headers"] : json::object();
    std::string bodyStr = req.value("body", "");

    // Strip trailing slash (except root)
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }

    std::smatch m;

    // GET /api/health
    if (method == "GET" && path == "/api/health") {
        emitResponse(200, {
            {"status", "ok"},
            {"service", "roomwise"},
            {"engine", "cpp17"},
            {"floors", static_cast<int>(storage.getFloors().size())},
            {"rooms", static_cast<int>(storage.getClassrooms().size())},
            {"timetable_entries", static_cast<int>(storage.getTimetable().size())}
        });
        return 0;
    }

    // GET /api/settings
    if (method == "GET" && path == "/api/settings") {
        emitResponse(200, storage.getSettings().toJson(false));
        return 0;
    }

    // GET /api/summary
    if (method == "GET" && path == "/api/summary") {
        std::string dateStr = getQueryParam(query, "date");
        if (!dateStr.empty() && Scheduler::resolveDayStrict(dateStr).empty()) {
            emitResponse(400, {{"error", "Invalid date parameter. Expected YYYY-MM-DD or weekday name."}});
            return 0;
        }
        int timeMins = -1;
        if (hasQueryParam(query, "time")) {
            timeMins = TimeUtil::parseTimeToMinutes(getQueryParam(query, "time"));
            if (timeMins < 0) {
                emitResponse(400, {{"error", "Invalid time format. Use 24h HH:MM."}});
                return 0;
            }
        }
        emitResponse(200, scheduler.getCampusSummary(dateStr, timeMins));
        return 0;
    }

    // GET /api/floors
    if (method == "GET" && path == "/api/floors") {
        auto floors = storage.getFloors();
        auto rooms = storage.getClassrooms();
        std::map<int, int> countMap;
        for (const auto& r : rooms) countMap[r.floor_number]++;
        json arr = json::array();
        for (const auto& f : floors) {
            json item = f.toJson();
            item["room_count"] = countMap[f.floor_number];
            arr.push_back(item);
        }
        emitResponse(200, arr);
        return 0;
    }

    // GET /api/rooms
    if (method == "GET" && path == "/api/rooms") {
        int floor = 0;
        if (hasQueryParam(query, "floor")) {
            try { floor = std::stoi(getQueryParam(query, "floor")); } catch (...) { floor = 0; }
        }
        auto rooms = (floor > 0) ? storage.getClassroomsByFloor(floor) : storage.getClassrooms();
        json arr = json::array();
        for (const auto& r : rooms) {
            json item = r.toJson();
            item["has_schedule"] = storage.hasTimetableConfigured(r.room_number);
            arr.push_back(item);
        }
        emitResponse(200, arr);
        return 0;
    }

    // GET /api/floors/:floor_number/rooms
    if (method == "GET" && std::regex_match(path, m, std::regex(R"(/api/floors/(\d+)/rooms)"))) {
        int floorNum = 0;
        try { floorNum = std::stoi(m[1]); } catch (...) {
            emitResponse(400, {{"error", "Invalid floor number."}});
            return 0;
        }
        std::string dateStr = getQueryParam(query, "date");
        if (!dateStr.empty() && Scheduler::resolveDayStrict(dateStr).empty()) {
            emitResponse(400, {{"error", "Invalid date parameter."}});
            return 0;
        }
        int timeMins = -1;
        if (hasQueryParam(query, "time")) {
            timeMins = TimeUtil::parseTimeToMinutes(getQueryParam(query, "time"));
            if (timeMins < 0) {
                emitResponse(400, {{"error", "Invalid time format. Use 24h HH:MM."}});
                return 0;
            }
        }
        auto statuses = scheduler.getFloorRoomStatuses(floorNum, dateStr, timeMins);
        json arr = json::array();
        for (const auto& s : statuses) arr.push_back(s.toJson());
        emitResponse(200, arr);
        return 0;
    }

    // GET /api/rooms/:room_number/status
    if (method == "GET" && std::regex_match(path, m, std::regex(R"(/api/rooms/([^/]+)/status)"))) {
        std::string roomNum = m[1];
        std::string dateStr = getQueryParam(query, "date");
        if (!dateStr.empty() && Scheduler::resolveDayStrict(dateStr).empty()) {
            emitResponse(400, {{"error", "Invalid date parameter."}});
            return 0;
        }
        int timeMins = -1;
        if (hasQueryParam(query, "time")) {
            timeMins = TimeUtil::parseTimeToMinutes(getQueryParam(query, "time"));
            if (timeMins < 0) {
                emitResponse(400, {{"error", "Invalid time format. Use 24h HH:MM."}});
                return 0;
            }
        }
        emitResponse(200, scheduler.getRoomStatus(roomNum, dateStr, timeMins).toJson());
        return 0;
    }

    // GET /api/rooms/:room_number/schedule
    if (method == "GET" && std::regex_match(path, m, std::regex(R"(/api/rooms/([^/]+)/schedule)"))) {
        std::string roomNum = m[1];
        std::string dateStr = hasQueryParam(query, "date") ? getQueryParam(query, "date") : TimeUtil::getCurrentDateString();
        std::string day = Scheduler::resolveDayStrict(dateStr);
        if (day.empty()) {
            emitResponse(400, {{"error", "Invalid date parameter."}});
            return 0;
        }
        Classroom room;
        if (!storage.getClassroom(roomNum, room)) {
            emitResponse(404, {{"error", "Room not found"}});
            return 0;
        }
        auto entries = storage.getTimetableForRoomAndDay(roomNum, day);
        auto dailySlots = scheduler.getDailyAvailability(roomNum, day);
        json entriesArr = json::array();
        for (const auto& e : entries) entriesArr.push_back(e.toJson());
        json slotsArr = json::array();
        for (const auto& s : dailySlots) slotsArr.push_back(s.toJson());
        Settings settings = storage.getSettings();

        emitResponse(200, {
            {"room_number", roomNum},
            {"floor_number", room.floor_number},
            {"capacity", room.capacity},
            {"room_type", room.room_type},
            {"date", dateStr},
            {"day", day},
            {"operating_start", settings.operating_start},
            {"operating_end", settings.operating_end},
            {"operating_start_minutes", settings.operating_start_minutes},
            {"operating_end_minutes", settings.operating_end_minutes},
            {"has_schedule_configured", storage.hasTimetableConfigured(roomNum)},
            {"scheduled_classes", entriesArr},
            {"daily_slots", slotsArr}
        });
        return 0;
    }

    // GET /api/availability
    if (method == "GET" && path == "/api/availability") {
        std::string dateStr = hasQueryParam(query, "date") ? getQueryParam(query, "date") : TimeUtil::getCurrentDateString();
        std::string day = Scheduler::resolveDayStrict(dateStr);
        if (day.empty()) {
            emitResponse(400, {{"error", "Invalid date parameter. Expected YYYY-MM-DD or weekday name."}});
            return 0;
        }
        int floorFilter = 0;
        if (hasQueryParam(query, "floor")) {
            try { floorFilter = std::stoi(getQueryParam(query, "floor")); } catch (...) { floorFilter = 0; }
        }
        int startMins = -1, endMins = -1;
        if (hasQueryParam(query, "start_time") && hasQueryParam(query, "end_time")) {
            startMins = TimeUtil::parseTimeToMinutes(getQueryParam(query, "start_time"));
            endMins = TimeUtil::parseTimeToMinutes(getQueryParam(query, "end_time"));
        } else if (hasQueryParam(query, "time")) {
            startMins = TimeUtil::parseTimeToMinutes(getQueryParam(query, "time"));
            if (startMins >= 0) endMins = std::min(startMins + 60, 1439);
        } else {
            startMins = TimeUtil::getCurrentMinutesNow();
            endMins = std::min(startMins + 60, 1439);
        }

        if (startMins < 0 || endMins < 0 || startMins >= endMins) {
            emitResponse(400, {{"error", "Invalid time interval. Start time must be valid (HH:MM) and earlier than end time."}});
            return 0;
        }

        Settings settings = storage.getSettings();
        bool outsideHours = (startMins < settings.operating_start_minutes || endMins > settings.operating_end_minutes);
        auto matching = scheduler.findAvailableRooms(dateStr, startMins, endMins, floorFilter);
        json arr = json::array();
        for (const auto& r : matching) arr.push_back(r.toJson());

        json resp = {
            {"date", dateStr},
            {"day", day},
            {"start_time", TimeUtil::minutesToTimeString(startMins)},
            {"end_time", TimeUtil::minutesToTimeString(endMins)},
            {"start_time_12h", TimeUtil::minutesTo12HourString(startMins)},
            {"end_time_12h", TimeUtil::minutesTo12HourString(endMins)},
            {"floor_filter", floorFilter},
            {"outside_operating_hours", outsideHours},
            {"operating_start", settings.operating_start},
            {"operating_end", settings.operating_end},
            {"total_matches", static_cast<int>(matching.size())},
            {"rooms", arr}
        };
        if (outsideHours) {
            resp["message"] = "Requested time interval falls outside college operating hours (" +
                              settings.operating_start + " - " + settings.operating_end + ").";
        }
        emitResponse(200, resp);
        return 0;
    }

    // GET /api/daily-availability
    if (method == "GET" && path == "/api/daily-availability") {
        std::string dateStr = hasQueryParam(query, "date") ? getQueryParam(query, "date") : TimeUtil::getCurrentDateString();
        std::string day = Scheduler::resolveDayStrict(dateStr);
        if (day.empty()) {
            emitResponse(400, {{"error", "Invalid date parameter."}});
            return 0;
        }
        int floorFilter = 0;
        if (hasQueryParam(query, "floor")) {
            try { floorFilter = std::stoi(getQueryParam(query, "floor")); } catch (...) { floorFilter = 0; }
        }
        auto classrooms = storage.getClassrooms();
        json arr = json::array();
        for (const auto& room : classrooms) {
            if (!room.is_active) continue;
            if (floorFilter > 0 && room.floor_number != floorFilter) continue;
            if (!storage.hasTimetableConfigured(room.room_number)) continue;
            auto slots = scheduler.getDailyAvailability(room.room_number, day);
            json slotsArr = json::array();
            for (const auto& s : slots) slotsArr.push_back(s.toJson());
            arr.push_back({
                {"room_number", room.room_number},
                {"floor_number", room.floor_number},
                {"capacity", room.capacity},
                {"room_type", room.room_type},
                {"slots", slotsArr}
            });
        }
        emitResponse(200, {
            {"date", dateStr},
            {"day", day},
            {"floor_filter", floorFilter},
            {"results", arr}
        });
        return 0;
    }

    // POST /api/admin/login
    if (method == "POST" && path == "/api/admin/login") {
        try {
            if (!storage.hasAdminPasswordConfigured()) {
                emitResponse(503, {{"success", false}, {"error", "Admin authentication is not configured on the server."}});
                return 0;
            }
            json body = json::parse(bodyStr);
            std::string pwd = body.value("password", "");
            if (storage.validateAdminPassword(pwd)) {
                std::string token = storage.createAdminSession();
                emitResponse(200, {{"success", true}, {"token", token}, {"message", "Admin authentication successful."}}, true);
            } else {
                emitResponse(401, {{"success", false}, {"error", "Invalid administrator password."}});
            }
        } catch (...) {
            emitResponse(400, {{"success", false}, {"error", "Invalid JSON format"}});
        }
        return 0;
    }

    // POST /api/admin/logout
    if (method == "POST" && path == "/api/admin/logout") {
        std::string token = extractTokenFromJson(headers);
        bool mutated = false;
        if (!token.empty()) {
            storage.revokeSession(token);
            mutated = true;
        }
        emitResponse(200, {{"success", true}}, mutated);
        return 0;
    }

    // GET /api/admin/dashboard
    if (method == "GET" && path == "/api/admin/dashboard") {
        if (!requireAuth(headers, storage)) return 0;
        std::string today = TimeUtil::getCurrentDateString();
        std::string day = scheduler.resolveDay(today);
        int currentMins = TimeUtil::getCurrentMinutesNow();

        auto floors = storage.getFloors();
        auto rooms = storage.getClassrooms();
        auto timetable = storage.getTimetable();

        std::vector<ScheduleEntry> upcoming;
        for (const auto& e : timetable) {
            if (TimeUtil::normalizeDayName(e.day) == day && e.start_minutes >= currentMins) {
                upcoming.push_back(e);
            }
        }
        std::sort(upcoming.begin(), upcoming.end(), [](const ScheduleEntry& a, const ScheduleEntry& b) {
            return a.start_minutes < b.start_minutes;
        });
        if (upcoming.size() > 10) upcoming.resize(10);
        json upArr = json::array();
        for (const auto& u : upcoming) upArr.push_back(u.toJson());

        emitResponse(200, {
            {"total_floors", static_cast<int>(floors.size())},
            {"total_rooms", static_cast<int>(rooms.size())},
            {"total_timetable_entries", static_cast<int>(timetable.size())},
            {"upcoming_classes_today", upArr},
            {"today_date", today},
            {"today_day", day},
            {"settings", storage.getSettings().toJson(false)}
        });
        return 0;
    }

    // POST /api/admin/floors
    if (method == "POST" && path == "/api/admin/floors") {
        if (!requireAuth(headers, storage)) return 0;
        try {
            Floor f = Floor::fromJson(json::parse(bodyStr));
            std::string err;
            if (storage.addFloor(f, err)) {
                emitResponse(200, {{"success", true}, {"floor", f.toJson()}}, true);
            } else {
                emitResponse(400, {{"success", false}, {"error", err}});
            }
        } catch (...) {
            emitResponse(400, {{"success", false}, {"error", "Invalid JSON format"}});
        }
        return 0;
    }

    // PUT /api/admin/floors/:num
    if (method == "PUT" && std::regex_match(path, m, std::regex(R"(/api/admin/floors/(\d+))"))) {
        if (!requireAuth(headers, storage)) return 0;
        try {
            int floorNum = std::stoi(m[1]);
            Floor f = Floor::fromJson(json::parse(bodyStr));
            f.floor_number = floorNum;
            std::string err;
            if (storage.updateFloor(floorNum, f, err)) {
                emitResponse(200, {{"success", true}}, true);
            } else {
                emitResponse(400, {{"success", false}, {"error", err}});
            }
        } catch (...) {
            emitResponse(400, {{"success", false}, {"error", "Invalid request"}});
        }
        return 0;
    }

    // DELETE /api/admin/floors/:num
    if (method == "DELETE" && std::regex_match(path, m, std::regex(R"(/api/admin/floors/(\d+))"))) {
        if (!requireAuth(headers, storage)) return 0;
        try {
            int floorNum = std::stoi(m[1]);
            std::string err;
            if (storage.deleteFloor(floorNum, err)) {
                emitResponse(200, {{"success", true}}, true);
            } else {
                emitResponse(400, {{"success", false}, {"error", err}});
            }
        } catch (...) {
            emitResponse(400, {{"success", false}, {"error", "Invalid floor number"}});
        }
        return 0;
    }

    // POST /api/admin/rooms
    if (method == "POST" && path == "/api/admin/rooms") {
        if (!requireAuth(headers, storage)) return 0;
        try {
            Classroom c = Classroom::fromJson(json::parse(bodyStr));
            std::string err;
            if (storage.addClassroom(c, err)) {
                emitResponse(200, {{"success", true}, {"room", c.toJson()}}, true);
            } else {
                emitResponse(400, {{"success", false}, {"error", err}});
            }
        } catch (...) {
            emitResponse(400, {{"success", false}, {"error", "Invalid JSON format"}});
        }
        return 0;
    }

    // PUT /api/admin/rooms/:room
    if (method == "PUT" && std::regex_match(path, m, std::regex(R"(/api/admin/rooms/([^/]+))"))) {
        if (!requireAuth(headers, storage)) return 0;
        try {
            std::string roomNum = m[1];
            Classroom c = Classroom::fromJson(json::parse(bodyStr));
            std::string err;
            if (storage.updateClassroom(roomNum, c, err)) {
                emitResponse(200, {{"success", true}}, true);
            } else {
                emitResponse(400, {{"success", false}, {"error", err}});
            }
        } catch (...) {
            emitResponse(400, {{"success", false}, {"error", "Invalid request"}});
        }
        return 0;
    }

    // DELETE /api/admin/rooms/:room
    if (method == "DELETE" && std::regex_match(path, m, std::regex(R"(/api/admin/rooms/([^/]+))"))) {
        if (!requireAuth(headers, storage)) return 0;
        std::string roomNum = m[1];
        std::string err;
        if (storage.deleteClassroom(roomNum, err)) {
            emitResponse(200, {{"success", true}}, true);
        } else {
            emitResponse(400, {{"success", false}, {"error", err}});
        }
        return 0;
    }

    // POST /api/admin/timetable/validate
    if (method == "POST" && path == "/api/admin/timetable/validate") {
        if (!requireAuth(headers, storage)) return 0;
        std::string csvContent;
        try { csvContent = json::parse(bodyStr).value("csv_content", ""); } catch (...) { csvContent = bodyStr; }
        if (csvContent.empty()) {
            emitResponse(400, {{"error", "No CSV content provided."}});
            return 0;
        }
        CSVValidationResult val = scheduler.validateCSV(csvContent);
        json rowsArr = json::array();
        for (const auto& r : val.rows) {
            rowsArr.push_back({
                {"row_number", r.row_number},
                {"room_number", r.room_number},
                {"section", r.section},
                {"subject", r.subject},
                {"day", r.day},
                {"start_time", r.start_time},
                {"end_time", r.end_time},
                {"is_valid", r.is_valid},
                {"conflicts_existing", r.conflicts_existing},
                {"error_message", r.error_message},
                {"warning_message", r.warning_message}
            });
        }
        emitResponse(200, {
            {"can_import", val.can_import},
            {"total_rows", val.total_rows},
            {"valid_rows", val.valid_rows},
            {"invalid_rows", val.invalid_rows},
            {"existing_conflicts", val.existing_conflicts},
            {"summary_errors", val.summary_errors},
            {"rows", rowsArr}
        });
        return 0;
    }

    // POST /api/admin/timetable/import
    if (method == "POST" && path == "/api/admin/timetable/import") {
        if (!requireAuth(headers, storage)) return 0;
        std::string csvContent;
        bool replaceExisting = false;
        try {
            json body = json::parse(bodyStr);
            csvContent = body.value("csv_content", "");
            replaceExisting = body.value("replace", false);
        } catch (...) {
            csvContent = bodyStr;
        }
        if (csvContent.empty()) {
            emitResponse(400, {{"error", "No CSV content provided."}});
            return 0;
        }
        int importedCount = 0;
        std::vector<std::string> warnings;
        std::string errorMsg;
        if (scheduler.importCSV(csvContent, replaceExisting, importedCount, warnings, errorMsg)) {
            emitResponse(200, {
                {"success", true},
                {"imported_count", importedCount},
                {"mode", replaceExisting ? "replaced" : "appended"},
                {"warnings", warnings}
            }, importedCount > 0 || replaceExisting);
        } else {
            emitResponse(400, {{"success", false}, {"error", errorMsg}, {"warnings", warnings}});
        }
        return 0;
    }

    // GET /api/admin/timetable
    if (method == "GET" && path == "/api/admin/timetable") {
        if (!requireAuth(headers, storage)) return 0;
        std::string roomFilter = getQueryParam(query, "room");
        std::string dayFilter = getQueryParam(query, "day");
        auto entries = storage.getTimetable();
        json arr = json::array();
        for (const auto& e : entries) {
            if (!roomFilter.empty() && e.room_number != roomFilter) continue;
            if (!dayFilter.empty() && TimeUtil::normalizeDayName(e.day) != TimeUtil::normalizeDayName(dayFilter)) continue;
            arr.push_back(e.toJson());
        }
        emitResponse(200, arr);
        return 0;
    }

    // POST /api/admin/timetable
    if (method == "POST" && path == "/api/admin/timetable") {
        if (!requireAuth(headers, storage)) return 0;
        try {
            ScheduleEntry entry = ScheduleEntry::fromJson(json::parse(bodyStr));
            if (entry.id.empty()) entry.id = storage.generateUniqueId("sched");
            std::string err;
            if (storage.addScheduleEntry(entry, err)) {
                emitResponse(200, {{"success", true}, {"id", entry.id}}, true);
            } else {
                emitResponse(400, {{"success", false}, {"error", err}});
            }
        } catch (...) {
            emitResponse(400, {{"success", false}, {"error", "Invalid JSON format"}});
        }
        return 0;
    }

    // PUT /api/admin/timetable/:id
    if (method == "PUT" && std::regex_match(path, m, std::regex(R"(/api/admin/timetable/([^/]+))"))) {
        if (!requireAuth(headers, storage)) return 0;
        try {
            std::string id = m[1];
            ScheduleEntry entry = ScheduleEntry::fromJson(json::parse(bodyStr));
            std::string err;
            if (storage.updateScheduleEntry(id, entry, err)) {
                emitResponse(200, {{"success", true}}, true);
            } else {
                emitResponse(400, {{"success", false}, {"error", err}});
            }
        } catch (...) {
            emitResponse(400, {{"success", false}, {"error", "Invalid request"}});
        }
        return 0;
    }

    // DELETE /api/admin/timetable/:id
    if (method == "DELETE" && std::regex_match(path, m, std::regex(R"(/api/admin/timetable/([^/]+))"))) {
        if (!requireAuth(headers, storage)) return 0;
        std::string id = m[1];
        std::string err;
        if (storage.deleteScheduleEntry(id, err)) {
            emitResponse(200, {{"success", true}}, true);
        } else {
            emitResponse(400, {{"success", false}, {"error", err}});
        }
        return 0;
    }

    // GET /api/admin/settings
    if (method == "GET" && path == "/api/admin/settings") {
        if (!requireAuth(headers, storage)) return 0;
        emitResponse(200, storage.getSettings().toJson(false));
        return 0;
    }

    // PUT /api/admin/settings
    if (method == "PUT" && path == "/api/admin/settings") {
        if (!requireAuth(headers, storage)) return 0;
        try {
            json body = json::parse(bodyStr);
            Settings merged = storage.getSettings();
            if (body.contains("college_name") && body["college_name"].is_string()) merged.college_name = body["college_name"].get<std::string>();
            if (body.contains("building_name") && body["building_name"].is_string()) merged.building_name = body["building_name"].get<std::string>();
            if (body.contains("operating_start") && body["operating_start"].is_string()) merged.operating_start = body["operating_start"].get<std::string>();
            if (body.contains("operating_end") && body["operating_end"].is_string()) merged.operating_end = body["operating_end"].get<std::string>();
            std::string newPwd = "";
            if (body.contains("admin_password") && body["admin_password"].is_string()) newPwd = body["admin_password"].get<std::string>();
            std::string err;
            if (storage.updateSettings(merged, newPwd, err)) {
                emitResponse(200, {{"success", true}, {"settings", storage.getSettings().toJson(false)}}, true);
            } else {
                emitResponse(400, {{"success", false}, {"error", err}});
            }
        } catch (...) {
            emitResponse(400, {{"success", false}, {"error", "Invalid JSON format"}});
        }
        return 0;
    }

    emitResponse(404, {{"error", "API endpoint not found: " + method + " " + path}});
    return 0;
}
