#include "storage.h"
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cstdlib>
#include <cstdint>
#include <array>

namespace fs = std::filesystem;

namespace roomwise {

namespace {

// Compact standard SHA-256 implementation (zero external dependencies)
std::string sha256Hex(const std::string& input) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    auto rotr = [](uint32_t x, uint32_t n) -> uint32_t {
        return (x >> n) | (x << (32 - n));
    };

    uint32_t h[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    std::vector<uint8_t> msg(input.begin(), input.end());
    uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xff));
    }

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 2]) << 8) |
                   (static_cast<uint32_t>(msg[chunk + i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i) {
        oss << std::setw(8) << h[i];
    }
    return oss.str();
}

bool constantTimeEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

std::string randomHex(size_t len) {
    static const char hexChars[] = "0123456789abcdef";
    std::random_device rd;
    std::mt19937_64 gen(rd() ^ static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> dis(0, 15);
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(hexChars[dis(gen)]);
    }
    return out;
}

std::string trimWhitespace(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

int64_t nowEpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // anonymous namespace

std::string Storage::hashPassword(const std::string& password, const std::string& salt) {
    std::string useSalt = salt.empty() ? randomHex(16) : salt;
    std::string digest = sha256Hex(useSalt + ":" + password);
    return "sha256$" + useSalt + "$" + digest;
}

bool Storage::verifyPasswordHash(const std::string& password, const std::string& storedHash) {
    if (storedHash.rfind("sha256$", 0) != 0) return false;
    size_t secondDollar = storedHash.find('$', 7);
    if (secondDollar == std::string::npos) return false;
    std::string salt = storedHash.substr(7, secondDollar - 7);
    std::string expected = storedHash.substr(secondDollar + 1);
    std::string actual = sha256Hex(salt + ":" + password);
    return constantTimeEquals(actual, expected);
}

Storage& Storage::getInstance() {
    static Storage instance;
    return instance;
}

Storage::Storage() {
    dataDir_ = "data";
}

std::string Storage::generateUniqueId(const std::string& prefix) {
    uint64_t seq = ++idCounter_;
    auto ts = std::chrono::system_clock::now().time_since_epoch().count();
    return prefix + "_" + std::to_string(ts) + "_" + std::to_string(seq);
}

bool Storage::atomicWriteFile(const std::string& path, const std::string& content) {
    std::string tmpPath = path + ".tmp";
    {
        std::ofstream file(tmpPath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file.is_open()) return false;
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.flush();
        if (!file.good()) return false;
    }
    std::error_code ec;
    fs::rename(tmpPath, path, ec);
    if (ec) {
        // Fallback on Windows when replacing an existing file
        fs::remove(path, ec);
        ec.clear();
        fs::rename(tmpPath, path, ec);
        if (ec) {
            std::ofstream direct(path, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!direct.is_open()) return false;
            direct.write(content.data(), static_cast<std::streamsize>(content.size()));
            fs::remove(tmpPath, ec);
            return direct.good();
        }
    }
    return true;
}

bool Storage::initialize(const std::string& dataDirectory) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    dataDir_ = dataDirectory;

    // Load ADMIN_PASSWORD from environment variable (never hardcoded in source)
    const char* envPwd = std::getenv("ADMIN_PASSWORD");
    if (envPwd && std::string(envPwd).size() > 0) {
        envAdminPassword_ = envPwd;
    } else {
        envAdminPassword_.clear();
    }

    try {
        if (!fs::exists(dataDir_)) {
            fs::create_directories(dataDir_);
        }
    } catch (const std::exception& e) {
        std::cerr << "[Storage] Error creating data directory: " << e.what() << std::endl;
        return false;
    }

    floors_.clear();
    classrooms_.clear();
    timetable_.clear();
    activeSessions_.clear();

    bool floorsLoaded = loadFloors();
    bool roomsLoaded = loadRooms();
    loadTimetable();
    loadSettings();
    loadSessions();

    // If core building definition files do not exist or are empty, seed realistic sample data
    if (!floorsLoaded || floors_.empty() || !roomsLoaded || classrooms_.empty()) {
        std::cout << "[Storage] First run or missing data detected. Seeding realistic sample campus data..." << std::endl;
        seedDefaultData();
    } else {
        settings_.total_floors = static_cast<int>(floors_.size());
    }

    std::cout << "[Storage] Initialized successfully with "
              << floors_.size() << " floors, "
              << classrooms_.size() << " rooms, "
              << timetable_.size() << " timetable entries." << std::endl;

    return true;
}

// -------------------------------------------------------------
// Floor Operations
// -------------------------------------------------------------
std::vector<Floor> Storage::getFloors() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return floors_;
}

bool Storage::getFloor(int floor_number, Floor& floor) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (const auto& f : floors_) {
        if (f.floor_number == floor_number) {
            floor = f;
            return true;
        }
    }
    return false;
}

