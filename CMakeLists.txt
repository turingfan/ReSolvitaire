# File based on https://stackoverflow.com/questions/21163188/most-simple-but-complete-cmake-example

cmake_minimum_required(VERSION 3.14...4.0)
project(solvitaire)

# CMP0167: Use Boost's own CMake config instead of removed FindBoost module
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# For -flto flag
include(CheckIPOSupported)
check_ipo_supported(RESULT supported OUTPUT error)

# GCC LTO support: ensure gcc-ar and gcc-ranlib are available on Linux
if(NOT APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_AR "gcc-ar")
    set(CMAKE_RANLIB "gcc-ranlib")
endif()

###############################################################################
## source files ###############################################################
###############################################################################

set(main src/main/main.cpp)

set(sources_io
        src/main/input-output/input/command_line_helper.cpp
        src/main/input-output/input/command_line_helper.h
        src/main/input-output/output/log_helper.h
        src/main/input-output/input/json-parsing/deal_parser.h
        src/main/input-output/input/json-parsing/deal_parser.cpp
        src/main/input-output/input/json-parsing/json_helper.cpp
        src/main/input-output/input/json-parsing/json_helper.h
        src/main/input-output/input/sol_preset_types.h
        src/main/input-output/input/sol_preset_types.cpp
        src/main/input-output/input/json-parsing/rules_parser.cpp
        src/main/input-output/input/json-parsing/rules_parser.h
        src/main/input-output/output/state_printer.cpp
        src/main/input-output/output/state_printer.h
)

set(sources_game
        src/main/game/card.h
        src/main/game/card.cpp
        src/main/game/search-state/game_state.cpp
        src/main/game/search-state/game_state.h
        src/main/game/sol_rules.h
        src/main/game/pile.cpp
        src/main/game/pile.h
        src/main/game/global_cache.cpp
        src/main/game/global_cache.h
        src/main/game/cache_interface.h
        src/main/game/sol_rules.cpp
        src/main/game/search-state/game_state.legal_moves.cpp
        src/main/game/search-state/game_state.dominance_moves.cpp
        src/main/game/search-state/game_state.pile_order.cpp
        src/main/game/move.cpp
        src/main/game/move.h
        src/main/game/zobrist.h
        src/main/game/zobrist.cpp
        src/main/game/compact_state.h
        src/main/game/compact_state.cpp
        src/main/game/parent_table.h
        src/main/game/parent_table.cpp
        src/main/game/predecessor_state.h
        src/main/game/predecessor_state.cpp
)

set(sources_solver
        src/main/solver/solver.cpp
        src/main/solver/solver.h
        src/main/solver/search_trace.h
)

set(sources_evaluation
        src/main/evaluation/solvability_calc.cpp
        src/main/evaluation/solvability_calc.h
        src/main/evaluation/benchmark.cpp
        src/main/evaluation/benchmark.h
        src/main/evaluation/binomial_ci.cpp
        src/main/evaluation/binomial_ci.h
)

set(sources_rapidjson
        lib/rapidjson/document.h
        lib/rapidjson/schema.h
        lib/rapidjson/stringbuffer.h
)

set(sources ${sources_io} ${sources_game} ${sources_evaluation} ${sources_rapidjson} ${sources_solver})

source_group("IO" FILES ${sources_io})
source_group("Game" FILES ${sources_game})
source_group("Solver" FILES ${sources_solver})
source_group("Evaluation" FILES ${sources_evaluation})
source_group("RapidJSON" FILES ${sources_rapidjson})
source_group("Main" FILES ${main})

