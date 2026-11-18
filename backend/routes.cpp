#include "routes.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>

namespace roomwise {

static std::string readFileContents(const std::string& path) {
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void Routes::setCorsHeaders(httplib::Response& res) {
    const char* allowedOrigin = std::getenv("ALLOWED_ORIGINS");
    std::string origin = (allowedOrigin && std::string(allowedOrigin).size() > 0) ? allowedOrigin : "*";
    res.set_header("Access-Control-Allow-Origin", origin);
    res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Admin-Token");
}

std::string Routes::extractToken(const httplib::Request& req) {
    if (req.has_header("X-Admin-Token")) {
        return req.get_header_value("X-Admin-Token");
    }
    if (req.has_header("Authorization")) {
        std::string auth = req.get_header_value("Authorization");
        if (auth.rfind("Bearer ", 0) == 0) {
            return auth.substr(7);
        }
    }
    return "";
}

bool Routes::checkAdminAuth(const httplib::Request& req, httplib::Response& res, Storage& storage) {
    std::string token = extractToken(req);
    if (token.empty() || !storage.isValidSession(token)) {
        res.status = 401;
        res.set_content(nlohmann::json{
            {"success", false},
            {"error", "Unauthorized: Valid admin session token required."}
        }.dump(), "application/json");
        return false;
    }
    return true;
}

void Routes::setupRoutes(httplib::Server& svr, Storage& storage, Scheduler& scheduler,
                         const std::string& frontendDir) {

    // Global Options preflight
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        res.status = 204;
    });

    // -----------------------------------------------------------------
    // Frontend HTML & Static Assets Serving
    // -----------------------------------------------------------------
    svr.Get("/", [frontendDir](const httplib::Request&, httplib::Response& res) {
        std::string content = readFileContents(frontendDir + "/index.html");
        if (content.empty()) {
            res.status = 404;
            res.set_content("frontend/index.html not found.", "text/plain");
            return;
        }
        res.set_content(content, "text/html; charset=utf-8");
    });

    svr.Get("/admin", [frontendDir](const httplib::Request&, httplib::Response& res) {
        std::string content = readFileContents(frontendDir + "/admin.html");
        if (content.empty()) {
            res.status = 404;
            res.set_content("frontend/admin.html not found.", "text/plain");
            return;
        }
        res.set_content(content, "text/html; charset=utf-8");
    });

    svr.Get("/styles.css", [frontendDir](const httplib::Request&, httplib::Response& res) {
        std::string content = readFileContents(frontendDir + "/styles.css");
        if (content.empty()) { res.status = 404; return; }
        res.set_content(content, "text/css; charset=utf-8");
    });

    svr.Get("/config.js", [frontendDir](const httplib::Request&, httplib::Response& res) {
        std::string content = readFileContents(frontendDir + "/config.js");
        if (content.empty()) {
            content = "window.ROOMWISE_API_URL = window.ROOMWISE_API_URL || '';\n";
        }
        res.set_content(content, "application/javascript; charset=utf-8");
    });

    svr.Get("/app.js", [frontendDir](const httplib::Request&, httplib::Response& res) {
        std::string content = readFileContents(frontendDir + "/app.js");
        if (content.empty()) { res.status = 404; return; }
        res.set_content(content, "application/javascript; charset=utf-8");
    });

    svr.Get("/admin.js", [frontendDir](const httplib::Request&, httplib::Response& res) {
        std::string content = readFileContents(frontendDir + "/admin.js");
        if (content.empty()) { res.status = 404; return; }
        res.set_content(content, "application/javascript; charset=utf-8");
    });

    svr.Get("/sample_timetable.csv", [](const httplib::Request&, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        std::string content = readFileContents("samples/sample_timetable.csv");
        if (content.empty()) {
            content = "room_number,section,subject,day,start_time,end_time\n"
                      "101,CSCB,Data Structures,Monday,09:00,10:00\n"
                      "101,CSCA,Mathematics,Monday,10:00,11:00\n"
                      "102,CSCC,Operating Systems,Monday,09:00,10:00\n"
                      "201,CSE-2A,Algorithms,Tuesday,11:30,12:30\n"
                      "301,CSE-3A,Database Systems,Wednesday,14:00,16:00\n";
        }
        res.set_header("Content-Disposition", "attachment; filename=\"sample_timetable.csv\"");
        res.set_content(content, "text/csv; charset=utf-8");
    });