bool Storage::addFloor(const Floor& floor, std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (floor.floor_number <= 0 || floor.floor_number > 100) {
        errorMsg = "Floor number must be between 1 and 100.";
        return false;
    }
    std::string trimmedName = trimWhitespace(floor.floor_name);
    if (trimmedName.empty()) {
        errorMsg = "Floor name cannot be empty.";
        return false;
    }
    for (const auto& f : floors_) {
        if (f.floor_number == floor.floor_number) {
            errorMsg = "Floor " + std::to_string(floor.floor_number) + " already exists.";
            return false;
        }
    }
    Floor toAdd = floor;
    toAdd.floor_name = trimmedName;
    toAdd.description = trimWhitespace(floor.description);
    floors_.push_back(toAdd);
    std::sort(floors_.begin(), floors_.end(), [](const Floor& a, const Floor& b) {
        return a.floor_number < b.floor_number;
    });
    settings_.total_floors = static_cast<int>(floors_.size());
    saveFloors();
    saveSettings();
    return true;
}

bool Storage::updateFloor(int floor_number, const Floor& floor, std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    std::string trimmedName = trimWhitespace(floor.floor_name);
    if (trimmedName.empty()) {
        errorMsg = "Floor name cannot be empty.";
        return false;
    }
    for (auto& f : floors_) {
        if (f.floor_number == floor_number) {
            f.floor_name = trimmedName;
            f.description = trimWhitespace(floor.description);
            saveFloors();
            return true;
        }
    }
    errorMsg = "Floor " + std::to_string(floor_number) + " not found.";
    return false;
}

bool Storage::deleteFloor(int floor_number, std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto it = std::find_if(floors_.begin(), floors_.end(), [floor_number](const Floor& f) {
        return f.floor_number == floor_number;
    });
    if (it == floors_.end()) {
        errorMsg = "Floor " + std::to_string(floor_number) + " not found.";
        return false;
    }

    for (const auto& c : classrooms_) {
        if (c.floor_number == floor_number) {
            errorMsg = "Cannot delete floor " + std::to_string(floor_number) + " because classroom " + c.room_number + " is assigned to it.";
            return false;
        }
    }

    floors_.erase(it);
    settings_.total_floors = static_cast<int>(floors_.size());
    saveFloors();
    saveSettings();
    return true;
}

// -------------------------------------------------------------
// Classroom Operations
// -------------------------------------------------------------
std::vector<Classroom> Storage::getClassrooms() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return classrooms_;
}

std::vector<Classroom> Storage::getClassroomsByFloor(int floor_number) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    std::vector<Classroom> result;
    for (const auto& c : classrooms_) {
        if (c.floor_number == floor_number) {
            result.push_back(c);
        }
    }
    std::sort(result.begin(), result.end(), [](const Classroom& a, const Classroom& b) {
        return a.room_number < b.room_number;
    });
    return result;
}

bool Storage::getClassroom(const std::string& room_number, Classroom& room) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (const auto& c : classrooms_) {
        if (c.room_number == room_number) {
            room = c;
            return true;
        }
    }
    return false;
}

bool Storage::addClassroom(const Classroom& room, std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    Classroom cleanRoom = room;
    cleanRoom.room_number = trimWhitespace(room.room_number);
    cleanRoom.room_type = trimWhitespace(room.room_type);
    if (cleanRoom.room_type.empty()) cleanRoom.room_type = "Standard Classroom";

    if (cleanRoom.room_number.empty()) {
        errorMsg = "Room number cannot be empty.";
        return false;
    }
    if (cleanRoom.capacity <= 0 || cleanRoom.capacity > 2000) {
        errorMsg = "Classroom capacity must be between 1 and 2000.";
        return false;
    }

    bool floorExists = false;
    for (const auto& f : floors_) {
        if (f.floor_number == cleanRoom.floor_number) {
            floorExists = true;
            break;
        }
    }
    if (!floorExists) {
        errorMsg = "Assigned floor " + std::to_string(cleanRoom.floor_number) + " does not exist.";
        return false;
    }

    for (const auto& c : classrooms_) {
        if (c.room_number == cleanRoom.room_number) {
            errorMsg = "Classroom " + cleanRoom.room_number + " already exists.";
            return false;
        }
    }

    classrooms_.push_back(cleanRoom);
    std::sort(classrooms_.begin(), classrooms_.end(), [](const Classroom& a, const Classroom& b) {
        if (a.floor_number != b.floor_number) return a.floor_number < b.floor_number;
        return a.room_number < b.room_number;
    });
    saveRooms();
    return true;
}

