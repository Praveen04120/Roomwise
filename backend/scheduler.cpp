#include "scheduler.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace roomwise {

Scheduler::Scheduler(Storage& storage) : storage_(storage) {}

std::string Scheduler::resolveDayStrict(const std::string& date_or_day) {
    if (date_or_day.empty()) {
        std::string today = TimeUtil::getCurrentDateString();
        return TimeUtil::getDayOfWeekFromDate(today);
    }
    if (date_or_day.find('-') != std::string::npos || date_or_day.find('/') != std::string::npos) {
        return TimeUtil::getDayOfWeekFromDate(date_or_day);
    }
    return TimeUtil::normalizeDayNameStrict(date_or_day);
}

std::string Scheduler::resolveDay(const std::string& date_or_day) {
    std::string strict = resolveDayStrict(date_or_day);
    if (!strict.empty()) return strict;
    std::string today = TimeUtil::getCurrentDateString();
    return TimeUtil::getDayOfWeekFromDate(today);
}

RoomStatusResult Scheduler::getRoomStatus(const std::string& room_number,
                                          const std::string& date_or_day,
                                          int current_minutes) {
    RoomStatusResult result;
    result.room_number = room_number;

    Classroom room;
    if (!storage_.getClassroom(room_number, room)) {
        result.status = "UNAVAILABLE";
        result.status_label = "Classroom Not Found";
        return result;
    }

    result.floor_number = room.floor_number;
    result.capacity = room.capacity;
    result.room_type = room.room_type;

    Floor floor;
    if (storage_.getFloor(room.floor_number, floor)) {
        result.floor_name = floor.floor_name;
    }

    if (!room.is_active) {
        result.status = "UNAVAILABLE";
        result.status_label = "Room Inactive";
        return result;
    }

    // Check if room has timetable configured in system
    bool hasConfig = storage_.hasTimetableConfigured(room_number);
    result.has_schedule_data = hasConfig;

    if (!hasConfig) {
        result.status = "UNAVAILABLE";
        result.status_label = "No Timetable Data";
        return result;
    }

    std::string day = resolveDay(date_or_day);
    Settings settings = storage_.getSettings();

    if (current_minutes < 0) {
        current_minutes = TimeUtil::getCurrentMinutesNow();
    }

    // Daily slots breakdown
    result.daily_slots = getDailyAvailability(room_number, day);

    // Outside operating hours check
    if (current_minutes < settings.operating_start_minutes || current_minutes >= settings.operating_end_minutes) {
        result.status = "UNAVAILABLE";
        result.status_label = "Closed (Outside Operating Hours)";
        return result;
    }

    // Check classes on this day (already sorted by start_minutes)
    std::vector<ScheduleEntry> entries = storage_.getTimetableForRoomAndDay(room_number, day);

    const ScheduleEntry* currentClass = nullptr;
    const ScheduleEntry* nextClass = nullptr;
    const ScheduleEntry* prevClass = nullptr;

    for (const auto& entry : entries) {
        if (entry.start_minutes <= current_minutes && entry.end_minutes > current_minutes) {
            currentClass = &entry;
            break;
        }
    }

    for (const auto& entry : entries) {
        if (entry.end_minutes <= current_minutes) {
            if (!prevClass || entry.end_minutes > prevClass->end_minutes) {
                prevClass = &entry;
            }
        }
        if (&entry != currentClass && entry.start_minutes >= current_minutes) {
            if (!nextClass || entry.start_minutes < nextClass->start_minutes) {
                nextClass = &entry;
            }
        }
    }

    if (prevClass) {
        result.previous_class = prevClass->subject + " (" + prevClass->section + ") until " + prevClass->end_time;
    }

    if (currentClass) {
        result.status = "OCCUPIED";
        result.status_label = "Occupied";
        result.current_class = currentClass->section;
        result.current_subject = currentClass->subject;
        result.current_time_slot = currentClass->start_time + " - " + currentClass->end_time;

        // Calculate when room next becomes free across any consecutive classes
        int freeFrom = currentClass->end_minutes;
        for (const auto& entry : entries) {
            if (entry.start_minutes <= freeFrom && entry.end_minutes > freeFrom) {
                freeFrom = entry.end_minutes;
            }
        }
        if (freeFrom < settings.operating_end_minutes) {
            result.next_available_slot = "Available from " + TimeUtil::minutesToTimeString(freeFrom);
        } else {
            result.next_available_slot = "Occupied for rest of day";
        }
    } else {
        result.status = "AVAILABLE";
        result.status_label = "Available";

        if (nextClass) {
            result.next_class = nextClass->subject + " (" + nextClass->section + ") at " + nextClass->start_time;
            result.next_available_slot = "Free until " + nextClass->start_time;
        } else {
            result.next_available_slot = "Free until " + settings.operating_end + " (Closing)";
        }
    }

    return result;
}

