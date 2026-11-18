# Roomwise — Viva Preparation & Technical Defense Guide
**Target Audience:** 1st & 2nd Year B.Tech Computer Science & Engineering Students  
**Project:** Roomwise — Empty Classroom Finder & Campus Scheduling Engine

---

## 1. Project High-Level Overview (The Elevator Pitch)

> **"What is Roomwise and what problem does it solve?"**

**Sample Student Answer:**
> *"Roomwise is a campus utility designed to eliminate wasted time searching for empty study and lecture halls across our 9-floor academic building (over 140 classrooms). Instead of manually reading dozens of department timetables, Roomwise uses a high-performance C++ scheduling engine that takes institutional weekly timetables, converts calendar dates to active weekdays, and performs real-time interval intersection algorithms. It determines exactly which rooms are unoccupied right now or during any requested time interval, while ensuring that classrooms without verified timetable data are never falsely reported as available."*

---

## 2. Core Question: How Does Roomwise Determine if a Classroom is Empty?

During the viva, professors will specifically evaluate how your algorithm determines room availability. Here is the step-by-step breakdown:

### Step 1: Internal Time Representation
- All times are parsed from `HH:MM` strings into **integer minutes after midnight**:
  $$\text{Minutes} = (\text{Hours} \times 60) + \text{Minutes}$$
  *Example:* `09:30` becomes $9 \times 60 + 30 = 570$ minutes.
  *Benefit:* Avoids complex date-time object overhead, floating-point inaccuracies, and makes comparisons simple integer inequality checks.

### Step 2: Date-to-Day Mapping
- College timetables follow recurring weekly schedules (Monday through Saturday).
- When a user picks a date (e.g., `2026-10-14`), Roomwise computes the corresponding weekday (`Wednesday`).
- The engine then queries all classes scheduled for that classroom on `Wednesday`.

### Step 3: Interval Overlap Condition (The Core Algorithm)
To check if a room is occupied during a requested interval $[S_{\text{req}}, E_{\text{req}}]$ against a scheduled class $[S_{\text{class}}, E_{\text{class}}]$:

$$\text{Overlap} \iff (S_{\text{class}} < E_{\text{req}}) \land (E_{\text{class}} > S_{\text{req}})$$

#### Why does this work?
- If the class starts after or exactly when the requested slot ends ($S_{\text{class}} \ge E_{\text{req}}$), there is no conflict.
- If the class ends before or exactly when the requested slot starts ($E_{\text{class}} \le S_{\text{req}}$), there is no conflict.
- The negation of both disjoint conditions gives the strict overlap formula above.

#### Proving the Adjacent Class Rule (Crucial Viva Point!):
- Suppose Class A runs from **09:00 to 10:00** ($540$ to $600$), and Class B runs from **10:00 to 11:00** ($600$ to $660$).
- Overlap check:
  $$(540 < 660) \land (600 > 600)$$
  Since $600 > 600$ evaluates to **`false`**, there is **no overlap**.
- Hence, adjacent classes never trigger a false collision.

---

## 3. The 3-State Availability Principle (Fail-Safe Engineering)

A major flaw in basic scheduling projects is assuming that any room without a class is "Available". Roomwise enforces a **three-state system**:

| Status | Color | Condition |
| :--- | :--- | :--- |
| **AVAILABLE** | **Green** | Timetable coverage is verified, operating hours are active, and no class overlaps the requested window. |
| **OCCUPIED** | **Red** | A confirmed lecture/lab overlaps the time window. Displays active course, section, and end time. |
| **UNAVAILABLE** | **Grey** | Timetable data is missing/unconfigured, or the query falls outside institutional operating hours (08:00 – 18:00). |

> *Viva Tip: Explain that "Absence of evidence is not evidence of absence". If an admin hasn't configured room 115, Roomwise displays "No Timetable Data" in grey rather than falsely sending students to a locked or unmonitored room.*

---

## 4. Daily Availability Breakdown (Gap Detection Algorithm)