bool Storage::updateClassroom(const std::string& room_number, const Classroom& room, std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto it = std::find_if(classrooms_.begin(), classrooms_.end(), [&](const Classroom& c) {
        return c.room_number == room_number;
    });
    if (it == classrooms_.end()) {
        errorMsg = "Classroom " + room_number + " not found.";
        return false;
    }

    Classroom cleanRoom = room;
    cleanRoom.room_number = trimWhitespace(room.room_number);
    cleanRoom.room_type = trimWhitespace(room.room_type);
    if (cleanRoom.room_number.empty()) {
        errorMsg = "Room number cannot be empty.";
        return false;
    }
    if (cleanRoom.capacity <= 0 || cleanRoom.capacity > 2000) {
        errorMsg = "Classroom capacity must be between 1 and 2000.";
        return false;
    }

    // Verify target floor exists
    bool floorExists = false;
    for (const auto& f : floors_) {
        if (f.floor_number == cleanRoom.floor_number) {
            floorExists = true;
            break;
        }
    }
    if (!floorExists) {
        errorMsg = "Assigned floor " + std::to_string(cleanRoom.floor_number) + " does not exist.";
        return false;
    }

    // If room number changed, check duplicate and cascade update timetable
    if (room_number != cleanRoom.room_number) {
        for (const auto& c : classrooms_) {
            if (c.room_number == cleanRoom.room_number) {
                errorMsg = "Classroom " + cleanRoom.room_number + " already exists.";
                return false;
            }
        }
        for (auto& entry : timetable_) {
            if (entry.room_number == room_number) {
                entry.room_number = cleanRoom.room_number;
            }
        }
        saveTimetable();
    }

    *it = cleanRoom;
    std::sort(classrooms_.begin(), classrooms_.end(), [](const Classroom& a, const Classroom& b) {
        if (a.floor_number != b.floor_number) return a.floor_number < b.floor_number;
        return a.room_number < b.room_number;
    });
    saveRooms();
    return true;
}

bool Storage::deleteClassroom(const std::string& room_number, std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto it = std::find_if(classrooms_.begin(), classrooms_.end(), [&](const Classroom& c) {
        return c.room_number == room_number;
    });
    if (it == classrooms_.end()) {
        errorMsg = "Classroom " + room_number + " not found.";
        return false;
    }

    classrooms_.erase(it);

    timetable_.erase(std::remove_if(timetable_.begin(), timetable_.end(), [&](const ScheduleEntry& s) {
        return s.room_number == room_number;
    }), timetable_.end());

    saveRooms();
    saveTimetable();
    return true;
}

// -------------------------------------------------------------
// Timetable Operations
// -------------------------------------------------------------
std::vector<ScheduleEntry> Storage::getTimetable() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return timetable_;
}

std::vector<ScheduleEntry> Storage::getTimetableForRoom(const std::string& room_number) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    std::vector<ScheduleEntry> res;
    for (const auto& s : timetable_) {
        if (s.room_number == room_number) {
            res.push_back(s);
        }
    }
    return res;
}

std::vector<ScheduleEntry> Storage::getTimetableForRoomAndDay(const std::string& room_number, const std::string& day) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    std::vector<ScheduleEntry> res;
    std::string normDay = TimeUtil::normalizeDayName(day);
    for (const auto& s : timetable_) {
        if (s.room_number == room_number && TimeUtil::normalizeDayName(s.day) == normDay) {
            res.push_back(s);
        }
    }
    std::sort(res.begin(), res.end(), [](const ScheduleEntry& a, const ScheduleEntry& b) {
        return a.start_minutes < b.start_minutes;
    });
    return res;
}

bool Storage::hasTimetableConfigured(const std::string& room_number) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    for (const auto& s : timetable_) {
        if (s.room_number == room_number) {
            return true;
        }
    }
    return false;
}

bool Storage::addScheduleEntry(const ScheduleEntry& entry, std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(dataMutex_);

    ScheduleEntry clean = entry;
    clean.room_number = trimWhitespace(entry.room_number);
    clean.section = trimWhitespace(entry.section);
    clean.subject = trimWhitespace(entry.subject);
    clean.start_time = trimWhitespace(entry.start_time);
    clean.end_time = trimWhitespace(entry.end_time);
    clean.start_minutes = TimeUtil::parseTimeToMinutes(clean.start_time);
    clean.end_minutes = TimeUtil::parseTimeToMinutes(clean.end_time);

    bool roomExists = false;
    for (const auto& c : classrooms_) {
        if (c.room_number == clean.room_number) {
            roomExists = true;
            break;
        }
    }
    if (!roomExists) {
        errorMsg = "Classroom " + clean.room_number + " is not configured in the building.";
        return false;
    }

    if (clean.section.empty() || clean.subject.empty()) {
        errorMsg = "Section and subject cannot be empty.";
        return false;
    }

    std::string normDay = TimeUtil::normalizeDayNameStrict(entry.day);
    if (normDay.empty()) {
        errorMsg = "Invalid day of week: '" + entry.day + "'.";
        return false;
    }

    if (clean.start_minutes < 0 || clean.end_minutes < 0) {
        errorMsg = "Invalid time format. Use 24-hour HH:MM (e.g. 09:00).";
        return false;
    }

    if (clean.start_minutes >= clean.end_minutes) {
        errorMsg = "End time (" + clean.end_time + ") must be after start time (" + clean.start_time + ").";
        return false;
    }

    // Normalize formatted times (e.g. "9:00" -> "09:00")
    clean.start_time = TimeUtil::minutesToTimeString(clean.start_minutes);
    clean.end_time = TimeUtil::minutesToTimeString(clean.end_minutes);
    clean.day = normDay;

    // Check collision with existing entries for this room on the same day
    for (const auto& s : timetable_) {
        if (s.room_number == clean.room_number && TimeUtil::normalizeDayName(s.day) == normDay) {
            if (TimeUtil::intervalsOverlap(clean.start_minutes, clean.end_minutes, s.start_minutes, s.end_minutes)) {
                errorMsg = "Schedule conflict: Classroom " + clean.room_number + " is already occupied by " +
                           s.subject + " (" + s.section + ") from " + s.start_time + " to " + s.end_time + ".";
                return false;
            }
        }
    }

    if (clean.id.empty()) {
        clean.id = generateUniqueId("sched");
    }
    timetable_.push_back(clean);
    saveTimetable();
    return true;
}

