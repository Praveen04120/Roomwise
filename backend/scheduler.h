#ifndef ROOMWISE_SCHEDULER_H
#define ROOMWISE_SCHEDULER_H

#include <string>
#include <vector>
#include <memory>
#include "models.h"
#include "storage.h"

namespace roomwise {

// CSV Preview and Validation Row
struct CSVRowValidation {
    int row_number = 0;
    std::string room_number = "";
    std::string section = "";
    std::string subject = "";
    std::string day = "";
    std::string start_time = "";
    std::string end_time = "";
    bool is_valid = false;
    bool conflicts_existing = false;
    std::string error_message = "";
    std::string warning_message = "";
};

// CSV Validation Summary
struct CSVValidationResult {
    bool can_import = false;
    int total_rows = 0;
    int valid_rows = 0;
    int invalid_rows = 0;
    int existing_conflicts = 0;
    std::vector<CSVRowValidation> rows;
    std::vector<std::string> summary_errors;
};

class Scheduler {
public:
    Scheduler(Storage& storage);

    // Calculate the status of a specific room at a given time on a given date/day
    RoomStatusResult getRoomStatus(const std::string& room_number,
                                   const std::string& date_or_day,
                                   int current_minutes = -1);

    // Find all available rooms for a requested time interval [start_minutes, end_minutes]
    // floor_filter = 0 means all floors. Respects college operating hours.
    std::vector<RoomStatusResult> findAvailableRooms(const std::string& date_or_day,
                                                     int start_minutes,
                                                     int end_minutes,
                                                     int floor_filter = 0);

    // Get the complete daily availability breakdown (free & occupied slots) for a room
    std::vector<AvailableSlot> getDailyAvailability(const std::string& room_number,
                                                    const std::string& date_or_day);

    // Get status for all rooms on a specific floor for a given date/time
    std::vector<RoomStatusResult> getFloorRoomStatuses(int floor_number,
                                                       const std::string& date_or_day,
                                                       int current_minutes = -1);

    // Campus-wide stats at current or specified time
    nlohmann::json getCampusSummary(const std::string& date_or_day, int current_minutes = -1);

    // CSV Timetable Import & Validation
    CSVValidationResult validateCSV(const std::string& csvContent);
    bool importCSV(const std::string& csvContent, bool replaceExisting,
                   int& importedCount, std::vector<std::string>& warnings, std::string& errorMsg);

    // Resolve date string or day name to canonical Day of Week ("Monday".."Sunday")
    static std::string resolveDay(const std::string& date_or_day);
    // Strict variant: returns "" if date_or_day is non-empty and invalid
    static std::string resolveDayStrict(const std::string& date_or_day);

private:
    Storage& storage_;
};

} // namespace roomwise

#endif // ROOMWISE_SCHEDULER_H
