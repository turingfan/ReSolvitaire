/*
  Solvitaire: a solver for perfect information solitaire games
  Copyright (C) 2018 Charles Blake <thecharlesblake@live.co.uk> and
  Ian Gent <Ian.Gent@st-andrews.ac.uk>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#include "search_trace.h"

#ifdef SOLVITAIRE_SEARCH_TRACE

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>

// ── Singleton ────────────────────────────────────────────────────────────────

trace_writer& trace_writer::instance() {
    static trace_writer inst;
    return inst;
}

// ── Open / close ─────────────────────────────────────────────────────────────

void trace_writer::open(const std::string& path, int argc, char** argv) {
    file_ = std::fopen(path.c_str(), "w");
    if (!file_) {
        std::cerr << "Warning: could not open trace file: " << path << "\n";
        return;
    }
    enabled_ = true;

    // Line 1: format version
    std::fprintf(file_, "TRACE v=1\n");

    // Line 2: ISO 8601 local date/time
    std::time_t now = std::time(nullptr);
    char timebuf[32];
    std::strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S",
                  std::localtime(&now));
    std::fprintf(file_, "DATE %s\n", timebuf);

    // Line 3: full command line reconstructed from argv
    std::fprintf(file_, "CMD");
    for (int i = 0; i < argc; ++i) {
        std::fprintf(file_, " %s", argv[i]);
    }
    std::fprintf(file_, "\n");

    // Lines 4-5 (GAME, POLICY) and blank separator written by write_init()
}

void trace_writer::write_init(const std::string& game_type, int seed,
                               const std::string& streamliner,
                               const std::string& policy) {
    if (!enabled_) return;
    std::fprintf(file_, "GAME type=%s seed=%d streamliner=%s\n",
                 game_type.c_str(), seed, streamliner.c_str());
    std::fprintf(file_, "POLICY %s\n", policy.c_str());
    std::fprintf(file_, "\n");  // blank separator before event stream
    std::fflush(file_);
}

void trace_writer::close() {
    if (file_) {
        std::fclose(file_);
        file_    = nullptr;
        enabled_ = false;
    }
}

// ── Event writers ─────────────────────────────────────────────────────────────

void trace_writer::write_event(const char* keyword) {
    if (!enabled_) return;
    write_line(keyword);
}

void trace_writer::write_depth(uint64_t d) {
    if (!enabled_) return;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "DEPTH d=%llu",
                  static_cast<unsigned long long>(d));
    write_line(buf);
}

void trace_writer::write_legal(std::size_t n) {
    if (!enabled_) return;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "LEGAL n=%zu", n);
    write_line(buf);
}

void trace_writer::write_move_event(const char* keyword, const move& mv) {
    if (!enabled_) return;
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "%-5s t=%-20s f=%-3d to=%-3d c=%-2d rev=%d flip=%d dom=%d",
                  keyword,
                  mtype_str(mv.type),
                  static_cast<int>(mv.from),
                  static_cast<int>(mv.to),
                  static_cast<int>(mv.count),
                  mv.reveal_move    ? 1 : 0,
                  mv.flip_waste     ? 1 : 0,
                  mv.dominance_move ? 1 : 0);
    write_line(buf);
}

// ── Internal helpers ──────────────────────────────────────────────────────────

// Writes one event line: "<10-digit counter> <body>\n"
// body must be a pre-formatted, NUL-terminated string.
void trace_writer::write_line(const char* body) {
    assert(file_);
    std::fprintf(file_, "%010llu %s\n",
                 static_cast<unsigned long long>(op_), body);
    ++op_;
    check_break();
}

void trace_writer::check_break() {
    if (op_ != break_at_) return;

    std::cout << "=== BREAK AT OPERATION " << break_at_ << " ===\n";
    if (break_printer_) {
        break_printer_();
    } else {
        std::cout << "(game state printer not yet registered)\n";
    }
    std::fflush(stdout);
    if (file_) std::fflush(file_);
    std::exit(0);
}

const char* trace_writer::mtype_str(move::mtype t) {
    switch (t) {
        case move::mtype::regular:              return "regular";
        case move::mtype::built_group:          return "built_group";
        case move::mtype::stock_k_plus:         return "stock_k_plus";
        case move::mtype::stock_to_all_tableau: return "stock_to_all_tableau";
        case move::mtype::sequence:             return "sequence";
        case move::mtype::accordion:            return "accordion";
        case move::mtype::null:                 return "null";
        default:                                return "unknown";
    }
}

#endif  // SOLVITAIRE_SEARCH_TRACE