bool Storage::updateScheduleEntry(const std::string& id, const ScheduleEntry& entry, std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto it = std::find_if(timetable_.begin(), timetable_.end(), [&](const ScheduleEntry& s) {
        return s.id == id;
    });
    if (it == timetable_.end()) {
        errorMsg = "Schedule entry not found.";
        return false;
    }

    ScheduleEntry clean = entry;
    clean.room_number = trimWhitespace(entry.room_number);
    clean.section = trimWhitespace(entry.section);
    clean.subject = trimWhitespace(entry.subject);
    clean.start_time = trimWhitespace(entry.start_time);
    clean.end_time = trimWhitespace(entry.end_time);
    clean.start_minutes = TimeUtil::parseTimeToMinutes(clean.start_time);
    clean.end_minutes = TimeUtil::parseTimeToMinutes(clean.end_time);

    bool roomExists = false;
    for (const auto& c : classrooms_) {
        if (c.room_number == clean.room_number) {
            roomExists = true;
            break;
        }
    }
    if (!roomExists) {
        errorMsg = "Classroom " + clean.room_number + " is not configured in the building.";
        return false;
    }

    if (clean.section.empty() || clean.subject.empty()) {
        errorMsg = "Section and subject cannot be empty.";
        return false;
    }

    std::string normDay = TimeUtil::normalizeDayNameStrict(entry.day);
    if (normDay.empty()) {
        errorMsg = "Invalid day of week: '" + entry.day + "'.";
        return false;
    }

    if (clean.start_minutes < 0 || clean.end_minutes < 0) {
        errorMsg = "Invalid time format. Use 24-hour HH:MM (e.g. 09:00).";
        return false;
    }

    if (clean.start_minutes >= clean.end_minutes) {
        errorMsg = "End time must be after start time.";
        return false;
    }

    clean.start_time = TimeUtil::minutesToTimeString(clean.start_minutes);
    clean.end_time = TimeUtil::minutesToTimeString(clean.end_minutes);
    clean.day = normDay;

    for (const auto& s : timetable_) {
        if (s.id != id && s.room_number == clean.room_number && TimeUtil::normalizeDayName(s.day) == normDay) {
            if (TimeUtil::intervalsOverlap(clean.start_minutes, clean.end_minutes, s.start_minutes, s.end_minutes)) {
                errorMsg = "Schedule conflict with existing class " + s.subject + " (" + s.start_time + " - " + s.end_time + ").";
                return false;
            }
        }
    }

    clean.id = id;
    *it = clean;
    saveTimetable();
    return true;
}

bool Storage::deleteScheduleEntry(const std::string& id, std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto it = std::find_if(timetable_.begin(), timetable_.end(), [&](const ScheduleEntry& s) {
        return s.id == id;
    });
    if (it == timetable_.end()) {
        errorMsg = "Schedule entry not found.";
        return false;
    }
    timetable_.erase(it);
    saveTimetable();
    return true;
}

bool Storage::replaceTimetable(const std::vector<ScheduleEntry>& newEntries, std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (newEntries.empty()) {
        errorMsg = "Cannot replace timetable with an empty set.";
        return false;
    }
    timetable_ = newEntries;
    saveTimetable();
    return true;
}

