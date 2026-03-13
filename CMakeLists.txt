# File based on https://stackoverflow.com/questions/21163188/most-simple-but-complete-cmake-example

cmake_minimum_required(VERSION 3.14...4.0)
project(solvitaire)

# CMP0167: Use Boost's own CMake config instead of removed FindBoost module
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# For -flto flag
include(CheckIPOSupported)
check_ipo_supported(RESULT supported OUTPUT error)

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
        src/main/game/sol_rules.cpp
        src/main/game/search-state/game_state.legal_moves.cpp
        src/main/game/search-state/game_state.dominance_moves.cpp
        src/main/game/search-state/game_state.pile_order.cpp
        src/main/game/move.cpp
        src/main/game/move.h
)

set(sources_solver
        src/main/solver/solver.cpp
        src/main/solver/solver.h
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


###############################################################################
## testing ####################################################################
###############################################################################

add_executable(unit_tests ${sources_test} ${sources})
target_link_libraries(unit_tests PRIVATE gtest_main ${Boost_LIBRARIES})

enable_testing()
add_test(NAME unit_tests
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/test"
        COMMAND unit_tests)
add_test(NAME trailing_space
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/test"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/src/test/check_for_no_trailing_space_in_output.py"
        "--exe" "$<TARGET_FILE:solvitaire>"
        "--gametype" "klondike"
        "--deal" "${CMAKE_CURRENT_SOURCE_DIR}/resources/deal-presets/klondikeSM1.json"
)
add_test(NAME trailing_space_free_cell
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/test"
        COMMAND "python3" "${CMAKE_CURRENT_SOURCE_DIR}/src/test/check_for_no_trailing_space_in_output.py"
        "--exe" "$<TARGET_FILE:solvitaire>"
        "--gametype" "free-cell"
        "--deal" "${CMAKE_CURRENT_SOURCE_DIR}/src/test/resources/free_cell/fc-pro-3.json"
)

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