std::vector<AvailableSlot> Scheduler::getDailyAvailability(const std::string& room_number,
                                                           const std::string& date_or_day) {
    std::vector<AvailableSlot> slots;
    Classroom room;
    if (!storage_.getClassroom(room_number, room) || !room.is_active) {
        return slots;
    }
    if (!storage_.hasTimetableConfigured(room_number)) {
        return slots;
    }

    std::string day = resolveDay(date_or_day);
    Settings settings = storage_.getSettings();
    int opStart = settings.operating_start_minutes;
    int opEnd = settings.operating_end_minutes;

    std::vector<ScheduleEntry> entries = storage_.getTimetableForRoomAndDay(room_number, day);

    std::vector<ScheduleEntry> validEntries;
    for (const auto& e : entries) {
        if (e.start_minutes < opEnd && e.end_minutes > opStart) {
            validEntries.push_back(e);
        }
    }

    std::sort(validEntries.begin(), validEntries.end(), [](const ScheduleEntry& a, const ScheduleEntry& b) {
        return a.start_minutes < b.start_minutes;
    });

    int current = opStart;

    for (const auto& e : validEntries) {
        int classStart = std::max(opStart, e.start_minutes);
        int classEnd = std::min(opEnd, e.end_minutes);

        if (classStart > current) {
            AvailableSlot freeSlot;
            freeSlot.start_minutes = current;
            freeSlot.end_minutes = classStart;
            freeSlot.start_time = TimeUtil::minutesToTimeString(current);
            freeSlot.end_time = TimeUtil::minutesToTimeString(classStart);
            freeSlot.status = "AVAILABLE";
            slots.push_back(freeSlot);
        }

        AvailableSlot occSlot;
        occSlot.start_minutes = classStart;
        occSlot.end_minutes = classEnd;
        occSlot.start_time = TimeUtil::minutesToTimeString(classStart);
        occSlot.end_time = TimeUtil::minutesToTimeString(classEnd);
        occSlot.status = "OCCUPIED";
        occSlot.section = e.section;
        occSlot.subject = e.subject;
        slots.push_back(occSlot);

        current = std::max(current, classEnd);
    }

    if (current < opEnd) {
        AvailableSlot freeSlot;
        freeSlot.start_minutes = current;
        freeSlot.end_minutes = opEnd;
        freeSlot.start_time = TimeUtil::minutesToTimeString(current);
        freeSlot.end_time = TimeUtil::minutesToTimeString(opEnd);
        freeSlot.status = "AVAILABLE";
        slots.push_back(freeSlot);
    }

    return slots;
}