bool Storage::appendTimetable(const std::vector<ScheduleEntry>& newEntries, int& addedCount, std::vector<std::string>& warnings) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    addedCount = 0;
    for (const auto& entry : newEntries) {
        bool roomExists = false;
        for (const auto& c : classrooms_) {
            if (c.room_number == entry.room_number) {
                roomExists = true;
                break;
            }
        }
        if (!roomExists) {
            warnings.push_back("Skipped entry for unknown room " + entry.room_number);
            continue;
        }

        if (entry.start_minutes < 0 || entry.end_minutes < 0 || entry.start_minutes >= entry.end_minutes) {
            warnings.push_back("Skipped entry with invalid time range for room " + entry.room_number);
            continue;
        }

        bool conflict = false;
        std::string normDay = TimeUtil::normalizeDayNameStrict(entry.day);
        if (normDay.empty()) {
            warnings.push_back("Skipped entry with invalid day '" + entry.day + "' for room " + entry.room_number);
            continue;
        }

        for (const auto& s : timetable_) {
            if (s.room_number == entry.room_number && TimeUtil::normalizeDayName(s.day) == normDay) {
                if (TimeUtil::intervalsOverlap(entry.start_minutes, entry.end_minutes, s.start_minutes, s.end_minutes)) {
                    warnings.push_back("Conflict for room " + entry.room_number + " on " + normDay +
                                       " (" + entry.start_time + "-" + entry.end_time + "): overlaps with " + s.subject +
                                       " (" + s.start_time + "-" + s.end_time + ")");
                    conflict = true;
                    break;
                }
            }
        }
        if (!conflict) {
            ScheduleEntry toAdd = entry;
            if (toAdd.id.empty()) {
                toAdd.id = generateUniqueId("sched");
            }
            toAdd.day = normDay;
            toAdd.start_time = TimeUtil::minutesToTimeString(toAdd.start_minutes);
            toAdd.end_time = TimeUtil::minutesToTimeString(toAdd.end_minutes);
            timetable_.push_back(toAdd);
            addedCount++;
        }
    }
    if (addedCount > 0) {
        saveTimetable();
    }
    return true;
}

// -------------------------------------------------------------
// Settings Operations
// -------------------------------------------------------------
Settings Storage::getSettings() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    Settings s = settings_;
    s.total_floors = static_cast<int>(floors_.size());
    return s;
}

bool Storage::updateSettings(const Settings& newSettings, const std::string& newPlaintextPassword, std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    int sMins = TimeUtil::parseTimeToMinutes(newSettings.operating_start);
    int eMins = TimeUtil::parseTimeToMinutes(newSettings.operating_end);
    if (sMins < 0 || eMins < 0) {
        errorMsg = "Invalid operating hours format. Use 24-hour HH:MM.";
        return false;
    }
    if (sMins >= eMins) {
        errorMsg = "Operating end time must be after operating start time.";
        return false;
    }

    std::string cName = trimWhitespace(newSettings.college_name);
    std::string bName = trimWhitespace(newSettings.building_name);
    if (cName.empty() || bName.empty()) {
        errorMsg = "College name and building name cannot be empty.";
        return false;
    }

    settings_.college_name = cName;
    settings_.building_name = bName;
    settings_.total_floors = static_cast<int>(floors_.size());
    settings_.operating_start = TimeUtil::minutesToTimeString(sMins);
    settings_.operating_end = TimeUtil::minutesToTimeString(eMins);
    settings_.operating_start_minutes = sMins;
    settings_.operating_end_minutes = eMins;

    if (!newPlaintextPassword.empty()) {
        if (newPlaintextPassword.size() < 6) {
            errorMsg = "Admin password must be at least 6 characters long.";
            return false;
        }
        settings_.admin_password_hash = hashPassword(newPlaintextPassword);
    }

    saveSettings();
    return true;
}

// -------------------------------------------------------------
// Authentication & Session Persistence
// -------------------------------------------------------------
bool Storage::hasAdminPasswordConfigured() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    return !settings_.admin_password_hash.empty() || !envAdminPassword_.empty();
}

bool Storage::validateAdminPassword(const std::string& password) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (password.empty()) return false;

    // 1. If password was explicitly updated via Admin Settings, validate against salted SHA-256 hash
    if (!settings_.admin_password_hash.empty()) {
        return verifyPasswordHash(password, settings_.admin_password_hash);
    }

    // 2. Otherwise validate against ADMIN_PASSWORD environment variable (constant-time)
    if (!envAdminPassword_.empty()) {
        return constantTimeEquals(password, envAdminPassword_);
    }

    // 3. Fail closed if no admin password is configured
    return false;
}

void Storage::pruneExpiredSessionsLocked() {
    int64_t now = nowEpochSeconds();
    for (auto it = activeSessions_.begin(); it != activeSessions_.end();) {
        if (it->second <= now) {
            it = activeSessions_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string Storage::createAdminSession() {
    std::lock_guard<std::mutex> lock(dataMutex_);
    pruneExpiredSessionsLocked();
    std::string token = "rw_sess_" + randomHex(40);
    int64_t expiresAt = nowEpochSeconds() + (24 * 60 * 60); // 24-hour TTL
    activeSessions_[token] = expiresAt;
    saveSessions();
    return token;
}

bool Storage::isValidSession(const std::string& token) {
    if (token.empty()) return false;
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto it = activeSessions_.find(token);
    if (it == activeSessions_.end()) return false;
    if (it->second <= nowEpochSeconds()) {
        activeSessions_.erase(it);
        saveSessions();
        return false;
    }
    return true;
}

void Storage::revokeSession(const std::string& token) {
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (activeSessions_.erase(token) > 0) {
        saveSessions();
    }
}

// -------------------------------------------------------------
// File Persistence
// -------------------------------------------------------------
bool Storage::loadFloors() {
    std::string path = dataDir_ + "/floors.json";
    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        nlohmann::json j;
        file >> j;
        if (!j.is_array()) return false;
        floors_.clear();
        for (const auto& item : j) {
            floors_.push_back(Floor::fromJson(item));
        }
        std::sort(floors_.begin(), floors_.end(), [](const Floor& a, const Floor& b) {
            return a.floor_number < b.floor_number;
        });
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Storage] Error parsing floors.json: " << e.what() << std::endl;
        return false;
    }
}

bool Storage::saveFloors() {
    std::string path = dataDir_ + "/floors.json";
    nlohmann::json j = nlohmann::json::array();
    for (const auto& f : floors_) {
        j.push_back(f.toJson());
    }
    return atomicWriteFile(path, j.dump(4));
}

bool Storage::loadRooms() {
    std::string path = dataDir_ + "/rooms.json";
    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        nlohmann::json j;
        file >> j;
        if (!j.is_array()) return false;
        classrooms_.clear();
        for (const auto& item : j) {
            classrooms_.push_back(Classroom::fromJson(item));
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Storage] Error parsing rooms.json: " << e.what() << std::endl;
        return false;
    }
}

