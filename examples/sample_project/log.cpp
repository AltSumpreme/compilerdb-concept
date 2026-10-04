#include "log.h"

#include <cstdio>

namespace logging {

void info(const char* msg) { std::printf("[info] %s\n", msg); }
void error(const char* msg) { std::fprintf(stderr, "[error] %s\n", msg); }

}  // namespace logging
