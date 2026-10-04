#include "util.h"
#include "net.h"
#include "cycle_a.h"

int main() {
    util::show_banner();
    net::Connection c;
    c.open("localhost", 8080);
    c.close();
    return cycle::a_value() + cycle::b_value() - 3;
}