bool Storage::saveRooms() {
    std::string path = dataDir_ + "/rooms.json";
    nlohmann::json j = nlohmann::json::array();
    for (const auto& c : classrooms_) {
        j.push_back(c.toJson());
    }
    return atomicWriteFile(path, j.dump(4));
}

bool Storage::loadTimetable() {
    std::string path = dataDir_ + "/timetable.json";
    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        nlohmann::json j;
        file >> j;
        if (!j.is_array()) return false;
        timetable_.clear();
        for (const auto& item : j) {
            ScheduleEntry e = ScheduleEntry::fromJson(item);
            if (!e.room_number.empty() && e.start_minutes >= 0 && e.end_minutes > e.start_minutes) {
                e.day = TimeUtil::normalizeDayName(e.day);
                timetable_.push_back(e);
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Storage] Error parsing timetable.json: " << e.what() << std::endl;
        return false;
    }
}

bool Storage::saveTimetable() {
    std::string path = dataDir_ + "/timetable.json";
    nlohmann::json j = nlohmann::json::array();
    for (const auto& s : timetable_) {
        j.push_back(s.toJson());
    }
    return atomicWriteFile(path, j.dump(4));
}

bool Storage::loadSettings() {
    std::string path = dataDir_ + "/settings.json";
    std::ifstream file(path);
    if (!file.is_open()) {
        settings_ = Settings();
        saveSettings();
        return true;
    }

    try {
        nlohmann::json j;
        file >> j;
        settings_ = Settings::fromJson(j);
        if (settings_.operating_start_minutes < 0 || settings_.operating_end_minutes <= settings_.operating_start_minutes) {
            settings_.operating_start = "08:00";
            settings_.operating_end = "18:00";
            settings_.operating_start_minutes = 480;
            settings_.operating_end_minutes = 1080;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Storage] Error parsing settings.json: " << e.what() << std::endl;
        settings_ = Settings();
        return false;
    }
}

bool Storage::saveSettings() {
    std::string path = dataDir_ + "/settings.json";
    nlohmann::json j = settings_.toJson(true);
    return atomicWriteFile(path, j.dump(4));
}

bool Storage::loadSessions() {
    std::string path = dataDir_ + "/active_sessions.json";
    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        nlohmann::json j;
        file >> j;
        activeSessions_.clear();
        if (j.is_object()) {
            int64_t now = nowEpochSeconds();
            for (auto& kv : j.items()) {
                if (kv.value().is_number_integer()) {
                    int64_t exp = kv.value().get<int64_t>();
                    if (exp > now) {
                        activeSessions_[kv.key()] = exp;
                    }
                }
            }
        }
        return true;
    } catch (...) {
        activeSessions_.clear();
        return false;
    }
}

bool Storage::saveSessions() {
    std::string path = dataDir_ + "/active_sessions.json";
    nlohmann::json j = nlohmann::json::object();
    for (const auto& kv : activeSessions_) {
        j[kv.first] = kv.second;
    }
    return atomicWriteFile(path, j.dump(2));
}