set(sources_test_integration
        src/test/integration_tests/spanish_patience_test.cpp
        src/test/integration_tests/black_hole_test.cpp
        src/test/integration_tests/free_cell_test.cpp
        src/test/integration_tests/bakers_dozen_test.cpp
        src/test/integration_tests/fortunes_favor_test.cpp
        src/test/integration_tests/flower_garden_test.cpp
        src/test/integration_tests/canfield_test.cpp
        src/test/integration_tests/somerset_test.cpp
        src/test/integration_tests/alpha_star_test.cpp
        src/test/integration_tests/spider_test.cpp
        src/test/integration_tests/klondike_test.cpp
        src/test/integration_tests/gaps_test.cpp
        src/test/integration_tests/accordion_test.cpp
)

set(sources_test_unit
        src/test/unit_tests/global_cache_test.cpp
        src/test/unit_tests/foundations_dominance_test.cpp
        src/test/unit_tests/deal_parser_test.cpp
        src/test/unit_tests/card_test.cpp
        src/test/unit_tests/pile_test.cpp
        src/test/unit_tests/legal_move_gen_test.cpp
        src/test/unit_tests/built_group_move_gen_test.cpp
        src/test/unit_tests/face_up_cards_test.cpp
        src/test/unit_tests/k_plus_stock_test.cpp
        src/test/unit_tests/zobrist_test.cpp
        src/test/unit_tests/solver_cache_selection_test.cpp
        src/test/unit_tests/predecessor_cache_test.cpp
        src/test/unit_tests/generic_flat_cache_compile_test.cpp
        src/test/unit_tests/generic_flat_cache_test.cpp
        src/test/unit_tests/search_trace_test.cpp
        src/test/unit_tests/search_trace_agreement_test.cpp
)

set(sources_test_helper
        src/test/test_helper.cpp
        src/test/test_helper.h
)

set(sources_test ${sources_test_integration} ${sources_test_unit} ${sources_test_helper})

source_group("Test/Integration" FILES ${sources_test_integration})
source_group("Test/Unit" FILES ${sources_test_unit})
source_group("Test/Helper" FILES ${sources_test_helper})

###############################################################################
## Rapid JSON setup ###########################################################
###############################################################################

include_directories(src/main/game)
include_directories(src/main/game/search-state)
include_directories(src/main/solver)
include_directories(src/main/evaluation)
include_directories(src/main/input-output/input)
include_directories(src/main/input-output/input/json-parsing)
include_directories(src/main/input-output/output)
include_directories(lib/rapidjson)

###############################################################################
## Boost setup ################################################################
###############################################################################

# set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

set(boost_components program_options)

find_package(Boost 1.53.0 REQUIRED COMPONENTS ${boost_components})

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
    message(STATUS "Boost libraries: ${Boost_LIBRARIES}")
else()
    message(FATAL_ERROR "Must install Boost libraries")
endif()

###############################################################################
## GTest setup ################################################################
###############################################################################

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
# For Windows: prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

###############################################################################
## compile options ############################################################
###############################################################################

# Common flags for all compilers
set(COMMON_WARNING_FLAGS
    -pedantic -Wall -Wextra
    -Wcast-align -Wcast-qual -Wformat=2 -Winit-self
    -Wmissing-declarations -Wmissing-include-dirs
    -Wno-old-style-cast -Woverloaded-virtual -Wredundant-decls -Wno-shadow
    -Werror
)

# GCC-specific warning flags
set(GCC_WARNING_FLAGS
    -Wlogical-op -Wnoexcept -Wstrict-null-sentinel
)