std::vector<RoomStatusResult> Scheduler::findAvailableRooms(const std::string& date_or_day,
                                                            int start_minutes,
                                                            int end_minutes,
                                                            int floor_filter) {
    std::vector<RoomStatusResult> results;
    if (start_minutes < 0 || end_minutes < 0 || start_minutes >= end_minutes) {
        return results;
    }

    // Respect college operating hours strictly
    Settings settings = storage_.getSettings();
    if (start_minutes < settings.operating_start_minutes || end_minutes > settings.operating_end_minutes) {
        return results;
    }

    std::string day = resolveDay(date_or_day);
    std::vector<Classroom> classrooms = storage_.getClassrooms();

    for (const auto& room : classrooms) {
        if (!room.is_active) {
            continue;
        }
        if (floor_filter > 0 && room.floor_number != floor_filter) {
            continue;
        }

        // Must have verified schedule data configured (never falsely mark unconfigured rooms empty)
        if (!storage_.hasTimetableConfigured(room.room_number)) {
            continue;
        }

        std::vector<ScheduleEntry> entries = storage_.getTimetableForRoomAndDay(room.room_number, day);

        bool isOccupied = false;
        for (const auto& e : entries) {
            if (TimeUtil::intervalsOverlap(e.start_minutes, e.end_minutes, start_minutes, end_minutes)) {
                isOccupied = true;
                break;
            }
        }

        if (!isOccupied) {
            RoomStatusResult res;
            res.room_number = room.room_number;
            res.floor_number = room.floor_number;
            res.capacity = room.capacity;
            res.room_type = room.room_type;
            res.status = "AVAILABLE";
            res.status_label = "Available";
            res.has_schedule_data = true;

            Floor floor;
            if (storage_.getFloor(room.floor_number, floor)) {
                res.floor_name = floor.floor_name;
            }

            const ScheduleEntry* prevClass = nullptr;
            const ScheduleEntry* nextClass = nullptr;

            for (const auto& e : entries) {
                if (e.end_minutes <= start_minutes) {
                    if (!prevClass || e.end_minutes > prevClass->end_minutes) {
                        prevClass = &e;
                    }
                }
                if (e.start_minutes >= end_minutes) {
                    if (!nextClass || e.start_minutes < nextClass->start_minutes) {
                        nextClass = &e;
                    }
                }
            }

            if (prevClass) {
                res.previous_class = prevClass->subject + " (" + prevClass->section + ") until " + prevClass->end_time;
            } else {
                res.previous_class = "No prior class";
            }

            if (nextClass) {
                res.next_class = nextClass->subject + " (" + nextClass->section + ") at " + nextClass->start_time;
            } else {
                res.next_class = "No scheduled class after";
            }

            res.current_time_slot = TimeUtil::minutesToTimeString(start_minutes) + " - " + TimeUtil::minutesToTimeString(end_minutes);
            results.push_back(res);
        }
    }

    std::sort(results.begin(), results.end(), [](const RoomStatusResult& a, const RoomStatusResult& b) {
        if (a.floor_number != b.floor_number) return a.floor_number < b.floor_number;
        return a.room_number < b.room_number;
    });

    return results;
}

std::vector<RoomStatusResult> Scheduler::getFloorRoomStatuses(int floor_number,
                                                              const std::string& date_or_day,
                                                              int current_minutes) {
    std::vector<RoomStatusResult> results;
    std::vector<Classroom> rooms = storage_.getClassroomsByFloor(floor_number);

    for (const auto& room : rooms) {
        results.push_back(getRoomStatus(room.room_number, date_or_day, current_minutes));
    }
    return results;
}

nlohmann::json Scheduler::getCampusSummary(const std::string& date_or_day, int current_minutes) {
    std::vector<Classroom> allRooms = storage_.getClassrooms();
    std::vector<Floor> allFloors = storage_.getFloors();
    std::vector<ScheduleEntry> timetable = storage_.getTimetable();
    Settings settings = storage_.getSettings();

    int effectiveMins = (current_minutes < 0) ? TimeUtil::getCurrentMinutesNow() : current_minutes;
    bool outsideHours = (effectiveMins < settings.operating_start_minutes || effectiveMins >= settings.operating_end_minutes);

    int totalRooms = static_cast<int>(allRooms.size());
    int availableRooms = 0;
    int occupiedRooms = 0;
    int unconfiguredRooms = 0;

    for (const auto& room : allRooms) {
        RoomStatusResult status = getRoomStatus(room.room_number, date_or_day, effectiveMins);
        if (status.status == "AVAILABLE") {
            availableRooms++;
        } else if (status.status == "OCCUPIED") {
            occupiedRooms++;
        } else {
            unconfiguredRooms++;
        }
    }

    return {
        {"total_floors", static_cast<int>(allFloors.size())},
        {"total_rooms", totalRooms},
        {"total_timetable_entries", static_cast<int>(timetable.size())},
        {"available_rooms", availableRooms},
        {"occupied_rooms", occupiedRooms},
        {"unconfigured_rooms", unconfiguredRooms},
        {"outside_operating_hours", outsideHours},
        {"operating_start", settings.operating_start},
        {"operating_end", settings.operating_end},
        {"resolved_day", resolveDay(date_or_day)}
    };
}

