#include <iostream>
#include <cassert>
#include <filesystem>
#include <cstdlib>
#include "../backend/models.h"
#include "../backend/storage.h"
#include "../backend/scheduler.h"

using namespace roomwise;

static int g_passed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "[FAIL] " << msg << " (line " << __LINE__ << ")" << std::endl; \
        std::exit(1); \
    } \
    g_passed++; \
} while (0)

void testIntervalsAndBoundaries() {
    std::cout << "[Test 1] Interval Overlap & Boundary Rules..." << std::endl;

    // Strict overlap: 09:00-10:00 vs 09:30-10:30
    CHECK(TimeUtil::intervalsOverlap(540, 600, 570, 630) == true, "09:00-10:00 overlaps 09:30-10:30");
    CHECK(TimeUtil::intervalsOverlap(540, 660, 570, 600) == true, "Outer contains inner");
    CHECK(TimeUtil::intervalsOverlap(570, 600, 540, 660) == true, "Inner inside outer");
    CHECK(TimeUtil::intervalsOverlap(540, 600, 540, 600) == true, "Identical intervals overlap");

    // Adjacent classes: 09:00-10:00 and 10:00-11:00 must NOT overlap
    CHECK(TimeUtil::intervalsOverlap(540, 600, 600, 660) == false, "09:00-10:00 adjacent to 10:00-11:00");
    CHECK(TimeUtil::intervalsOverlap(600, 660, 540, 600) == false, "10:00-11:00 adjacent to 09:00-10:00");

    // Disjoint
    CHECK(TimeUtil::intervalsOverlap(540, 600, 720, 780) == false, "Disjoint intervals");
}

void testStrictTimeAndDateParsing() {
    std::cout << "[Test 2] Strict Time & Date Parsing..." << std::endl;

    CHECK(TimeUtil::parseTimeToMinutes("00:00") == 0, "00:00 == 0");
    CHECK(TimeUtil::parseTimeToMinutes("09:00") == 540, "09:00 == 540");
    CHECK(TimeUtil::parseTimeToMinutes("09:30") == 570, "09:30 == 570");
    CHECK(TimeUtil::parseTimeToMinutes("13:45") == 825, "13:45 == 825");
    CHECK(TimeUtil::parseTimeToMinutes("23:59") == 1439, "23:59 == 1439");

    // Malformed / invalid times must be rejected
    CHECK(TimeUtil::parseTimeToMinutes("24:00") == -1, "24:00 rejected");
    CHECK(TimeUtil::parseTimeToMinutes("09x30") == -1, "09x30 rejected");
    CHECK(TimeUtil::parseTimeToMinutes("09:30abc") == -1, "09:30abc rejected");
    CHECK(TimeUtil::parseTimeToMinutes("-01:00") == -1, "-01:00 rejected");
    CHECK(TimeUtil::parseTimeToMinutes("12:60") == -1, "12:60 rejected");
    CHECK(TimeUtil::parseTimeToMinutes("invalid") == -1, "invalid rejected");

    // 12h formatting
    CHECK(TimeUtil::minutesToTimeString(570) == "09:30", "570 -> 09:30");
    CHECK(TimeUtil::minutesTo12HourString(570) == "09:30 AM", "570 -> 09:30 AM");
    CHECK(TimeUtil::minutesTo12HourString(825) == "01:45 PM", "825 -> 01:45 PM");
    CHECK(TimeUtil::minutesTo12HourString(0) == "12:00 AM", "0 -> 12:00 AM");
    CHECK(TimeUtil::minutesTo12HourString(720) == "12:00 PM", "720 -> 12:00 PM");

    // Date to weekday accuracy (past, present, future dates)
    CHECK(TimeUtil::getDayOfWeekFromDate("2026-10-14") == "Wednesday", "2026-10-14 is Wednesday");
    CHECK(TimeUtil::getDayOfWeekFromDate("2026-11-18") == "Wednesday", "2026-11-18 is Wednesday");
    CHECK(TimeUtil::getDayOfWeekFromDate("2026-11-23") == "Monday", "2026-11-23 is Monday");
    CHECK(TimeUtil::getDayOfWeekFromDate("2027-01-01") == "Friday", "2027-01-01 is Friday");
    CHECK(TimeUtil::getDayOfWeekFromDate("2026-02-30") == "", "Invalid calendar date 2026-02-30 rejected");
    CHECK(TimeUtil::getDayOfWeekFromDate("not-a-date") == "", "Garbage date rejected");

    // Strict day normalization
    CHECK(TimeUtil::normalizeDayNameStrict("monday") == "Monday", "monday -> Monday");
    CHECK(TimeUtil::normalizeDayNameStrict("Fri") == "Friday", "Fri -> Friday");
    CHECK(TimeUtil::normalizeDayNameStrict("Funday") == "", "Invalid weekday rejected");
}

