#pragma once

#include "log.h"

namespace net {

class Connection {
public:
    bool open(const char* host, int port);
    void close();
};

}  // namespace net
