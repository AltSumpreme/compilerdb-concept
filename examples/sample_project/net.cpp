#include "net.h"

namespace net {

bool Connection::open(const char* host, int port) {
    (void)host;
    (void)port;
    logging::info("connection opened");
    return true;
}

void Connection::close() { logging::info("connection closed"); }

}  // namespace net
