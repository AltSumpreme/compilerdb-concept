#include "util.h"

namespace util {

void show_banner() {
    config::Settings s = config::defaults();
    config::print(s);
}

}  // namespace util