void testSchedulingEngineAndEdgeCases() {
    std::cout << "[Test 3] Complete Scheduling Engine & Edge Cases..." << std::endl;

    std::string testDir = "tmp_test_sched_dir";
    std::filesystem::remove_all(testDir);

#ifdef _WIN32
    _putenv_s("ADMIN_PASSWORD", "UnitTestSecret#99");
#else
    setenv("ADMIN_PASSWORD", "UnitTestSecret#99", 1);
#endif

    Storage& storage = Storage::getInstance();
    CHECK(storage.initialize(testDir) == true, "Storage initializes cleanly");
    Scheduler scheduler(storage);

    // 1. Occupied when class is scheduled (Room 101: 09:00-10:00, 10:00-11:00, 11:30-12:30 Mon-Sat)
    RoomStatusResult occ = scheduler.getRoomStatus("101", "2026-11-18", 570); // Wed 09:30
    CHECK(occ.status == "OCCUPIED", "Room 101 occupied at 09:30");
    CHECK(occ.current_subject == "Applied Mathematics I", "Subject matches");
    // Consecutive classes 09:00-10:00 then 10:00-11:00 -> next available is 11:00!
    CHECK(occ.next_available_slot == "Available from 11:00", "Consecutive classes skip to 11:00");

    // 2. Available when no class overlaps (Room 101 at 11:15)
    RoomStatusResult avail = scheduler.getRoomStatus("101", "2026-11-18", 675);
    CHECK(avail.status == "AVAILABLE", "Room 101 free at 11:15");
    CHECK(avail.next_available_slot == "Free until 11:30", "Free until next class 11:30");

    // 3. Adjacent class boundary: Room 101 has 09:00-10:00 and 10:00-11:00; 11:00-11:30 is free
    auto free1100 = scheduler.findAvailableRooms("2026-11-18", 660, 690, 1); // 11:00-11:30
    bool has101 = false;
    for (const auto& r : free1100) if (r.room_number == "101") has101 = true;
    CHECK(has101, "Adjacent interval 11:00-11:30 is available for Room 101");

    // 4. Overlapping query 09:30-10:30 must NOT include Room 101
    auto free0930 = scheduler.findAvailableRooms("2026-11-18", 570, 630, 1);
    bool has101Overlap = false;
    bool has103Free = false; // Room 103 only has 13:00-15:00
    for (const auto& r : free0930) {
        if (r.room_number == "101") has101Overlap = true;
        if (r.room_number == "103") has103Free = true;
    }
    CHECK(!has101Overlap, "Overlapping 09:30-10:30 excludes Room 101");
    CHECK(has103Free, "Different classroom 103 checked independently and is free 09:30-10:30");

    // 5. Floor filter respected
    auto floor2Free = scheduler.findAvailableRooms("2026-11-18", 780, 840, 2);
    CHECK(!floor2Free.empty(), "Floor 2 has free rooms 13:00-14:00");
    for (const auto& r : floor2Free) {
        CHECK(r.floor_number == 2, "Floor filter returns only Floor 2 rooms");
    }

    // 6. Future date resolves recurring weekday (2027-03-08 is Monday)
    RoomStatusResult futureStatus = scheduler.getRoomStatus("201", "2027-03-08", 540); // Mon 09:00
    CHECK(futureStatus.status == "OCCUPIED", "Future Monday 09:00 Room 201 is occupied");
    CHECK(futureStatus.current_subject == "Data Structures & Algorithms", "Future Monday subject matches");

    // 7. Room with no scheduled classes on Sunday (but configured Mon-Sat) -> free all operating hours
    auto sundaySlots = scheduler.getDailyAvailability("101", "2026-11-22"); // Sunday
    CHECK(sundaySlots.size() == 1 && sundaySlots[0].status == "AVAILABLE" &&
          sundaySlots[0].start_time == "08:00" && sundaySlots[0].end_time == "18:00",
          "Configured room with no classes on Sunday shows single 08:00-18:00 AVAILABLE slot");

    // 8. Room with multiple free periods (Room 101 on Wednesday: 08-09 free, 09-10 occ, 10-11 occ, 11-11:30 free, 11:30-12:30 occ, 12:30-18:00 free)
    auto wedSlots = scheduler.getDailyAvailability("101", "Wednesday");
    CHECK(wedSlots.size() == 6, "Room 101 Wednesday has 6 alternating free/occupied slots");
    CHECK(wedSlots[0].status == "AVAILABLE" && wedSlots[0].start_time == "08:00" && wedSlots[0].end_time == "09:00", "Slot 0: 08:00-09:00 free");
    CHECK(wedSlots[3].status == "AVAILABLE" && wedSlots[3].start_time == "11:00" && wedSlots[3].end_time == "11:30", "Slot 3: 11:00-11:30 free");
    CHECK(wedSlots[5].status == "AVAILABLE" && wedSlots[5].start_time == "12:30" && wedSlots[5].end_time == "18:00", "Slot 5: 12:30-18:00 free");

    // 9. Missing timetable data must NOT mark room as available (Room 115 has no timetable)
    RoomStatusResult unconf = scheduler.getRoomStatus("115", "Wednesday", 675);
    CHECK(unconf.status == "UNAVAILABLE", "Unconfigured room 115 is UNAVAILABLE");
    auto allFree = scheduler.findAvailableRooms("Wednesday", 660, 690, 1);
    for (const auto& r : allFree) {
        CHECK(r.room_number != "115", "Unconfigured room 115 excluded from findAvailableRooms");
    }

    // 10. Invalid time ranges rejected
    CHECK(scheduler.findAvailableRooms("Wednesday", 660, 600, 0).empty(), "Inverted interval returns empty");
    CHECK(scheduler.findAvailableRooms("Wednesday", 600, 600, 0).empty(), "Zero-length interval returns empty");

    // 11. College operating hours (08:00-18:00) strictly respected
    CHECK(scheduler.findAvailableRooms("Wednesday", 420, 480, 0).empty(), "07:00-08:00 outside operating hours returns empty");
    CHECK(scheduler.findAvailableRooms("Wednesday", 1080, 1140, 0).empty(), "18:00-19:00 outside operating hours returns empty");
    CHECK(scheduler.findAvailableRooms("Wednesday", 1050, 1110, 0).empty(), "17:30-18:30 crossing closing time returns empty");
    CHECK(!scheduler.findAvailableRooms("Wednesday", 480, 540, 0).empty(), "08:00-09:00 inside operating hours returns rooms");
    CHECK(scheduler.getRoomStatus("101", "Wednesday", 1200).status == "UNAVAILABLE", "20:00 outside operating hours -> UNAVAILABLE");

    // 12. Auth & Session tests
    CHECK(storage.validateAdminPassword("UnitTestSecret#99") == true, "Correct admin password accepted");
    CHECK(storage.validateAdminPassword("WrongPass") == false, "Wrong admin password rejected");
    std::string tok = storage.createAdminSession();
    CHECK(storage.isValidSession(tok) == true, "Created session token is valid");
    storage.revokeSession(tok);
    CHECK(storage.isValidSession(tok) == false, "Revoked session token rejected");

    // 13. CSV Validation & Import + Conflict + Persistence
    std::string mixedCsv =
        "room_number,section,subject,day,start_time,end_time\n"
        "101,TEST-A,\"Algorithms, Part 1\",Monday,16:00,17:00\n"
        "999,BAD-ROOM,Unknown Room,Monday,10:00,11:00\n"
        "101,BAD-DAY,Bad Day,Funday,16:00,17:00\n"
        "101,BAD-TIME,Inverted,Monday,15:00,14:00\n"
        "101,DUP-CSV,Overlap Within CSV,Monday,16:30,17:30\n";
    CSVValidationResult csvRes = scheduler.validateCSV(mixedCsv);
    CHECK(csvRes.total_rows == 5, "5 CSV rows parsed");
    CHECK(csvRes.valid_rows == 1, "1 valid CSV row (quoted comma parsed properly)");
    CHECK(csvRes.invalid_rows == 4, "4 invalid CSV rows caught (unknown room, bad day, inverted time, overlap)");

    int imported = 0;
    std::vector<std::string> warnings;
    std::string errMsg;
    CHECK(scheduler.importCSV(mixedCsv, false, imported, warnings, errMsg) == true, "CSV append import succeeds");
    CHECK(imported == 1, "Exactly 1 valid row imported");

    // Verify immediate public availability reflects newly imported class (101 Mon 16:00-17:00 is now OCCUPIED)
    RoomStatusResult afterImport = scheduler.getRoomStatus("101", "Monday", 990); // 16:30
    CHECK(afterImport.status == "OCCUPIED" && afterImport.current_subject == "Algorithms, Part 1",
          "Public status immediately reflects imported CSV entry");

    // Verify persistence across re-initialization (simulated server restart)
    CHECK(storage.initialize(testDir) == true, "Storage re-initializes from disk");
    RoomStatusResult afterRestart = scheduler.getRoomStatus("101", "Monday", 990);
    CHECK(afterRestart.status == "OCCUPIED" && afterRestart.current_subject == "Algorithms, Part 1",
          "Imported timetable entry survives server restart");

    // Verify duplicate classroom rejection & settings update preserving password
    Classroom dupRoom;
    dupRoom.room_number = "101";
    dupRoom.floor_number = 1;
    dupRoom.capacity = 60;
    CHECK(storage.addClassroom(dupRoom, errMsg) == false, "Duplicate room 101 rejected");

    Settings curSet = storage.getSettings();
    curSet.college_name = "Updated College";
    CHECK(storage.updateSettings(curSet, "", errMsg) == true, "Settings update without password succeeds");
    CHECK(storage.validateAdminPassword("UnitTestSecret#99") == true, "Existing password preserved after settings update");

    std::filesystem::remove_all(testDir);
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Roomwise - C++17 Comprehensive Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    testIntervalsAndBoundaries();
    testStrictTimeAndDateParsing();
    testSchedulingEngineAndEdgeCases();

    std::cout << "\nALL " << g_passed << " ROOMWISE ASSERTIONS PASSED!" << std::endl;
    return 0;
}
