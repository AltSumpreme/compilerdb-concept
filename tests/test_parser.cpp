// test_parser: fixture compile_commands.json + .d files -> assert exact
// expected nodes/edges; per-object include lists must equal the .d
// contents. Also unit-checks the depfile parser on tricky syntax.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#include "db.h"
#include "depfile.h"
#include "ingest.h"

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

void write_file(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p);
    out << content;
}

std::set<std::string> includes_of(const Graph& g, const std::string& src) {
    // Headers H such that edge (src -> H, includes) exists. Derived from
    // upstream(src) restricted to headers: every header reachable via
    // includes-forward from a source is a direct edge target... simpler:
    // walk the whole edge list once.
    std::set<std::string> out;
    g.for_each_edge([&](NodeId f, NodeId t, EdgeKind k) {
        if (k == EdgeKind::Includes && g.nodes()[f].name == src)
            out.insert(g.nodes()[t].name);
    });
    return out;
}

void test_depfile_syntax() {
    // Continuations, escaped spaces, $$ escapes, multiple stanzas,
    // -MP-style phony targets, comment line.
    std::string text =
        "build/main.o: src/main.cpp include/a.h \\\n"
        "    include/weird\\ name.h include/money$$.h\n"
        "# a comment line\n"
        "build/lib.o build/extra.o : src/lib.cpp include/a.h\n"
        "include/a.h:\n"
        "include/weird\\ name.h:\n";
    auto stanzas = parse_depfile(text);
    CHECK(stanzas.size() == 4);
    if (stanzas.size() >= 4) {
        CHECK(stanzas[0].targets.size() == 1);
        CHECK(stanzas[0].targets[0] == "build/main.o");
        CHECK(stanzas[0].deps.size() == 4);
        if (stanzas[0].deps.size() == 4) {
            CHECK(stanzas[0].deps[0] == "src/main.cpp");
            CHECK(stanzas[0].deps[1] == "include/a.h");
            CHECK(stanzas[0].deps[2] == "include/weird name.h");
            CHECK(stanzas[0].deps[3] == "include/money$.h");
        }
        CHECK(stanzas[1].targets.size() == 2);  // multi-target stanza
        CHECK(stanzas[1].deps.size() == 2);
        CHECK(stanzas[2].targets.size() == 1 && stanzas[2].deps.empty());
        CHECK(stanzas[3].targets.size() == 1 && stanzas[3].deps.empty());
    }
}

void test_shell_and_flags() {
    auto words = shell_split("c++ -std=c++17 '-DNAME=a b' -I include -c "
                             "\"src/my file.cpp\" -o build/main.o");
    CHECK(words.size() == 9);
    if (words.size() == 9) {
        CHECK(words[2] == "-DNAME=a b");
        CHECK(words[6] == "src/my file.cpp");
    }
    std::string output;
    auto flags = extract_flags(words, &output);
    CHECK(output == "build/main.o");
    // -I (separate arg), -c, -o (with arg) are skipped.
    CHECK((flags == std::vector<std::string>{"-std=c++17", "-DNAME=a b"}));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: test_parser <tmp_dir>\n";
        return 2;
    }
    test_depfile_syntax();
    test_shell_and_flags();

    fs::path proj = fs::absolute(fs::path(argv[1]) / "parser_proj");
    fs::remove_all(proj);
    std::string P = proj.string();

    write_file(proj / "include/a.h", "#pragma once\n#include \"b.h\"\n");
    write_file(proj / "include/b.h", "#pragma once\n");
    write_file(proj / "include/weird name.h",
               "#pragma once\n#include \"b.h\"\n");
    write_file(proj / "src/main.cpp",
               "#include \"a.h\"\n#include \"weird name.h\"\n#include <vector>\n"
               "int main() { return 0; }\n");
    write_file(proj / "src/lib.cpp", "#include \"b.h\"\nint lib() { return 1; }\n");

    write_file(proj / "build/compile_commands.json", R"JSON([
  {
    "directory": "@P@",
    "arguments": ["c++", "-std=c++17", "-O2", "-DNDEBUG", "-Iinclude",
                  "-c", "src/main.cpp", "-o", "build/main.o"],
    "file": "src/main.cpp",
    "output": "build/main.o"
  },
  {
    "directory": "@P@",
    "command": "c++ -std=c++17 -Wall -I include -c src/lib.cpp -o build/lib.o",
    "file": "src/lib.cpp"
  }
]
)JSON");
    // Substitute the runtime project path into the fixture.
    {
        fs::path cc = proj / "build/compile_commands.json";
        std::ifstream in(cc);
        std::string text((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        in.close();
        size_t at;
        while ((at = text.find("@P@")) != std::string::npos) text.replace(at, 3, P);
        std::ofstream out(cc);
        out << text;
    }

    // Depfiles list the full transitive header closure, like gcc -MMD,
    // plus -MP phony stanzas; one uses a continuation + escaped space.
    write_file(proj / "build/main.d",
               "build/main.o: src/main.cpp include/a.h include/b.h \\\n"
               "  include/weird\\ name.h\n"
               "include/a.h:\n"
               "include/weird\\ name.h:\n");
    write_file(proj / "build/lib.d",
               "build/lib.o: src/lib.cpp include/b.h\n"
               "include/b.h:\n");

    Database db = Database::open((proj / "graph.cgdb").string());
    IngestOptions opt;
    opt.compile_commands = (proj / "build/compile_commands.json").string();
    opt.root = P;
    ingest(db, opt);
    const Graph& g = db.graph();

    CHECK(g.node_count(NodeKind::Source) == 2);
    CHECK(g.node_count(NodeKind::Header) == 3);
    CHECK(g.node_count(NodeKind::Object) == 2);
    CHECK(g.node_count(NodeKind::Flag) == 5);  // -std=c++17 -O2 -DNDEBUG -Iinclude -Wall
    CHECK(g.node_count() == 12);

    CHECK(g.edge_count(EdgeKind::CompilesTo) == 2);
    CHECK(g.edge_count(EdgeKind::Uses) == 6);
    // .d: main->{a,b,weird}, lib->{b}; scan adds a->b, weird->b.
    CHECK(g.edge_count(EdgeKind::Includes) == 6);
    CHECK(g.edge_count() == 14);

    // Per-object include lists equal the .d contents (deps minus source).
    std::string main_cpp = P + "/src/main.cpp";
    std::string lib_cpp = P + "/src/lib.cpp";
    CHECK((includes_of(g, main_cpp) ==
           std::set<std::string>{P + "/include/a.h", P + "/include/b.h",
                                 P + "/include/weird name.h"}));
    CHECK((includes_of(g, lib_cpp) == std::set<std::string>{P + "/include/b.h"}));

    // Kind precedence: a path seen as both header and source is a source.
    CHECK(g.find(P + "/src/main.cpp")->kind == NodeKind::Source);
    // Flag node from the "command" variant entry is present.
    CHECK(g.find("-Wall") != nullptr);
    // Object from the "arguments" variant entry.
    CHECK(g.find(P + "/build/main.o") != nullptr);

    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "test_parser: ok\n";
    return 0;
}
