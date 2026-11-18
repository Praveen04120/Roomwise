#ifndef ROOMWISE_STORAGE_H
#define ROOMWISE_STORAGE_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include "models.h"

namespace roomwise {

class Storage {
public:
    static Storage& getInstance();

    // Initialize data directory and load existing files or seed samples
    bool initialize(const std::string& dataDirectory = "data");

    // Floor operations
    std::vector<Floor> getFloors();
    bool getFloor(int floor_number, Floor& floor);
    bool addFloor(const Floor& floor, std::string& errorMsg);
    bool updateFloor(int floor_number, const Floor& floor, std::string& errorMsg);
    bool deleteFloor(int floor_number, std::string& errorMsg);

    // Classroom operations
    std::vector<Classroom> getClassrooms();
    std::vector<Classroom> getClassroomsByFloor(int floor_number);
    bool getClassroom(const std::string& room_number, Classroom& room);
    bool addClassroom(const Classroom& room, std::string& errorMsg);
    bool updateClassroom(const std::string& room_number, const Classroom& room, std::string& errorMsg);
    bool deleteClassroom(const std::string& room_number, std::string& errorMsg);

    // Timetable operations
    std::vector<ScheduleEntry> getTimetable();
    std::vector<ScheduleEntry> getTimetableForRoom(const std::string& room_number);
    std::vector<ScheduleEntry> getTimetableForRoomAndDay(const std::string& room_number, const std::string& day);
    bool addScheduleEntry(const ScheduleEntry& entry, std::string& errorMsg);
    bool updateScheduleEntry(const std::string& id, const ScheduleEntry& entry, std::string& errorMsg);
    bool deleteScheduleEntry(const std::string& id, std::string& errorMsg);
    bool replaceTimetable(const std::vector<ScheduleEntry>& newEntries, std::string& errorMsg);
    bool appendTimetable(const std::vector<ScheduleEntry>& newEntries, int& addedCount, std::vector<std::string>& warnings);

    // Check if a room has timetable configured (at least 1 entry in the system)
    bool hasTimetableConfigured(const std::string& room_number);

    // Settings operations
    Settings getSettings();
    bool updateSettings(const Settings& newSettings, const std::string& newPlaintextPassword, std::string& errorMsg);

    // Authentication & session management
    bool hasAdminPasswordConfigured();
    bool validateAdminPassword(const std::string& password);
    std::string createAdminSession();
    bool isValidSession(const std::string& token);
    void revokeSession(const std::string& token);

    // Unique ID generator
    std::string generateUniqueId(const std::string& prefix);

    // Seed realistic sample data if empty
    void seedDefaultData();

    // Password hashing utilities (salted SHA-256)
    static std::string hashPassword(const std::string& password, const std::string& salt = "");
    static bool verifyPasswordHash(const std::string& password, const std::string& storedHash);

private:
    Storage();
    ~Storage() = default;
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    // Atomic JSON file I/O
    static bool atomicWriteFile(const std::string& path, const std::string& content);
    bool loadFloors();
    bool saveFloors();
    bool loadRooms();
    bool saveRooms();
    bool loadTimetable();
    bool saveTimetable();
    bool loadSettings();
    bool saveSettings();
    bool loadSessions();
    bool saveSessions();
    void pruneExpiredSessionsLocked();

    std::string dataDir_;
    std::mutex dataMutex_;

    std::vector<Floor> floors_;
    std::vector<Classroom> classrooms_;
    std::vector<ScheduleEntry> timetable_;
    Settings settings_;
    std::string envAdminPassword_;

    // token -> expiry epoch seconds
    std::map<std::string, int64_t> activeSessions_;
    std::atomic<uint64_t> idCounter_{0};
};

} // namespace roomwise

#endif // ROOMWISE_STORAGE_H
