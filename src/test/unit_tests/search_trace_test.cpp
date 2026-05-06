/*
  Solvitaire: a solver for perfect information solitaire games
  Copyright (C) 2018 Charles Blake <thecharlesblake@live.co.uk> and
  Ian Gent <Ian.Gent@st-andrews.ac.uk>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#include <gtest/gtest.h>

#ifdef SOLVITAIRE_SEARCH_TRACE

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../../main/solver/search_trace.h"
#include "../../main/game/move.h"

namespace {

// Read all lines from a file (strips trailing newline from each).
std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        lines.push_back(line);
    }
    return lines;
}

// Extract the 10-digit monotonic counter from an event line.
uint64_t event_counter(const std::string& line) {
    std::istringstream iss(line);
    uint64_t n = 0;
    iss >> n;
    return n;
}

// Return true if the second whitespace-delimited token of line equals kw.
// Event lines have the form: "XXXXXXXXXX KEYWORD [fields...]"
bool has_keyword(const std::string& line, const std::string& kw) {
    std::istringstream iss(line);
    std::string tok;
    iss >> tok;  // skip counter
    iss >> tok;  // keyword
    return tok == kw;
}

static const char* k_fake_argv[] = {"unit_tests"};

}  // namespace

class SearchTraceTest : public ::testing::Test {
protected:
    std::string trace_path_;

    void SetUp() override {
        trace_path_ = ::testing::TempDir() + "solvitaire_strace_test.trace";
        std::remove(trace_path_.c_str());
        // Ensure the singleton is in a closed (disabled) state.
        trace_writer::instance().close();
    }

    void TearDown() override {
        trace_writer::instance().close();
        std::remove(trace_path_.c_str());
    }
};

// Test 1: HeaderFormat — verify 5-line header + blank separator
TEST_F(SearchTraceTest, HeaderFormat) {
    trace_writer::instance().open(trace_path_, 1, k_fake_argv);
    trace_writer::instance().write_init("test-game", 42, "none", "flat");
    trace_writer::instance().close();

    auto lines = read_lines(trace_path_);
    ASSERT_GE(lines.size(), 6u) << "Expected at least 6 header lines";

    EXPECT_EQ(lines[0], "TRACE v=1");
    EXPECT_EQ(lines[1].substr(0, 4), "DATE");
    EXPECT_EQ(lines[2], "CMD unit_tests");
    EXPECT_EQ(lines[3].substr(0, 4), "GAME");
    EXPECT_NE(lines[3].find("type=test-game"),    std::string::npos);
    EXPECT_NE(lines[3].find("seed=42"),           std::string::npos);
    EXPECT_NE(lines[3].find("streamliner=none"),  std::string::npos);
    EXPECT_EQ(lines[4], "POLICY flat");
    EXPECT_EQ(lines[5], "") << "Line 6 (index 5) must be the blank separator";
}

// Test 2: EventOrdering — emit a known sequence of events, verify ordering
TEST_F(SearchTraceTest, EventOrdering) {
    trace_writer::instance().open(trace_path_, 1, k_fake_argv);
    trace_writer::instance().write_init("test-game", 0, "none", "flat");

    // Emit the canonical DFS sequence for a single cache-miss step.
    trace_writer::instance().write_event("QUERY");
    trace_writer::instance().write_event("MISS");
    trace_writer::instance().write_event("INSERT");
    trace_writer::instance().write_legal(3);
    move mv(move::mtype::regular, 0, 1);
    trace_writer::instance().write_move_event("MOVE", mv);
    trace_writer::instance().write_depth(1);
    trace_writer::instance().write_event("SOLVED");

    trace_writer::instance().close();

    auto lines = read_lines(trace_path_);
    // 6 header lines + 7 event lines
    ASSERT_GE(lines.size(), 13u);

    const std::vector<std::string> expected_kw = {
        "QUERY", "MISS", "INSERT", "LEGAL", "MOVE", "DEPTH", "SOLVED"
    };
    for (std::size_t i = 0; i < expected_kw.size(); ++i) {
        EXPECT_TRUE(has_keyword(lines[6 + i], expected_kw[i]))
            << "Line " << (6 + i) << ": expected keyword "
            << expected_kw[i] << ", got: " << lines[6 + i];
    }
}

// Test 3: MonotonicCounter — counter increments by exactly 1 per event, no gaps
TEST_F(SearchTraceTest, MonotonicCounter) {
    trace_writer::instance().open(trace_path_, 1, k_fake_argv);
    trace_writer::instance().write_init("test-game", 0, "none", "flat");

    const int N = 10;
    for (int i = 0; i < N; ++i) {
        trace_writer::instance().write_event("QUERY");
    }

    trace_writer::instance().close();

    auto lines = read_lines(trace_path_);
    ASSERT_GE(lines.size(), static_cast<std::size_t>(6 + N));

    uint64_t prev = event_counter(lines[6]);
    for (int i = 1; i < N; ++i) {
        uint64_t curr = event_counter(lines[6 + i]);
        EXPECT_EQ(curr, prev + 1u)
            << "Counter gap at event " << i
            << ": prev=" << prev << " curr=" << curr;
        prev = curr;
    }
}

// Test 4: NoOpWhenDisabled — verify no file created when open() is not called
TEST_F(SearchTraceTest, NoOpWhenDisabled) {
    // SetUp calls close(), so enabled_ is false.
    EXPECT_FALSE(trace_writer::instance().enabled());

    // All write methods should be no-ops when disabled.
    trace_writer::instance().write_event("QUERY");
    trace_writer::instance().write_event("HIT");
    trace_writer::instance().write_depth(0);
    trace_writer::instance().write_legal(0);

    // No trace file should have been created.
    std::ifstream f(trace_path_);
    EXPECT_FALSE(f.good())
        << "No trace file should exist when open() was not called";
}

#endif  // SOLVITAIRE_SEARCH_TRACE
