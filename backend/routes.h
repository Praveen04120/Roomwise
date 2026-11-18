#ifndef ROOMWISE_ROUTES_H
#define ROOMWISE_ROUTES_H

#include "httplib.h"
#include "storage.h"
#include "scheduler.h"
#include <string>

namespace roomwise {

class Routes {
public:
    static void setupRoutes(httplib::Server& svr, Storage& storage, Scheduler& scheduler,
                            const std::string& frontendDir = "frontend");

private:
    static bool checkAdminAuth(const httplib::Request& req, httplib::Response& res, Storage& storage);
    static std::string extractToken(const httplib::Request& req);
    static void setCorsHeaders(httplib::Response& res);
};

} // namespace roomwise

#endif // ROOMWISE_ROUTES_H
