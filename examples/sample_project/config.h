#pragma once

#include "log.h"

namespace config {

struct Settings {
    int port;
    bool verbose;
};

Settings defaults();
void print(const Settings& s);

}  // namespace config