// -------------------------------------------------------------
// CSV Validation & Import (RFC-4180 Compliant)
// -------------------------------------------------------------
static std::string trimCsvCell(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

static std::vector<std::string> parseCsvLine(const std::string& line) {
    std::vector<std::string> cells;
    std::string current;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current.push_back('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                current.push_back(c);
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (c == ',') {
                cells.push_back(trimCsvCell(current));
                current.clear();
            } else if (c != '\r') {
                current.push_back(c);
            }
        }
    }
    cells.push_back(trimCsvCell(current));
    return cells;
}

CSVValidationResult Scheduler::validateCSV(const std::string& csvContent) {
    CSVValidationResult result;

    // Strip UTF-8 BOM if present
    std::string content = csvContent;
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content = content.substr(3);
    }

    std::stringstream ss(content);
    std::string line;

    // Read header line
    if (!std::getline(ss, line)) {
        result.summary_errors.push_back("CSV file is empty.");
        return result;
    }

    std::vector<std::string> headerCells = parseCsvLine(line);
    if (headerCells.size() < 6) {
        result.summary_errors.push_back("Invalid CSV header. Expected 6 columns: room_number,section,subject,day,start_time,end_time");
        return result;
    }

    std::string firstHeader = headerCells[0];
    std::transform(firstHeader.begin(), firstHeader.end(), firstHeader.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (firstHeader != "room_number" && firstHeader != "room") {
        result.summary_errors.push_back("CSV header must start with 'room_number' (expected: room_number,section,subject,day,start_time,end_time).");
        return result;
    }

    std::vector<Classroom> classrooms = storage_.getClassrooms();
    std::map<std::string, bool> roomExistsMap;
    for (const auto& c : classrooms) {
        roomExistsMap[c.room_number] = true;
    }

    std::vector<ScheduleEntry> existingTimetable = storage_.getTimetable();

    struct ParsedRow {
        std::string room;
        std::string day;
        int start;
        int end;
        int rowIdx;
    };
    std::vector<ParsedRow> validParsedRows;

    int lineNum = 1; // Line 1 is header
    while (std::getline(ss, line)) {
        lineNum++;
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }

        std::vector<std::string> cells = parseCsvLine(line);

        CSVRowValidation rowVal;
        rowVal.row_number = lineNum;
        result.total_rows++;

        if (cells.size() < 6) {
            rowVal.is_valid = false;
            rowVal.error_message = "Expected 6 columns (room_number,section,subject,day,start_time,end_time), found " + std::to_string(cells.size());
            result.invalid_rows++;
            result.rows.push_back(rowVal);
            continue;
        }

        rowVal.room_number = cells[0];
        rowVal.section = cells[1];
        rowVal.subject = cells[2];
        rowVal.day = cells[3];
        rowVal.start_time = cells[4];
        rowVal.end_time = cells[5];

        // 1. Validate Room Number
        if (rowVal.room_number.empty() || !roomExistsMap[rowVal.room_number]) {
            rowVal.is_valid = false;
            rowVal.error_message = "Room '" + rowVal.room_number + "' is not configured in building.";
            result.invalid_rows++;
            result.rows.push_back(rowVal);
            continue;
        }

        // 2. Validate Section & Subject
        if (rowVal.section.empty() || rowVal.subject.empty()) {
            rowVal.is_valid = false;
            rowVal.error_message = "Section and subject must not be empty.";
            result.invalid_rows++;
            result.rows.push_back(rowVal);
            continue;
        }

        // 3. Validate Day strictly
        std::string normDay = TimeUtil::normalizeDayNameStrict(rowVal.day);
        if (normDay.empty()) {
            rowVal.is_valid = false;
            rowVal.error_message = "Invalid day name '" + rowVal.day + "' (use Monday..Sunday).";
            result.invalid_rows++;
            result.rows.push_back(rowVal);
            continue;
        }
        rowVal.day = normDay;

        // 4. Validate Times strictly
        int sMins = TimeUtil::parseTimeToMinutes(rowVal.start_time);
        int eMins = TimeUtil::parseTimeToMinutes(rowVal.end_time);

        if (sMins < 0 || eMins < 0) {
            rowVal.is_valid = false;
            rowVal.error_message = "Invalid time format (must be 24h HH:MM, e.g. 09:30).";
            result.invalid_rows++;
            result.rows.push_back(rowVal);
            continue;
        }

        if (sMins >= eMins) {
            rowVal.is_valid = false;
            rowVal.error_message = "Start time (" + rowVal.start_time + ") must be earlier than end time (" + rowVal.end_time + ").";
            result.invalid_rows++;
            result.rows.push_back(rowVal);
            continue;
        }

        rowVal.start_time = TimeUtil::minutesToTimeString(sMins);
        rowVal.end_time = TimeUtil::minutesToTimeString(eMins);

        // 5. Conflict check with other rows in this CSV
        bool conflict = false;
        for (const auto& pr : validParsedRows) {
            if (pr.room == rowVal.room_number && pr.day == normDay) {
                if (TimeUtil::intervalsOverlap(sMins, eMins, pr.start, pr.end)) {
                    rowVal.is_valid = false;
                    rowVal.error_message = "Conflicts with row " + std::to_string(pr.rowIdx) + " in this file.";
                    conflict = true;
                    break;
                }
            }
        }

        if (conflict) {
            result.invalid_rows++;
            result.rows.push_back(rowVal);
            continue;
        }

        // 6. Check conflict against existing stored timetable (informational warning for Append mode)
        for (const auto& ex : existingTimetable) {
            if (ex.room_number == rowVal.room_number && TimeUtil::normalizeDayName(ex.day) == normDay) {
                if (TimeUtil::intervalsOverlap(sMins, eMins, ex.start_minutes, ex.end_minutes)) {
                    rowVal.conflicts_existing = true;
                    rowVal.warning_message = "Overlaps existing class: " + ex.subject + " (" + ex.start_time + "-" + ex.end_time + "). Skipped in Append mode.";
                    result.existing_conflicts++;
                    break;
                }
            }
        }

        rowVal.is_valid = true;
        result.valid_rows++;
        validParsedRows.push_back({rowVal.room_number, normDay, sMins, eMins, lineNum});
        result.rows.push_back(rowVal);
    }

    if (result.total_rows == 0) {
        result.summary_errors.push_back("CSV contains header only and no data rows.");
    }

    result.can_import = (result.valid_rows > 0);
    return result;
}

