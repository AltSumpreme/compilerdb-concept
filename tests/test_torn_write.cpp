// test_torn_write: after ingest, append HALF of a valid record to the log,
// reopen -> all complete records recovered, the partial record is skipped,
// the file is truncated back to the last good offset, and further appends
// still work.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "db.h"
#include "ingest.h"
#include "log.h"

namespace fs = std::filesystem;
using namespace cgdb;

namespace {
int failures = 0;
#define CHECK(cond)                                              \
    do {                                                         \
        if (!(cond)) {                                           \
            std::cerr << "FAIL " << __LINE__ << ": " #cond "\n"; \
            ++failures;                                          \
        }                                                        \
    } while (0)
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: test_torn_write <sample_project_dir> <tmp_dir>\n";
        return 2;
    }
    std::string sample = argv[1];
    fs::path tmp = argv[2];
    std::string dbpath = (tmp / "torn.cgdb").string();
    fs::remove(dbpath);

    size_t nodes1, edges1;
    {
        Database db = Database::open(dbpath);
        IngestOptions opt;
        opt.compile_commands = sample + "/build/compile_commands.json";
        opt.root = sample;
        ingest(db, opt);
        nodes1 = db.graph().node_count();
        edges1 = db.graph().edge_count();
    }

    const uintmax_t good_size = fs::file_size(dbpath);

    // Produce one valid record elsewhere, then append only half of it.
    std::string scratch = (tmp / "torn_scratch.cgdb").string();
    fs::remove(scratch);
    {
        Log log = Log::open(scratch, [](const Record&) {});
        log.append_node(NodeKind::Header, "torn/victim.h");
    }
    std::ifstream in(scratch, std::ios::binary);
    std::string record((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
    in.close();
    CHECK(record.size() > 10);
    {
        std::ofstream out(dbpath, std::ios::binary | std::ios::app);
        out.write(record.data(), static_cast<std::streamsize>(record.size() / 2));
    }
    CHECK(fs::file_size(dbpath) == good_size + record.size() / 2);

    {
        Database db = Database::open(dbpath);
        // Complete records recovered; the partial record is invisible.
        CHECK(db.graph().node_count() == nodes1);
        CHECK(db.graph().edge_count() == edges1);
        CHECK(db.graph().find("torn/victim.h") == nullptr);
        // The torn tail was truncated on open.
        CHECK(fs::file_size(dbpath) == good_size);

        // Further appends still work after truncation.
        db.add_node("torn/after_crash.h", NodeKind::Header);
        CHECK(db.graph().find("torn/after_crash.h") != nullptr);
    }

    {
        Database db = Database::open(dbpath);
        CHECK(db.graph().node_count() == nodes1 + 1);
        CHECK(db.graph().find("torn/after_crash.h") != nullptr);
        CHECK(db.graph().find("torn/victim.h") == nullptr);
    }

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "test_torn_write: ok\n";
    return 0;
}
