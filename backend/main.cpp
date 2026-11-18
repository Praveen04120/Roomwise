#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "httplib.h"
#include <iostream>
#include <fstream>
#include <string>
#include <csignal>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
#include "storage.h"
#include "scheduler.h"
#include "routes.h"

std::unique_ptr<httplib::Server> g_server;

void signalHandler(int signum) {
    std::cout << "\n[Roomwise] Received termination signal (" << signum << "). Stopping server..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

static void loadDotEnvFile(const std::string& path = ".env") {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#') continue;
        size_t eq = line.find('=', first);
        if (eq == std::string::npos) continue;

        std::string key = line.substr(first, eq - first);
        size_t keyEnd = key.find_last_not_of(" \t");
        if (keyEnd != std::string::npos) key = key.substr(0, keyEnd + 1);

        std::string val = line.substr(eq + 1);
        size_t valStart = val.find_first_not_of(" \t");
        size_t valEnd = val.find_last_not_of(" \t\r\n");
        if (valStart == std::string::npos) {
            val = "";
        } else {
            val = val.substr(valStart, valEnd - valStart + 1);
        }
        // Strip matching quotes
        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }

        // Do not overwrite environment variables already provided by the OS / cloud host
        if (std::getenv(key.c_str()) == nullptr) {
#ifdef _WIN32
            _putenv_s(key.c_str(), val.c_str());
#else
            setenv(key.c_str(), val.c_str(), 0);
#endif
        }
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    loadDotEnvFile(".env");

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    int port = 3000;
    std::string host = "0.0.0.0";
    std::string dataDir = "data";
    std::string frontendDir = "frontend";

    if (const char* envPort = std::getenv("PORT")) {
        try { port = std::stoi(envPort); } catch (...) {}
    }
    if (const char* envHost = std::getenv("HOST")) {
        host = envHost;
    }
    if (const char* envData = std::getenv("DATA_DIR")) {
        dataDir = envData;
    }
    if (const char* envFrontend = std::getenv("FRONTEND_DIR")) {
        frontendDir = envFrontend;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--data" && i + 1 < argc) {
            dataDir = argv[++i];
        } else if (arg == "--frontend" && i + 1 < argc) {
            frontendDir = argv[++i];
        }
    }

    std::cout << "============================================================" << std::endl;
    std::cout << "   ROOMWISE - Empty Classroom Finder & Scheduling System    " << std::endl;
    std::cout << "          B.Tech CSE Project & Campus Utility               " << std::endl;
    std::cout << "============================================================" << std::endl;

    roomwise::Storage& storage = roomwise::Storage::getInstance();
    if (!storage.initialize(dataDir)) {
        std::cerr << "[Roomwise] Fatal error: Could not initialize storage in " << dataDir << std::endl;
        return 1;
    }

    if (!storage.hasAdminPasswordConfigured()) {
        std::cerr << "[Roomwise] Warning: ADMIN_PASSWORD is not configured. Set ADMIN_PASSWORD in environment or .env to enable /admin login." << std::endl;
    }

    roomwise::Scheduler scheduler(storage);
    roomwise::Settings settings = storage.getSettings();

    g_server = std::make_unique<httplib::Server>();
    g_server->set_payload_max_length(5 * 1024 * 1024); // 5 MB safety cap

    roomwise::Routes::setupRoutes(*g_server, storage, scheduler, frontendDir);

    std::cout << "\nCampus: " << settings.college_name << std::endl;
    std::cout << "Building: " << settings.building_name << " (" << settings.total_floors << " Floors)" << std::endl;
    std::cout << "Operating Hours: " << settings.operating_start << " - " << settings.operating_end << std::endl;
    std::cout << "------------------------------------------------------------" << std::endl;
    std::cout << ">> Public Student Portal : http://localhost:" << port << "/" << std::endl;
    std::cout << ">> Admin Management      : http://localhost:" << port << "/admin" << std::endl;
    std::cout << ">> Admin Auth            : Configured via ADMIN_PASSWORD env" << std::endl;
    std::cout << "============================================================" << std::endl;
    std::cout << "[Roomwise] Server started. Listening on http://" << host << ":" << port << std::endl;
    std::cout << "[Roomwise] Press Ctrl+C to stop.\n" << std::endl;

    bool ok = g_server->listen(host.c_str(), port);
    if (!ok) {
        std::cerr << "[Roomwise] Failed to bind to port " << port << ". Check if another process is using it." << std::endl;
        return 1;
    }

    std::cout << "[Roomwise] Server stopped gracefully." << std::endl;
    return 0;
}