bool Scheduler::importCSV(const std::string& csvContent, bool replaceExisting,
                          int& importedCount, std::vector<std::string>& warnings, std::string& errorMsg) {
    CSVValidationResult valResult = validateCSV(csvContent);
    if (!valResult.can_import) {
        if (!valResult.summary_errors.empty()) {
            errorMsg = valResult.summary_errors[0];
        } else {
            errorMsg = "CSV contains no valid timetable rows to import.";
        }
        return false;
    }

    std::vector<ScheduleEntry> entriesToImport;
    for (const auto& row : valResult.rows) {
        if (row.is_valid) {
            ScheduleEntry e;
            e.id = storage_.generateUniqueId("csv");
            e.room_number = row.room_number;
            e.section = row.section;
            e.subject = row.subject;
            e.day = row.day;
            e.start_time = row.start_time;
            e.end_time = row.end_time;
            e.start_minutes = TimeUtil::parseTimeToMinutes(row.start_time);
            e.end_minutes = TimeUtil::parseTimeToMinutes(row.end_time);
            entriesToImport.push_back(e);
        } else {
            warnings.push_back("Row " + std::to_string(row.row_number) + " skipped: " + row.error_message);
        }
    }

    if (replaceExisting) {
        if (!storage_.replaceTimetable(entriesToImport, errorMsg)) {
            return false;
        }
        importedCount = static_cast<int>(entriesToImport.size());
        return true;
    } else {
        return storage_.appendTimetable(entriesToImport, importedCount, warnings);
    }
}

} // namespace roomwise