When an administrator or student requests the full-day view:
1. Roomwise retrieves the building operating hours: $[S_{\text{op}}, E_{\text{op}}]$ (e.g., $480$ to $1080$ for 08:00 to 18:00).
2. It sorts all scheduled classes for that room on that day in ascending order by start time: $C_1, C_2, \dots, C_n$.
3. It sets a cursor `current = S_op`.
4. For each class $C_i$:
   - If $C_i.\text{start} > \text{current}$, the gap $[\text{current}, C_i.\text{start}]$ is marked **AVAILABLE**.
   - The class duration $[C_i.\text{start}, C_i.\text{end}]$ is marked **OCCUPIED** with section and subject metadata.
   - The cursor advances: $\text{current} = \max(\text{current}, C_i.\text{end})$.
5. After the last class, if $\text{current} < E_{\text{op}}$, the remaining window $[\text{current}, E_{\text{op}}]$ is marked **AVAILABLE**.

---

## 5. Data Structures Used & Their Purpose

| C++ Data Structure | Location | Justification & Purpose |
| :--- | :--- | :--- |
| `struct Floor`, `struct Classroom`, `struct ScheduleEntry` | `models.h` | Encapsulates campus entity attributes cleanly without object bloat. |
| `std::vector<Classroom>` | `storage.h` | Dynamic sequential array for rooms. Provides $O(1)$ random access and cache-friendly contiguous memory. |
| `std::vector<ScheduleEntry>` | `storage.h` | Holds the active schedule records. Easily sortable via `std::sort`. |
| `std::map<int, Floor>` & `countMap` | `routes.cpp` | Key-value dictionary providing $O(\log N)$ floor lookups and room aggregations. |
| `std::set<std::string>` | `storage.h` | Stores active admin session tokens with $O(\log N)$ membership verification. |
| `std::mutex` | `storage.h` | Protects in-memory collections against race conditions during concurrent HTTP requests. |

---

## 6. Time and Space Complexity Analysis

- **Room Availability Search (`findAvailableRooms`):**
  - Let $R$ be the number of classrooms ($144$) and $C$ be the max classes per room per day ($\approx 6$).
  - For each room, we check $C$ entries for interval overlap: $O(R \times C)$.
  - Sorting matching rooms: $O(M \log M)$ where $M \le R$.
  - Total Time: $\approx 144 \times 6 = 864$ operations, taking **less than 1 millisecond** in C++.
- **Space Complexity:**
  - In-memory data store requires only $\approx 500$ KB of RAM for the entire 9-floor building.

---

## 7. Expected Viva Questions & Answers

### Q1: Why did you use C++ for the backend instead of Python or Node.js?
**Answer:**  
*"C++ provides deterministic execution, zero runtime garbage collection pauses, and direct memory layout with low CPU overhead. For high-frequency schedule calculations and multi-room interval queries, C++ easily computes hundreds of room comparisons in microseconds. It also demonstrates standard data structures (`vector`, `map`, `struct`) and clean systems programming."*

### Q2: What prevents overlapping classes from being entered for the same room?
**Answer:**  
*"Roomwise implements server-side collision detection in both manual entry and CSV bulk import. Before saving any entry, `Storage::addScheduleEntry` and `Scheduler::validateCSV` iterate through existing entries for that specific room and day. If `TimeUtil::intervalsOverlap` returns true, the transaction is rejected with a descriptive collision error message specifying the conflicting class and timing."*

### Q3: How is persistent storage handled?
**Answer:**  
*"Data is stored in local, human-readable JSON files (`floors.json`, `rooms.json`, `timetable.json`, `settings.json`) using the lightweight `nlohmann/json` header. When the server launches, it loads these into memory. Any administrative mutation (add, edit, delete, or import) automatically updates memory and writes to disk in thread-safe fashion."*

### Q4: How is security handled on the `/admin` portal?
**Answer:**  
*"Rather than relying on client-side route guards, Roomwise protects every administrative REST endpoint at the C++ layer. The administrator password is supplied securely through the `ADMIN_PASSWORD` environment variable (or updated and stored as a salted SHA-256 hash). When an administrator signs in, the backend validates the credential in constant time and generates a random session token with a 24-hour TTL. Every administrative API call (`POST`, `PUT`, `DELETE`, `/api/admin/*`) extracts the token from the `Authorization: Bearer` header. Requests lacking a valid token are rejected with HTTP 401 Unauthorized."*
