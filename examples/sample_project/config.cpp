#include "config.h"

namespace config {

Settings defaults() { return Settings{8080, false}; }

void print(const Settings& s) {
    logging::info(s.verbose ? "verbose mode" : "quiet mode");
}

}  // namespace config
