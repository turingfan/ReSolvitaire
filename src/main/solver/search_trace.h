/*
  Solvitaire: a solver for perfect information solitaire games
  Copyright (C) 2018 Charles Blake <thecharlesblake@live.co.uk> and
  Ian Gent <Ian.Gent@st-andrews.ac.uk>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
*/

#ifndef SOLVITAIRE_SEARCH_TRACE_H
#define SOLVITAIRE_SEARCH_TRACE_H

// Search trace infrastructure.
//
// When SOLVITAIRE_SEARCH_TRACE is defined (debug builds by default; release
// builds with -DSOLVITAIRE_TRACE=ON), the STRACE_* macros emit one-line events
// to an output file, enabling two runs to be diffed for correctness validation.
//
// When SOLVITAIRE_SEARCH_TRACE is not defined, every macro expands to ((void)0)
// and the compiler eliminates all callsites and their arguments entirely.
//
// Argument note: macro arguments are all simple values (move struct, integral
// types). No expensive expressions appear at callsites, so argument evaluation
// in the disabled path is not a practical concern.

#ifdef SOLVITAIRE_SEARCH_TRACE

#include "../game/move.h"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <string>

class trace_writer {
public:
    // Meyer's singleton — one trace per process.
    static trace_writer& instance();

    // Open the trace file and write the first three header lines
    // (TRACE, DATE, CMD). Call write_init() once game type and policy
    // are known to complete the header.
    void open(const std::string& path, int argc, const char** argv);

    // Write GAME and POLICY header lines + blank separator.
    // Must be called after open() and before any event writes.
    void write_init(const std::string& game_type, int seed,
                    const std::string& streamliner,
                    const std::string& policy);

    // Close and flush the trace file.
    void close();

    bool enabled() const { return enabled_; }

    // Set the operation number at which to break and print game state.
    void set_break_at(uint64_t n) { break_at_ = n; }

    // Register a callback that prints the current game state to stdout.
    // Called when the operation counter reaches break_at_, or when
    // check_hash_break() fires a find-hash match.
    void set_break_state_printer(std::function<void()> printer) {
        break_printer_ = printer;
    }

    // Set a Zobrist hash to search for. When check_hash_break() is called
    // with a matching hash (at a new-state insertion), the break printer fires.
    // Pass 0 to disable (0 is never a valid Zobrist hash in practice).
    void set_find_hash(uint64_t h) { find_hash_ = h; }
    uint64_t find_hash() const { return find_hash_; }

    // Called after each new-state insertion (MISS) with the state's Zobrist
    // hash. If it matches find_hash_, fires the break printer and exits.
    void check_hash_break(uint64_t h) {
        if (find_hash_ == 0 || h != find_hash_) return;
        std::cout << "=== FOUND HASH " << std::hex << h << std::dec
                  << " AT OPERATION " << op_ << " ===\n";
        if (break_printer_) {
            break_printer_();
        } else {
            std::cout << "(game state printer not yet registered)\n";
        }
        std::fflush(stdout);
        if (file_) std::fflush(file_);
        std::exit(0);
    }

    // Event writers — called via macros below.
    void write_move_event(const char* keyword, const move& mv);
    void write_depth(uint64_t d);
    void write_legal(std::size_t n);
    void write_event(const char* keyword);   // for zero-field events

private:
    trace_writer() = default;

    FILE*    file_     = nullptr;
    uint64_t op_       = 0;          // monotonic counter; plain uint64_t (single-threaded)
    bool     enabled_  = false;
    uint64_t break_at_  = UINT64_MAX;
    uint64_t find_hash_ = 0;
    std::function<void()> break_printer_;

    void write_line(const char* body);  // body must be pre-formatted
    void check_break();
    static const char* mtype_str(move::mtype t);
};

#define STRACE_INIT(type, seed, streamliner, policy) \
    trace_writer::instance().write_init(type, seed, streamliner, policy)
#define STRACE_MOVE(mv)   trace_writer::instance().write_move_event("MOVE", mv)
#define STRACE_UNDO(mv)   trace_writer::instance().write_move_event("UNDO", mv)
#define STRACE_DEPTH(d)   trace_writer::instance().write_depth(static_cast<uint64_t>(d))
#define STRACE_QUERY()    trace_writer::instance().write_event("QUERY")
#define STRACE_HIT()      trace_writer::instance().write_event("HIT")
#define STRACE_MISS()     trace_writer::instance().write_event("MISS")
#define STRACE_INSERT()   trace_writer::instance().write_event("INSERT")
#define STRACE_EVICT()    trace_writer::instance().write_event("EVICT")
#define STRACE_LEGAL(n)   trace_writer::instance().write_legal(static_cast<std::size_t>(n))
#define STRACE_RESULT(r)  trace_writer::instance().write_event(r)

#else  // SOLVITAIRE_SEARCH_TRACE not defined — zero overhead

#define STRACE_INIT(type, seed, streamliner, policy)  ((void)0)
#define STRACE_MOVE(mv)   ((void)0)
#define STRACE_UNDO(mv)   ((void)0)
#define STRACE_DEPTH(d)   ((void)0)
#define STRACE_QUERY()    ((void)0)
#define STRACE_HIT()      ((void)0)
#define STRACE_MISS()     ((void)0)
#define STRACE_INSERT()   ((void)0)
#define STRACE_EVICT()    ((void)0)
#define STRACE_LEGAL(n)   ((void)0)
#define STRACE_RESULT(r)  ((void)0)

#endif  // SOLVITAIRE_SEARCH_TRACE
#endif  // SOLVITAIRE_SEARCH_TRACE_H