# Clang-specific warning flags
set(CLANG_WARNING_FLAGS
    -Wno-unknown-warning-option
    -Wno-nontrivial-memcall
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(ALL_WARNING_FLAGS ${COMMON_WARNING_FLAGS} ${GCC_WARNING_FLAGS})
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(ALL_WARNING_FLAGS ${COMMON_WARNING_FLAGS} ${CLANG_WARNING_FLAGS})
else()
    set(ALL_WARNING_FLAGS ${COMMON_WARNING_FLAGS})
endif()

add_compile_options(${ALL_WARNING_FLAGS})
add_compile_definitions(BOOST_LOG_DYN_LINK)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")

# Release flags: platform-aware linker options
if(APPLE)
    # Apple linker does not support -s or -Wl,-O1
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -flto -DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -flto -s -DNDEBUG -Wl,-O1")
endif()
# For profiling (uncomment and comment the above):
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O -fno-inline -fno-inline-functions -g")

# Default to building the 'Debug' configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "The build configuration" FORCE)
endif()

add_executable(solvitaire ${main} ${sources})
set_property(TARGET solvitaire PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_link_libraries(solvitaire PRIVATE ${Boost_LIBRARIES})

add_executable(solvitaire-flat     ${main} ${sources})
target_compile_definitions(solvitaire-flat PRIVATE SOLVITAIRE_FLAT_ONLY)
set_property(TARGET solvitaire-flat PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_link_libraries(solvitaire-flat PRIVATE ${Boost_LIBRARIES})

add_executable(solvitaire-hash-only ${main} ${sources})
target_compile_definitions(solvitaire-hash-only PRIVATE SOLVITAIRE_HASH_ONLY)
set_property(TARGET solvitaire-hash-only PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_link_libraries(solvitaire-hash-only PRIVATE ${Boost_LIBRARIES})

add_executable(solvitaire-lru       ${main} ${sources})
target_compile_definitions(solvitaire-lru PRIVATE SOLVITAIRE_LRU_ONLY)
set_property(TARGET solvitaire-lru PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
target_link_libraries(solvitaire-lru PRIVATE ${Boost_LIBRARIES})

###############################################################################
## Search trace — always-defined trace-variant binaries + optional CTest gate ##
##                                                                            ##
## Gated on SOLVITAIRE_TRACE (set by ./build.sh --trace → cmake-build-trace).##
## The four *-trace executables are ALWAYS defined regardless of the option  ##
## so they can be built via ./build.sh --trace which sets                    ##
## -DSOLVITAIRE_TRACE=ON. The CTest targets are only registered when the     ##
## option is ON.                                                              ##
###############################################################################

# Always add search_trace.cpp to the sources it needs
set(sources_trace src/main/solver/search_trace.cpp)

# -trace variant binaries always defined; SOLVITAIRE_SEARCH_TRACE always set on them
add_executable(solvitaire-trace      ${main} ${sources} ${sources_trace})
target_compile_definitions(solvitaire-trace      PRIVATE SOLVITAIRE_SEARCH_TRACE)
target_link_libraries(solvitaire-trace      PRIVATE ${Boost_LIBRARIES})

add_executable(solvitaire-flat-trace ${main} ${sources} ${sources_trace})
target_compile_definitions(solvitaire-flat-trace PRIVATE SOLVITAIRE_FLAT_ONLY SOLVITAIRE_SEARCH_TRACE)
target_link_libraries(solvitaire-flat-trace PRIVATE ${Boost_LIBRARIES})

add_executable(solvitaire-hash-only-trace ${main} ${sources} ${sources_trace})
target_compile_definitions(solvitaire-hash-only-trace PRIVATE SOLVITAIRE_HASH_ONLY SOLVITAIRE_SEARCH_TRACE)
target_link_libraries(solvitaire-hash-only-trace PRIVATE ${Boost_LIBRARIES})

add_executable(solvitaire-lru-trace  ${main} ${sources} ${sources_trace})
target_compile_definitions(solvitaire-lru-trace  PRIVATE SOLVITAIRE_LRU_ONLY SOLVITAIRE_SEARCH_TRACE)
target_link_libraries(solvitaire-lru-trace  PRIVATE ${Boost_LIBRARIES})

option(SOLVITAIRE_TRACE "Enable search trace CTest targets (requires trace-variant binaries)" OFF)
if(SOLVITAIRE_TRACE)
    message(STATUS "Search trace: ENABLED on all targets (SOLVITAIRE_SEARCH_TRACE)")
    # Also enable on the standard binaries when SOLVITAIRE_TRACE is ON
    foreach(tgt solvitaire solvitaire-flat solvitaire-hash-only solvitaire-lru)
        target_compile_definitions(${tgt} PRIVATE SOLVITAIRE_SEARCH_TRACE)
        target_sources(${tgt} PRIVATE ${sources_trace})
    endforeach()

    ###########################################################################
    ## Trace CTest targets                                                   ##
    ##                                                                       ##
    ## trace_identity_flat   flat-trace binary, same seed twice — prefix match  ##
    ## trace_identity_lru    lru-trace binary,  same seed twice — prefix match  ##
    ## trace_until_eviction  flat-trace vs lru-trace, match until first EVICT   ##
    ## trace_until_timeout   flat-trace timed-out instance, prefix match        ##
    ##                                                                       ##
    ## Solver args follow -- to avoid argparse ambiguity in compare_traces.py.  ##
    ## --until-timeout 10000 caps each comparison to the first 10 000 events.   ##
    ## trace_until_eviction uses --cache-capacity 200 to force early evictions. ##
    ###########################################################################

    # trace_identity_flat: run solvitaire-flat-trace twice with the same seed;
    # the first 10 000 events must be byte-identical.
    add_test(NAME trace_identity_flat
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compare_traces.py"
            "--until-timeout" "10000"
            "--binary" "$<TARGET_FILE:solvitaire-flat-trace>"
            "--" "--type" "black-hole" "--random" "1" "--timeout" "30000"
    )
    set_tests_properties(trace_identity_flat PROPERTIES TIMEOUT 120)

    # trace_identity_lru: same test with the LRU-trace binary.
    add_test(NAME trace_identity_lru
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compare_traces.py"
            "--until-timeout" "10000"
            "--binary" "$<TARGET_FILE:solvitaire-lru-trace>"
            "--" "--type" "black-hole" "--random" "1" "--timeout" "30000"
    )
    set_tests_properties(trace_identity_lru PROPERTIES TIMEOUT 120)

    # trace_until_eviction: flat-trace vs lru-trace on the same seed with a
    # tiny cache (200 states) to force early evictions.  Both traces must
    # agree up to (not including) the first EVICT in either stream.
    add_test(NAME trace_until_eviction
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compare_traces.py"
            "--until-evict"
            "--binary-a" "$<TARGET_FILE:solvitaire-flat-trace>"
            "--binary-b" "$<TARGET_FILE:solvitaire-lru-trace>"
            "--" "--type" "black-hole" "--random" "1"
            "--cache-capacity" "200" "--timeout" "30000"
    )
    set_tests_properties(trace_until_eviction PROPERTIES TIMEOUT 120)

    # trace_until_timeout: run a hard instance (klondike) twice with a
    # wall-clock timeout so both runs end mid-search.  The first 10 000
    # events of both partial traces must be byte-identical.
    add_test(NAME trace_until_timeout
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compare_traces.py"
            "--until-timeout" "10000"
            "--binary" "$<TARGET_FILE:solvitaire-flat-trace>"
            "--" "--type" "klondike" "--random" "1" "--timeout" "30000"
    )
    set_tests_properties(trace_until_timeout PROPERTIES TIMEOUT 120)

    ###########################################################################
    ## Trace regression: old-vs-new binary comparison                        ##
    ##                                                                        ##
    ## trace_regression_level1  — all 150 Level 1 instances (no timeout;     ##
    ##                            instances solve quickly; full comparison)   ##
    ## trace_regression_level2  — all 160 Level 2 instances (5s solver       ##
    ##                            timeout to keep traces manageable)          ##
    ##                                                                        ##
    ## Reference binary: 05-Executables/reference/solvitaire-trace-reference-##
    ##   mac-arm64 (macOS) or solvitaire-trace-reference-linux-amd64 (Linux) ##
    ## Built from: feature/search-trace commit f077558 (Commit E)            ##
    ## SHA-1: be81a19a26339833610467d78b635f4f97a9899e (mac-arm64)          ##
    ##                                                                        ##
    ## Run via: ctest -R trace_regression --output-on-failure                ##
    ###########################################################################

    # Platform-detect the reference binary.
    if(APPLE)
        set(TRACE_REF_BIN
            "${CMAKE_CURRENT_SOURCE_DIR}/../../05-Executables/reference/solvitaire-trace-reference-mac-arm64")
    else()
        set(TRACE_REF_BIN
            "${CMAKE_CURRENT_SOURCE_DIR}/../../05-Executables/reference/solvitaire-trace-reference-linux-amd64")
    endif()

    # trace_regression_level1: all 150 Level 1 instances, no solver timeout.
    # Instances are small JSON files in tests/resources/level1/ and solve fast.
    # CTest ceiling: 30 min (generous; typical run ~5-10 min in trace mode).
    add_test(NAME trace_regression_level1
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/trace_regression.py"
            "--level"      "1"
            "--ref-binary" "${TRACE_REF_BIN}"
            "--cur-binary" "$<TARGET_FILE:solvitaire-trace>"
            "--tests-dir"  "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    set_tests_properties(trace_regression_level1 PROPERTIES TIMEOUT 1800)

    # trace_regression_level2: all 160 Level 2 instances, 5s solver timeout.
    # Uses --random <seed> invocation (no instance files on disk).
    # CTest ceiling: 60 min (generous; typical run ~15-30 min in trace mode).
    add_test(NAME trace_regression_level2
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/trace_regression.py"
            "--level"      "2"
            "--ref-binary" "${TRACE_REF_BIN}"
            "--cur-binary" "$<TARGET_FILE:solvitaire-trace>"
            "--tests-dir"  "${CMAKE_CURRENT_SOURCE_DIR}/tests"
            "--timeout-ms" "5000"
    )
    set_tests_properties(trace_regression_level2 PROPERTIES TIMEOUT 3600)

else()
    message(STATUS "Search trace: disabled on regular targets (solvitaire-*-trace always available)")
endif()

###############################################################################
## testing ####################################################################
###############################################################################

add_executable(unit_tests ${sources_test} ${sources} ${sources_trace})
target_compile_definitions(unit_tests PRIVATE SOLVITAIRE_SEARCH_TRACE)
target_link_libraries(unit_tests PRIVATE gtest_main ${Boost_LIBRARIES})

enable_testing()

# Run the GoogleTest suite.  Working directory is the project root so that
# integration tests can resolve paths like "tests/resources/unit_tests/…"
#
# unit_tests       — M6 default: excludes DualCacheTest and MismatchDiagnostic
#                    (DualCacheTest targets M5 behaviour; MismatchDiagnostic is
#                    a diagnostic tool, not a regression gate)
# unit_tests_full  — all tests including DualCacheTest; use near end of M6
#                    for regression verification
add_test(NAME unit_tests
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "$<TARGET_FILE:unit_tests>"
                "--gtest_filter=-DualCacheTest.*:MismatchDiagnostic.*"
)

add_test(NAME unit_tests_full
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "$<TARGET_FILE:unit_tests>"
)

add_test(NAME compare_binaries
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/compare_binaries.sh"
)

add_test(NAME trailing_space
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_output_format.py"
        "--exe" "$<TARGET_FILE:solvitaire>"
        "--gametype" "klondike"
        "--deal" "${CMAKE_CURRENT_SOURCE_DIR}/tests/resources/unit_tests/deal-presets/klondikeSM1.json"
)
add_test(NAME trailing_space_free_cell
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_output_format.py"
        "--exe" "$<TARGET_FILE:solvitaire>"
        "--gametype" "free-cell"
        "--deal" "${CMAKE_CURRENT_SOURCE_DIR}/tests/resources/unit_tests/free_cell/fc-pro-3.json"
)
add_test(NAME regression_level1
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire>"
        "--instances" "${CMAKE_CURRENT_SOURCE_DIR}/tests/resources/level1"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level1.json"
        "--enforce-node-counts"
)
set_tests_properties(regression_level1 PROPERTIES TIMEOUT 900)
# Levels 2-5 use seed-based invocation (no JSON instance files on disk).
# --instances is omitted; --max-instance-timeout-ms caps each per-instance run.
# CTest TIMEOUT is set to 1.5x the worst-case total (instances * per-instance cap + overhead).
add_test(NAME regression_level2
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level2.json"
        "--max-instance-timeout-ms" "60000"
        "--enforce-node-counts"
)
set_tests_properties(regression_level2 PROPERTIES TIMEOUT 15000)
add_test(NAME regression_level3
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level3.json"
        "--max-instance-timeout-ms" "120000"
        "--enforce-node-counts"
)
set_tests_properties(regression_level3 PROPERTIES TIMEOUT 30000)
add_test(NAME regression_level4
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level4.json"
        "--max-instance-timeout-ms" "600000"
)
set_tests_properties(regression_level4 PROPERTIES TIMEOUT 150000)
add_test(NAME regression_level5
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level5.json"
        "--max-instance-timeout-ms" "1800000"
)
set_tests_properties(regression_level5 PROPERTIES TIMEOUT 450000)

###############################################################################
## Variant-binary regression targets                                         ##
##                                                                           ##
## 15 targets: regression_levelN_{flat,hash_only,lru} for N=1..5            ##
##                                                                           ##
## *_flat        --skip-ineligible                                           ##
##   Flat cache handles flat-eligible + accordion (predecessor_flat_cache).  ##
##   Suit-sym, sequence, and 2-deck games are rejected by the binary and     ##
##   logged as SKIP.                                                         ##
##                                                                           ##
## *_hash_only   --skip-ineligible (levels 1-4 have per-variant oracles)    ##
##   Hash-only cache handles the same eligible set as flat. Levels 1-4 use  ##
##   levelN_hash_only.json oracles generated from the pre-refactor-work tag  ##
##   binary with --cache-type hash-only; node counts are validated.          ##
##   Level 5 still uses level5.json + --compare-outcome-only (no oracle).   ##
##                                                                           ##
## *_lru         --force-lru --skip-ineligible                              ##
##   LRU cache handles suit-sym, 2-deck, sequence natively.                 ##
##   Flat-eligible games forced to LRU via --force-lru (nodes will differ   ##
##   from flat/default oracles).                                             ##
##   Accordion games rejected by the binary -> SKIP.                        ##
##   Levels 1-3 use per-variant levelN_lru.json oracles with node counts.  ##
##   Levels 4-5 use outcome-only comparison against default oracles.        ##
###############################################################################

# Level 1 — JSON-based; needs --instances
add_test(NAME regression_level1_flat
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-flat>"
        "--instances" "${CMAKE_CURRENT_SOURCE_DIR}/tests/resources/level1"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level1.json"
        "--skip-ineligible" "--enforce-node-counts"
)
set_tests_properties(regression_level1_flat PROPERTIES TIMEOUT 900)

add_test(NAME regression_level1_hash_only
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-hash-only>"
        "--instances" "${CMAKE_CURRENT_SOURCE_DIR}/tests/resources/level1"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level1_hash_only.json"
        "--skip-ineligible" "--enforce-node-counts"
)
set_tests_properties(regression_level1_hash_only PROPERTIES TIMEOUT 900)

add_test(NAME regression_level1_lru
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-lru>"
        "--instances" "${CMAKE_CURRENT_SOURCE_DIR}/tests/resources/level1"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level1_lru.json"
        "--force-lru" "--skip-ineligible" "--enforce-node-counts"
)
set_tests_properties(regression_level1_lru PROPERTIES TIMEOUT 900)

# Levels 2-5 — seed-based; no --instances
add_test(NAME regression_level2_flat
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-flat>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level2.json"
        "--max-instance-timeout-ms" "60000"
        "--skip-ineligible" "--enforce-node-counts"
)
set_tests_properties(regression_level2_flat PROPERTIES TIMEOUT 15000)

add_test(NAME regression_level2_hash_only
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-hash-only>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level2_hash_only.json"
        "--max-instance-timeout-ms" "60000"
        "--skip-ineligible" "--enforce-node-counts"
)
set_tests_properties(regression_level2_hash_only PROPERTIES TIMEOUT 15000)

add_test(NAME regression_level2_lru
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-lru>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level2_lru.json"
        "--max-instance-timeout-ms" "60000"
        "--force-lru" "--skip-ineligible" "--enforce-node-counts"
)
set_tests_properties(regression_level2_lru PROPERTIES TIMEOUT 15000)

add_test(NAME regression_level3_flat
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-flat>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level3.json"
        "--max-instance-timeout-ms" "120000"
        "--skip-ineligible" "--enforce-node-counts"
)
set_tests_properties(regression_level3_flat PROPERTIES TIMEOUT 30000)

add_test(NAME regression_level3_hash_only
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-hash-only>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level3_hash_only.json"
        "--max-instance-timeout-ms" "120000"
        "--skip-ineligible" "--enforce-node-counts"
)
set_tests_properties(regression_level3_hash_only PROPERTIES TIMEOUT 30000)

add_test(NAME regression_level3_lru
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-lru>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level3_lru.json"
        "--max-instance-timeout-ms" "120000"
        "--force-lru" "--skip-ineligible" "--enforce-node-counts"
)
set_tests_properties(regression_level3_lru PROPERTIES TIMEOUT 30000)

add_test(NAME regression_level4_flat
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-flat>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level4.json"
        "--max-instance-timeout-ms" "600000"
        "--skip-ineligible"
)
set_tests_properties(regression_level4_flat PROPERTIES TIMEOUT 150000)

add_test(NAME regression_level4_hash_only
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-hash-only>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level4_hash_only.json"
        "--max-instance-timeout-ms" "600000"
        "--skip-ineligible"
)
set_tests_properties(regression_level4_hash_only PROPERTIES TIMEOUT 150000)

add_test(NAME regression_level4_lru
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-lru>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level4.json"
        "--max-instance-timeout-ms" "600000"
        "--force-lru" "--skip-ineligible" "--compare-outcome-only"
)
set_tests_properties(regression_level4_lru PROPERTIES TIMEOUT 150000)

add_test(NAME regression_level5_flat
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-flat>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level5.json"
        "--max-instance-timeout-ms" "1800000"
        "--skip-ineligible"
)
set_tests_properties(regression_level5_flat PROPERTIES TIMEOUT 450000)

add_test(NAME regression_level5_hash_only
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-hash-only>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level5.json"
        "--max-instance-timeout-ms" "1800000"
        "--skip-ineligible" "--compare-outcome-only"
)
set_tests_properties(regression_level5_hash_only PROPERTIES TIMEOUT 450000)

add_test(NAME regression_level5_lru
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_runner.py"
        "--exe" "$<TARGET_FILE:solvitaire-lru>"
        "--oracle" "${CMAKE_CURRENT_SOURCE_DIR}/tests/oracles/level5.json"
        "--max-instance-timeout-ms" "1800000"
        "--force-lru" "--skip-ineligible" "--compare-outcome-only"
)
set_tests_properties(regression_level5_lru PROPERTIES TIMEOUT 450000)

###############################################################################
## packaging ##################################################################
###############################################################################

# all install commands get the same destination. this allows us to use paths
# relative to the executable.
install(TARGETS solvitaire DESTINATION solvitaire)

set(CPACK_OUTPUT_FILE_PREFIX installer)
set(CPACK_PACKAGE_VERSION "1.0.0")

# we don't want to split our program up into several things
set(CPACK_MONOLITHIC_INSTALL 1)

# This must be last
include(CPack)