    // -----------------------------------------------------------------
    // Health Check Endpoint
    // -----------------------------------------------------------------
    svr.Get("/api/health", [&storage](const httplib::Request&, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        nlohmann::json health = {
            {"status", "ok"},
            {"service", "roomwise"},
            {"engine", "cpp17"},
            {"floors", static_cast<int>(storage.getFloors().size())},
            {"rooms", static_cast<int>(storage.getClassrooms().size())},
            {"timetable_entries", static_cast<int>(storage.getTimetable().size())}
        };
        res.set_content(health.dump(), "application/json");
    });

    // -----------------------------------------------------------------
    // Public API Endpoints
    // -----------------------------------------------------------------

    // GET /api/settings
    svr.Get("/api/settings", [&storage](const httplib::Request&, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        Settings s = storage.getSettings();
        res.set_content(s.toJson(false).dump(), "application/json");
    });

    // GET /api/summary - Dynamic campus statistics
    svr.Get("/api/summary", [&scheduler](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        std::string dateStr = req.has_param("date") ? req.get_param_value("date") : "";
        if (!dateStr.empty() && Scheduler::resolveDayStrict(dateStr).empty()) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", "Invalid date parameter. Expected YYYY-MM-DD or weekday name."}}.dump(), "application/json");
            return;
        }
        int timeMins = -1;
        if (req.has_param("time") && !req.get_param_value("time").empty()) {
            timeMins = TimeUtil::parseTimeToMinutes(req.get_param_value("time"));
            if (timeMins < 0) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", "Invalid time format. Use 24h HH:MM."}}.dump(), "application/json");
                return;
            }
        }
        nlohmann::json summary = scheduler.getCampusSummary(dateStr, timeMins);
        res.set_content(summary.dump(), "application/json");
    });

    // GET /api/floors
    svr.Get("/api/floors", [&storage](const httplib::Request&, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        std::vector<Floor> floors = storage.getFloors();
        std::vector<Classroom> allRooms = storage.getClassrooms();

        std::map<int, int> countMap;
        for (const auto& r : allRooms) {
            countMap[r.floor_number]++;
        }

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& f : floors) {
            nlohmann::json item = f.toJson();
            item["room_count"] = countMap[f.floor_number];
            arr.push_back(item);
        }
        res.set_content(arr.dump(), "application/json");
    });

    // GET /api/rooms
    svr.Get("/api/rooms", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        int floor = 0;
        if (req.has_param("floor")) {
            try { floor = std::stoi(req.get_param_value("floor")); } catch (...) { floor = 0; }
        }
        std::vector<Classroom> rooms = (floor > 0) ? storage.getClassroomsByFloor(floor) : storage.getClassrooms();

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : rooms) {
            nlohmann::json item = r.toJson();
            item["has_schedule"] = storage.hasTimetableConfigured(r.room_number);
            arr.push_back(item);
        }
        res.set_content(arr.dump(), "application/json");
    });

    // GET /api/floors/:floor_number/rooms
    svr.Get(R"(/api/floors/(\d+)/rooms)", [&scheduler](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        int floorNum = 0;
        try {
            floorNum = std::stoi(req.matches[1]);
        } catch (...) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", "Invalid floor number."}}.dump(), "application/json");
            return;
        }

        std::string dateStr = req.has_param("date") ? req.get_param_value("date") : "";
        if (!dateStr.empty() && Scheduler::resolveDayStrict(dateStr).empty()) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", "Invalid date parameter."}}.dump(), "application/json");
            return;
        }
        int timeMins = -1;
        if (req.has_param("time") && !req.get_param_value("time").empty()) {
            timeMins = TimeUtil::parseTimeToMinutes(req.get_param_value("time"));
            if (timeMins < 0) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", "Invalid time format. Use 24h HH:MM."}}.dump(), "application/json");
                return;
            }
        }

        std::vector<RoomStatusResult> statuses = scheduler.getFloorRoomStatuses(floorNum, dateStr, timeMins);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& s : statuses) {
            arr.push_back(s.toJson());
        }
        res.set_content(arr.dump(), "application/json");
    });

    // GET /api/rooms/:room_number/status
    svr.Get(R"(/api/rooms/([^/]+)/status)", [&scheduler](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        std::string roomNum = req.matches[1];
        std::string dateStr = req.has_param("date") ? req.get_param_value("date") : "";
        if (!dateStr.empty() && Scheduler::resolveDayStrict(dateStr).empty()) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", "Invalid date parameter."}}.dump(), "application/json");
            return;
        }
        int timeMins = -1;
        if (req.has_param("time") && !req.get_param_value("time").empty()) {
            timeMins = TimeUtil::parseTimeToMinutes(req.get_param_value("time"));
            if (timeMins < 0) {
                res.status = 400;
                res.set_content(nlohmann::json{{"error", "Invalid time format. Use 24h HH:MM."}}.dump(), "application/json");
                return;
            }
        }
        RoomStatusResult status = scheduler.getRoomStatus(roomNum, dateStr, timeMins);
        res.set_content(status.toJson().dump(), "application/json");
    });

    // GET /api/rooms/:room_number/schedule
    svr.Get(R"(/api/rooms/([^/]+)/schedule)", [&storage, &scheduler](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        std::string roomNum = req.matches[1];
        std::string dateStr = req.has_param("date") ? req.get_param_value("date") : TimeUtil::getCurrentDateString();
        std::string day = Scheduler::resolveDayStrict(dateStr);
        if (day.empty()) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", "Invalid date parameter."}}.dump(), "application/json");
            return;
        }

        Classroom room;
        if (!storage.getClassroom(roomNum, room)) {
            res.status = 404;
            res.set_content(nlohmann::json{{"error", "Room not found"}}.dump(), "application/json");
            return;
        }

        std::vector<ScheduleEntry> entries = storage.getTimetableForRoomAndDay(roomNum, day);
        std::vector<AvailableSlot> dailySlots = scheduler.getDailyAvailability(roomNum, day);

        nlohmann::json entriesArr = nlohmann::json::array();
        for (const auto& e : entries) {
            entriesArr.push_back(e.toJson());
        }

        nlohmann::json slotsArr = nlohmann::json::array();
        for (const auto& s : dailySlots) {
            slotsArr.push_back(s.toJson());
        }

        Settings settings = storage.getSettings();
        nlohmann::json resp = {
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
        };
        res.set_content(resp.dump(), "application/json");
    });

    // GET /api/availability - Find all rooms available for requested interval
    svr.Get("/api/availability", [&storage, &scheduler](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        std::string dateStr = req.has_param("date") ? req.get_param_value("date") : TimeUtil::getCurrentDateString();
        std::string day = Scheduler::resolveDayStrict(dateStr);
        if (day.empty()) {
            res.status = 400;
            res.set_content(nlohmann::json{
                {"error", "Invalid date parameter. Expected YYYY-MM-DD or weekday name."}
            }.dump(), "application/json");
            return;
        }

        int floorFilter = 0;
        if (req.has_param("floor")) {
            try { floorFilter = std::stoi(req.get_param_value("floor")); } catch (...) { floorFilter = 0; }
        }

        int startMins = -1;
        int endMins = -1;

        if (req.has_param("start_time") && req.has_param("end_time")) {
            startMins = TimeUtil::parseTimeToMinutes(req.get_param_value("start_time"));
            endMins = TimeUtil::parseTimeToMinutes(req.get_param_value("end_time"));
        } else if (req.has_param("time")) {
            startMins = TimeUtil::parseTimeToMinutes(req.get_param_value("time"));
            if (startMins >= 0) {
                endMins = std::min(startMins + 60, 1439);
            }
        } else {
            startMins = TimeUtil::getCurrentMinutesNow();
            endMins = std::min(startMins + 60, 1439);
        }

        if (startMins < 0 || endMins < 0 || startMins >= endMins) {
            res.status = 400;
            res.set_content(nlohmann::json{
                {"error", "Invalid time interval. Start time must be valid (HH:MM) and earlier than end time."}
            }.dump(), "application/json");
            return;
        }

        Settings settings = storage.getSettings();
        bool outsideOperatingHours = (startMins < settings.operating_start_minutes || endMins > settings.operating_end_minutes);

        std::vector<RoomStatusResult> matchingRooms = scheduler.findAvailableRooms(dateStr, startMins, endMins, floorFilter);

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : matchingRooms) {
            arr.push_back(r.toJson());
        }

        nlohmann::json response = {
            {"date", dateStr},
            {"day", day},
            {"start_time", TimeUtil::minutesToTimeString(startMins)},
            {"end_time", TimeUtil::minutesToTimeString(endMins)},
            {"start_time_12h", TimeUtil::minutesTo12HourString(startMins)},
            {"end_time_12h", TimeUtil::minutesTo12HourString(endMins)},
            {"floor_filter", floorFilter},
            {"outside_operating_hours", outsideOperatingHours},
            {"operating_start", settings.operating_start},
            {"operating_end", settings.operating_end},
            {"total_matches", static_cast<int>(matchingRooms.size())},
            {"rooms", arr}
        };
        if (outsideOperatingHours) {
            response["message"] = "Requested time interval falls outside college operating hours (" +
                                  settings.operating_start + " - " + settings.operating_end + ").";
        }
        res.set_content(response.dump(), "application/json");
    });

    // GET /api/daily-availability - Full-day availability slots for all or filtered rooms
    svr.Get("/api/daily-availability", [&storage, &scheduler](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        std::string dateStr = req.has_param("date") ? req.get_param_value("date") : TimeUtil::getCurrentDateString();
        std::string day = Scheduler::resolveDayStrict(dateStr);
        if (day.empty()) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", "Invalid date parameter."}}.dump(), "application/json");
            return;
        }
        int floorFilter = 0;
        if (req.has_param("floor")) {
            try { floorFilter = std::stoi(req.get_param_value("floor")); } catch (...) { floorFilter = 0; }
        }

        std::vector<Classroom> classrooms = storage.getClassrooms();
        nlohmann::json arr = nlohmann::json::array();

        for (const auto& room : classrooms) {
            if (!room.is_active) continue;
            if (floorFilter > 0 && room.floor_number != floorFilter) continue;
            if (!storage.hasTimetableConfigured(room.room_number)) continue;

            std::vector<AvailableSlot> slots = scheduler.getDailyAvailability(room.room_number, day);
            nlohmann::json slotsArr = nlohmann::json::array();
            for (const auto& s : slots) {
                slotsArr.push_back(s.toJson());
            }

            arr.push_back({
                {"room_number", room.room_number},
                {"floor_number", room.floor_number},
                {"capacity", room.capacity},
                {"room_type", room.room_type},
                {"slots", slotsArr}
            });
        }

        nlohmann::json resp = {
            {"date", dateStr},
            {"day", day},
            {"floor_filter", floorFilter},
            {"results", arr}
        };
        res.set_content(resp.dump(), "application/json");
    });

    // -----------------------------------------------------------------
    // Admin Authentication
    // -----------------------------------------------------------------
    svr.Post("/api/admin/login", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        try {
            if (!storage.hasAdminPasswordConfigured()) {
                res.status = 503;
                res.set_content(nlohmann::json{
                    {"success", false},
                    {"error", "Admin authentication is not configured on the server (set ADMIN_PASSWORD)."}
                }.dump(), "application/json");
                return;
            }

            nlohmann::json body = nlohmann::json::parse(req.body);
            std::string pwd = body.value("password", "");

            if (storage.validateAdminPassword(pwd)) {
                std::string token = storage.createAdminSession();
                res.set_content(nlohmann::json{
                    {"success", true},
                    {"token", token},
                    {"message", "Admin authentication successful."}
                }.dump(), "application/json");
            } else {
                res.status = 401;
                res.set_content(nlohmann::json{
                    {"success", false},
                    {"error", "Invalid administrator password."}
                }.dump(), "application/json");
            }
        } catch (const std::exception&) {
            res.status = 400;
            res.set_content(nlohmann::json{{"success", false}, {"error", "Invalid JSON format"}}.dump(), "application/json");
        }
    });

    svr.Post("/api/admin/logout", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        std::string token = extractToken(req);
        if (!token.empty()) {
            storage.revokeSession(token);
        }
        res.set_content(nlohmann::json{{"success", true}}.dump(), "application/json");
    });

    // -----------------------------------------------------------------
    // Admin Dashboard & Management (Protected)
    // -----------------------------------------------------------------
    svr.Get("/api/admin/dashboard", [&storage, &scheduler](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        std::string today = TimeUtil::getCurrentDateString();
        std::string day = scheduler.resolveDay(today);
        int currentMins = TimeUtil::getCurrentMinutesNow();

        std::vector<Floor> floors = storage.getFloors();
        std::vector<Classroom> rooms = storage.getClassrooms();
        std::vector<ScheduleEntry> timetable = storage.getTimetable();

        std::vector<ScheduleEntry> upcoming;
        for (const auto& e : timetable) {
            if (TimeUtil::normalizeDayName(e.day) == day && e.start_minutes >= currentMins) {
                upcoming.push_back(e);
            }
        }
        std::sort(upcoming.begin(), upcoming.end(), [](const ScheduleEntry& a, const ScheduleEntry& b) {
            return a.start_minutes < b.start_minutes;
        });
        if (upcoming.size() > 10) {
            upcoming.resize(10);
        }

        nlohmann::json upArr = nlohmann::json::array();
        for (const auto& u : upcoming) {
            upArr.push_back(u.toJson());
        }

        nlohmann::json dash = {
            {"total_floors", static_cast<int>(floors.size())},
            {"total_rooms", static_cast<int>(rooms.size())},
            {"total_timetable_entries", static_cast<int>(timetable.size())},
            {"upcoming_classes_today", upArr},
            {"today_date", today},
            {"today_day", day},
            {"settings", storage.getSettings().toJson(false)}
        };
        res.set_content(dash.dump(), "application/json");
    });

    // POST /api/admin/floors - Add floor
    svr.Post("/api/admin/floors", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            Floor f = Floor::fromJson(body);
            std::string err;
            if (storage.addFloor(f, err)) {
                res.set_content(nlohmann::json{{"success", true}, {"floor", f.toJson()}}.dump(), "application/json");
            } else {
                res.status = 400;
                res.set_content(nlohmann::json{{"success", false}, {"error", err}}.dump(), "application/json");
            }
        } catch (...) {
            res.status = 400;
            res.set_content(nlohmann::json{{"success", false}, {"error", "Invalid JSON format"}}.dump(), "application/json");
        }
    });

    // PUT /api/admin/floors/:floor_number - Update floor
    svr.Put(R"(/api/admin/floors/(\d+))", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        try {
            int floorNum = std::stoi(req.matches[1]);
            nlohmann::json body = nlohmann::json::parse(req.body);
            Floor f = Floor::fromJson(body);
            f.floor_number = floorNum;
            std::string err;
            if (storage.updateFloor(floorNum, f, err)) {
                res.set_content(nlohmann::json{{"success", true}}.dump(), "application/json");
            } else {
                res.status = 400;
                res.set_content(nlohmann::json{{"success", false}, {"error", err}}.dump(), "application/json");
            }
        } catch (...) {
            res.status = 400;
            res.set_content(nlohmann::json{{"success", false}, {"error", "Invalid request"}}.dump(), "application/json");
        }
    });

    // DELETE /api/admin/floors/:floor_number - Delete floor
    svr.Delete(R"(/api/admin/floors/(\d+))", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        try {
            int floorNum = std::stoi(req.matches[1]);
            std::string err;
            if (storage.deleteFloor(floorNum, err)) {
                res.set_content(nlohmann::json{{"success", true}}.dump(), "application/json");
            } else {
                res.status = 400;
                res.set_content(nlohmann::json{{"success", false}, {"error", err}}.dump(), "application/json");
            }
        } catch (...) {
            res.status = 400;
            res.set_content(nlohmann::json{{"success", false}, {"error", "Invalid floor number"}}.dump(), "application/json");
        }
    });

    // POST /api/admin/rooms - Add classroom
    svr.Post("/api/admin/rooms", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            Classroom c = Classroom::fromJson(body);
            std::string err;
            if (storage.addClassroom(c, err)) {
                res.set_content(nlohmann::json{{"success", true}, {"room", c.toJson()}}.dump(), "application/json");
            } else {
                res.status = 400;
                res.set_content(nlohmann::json{{"success", false}, {"error", err}}.dump(), "application/json");
            }
        } catch (...) {
            res.status = 400;
            res.set_content(nlohmann::json{{"success", false}, {"error", "Invalid JSON format"}}.dump(), "application/json");
        }
    });

    // PUT /api/admin/rooms/:room_number - Edit classroom
    svr.Put(R"(/api/admin/rooms/([^/]+))", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        try {
            std::string roomNum = req.matches[1];
            nlohmann::json body = nlohmann::json::parse(req.body);
            Classroom c = Classroom::fromJson(body);
            std::string err;
            if (storage.updateClassroom(roomNum, c, err)) {
                res.set_content(nlohmann::json{{"success", true}}.dump(), "application/json");
            } else {
                res.status = 400;
                res.set_content(nlohmann::json{{"success", false}, {"error", err}}.dump(), "application/json");
            }
        } catch (...) {
            res.status = 400;
            res.set_content(nlohmann::json{{"success", false}, {"error", "Invalid request"}}.dump(), "application/json");
        }
    });

    // DELETE /api/admin/rooms/:room_number - Delete classroom
    svr.Delete(R"(/api/admin/rooms/([^/]+))", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        std::string roomNum = req.matches[1];
        std::string err;
        if (storage.deleteClassroom(roomNum, err)) {
            res.set_content(nlohmann::json{{"success", true}}.dump(), "application/json");
        } else {
            res.status = 400;
            res.set_content(nlohmann::json{{"success", false}, {"error", err}}.dump(), "application/json");
        }
    });

    // POST /api/admin/timetable/validate - CSV Preview & Validation
    svr.Post("/api/admin/timetable/validate", [&storage, &scheduler](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        std::string csvContent;
        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            csvContent = body.value("csv_content", "");
        } catch (...) {
            csvContent = req.body;
        }

        if (csvContent.empty()) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", "No CSV content provided."}}.dump(), "application/json");
            return;
        }

        CSVValidationResult val = scheduler.validateCSV(csvContent);
        nlohmann::json rowsArr = nlohmann::json::array();
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

        nlohmann::json resp = {
            {"can_import", val.can_import},
            {"total_rows", val.total_rows},
            {"valid_rows", val.valid_rows},
            {"invalid_rows", val.invalid_rows},
            {"existing_conflicts", val.existing_conflicts},
            {"summary_errors", val.summary_errors},
            {"rows", rowsArr}
        };
        res.set_content(resp.dump(), "application/json");
    });

    // POST /api/admin/timetable/import - CSV Import
    svr.Post("/api/admin/timetable/import", [&storage, &scheduler](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        std::string csvContent;
        bool replaceExisting = false;

        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            csvContent = body.value("csv_content", "");
            replaceExisting = body.value("replace", false);
        } catch (...) {
            csvContent = req.body;
        }

        if (csvContent.empty()) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", "No CSV content provided."}}.dump(), "application/json");
            return;
        }

        int importedCount = 0;
        std::vector<std::string> warnings;
        std::string errorMsg;

        if (scheduler.importCSV(csvContent, replaceExisting, importedCount, warnings, errorMsg)) {
            res.set_content(nlohmann::json{
                {"success", true},
                {"imported_count", importedCount},
                {"mode", replaceExisting ? "replaced" : "appended"},
                {"warnings", warnings}
            }.dump(), "application/json");
        } else {
            res.status = 400;
            res.set_content(nlohmann::json{
                {"success", false},
                {"error", errorMsg},
                {"warnings", warnings}
            }.dump(), "application/json");
        }
    });

    // GET /api/admin/timetable - All timetable entries with optional filter
    svr.Get("/api/admin/timetable", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        std::string roomFilter = req.has_param("room") ? req.get_param_value("room") : "";
        std::string dayFilter = req.has_param("day") ? req.get_param_value("day") : "";

        std::vector<ScheduleEntry> entries = storage.getTimetable();
        nlohmann::json arr = nlohmann::json::array();

        for (const auto& e : entries) {
            if (!roomFilter.empty() && e.room_number != roomFilter) continue;
            if (!dayFilter.empty() && TimeUtil::normalizeDayName(e.day) != TimeUtil::normalizeDayName(dayFilter)) continue;
            arr.push_back(e.toJson());
        }
        res.set_content(arr.dump(), "application/json");
    });

    // POST /api/admin/timetable - Manual Add
    svr.Post("/api/admin/timetable", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            ScheduleEntry entry = ScheduleEntry::fromJson(body);
            if (entry.id.empty()) {
                entry.id = storage.generateUniqueId("sched");
            }
            std::string err;
            if (storage.addScheduleEntry(entry, err)) {
                res.set_content(nlohmann::json{{"success", true}, {"id", entry.id}}.dump(), "application/json");
            } else {
                res.status = 400;
                res.set_content(nlohmann::json{{"success", false}, {"error", err}}.dump(), "application/json");
            }
        } catch (...) {
            res.status = 400;
            res.set_content(nlohmann::json{{"success", false}, {"error", "Invalid JSON format"}}.dump(), "application/json");
        }
    });

    // PUT /api/admin/timetable/:id - Manual Edit
    svr.Put(R"(/api/admin/timetable/([^/]+))", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        try {
            std::string id = req.matches[1];
            nlohmann::json body = nlohmann::json::parse(req.body);
            ScheduleEntry entry = ScheduleEntry::fromJson(body);
            std::string err;
            if (storage.updateScheduleEntry(id, entry, err)) {
                res.set_content(nlohmann::json{{"success", true}}.dump(), "application/json");
            } else {
                res.status = 400;
                res.set_content(nlohmann::json{{"success", false}, {"error", err}}.dump(), "application/json");
            }
        } catch (...) {
            res.status = 400;
            res.set_content(nlohmann::json{{"success", false}, {"error", "Invalid request"}}.dump(), "application/json");
        }
    });

    // DELETE /api/admin/timetable/:id - Manual Delete
    svr.Delete(R"(/api/admin/timetable/([^/]+))", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        std::string id = req.matches[1];
        std::string err;
        if (storage.deleteScheduleEntry(id, err)) {
            res.set_content(nlohmann::json{{"success", true}}.dump(), "application/json");
        } else {
            res.status = 400;
            res.set_content(nlohmann::json{{"success", false}, {"error", err}}.dump(), "application/json");
        }
    });

    // GET /api/admin/settings (Never leaks password or hash)
    svr.Get("/api/admin/settings", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        Settings s = storage.getSettings();
        res.set_content(s.toJson(false).dump(), "application/json");
    });

    // PUT /api/admin/settings (Preserves existing fields and password when omitted)
    svr.Put("/api/admin/settings", [&storage](const httplib::Request& req, httplib::Response& res) {
        Routes::setCorsHeaders(res);
        if (!checkAdminAuth(req, res, storage)) return;

        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            Settings merged = storage.getSettings();
            if (body.contains("college_name") && body["college_name"].is_string()) {
                merged.college_name = body["college_name"].get<std::string>();
            }
            if (body.contains("building_name") && body["building_name"].is_string()) {
                merged.building_name = body["building_name"].get<std::string>();
            }
            if (body.contains("operating_start") && body["operating_start"].is_string()) {
                merged.operating_start = body["operating_start"].get<std::string>();
            }
            if (body.contains("operating_end") && body["operating_end"].is_string()) {
                merged.operating_end = body["operating_end"].get<std::string>();
            }
            std::string newPwd = "";
            if (body.contains("admin_password") && body["admin_password"].is_string()) {
                newPwd = body["admin_password"].get<std::string>();
            }

            std::string err;
            if (storage.updateSettings(merged, newPwd, err)) {
                res.set_content(nlohmann::json{
                    {"success", true},
                    {"settings", storage.getSettings().toJson(false)}
                }.dump(), "application/json");
            } else {
                res.status = 400;
                res.set_content(nlohmann::json{{"success", false}, {"error", err}}.dump(), "application/json");
            }
        } catch (...) {
            res.status = 400;
            res.set_content(nlohmann::json{{"success", false}, {"error", "Invalid JSON format"}}.dump(), "application/json");
        }
    });
}

} // namespace roomwise