// -------------------------------------------------------------
// Seed Realistic Sample Data
// -------------------------------------------------------------
void Storage::seedDefaultData() {
    floors_.clear();
    classrooms_.clear();
    timetable_.clear();
    settings_ = Settings();

    struct FloorMeta {
        int num;
        std::string name;
        std::string desc;
    };

    std::vector<FloorMeta> floorMetaList = {
        {1, "Floor 1 - Foundation & Applied Sciences", "First-year general classrooms, Physics and Chemistry lecture halls"},
        {2, "Floor 2 - Computer Science Core Block", "Core programming labs, Data Structures and Algorithms lecture rooms"},
        {3, "Floor 3 - Software Engineering & Databases", "Database management, Web Technologies and Object-Oriented design studios"},
        {4, "Floor 4 - Systems & Networking Wing", "Operating Systems, Computer Networks and Microprocessor research halls"},
        {5, "Floor 5 - AI & Data Science Center", "Machine Learning, Artificial Intelligence and Big Data Analytics labs"},
        {6, "Floor 6 - Cyber Security & Cloud Wing", "Cryptography, Cloud Computing and Information Security seminar rooms"},
        {7, "Floor 7 - Advanced Computing & Honors", "Compiler Design, Distributed Systems and Honors elective classrooms"},
        {8, "Floor 8 - Post Graduate & Research Wing", "M.Tech lecture theatres, faculty research cubicles and conference rooms"},
        {9, "Floor 9 - Innovation & Seminar Theatres", "Project incubation suites, multipurpose auditoriums and hackathon halls"}
    };

    for (const auto& fm : floorMetaList) {
        Floor f;
        f.floor_number = fm.num;
        f.floor_name = fm.name;
        f.description = fm.desc;
        floors_.push_back(f);
    }

    for (int fl = 1; fl <= 9; ++fl) {
        for (int r = 1; r <= 16; ++r) {
            Classroom c;
            int roomNum = fl * 100 + r;
            c.room_number = std::to_string(roomNum);
            c.floor_number = fl;
            c.capacity = (r <= 4) ? 75 : ((r <= 12) ? 60 : 45);
            if (r == 1 || r == 2) {
                c.room_type = "Smart Lecture Hall";
            } else if (r == 7 || r == 8) {
                c.room_type = "Computing Laboratory";
            } else if (r == 15 || r == 16) {
                c.room_type = "Seminar & Tutorial Room";
            } else {
                c.room_type = "Standard Classroom";
            }
            c.is_active = true;
            classrooms_.push_back(c);
        }
    }

    std::vector<std::string> days = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

    int entryId = 1000;
    auto addEntry = [&](const std::string& room, const std::string& day, const std::string& sec,
                        const std::string& subj, const std::string& st, const std::string& et) {
        ScheduleEntry e;
        e.id = "sample_" + std::to_string(++entryId);
        e.room_number = room;
        e.day = day;
        e.section = sec;
        e.subject = subj;
        e.start_time = st;
        e.end_time = et;
        e.start_minutes = TimeUtil::parseTimeToMinutes(st);
        e.end_minutes = TimeUtil::parseTimeToMinutes(et);
        timetable_.push_back(e);
    };

    for (const auto& day : days) {
        addEntry("101", day, "CSE-1A", "Applied Mathematics I", "09:00", "10:00");
        addEntry("101", day, "CSE-1A", "Engineering Physics", "10:00", "11:00");
        addEntry("101", day, "CSE-1B", "Basic Electrical Engg", "11:30", "12:30");

        addEntry("102", day, "CSE-1B", "Applied Mathematics I", "09:00", "10:00");
        addEntry("102", day, "CSE-1C", "Engineering Chemistry", "11:30", "12:30");
        addEntry("102", day, "CSE-1A", "Environmental Studies", "15:00", "16:00");

        addEntry("103", day, "CSE-1C", "Programming in C", "13:00", "14:00");
        addEntry("103", day, "CSE-1C", "Applied Mathematics I", "14:00", "15:00");

        addEntry("104", day, "CSE-1D", "Engineering Physics", "09:00", "10:00");
        addEntry("104", day, "CSE-1D", "Programming in C", "10:00", "11:00");
        addEntry("104", day, "CSE-1D", "Physics Lab", "14:00", "16:00");

        addEntry("105", day, "IT-1A", "Mathematics I", "11:00", "12:00");
        addEntry("105", day, "IT-1A", "Engineering Mechanics", "12:00", "13:00");

        addEntry("106", day, "CSE-1E", "Communication Skills", "10:00", "11:00");
        addEntry("106", day, "CSE-1E", "Programming in C", "14:00", "15:00");
        addEntry("107", day, "CSE-1F", "Basic Electrical", "09:00", "10:00");
        addEntry("107", day, "CSE-1F", "Mathematics I", "11:30", "12:30");
        addEntry("108", day, "IT-1B", "Chemistry", "10:00", "12:00");
        addEntry("109", day, "CSE-1B", "Engineering Graphics", "13:30", "15:30");
        addEntry("110", day, "CSE-1C", "Physics", "09:00", "10:00");
    }

    for (const auto& day : days) {
        addEntry("201", day, "CS-2A", "Data Structures & Algorithms", "09:00", "10:00");
        addEntry("201", day, "CS-2A", "Discrete Mathematics", "10:00", "11:00");
        addEntry("201", day, "CS-2B", "Digital Logic Design", "11:30", "12:30");
        addEntry("201", day, "CS-2A", "Data Structures Lab", "14:00", "16:00");

        addEntry("202", day, "CS-2B", "Data Structures & Algorithms", "09:00", "10:00");
        addEntry("202", day, "CS-2B", "Object-Oriented Programming (C++)", "10:00", "11:00");
        addEntry("202", day, "CS-2A", "Computer Organization", "13:00", "14:00");

        addEntry("203", day, "CS-2C", "Discrete Mathematics", "11:00", "12:00");
        addEntry("203", day, "CS-2C", "Data Structures & Algorithms", "12:00", "13:00");

        addEntry("204", day, "CS-2D", "Object-Oriented Programming (Java)", "09:00", "10:00");
        addEntry("204", day, "CS-2D", "Digital Electronics", "10:00", "11:00");
        addEntry("204", day, "CS-2D", "Python Programming", "14:00", "15:30");

        addEntry("205", day, "CS-2A", "Computer Organization", "09:00", "10:00");
        addEntry("205", day, "CS-2C", "OOP with Java", "15:00", "16:00");
        addEntry("206", day, "CS-2B", "Discrete Mathematics", "10:00", "11:00");
        addEntry("207", day, "CS-2C", "Data Structures", "13:00", "14:00");
        addEntry("208", day, "CS-2D", "Software Engineering", "11:30", "12:30");
    }

    for (const auto& day : days) {
        addEntry("301", day, "CS-3A", "Database Management Systems", "09:00", "10:00");
        addEntry("301", day, "CS-3A", "Operating Systems", "10:00", "11:00");
        addEntry("301", day, "CS-3A", "DBMS Lab", "14:00", "16:00");

        addEntry("302", day, "CS-3B", "Theory of Computation", "09:00", "10:00");
        addEntry("302", day, "CS-3B", "Database Management Systems", "10:00", "11:00");
        addEntry("302", day, "CS-3C", "Operating Systems", "11:30", "12:30");

        addEntry("303", day, "CS-3C", "Software Engineering", "10:00", "11:00");
        addEntry("303", day, "CS-3C", "Database Management Systems", "13:00", "14:00");
        addEntry("304", day, "CS-3D", "Web Technologies", "09:00", "11:00");
        addEntry("305", day, "CS-3A", "Design & Analysis of Algorithms", "11:30", "12:30");
    }

    for (const auto& day : days) {
        addEntry("401", day, "CS-3B", "Computer Networks", "09:00", "10:00");
        addEntry("401", day, "CS-3B", "Microprocessors & Interfacing", "10:00", "11:00");
        addEntry("401", day, "CS-3B", "Networks Simulation Lab", "14:00", "16:00");

        addEntry("402", day, "CS-3A", "Computer Networks", "10:00", "11:00");
        addEntry("402", day, "CS-3C", "Cloud Computing Foundations", "11:30", "12:30");
        addEntry("403", day, "CS-3D", "Embedded Systems", "13:00", "14:00");
    }

    for (const auto& day : days) {
        addEntry("501", day, "AIML-4A", "Machine Learning Techniques", "09:00", "10:00");
        addEntry("501", day, "AIML-4A", "Artificial Intelligence", "10:00", "11:00");
        addEntry("501", day, "AIML-4A", "ML Python Practical", "14:00", "16:00");

        addEntry("502", day, "DS-4A", "Big Data Analytics", "09:00", "10:00");
        addEntry("502", day, "DS-4A", "Data Mining & Warehousing", "10:00", "11:00");
        addEntry("502", day, "AIML-4B", "Deep Learning Architectures", "11:30", "12:30");

        addEntry("503", day, "DS-4B", "Statistical Methods for Data Science", "10:00", "11:00");
        addEntry("504", day, "AIML-4B", "Natural Language Processing", "13:00", "14:00");
    }

    for (const auto& day : days) {
        addEntry("601", day, "CS-4A", "Cryptography & Network Security", "09:00", "10:00");
        addEntry("601", day, "CS-4A", "Cloud Architecture & Virtualization", "10:00", "11:00");
        addEntry("602", day, "CS-4B", "Cyber Forensics & Incident Handling", "11:30", "12:30");
        addEntry("603", day, "CS-4C", "Ethical Hacking Workshop", "14:00", "16:00");
    }

    for (const auto& day : days) {
        addEntry("701", day, "CS-4D", "Compiler Design", "09:00", "10:00");
        addEntry("701", day, "CS-4D", "Distributed Systems", "10:00", "11:00");
        addEntry("702", day, "CS-Honors", "Quantum Computing Intro", "11:30", "12:30");
    }

    for (const auto& day : days) {
        addEntry("801", day, "MTech-CSE", "Advanced Algorithms Analysis", "10:00", "11:30");
        addEntry("802", day, "MTech-AI", "Advanced Computer Vision", "13:00", "14:30");
    }

    for (const auto& day : days) {
        addEntry("901", day, "All-CSE", "Department Seminar & Tech Talk", "14:00", "16:00");
    }

    saveFloors();
    saveRooms();
    saveTimetable();
    saveSettings();

    std::cout << "[Storage] Seeded default sample data: "
              << floors_.size() << " floors, "
              << classrooms_.size() << " rooms, "
              << timetable_.size() << " timetable records." << std::endl;
}

} // namespace roomwise
