cmake_minimum_required(VERSION 3.16)
project(compilegraphdb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# So the project can ingest itself: produces compile_commands.json.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Werror)

# Core library: everything except the CLI entry point, so tests link it.
add_library(cgdb_core STATIC
  src/crc32.cpp
  src/graph.cpp
  src/log.cpp
  src/db.cpp
  src/json.cpp
  src/depfile.cpp
  src/ingest.cpp
  src/export.cpp
  src/server.cpp
  src/bench.cpp
)
target_include_directories(cgdb_core PUBLIC src)

add_executable(compilegraphdb src/main.cpp)
target_link_libraries(compilegraphdb PRIVATE cgdb_core)

enable_testing()

# --- C++ unit/integration tests (link cgdb_core) ---
foreach(t test_recovery test_torn_write test_parser test_cycles)
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE cgdb_core)
endforeach()

set(SAMPLE ${CMAKE_CURRENT_SOURCE_DIR}/examples/sample_project)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tmp)

# Builds the sample project (gcc -MMD depfiles + compile_commands.json);
# the integration tests below require this fixture.
add_test(NAME sample_build
  COMMAND bash ${SAMPLE}/build.sh
  WORKING_DIRECTORY ${SAMPLE})
set_tests_properties(sample_build PROPERTIES FIXTURES_SETUP SAMPLE)

# Ingests the sample project into a db shared by bench_smoke.
add_test(NAME sample_ingest
  COMMAND compilegraphdb ingest --db ${CMAKE_CURRENT_BINARY_DIR}/tmp/sample.cgdb
          --compile-commands ${SAMPLE}/build/compile_commands.json --root ${SAMPLE})
set_tests_properties(sample_ingest PROPERTIES FIXTURES_REQUIRED SAMPLE FIXTURES_SETUP INGESTED)

add_test(NAME test_recovery
  COMMAND test_recovery ${SAMPLE} ${CMAKE_CURRENT_BINARY_DIR}/tmp)
add_test(NAME test_torn_write
  COMMAND test_torn_write ${SAMPLE} ${CMAKE_CURRENT_BINARY_DIR}/tmp)
set_tests_properties(test_recovery test_torn_write PROPERTIES FIXTURES_REQUIRED SAMPLE)

add_test(NAME test_parser COMMAND test_parser ${CMAKE_CURRENT_BINARY_DIR}/tmp)

# Unit-level Tarjan SCC check (3-cycle, self-loop, acyclic control).
add_test(NAME test_cycles COMMAND test_cycles)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME test_queries_vs_grep
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_queries_vs_grep.py
            $<TARGET_FILE:compilegraphdb> ${SAMPLE} ${CMAKE_CURRENT_BINARY_DIR}/tmp)
  add_test(NAME test_http
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_http.py
            $<TARGET_FILE:compilegraphdb> ${SAMPLE} ${CMAKE_CURRENT_BINARY_DIR}/tmp)
  # CLI `cycles` on the sample project + acyclic control fixture.
  add_test(NAME test_cycles_cli
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cycles_cli.py
            $<TARGET_FILE:compilegraphdb> ${SAMPLE} ${CMAKE_CURRENT_BINARY_DIR}/tmp)
  # Coloured export carries fill colours + legend; --no-color stays plain.
  add_test(NAME test_export
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_export.py
            $<TARGET_FILE:compilegraphdb> ${SAMPLE} ${CMAKE_CURRENT_BINARY_DIR}/tmp)
  # ~200-file generated project: ingest + downstream vs recursive grep.
  add_test(NAME test_gen_roundtrip
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gen_roundtrip.py
            $<TARGET_FILE:compilegraphdb>
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_project.py
            ${CMAKE_CURRENT_BINARY_DIR}/tmp)
  set_tests_properties(test_queries_vs_grep test_http test_cycles_cli test_export
    PROPERTIES FIXTURES_REQUIRED SAMPLE)
endif()

# Benchmark smoke test on the tiny sample project (fast: --runs 2).
add_test(NAME bench_smoke
  COMMAND compilegraphdb bench --db ${CMAKE_CURRENT_BINARY_DIR}/tmp/sample.cgdb
          --src ${SAMPLE} --runs 2)
set_tests_properties(bench_smoke PROPERTIES FIXTURES_REQUIRED INGESTED)
