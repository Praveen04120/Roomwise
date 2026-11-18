cmake_minimum_required(VERSION 3.15)
project(Roomwise VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/backend
)

# Standalone HTTP Backend Server
add_executable(roomwise
    backend/main.cpp
    backend/storage.cpp
    backend/scheduler.cpp
    backend/routes.cpp
)
target_link_libraries(roomwise PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(roomwise PRIVATE ws2_32 crypt32)
endif()

# Stateless CLI/Serverless Request Dispatcher (same C++17 engine)
add_executable(roomwise_cli
    backend/cli_handler.cpp
    backend/storage.cpp
    backend/scheduler.cpp
)
target_link_libraries(roomwise_cli PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(roomwise_cli PRIVATE ws2_32 crypt32)
endif()

# Unit Tests Target
add_executable(test_scheduler
    tests/test_scheduler.cpp
    backend/storage.cpp
    backend/scheduler.cpp
)
target_link_libraries(test_scheduler PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(test_scheduler PRIVATE ws2_32 crypt32)
endif()
