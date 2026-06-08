/*
  Solvitaire: a solver for perfect information solitaire games
  Copyright (C) 2018 Charles Blake <thecharlesblake@live.co.uk> and 
  Ian Gent <Ian.Gent@st-andrews.ac.uk>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program (see LICENSE file); if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
//
// Created by thecharlesblake on 10/24/17.
//

#include <string>
#include <ostream>
#include <iostream>
#include <algorithm>
#include <list>

#include <chrono>
#include <iomanip>
#include <signal.h>

#include "solver.h"
#include "search_trace.h"
#include "../game/move.h"
#include "../input-output/output/log_helper.h"
#include "../input-output/output/state_printer.h"
#include "../input-output/input/command_line_helper.h"

using std::atomic;
using std::vector;
using std::cout;
using std::clog;
using std::pair;
using std::max;
using std::min;
using std::begin;
using std::end;
using boost::optional;
using std::fixed;
using std::setprecision;

static bool sigint = false;

void sigint_handler(int i) {
    // So it doesn't complain about unused param
    sigint = i == 1 ? true : true;
}

template <typename Policy>
solver_impl<Policy>::solver_impl(const game_state_impl<Policy>& gs, typename Policy::cache_type& c)
        : cache(c)
        , init_state(gs)
        , state(gs)
        , frontier()
        , root(move(move::mtype::null))
        , current_node() {
    frontier.push_back(root);
    current_node = begin(frontier);
    res.states_searched = 0;
    res.unique_states_searched = 0;
    res.backtracks = 0;
    res.dominance_moves = 0;
    res.states_removed_from_cache = 0;
    res.max_depth = 0;
    res.depth = 0;
#ifdef SOLVITAIRE_SEARCH_TRACE
    trace_writer::instance().set_break_state_printer([this]() {
        if constexpr (Policy::computes_hash) {
            std::cout << "hash: 0x" << std::hex << state.get_zobrist_hash()
                      << std::dec << '\n';
        }
        state_printer::print(std::cout, state);
        std::cout << '\n';
        std::cout.flush();
    });
#endif
}

solver_node::solver_node(const ::move m) noexcept
        : mv(m), child_moves(), cache_state() {
}

template <typename Policy>
solver_result solver_impl<Policy>::run(boost::optional<millisec> timeout,
                                       boost::optional<uint64_t> bound) {
    // Set interrupt handler
    signal(SIGINT, sigint_handler);

    // Configure the depth bound for this pass. boost::none => unbounded (L = inf).
    depth_bound = bound;
    any_truncation = false;

    // Set timings
    const clock::time_point start_time = clock::now();
    typename result::type res_type = timeout ? dfs(start_time + *timeout) : dfs();
    res.sol_type = res_type;
    res.states_removed_from_cache = cache.get_states_removed_from_cache();
    res.cache_size = cache.size();
    res.cache_bucket_count = cache.bucket_count();
    res.time = std::chrono::duration_cast<millisec>(clock::now() - start_time);
    return res;
}

template <typename Policy>
solver_result::type solver_impl<Policy>::dfs(boost::optional<clock::time_point> end_time) {
    bool states_exhausted = false;

    while(!(state.is_solved() || states_exhausted)) {
        if (end_time && clock::now() >= *end_time) {
            STRACE_RESULT("TIMEOUT");
            return result::type::TIMEOUT;
        } else if (sigint) {
            STRACE_RESULT("TERMINATED");
            return result::type::TERMINATED;
        }

        // ─── Depth cut (Stage 1) ─────────────────────────────────────────────
        // If this node's depth has reached the bound L, treat it as a truncated
        // leaf: do NOT expand it (neither the dominance/auto-foundation push nor
        // legal-move expansion). Record that a truncation happened (monotone,
        // set-only) and backtrack. res.depth is the number of moves from the root
        // to the current node (incremented once per ply below), so this fires
        // exactly when the node sits at depth == L.
        //
        // When depth_bound is boost::none the bound is disabled (L = infinity):
        // this condition can never be true, the node is never an artificial leaf,
        // and any_truncation can never be set — the search is byte-identical to an
        // unbounded run. The truncated node was cut before any cache insert, so it
        // has no cache entry / live bit; backtracking with no iterator is correct.
        if (depth_bound && res.depth >= *depth_bound) {
            any_truncation = true;
            // (bounded LRU only) This node is a truncated leaf: OPEN(0). Record b = 0
            // so its parent folds plus_one(0) = 1 into its running `verified`, making
            // every ancestor of a truncated leaf OPEN (never DEAD) — the core
            // soundness mechanism. The node itself is uncached (cut before insert),
            // so nothing is written to the cache here. (Flat bounded keeps Stage-1
            // behaviour: any_truncation only.)
            if constexpr (!Policy::computes_hash) current_node->verified = 0;
            states_exhausted = revert_to_last_node_with_children();
        } else {

#ifndef NDEBUG
        if (current_node->mv.dominance_move) {
            LOG_DEBUG("(dominance move)");
        }
        LOG_DEBUG(state);
#endif

        // If there is a dominance move available, adds it to the search tree
        // and repeats. Doesn't cache the state.
        optional<move> dominance_move = state.get_dominance_move();
        if (dominance_move) {
            // Adds the dominance move as a child of the current search node;
            current_node->child_moves.emplace_back(*dominance_move);
        } else {
            try {
                // Caches the current state
                bool is_new_state;
                STRACE_QUERY();
                if constexpr (Policy::computes_hash) {
                    // Flat-cache path: set payload depth, then insert directly
                    if constexpr (Policy::computes_payload) {
                        state.set_payload_depth(static_cast<uint16_t>(
                            min(res.depth, static_cast<uint64_t>(UINT16_MAX))));
                    }
                    if (state.uses_predecessor_cache()) {
                        state.set_predecessor_payload_depth(static_cast<uint8_t>(
                            min(res.depth, static_cast<uint64_t>(UINT8_MAX))));
                    }
                    is_new_state = cache.insert_t(state);
#ifdef SOLVITAIRE_SEARCH_TRACE
                    if (is_new_state) {
                        trace_writer::instance().check_hash_break(
                            state.get_zobrist_hash());
                    }
#endif
#ifndef NDEBUG
                    if constexpr (Policy::computes_payload) {
                        state.assert_payload_consistent();
                    }
#endif
                } else {
                    // LRU-cache path: insert with iterator for live-bit tracking
                    pair<lru_cache::item_list::iterator, bool> insert_res = cache.insert_with_iterator(state);
                    current_node->cache_state = insert_res.first;
                    is_new_state = insert_res.second;
                }
                if (is_new_state) { STRACE_MISS(); STRACE_INSERT(); }
                else              { STRACE_HIT(); }

                // ─── Stage 2b: DFSTT3 cross-pass reuse (bounded LRU only) ────────
                // Replaces the plain "in cache ⇒ backtrack" prune (which is unsound
                // under a bound: a node searched only to a small budget may hide a
                // win/truncation beyond its old horizon) with the reuse inequality
                // (proposal §3.3/§3.4) + the DFSTT3 cycle backup (§3.5). Engaged only
                // when depth_bound is set on the LRU policy, so the unbounded/legacy
                // path below is byte-identical (the L=∞ trace identity gate enforces
                // this) and the flat path is untouched.
                bool handled_by_reuse = false;
                if constexpr (!Policy::computes_hash) {
                  if (depth_bound) {
                    handled_by_reuse = true;
                    const lru_cache::item_list::iterator e_it = *current_node->cache_state;
                    const uint64_t L     = *depth_bound;
                    const uint64_t d     = res.depth;          // d < L (the cut handled d >= L)
                    const uint64_t B_now = L - d;              // remaining budget here

                    bool expand;
                    if (is_new_state) {
                        expand = true;                          // never seen ⇒ expand
                    } else if (e_it->live) {
                        // CYCLE: back-edge to an ON_PATH ancestor. Contribute the
                        // ancestor's CURRENT finite estimate (its provisional OPEN b),
                        // never +inf / a closed edge (DFSTT3, proposal §3.5).
                        current_node->verified = e_it->b;
                        expand = false;
                    } else if (e_it->dead) {
                        // DEAD: subtree exhausted with no truncation below ⇒ prune,
                        // budget-independent and safe across all passes (the collapse).
                        current_node->verified = solver_node::INF_B;
                        expand = false;
                    } else {
                        // OPEN(b): prune iff covered — b ≥ B_now AND not re-reached via
                        // a strictly shorter path (a shorter path grants more budget).
                        const bool shorter_path = d < e_it->g_min;
                        if (!shorter_path && static_cast<uint64_t>(e_it->b) >= B_now) {
                            assert(static_cast<uint64_t>(e_it->b) >= B_now);  // §7.3(b)
                            current_node->verified = e_it->b;
                            expand = false;
                        } else {
                            expand = true;                      // re-open: more budget / shorter path
                        }
                    }

                    if (expand) {
                        // Mark ON_PATH + record a provisional OPEN estimate while the
                        // subtree is explored (so back-edges to it read a finite b).
                        cache.begin_expand(e_it,
                            static_cast<uint32_t>(std::min<uint64_t>(d, UINT32_MAX)),
                            static_cast<uint32_t>(std::min<uint64_t>(B_now, UINT32_MAX)));
                        current_node->expanded = true;
                        current_node->verified = solver_node::INF_B;   // reset accumulator
                        vector<move> next_moves = state.get_legal_moves(current_node->mv);
                        STRACE_LEGAL(next_moves.size());
                        if (next_moves.empty()) {
                            // genuine dead end ⇒ DEAD (verified stays +inf; finalised
                            // in revert).
                            states_exhausted = revert_to_last_node_with_children(current_node->cache_state);
                        } else {
                            current_node->child_moves = std::move(next_moves);
                        }
                    } else {
                        // Prune: current_node->verified is set; revert folds
                        // plus_one(verified) into the parent and (for hit nodes) does
                        // NOT touch the cache entry or its live bit (a cycle target is
                        // a still-live ancestor).
                        if (!is_new_state) res.unique_states_searched--;
                        states_exhausted = revert_to_last_node_with_children(current_node->cache_state);
                    }
                  }
                }

                if (!handled_by_reuse) {
                  if (is_new_state) {
                    // Gets the legal moves in the current state
                    vector<move> next_moves = state.get_legal_moves(current_node->mv);
                    STRACE_LEGAL(next_moves.size());

                    // If there are none, reverts to the last node with children
                    if (next_moves.empty()) {
                        if constexpr (Policy::computes_hash) {
                            states_exhausted = revert_to_last_node_with_children();
                        } else {
                            states_exhausted = revert_to_last_node_with_children(current_node->cache_state);
                        }
                    } else {
                        current_node->child_moves = std::move(next_moves);
                    }
                  }
                    // If the state is not a new one, reverts to the last node with children
                  else {
                    res.unique_states_searched--;
                    states_exhausted = revert_to_last_node_with_children();
                  }
                }
            } catch (const std::runtime_error& e) {
                return result::type::MEM_LIMIT;
            }
        }
        } // end else of the depth-cut guard

        // Sets the current node to one of its children
        assert(states_exhausted == current_node->child_moves.empty());
        if (!states_exhausted) {
            set_to_child();
            state.make_move(current_node->mv);
            STRACE_MOVE(current_node->mv);
            res.depth++;
            STRACE_DEPTH(res.depth);
            res.max_depth = max(res.depth, res.max_depth);
            if (current_node->mv.dominance_move) res.dominance_moves++;
        }

        res.states_searched++;
        res.unique_states_searched++;
    }

    if (state.is_solved()) {
        STRACE_RESULT("SOLVED");
        return result::type::SOLVED;
    } else {
        assert(states_exhausted);
        // The search was exhausted within the bound without finding a solution.
        // SOUNDNESS: we may report UNSOLVABLE only if NO node was ever truncated
        // at the bound — then the bound did not restrict the proof and the
        // exhaustion is a genuine completeness certificate, exactly as in an
        // unbounded run. If any truncation occurred the verdict is not trustworthy
        // as UNSOLVABLE, so we return BOUNDED_EXHAUSTED instead (consumed by the
        // outer iterative-deepening loop in a later change).
        //
        // Invariant: when the bound is disabled (L = infinity) the cut can never
        // fire, so any_truncation is always false here and this path is identical
        // to the original "exhausted => UNSOLVABLE".
        if (any_truncation) {
            STRACE_RESULT("BOUNDED_EXHAUSTED");
            return result::type::BOUNDED_EXHAUSTED;
        } else {
            // §7.3(c): UNSOLVABLE is sound iff NO node was truncated at the bound —
            // then the search was complete (it explored everything an unbounded run
            // would; nothing was cut), so the exhaustion is a genuine proof regardless
            // of cycles. NOTE: the root's DFSTT3 budget is NOT necessarily +inf here:
            // a back-edge to an on-path ancestor contributes that ancestor's finite
            // provisional estimate (the admissible DFSTT3 cycle rule, proposal §3.5),
            // so a fully-exhausted node in a CYCLIC region backs up to a finite OPEN
            // esti even with any_truncation == false. That finite esti is sound for
            // cross-pass reuse (it can only cause re-search, never a false prune); the
            // verdict rests on any_truncation, not on the root being finalised DEAD.
            assert(!any_truncation);
            STRACE_RESULT("UNSOLV");
            return result::type::UNSOLVABLE;
        }
    }
}

// Called when the current node's children have been exhausted. Travels back up
// the search tree until it finds a node which still has children. Returns true
// unless all children have been exhausted.

// If an iterator to the current state is supplied to the function, will also
// make sure to turn the 'live' bit off upon backtracking
template <typename Policy>
bool solver_impl<Policy>::revert_to_last_node_with_children(optional<lru_cache::item_list::iterator> cur_state) {
    if (current_node == begin(frontier)) {
        // Reached the root with nothing left ⇒ the pass is exhausted. (bounded LRU)
        // Finalise the root too — write its DEAD/OPEN status and clear its live bit —
        // so the reused cache carries NO stale ON_PATH marker into the next pass
        // (assert 4.4d) and the root's verdict is available for cross-pass reuse.
        if constexpr (!Policy::computes_hash) {
            if (depth_bound) finalise_node(*current_node);
        }
        return true;
    }

    // (bounded LRU) The node we are backtracking out of is fully resolved; remember
    // its budget so we can fold plus_one(b) into its parent's running min below.
    const uint64_t abandoned_b = current_node->verified;

    if constexpr (!Policy::computes_hash) {
        if (depth_bound) {
            // Stage 2b: write this node's final DEAD/OPEN(b) status and clear its live
            // bit — a no-op for hit-pruned/cycle/dominance/truncated nodes, so a cycle
            // target (a still-live ancestor) is never written or un-lived.
            finalise_node(*current_node);
        } else if (cur_state) {
            // Legacy: just turn the 'live' bit off on the state we back out of.
            cache.set_non_live(*cur_state);
        }
    }

    state.undo_move(current_node->mv);
    STRACE_UNDO(current_node->mv);
    res.depth--;
    STRACE_DEPTH(res.depth);
    res.backtracks++;

#ifndef NDEBUG
    // Checks that the state after the undo is in the cache
    // (as long as the move wasn't a dominance move)

    if (! current_node->mv.dominance_move) {
        if (cache.get_states_removed_from_cache() == 0) {
            if constexpr (Policy::computes_hash) {
                assert(cache.contains_t(state));
            } else {
                assert(cache.contains_t(state));
            }
        }
        LOG_DEBUG("(undo move)");
    } else {
        LOG_DEBUG("(undo dominance move)");
    }
    LOG_DEBUG(state);
#endif

    // Gets a reference to the parent state which can be supplied if this function is
    // called recursively. This ensures that the cached state's 'live' bit is set as appropriate
    optional<lru_cache::item_list::iterator> p_state = prev(current_node)->cache_state;

    // Reverts the current node to its parent and removes it
    frontier.pop_back();
    current_node = prev(end(frontier));

    // (bounded LRU) DFSTT3 backup: fold the abandoned child's resolved budget into
    // its parent's running min — verified = min over children of (1 + child_b). A
    // forced UNCACHED edge (dominance/K+) folds through here transparently, charging
    // +1 ply into the nearest cached ancestor without ever keying on a cache
    // iterator (B1 = A; the trap the docs warn about).
    if constexpr (!Policy::computes_hash) {
        if (depth_bound)
            current_node->verified = std::min(current_node->verified, plus_one(abandoned_b));
    }

    // If the current node now has no children, repeat
    if (current_node->child_moves.empty()) {
        return revert_to_last_node_with_children(p_state);
    } else {
        return false;
    }
}

template <typename Policy>
void solver_impl<Policy>::set_to_child() {
    assert(!current_node->child_moves.empty());

    move b = current_node->child_moves.back();
    current_node->child_moves.pop_back();
    frontier.emplace_back(b);

    current_node = prev(end(frontier));
}

// ─── Stage 2b: finalise an EXPANDED node's cache entry (bounded LRU only) ─────
// Called from revert when a node's subtree is fully explored. Writes the node's
// terminal status — DEAD (verified == +inf: no truncation and no goal below it) or
// OPEN(verified) (a truncation remains below) — and clears its live bit. It acts
// ONLY on nodes WE expanded this visit (current_node->expanded): hit-pruned, cycle,
// dominance and truncated nodes are left completely untouched, which is essential —
// a cycle target is a still-live ancestor whose entry must not be written or
// un-lived. set_dead is monotone, so re-finalising a node that became DEAD with more
// budget only ever strengthens the verdict (the depth collapse).
template <typename Policy>
void solver_impl<Policy>::finalise_node(node& n) {
    if constexpr (!Policy::computes_hash) {
        if (n.expanded && n.cache_state) {
            if (n.verified == solver_node::INF_B) {
                cache.set_dead(*n.cache_state);
            } else {
                cache.finalise_open(*n.cache_state,
                    static_cast<uint32_t>(std::min<uint64_t>(n.verified, UINT32_MAX)));
            }
            cache.set_non_live(*n.cache_state);
        }
    } else {
        (void)n;
    }
}

template <typename Policy>
void solver_impl<Policy>::print_solution() const {
    std::flush(clog);
    std::flush(cout);

    auto i = begin(frontier);
    game_state_impl<Policy> state_copy = init_state;

    cout << "Solution:\n";
    cout << state_copy << "\n";

    if (res.states_searched > 1) {
        while (++i != end(frontier)) {
            state_copy.make_move(i->mv);
            cout << state_copy << "\n";
        }
    }
    cout << "\n";
}

// ─── Free functions (not templated — use solver typedef) ─────────────────────

std::ostream& operator<< (std::ostream& out, const solver_result::type& rt) {
    switch(rt) {
        case solver_result::type::TIMEOUT:
            out << "timed-out";
            break;
        case solver_result::type::SOLVED:
            out << "solved";
            break;
        case solver_result::type::UNSOLVABLE:
            out << "unsolvable";
            break;
        case solver_result::type::MEM_LIMIT:
            out << "memory-limit-reached";
            break;
        case solver_result::type::TERMINATED:
            out << "terminated";
            break;
        case solver_result::type::BOUNDED_EXHAUSTED:
            out << "bounded-exhausted";
            break;
    }
    return out;
}

std::ostream& operator<< (std::ostream& out, const solver_result& r) {
    return out
            << "Solution Type: "             << r.sol_type                   << "\n"
            << "States Searched: "           << r.states_searched            << "\n"
            << "Unique States Searched: "    << r.unique_states_searched     << "\n"
            << "Backtracks: "                << r.backtracks                 << "\n"
            << "Dominance Moves: "           << r.dominance_moves            << "\n"
            << "States Removed From Cache: " << r.states_removed_from_cache  << "\n"
            << "Final States In Cache: "     << r.cache_size                 << "\n"
            << "Final Buckets In Cache: "    << r.cache_bucket_count         << "\n"
            << "Maximum Search Depth: "      << r.max_depth                  << "\n"
            << "Final Search Depth: "        << r.depth                      << "\n"
            << "Time Taken (milliseconds): " << r.time.count()               << "\n";
}

template <typename Policy>
void solver_impl<Policy>::print_header(long t, command_line_helper::streamliner_opt stream_opt) {
    cout << "Calculating solvability percentage...\n\n";
    if (stream_opt == command_line_helper::streamliner_opt::SMART) {
        cout << ", (Streamliner Results:) "
                "Attempted Seed"
                ", Outcome"
                ", Time Taken(ms)"
                ", States Searched"
                ", Unique States Searched"
                ", Backtracks"
                ", Dominance Moves"
                ", States Removed From Cache"
                ", Final States In Cache"
                ", Final Buckets In Cache"
                ", Maximum Search Depth"
                ", Final Search Depth"
                ", (Non-Streamliner Results:) ";
    }
    cout << "Attempted Seed"
            ", Outcome"
            ", Time Taken(ms)"
            ", States Searched"
            ", Unique States Searched"
            ", Backtracks"
            ", Dominance Moves"
            ", States Removed From Cache"
            ", Final States In Cache"
            ", Final Buckets In Cache"
            ", Maximum Search Depth"
            ", Final Search Depth"
            ", Overall Result"
            "\n--- Timeout = " << t << " milliseconds ---\n";
    cout << fixed << setprecision(3);
}

template <typename Policy>
void solver_impl<Policy>::print_result_csv(solver_result res) {
    cout << ", " << res.sol_type
         << ", " << res.time.count()
         << ", " << res.states_searched
         << ", " << res.unique_states_searched
         << ", " << res.backtracks
         << ", " << res.dominance_moves
         << ", " << res.states_removed_from_cache
         << ", " << res.cache_size
         << ", " << res.cache_bucket_count
         << ", " << res.max_depth
         << ", " << res.depth;
}

template <typename Policy>
void solver_impl<Policy>::print_null_seed_info() {
    cout << ", , , , , , , , , , , ";
}

template <typename Policy>
const vector<typename solver_impl<Policy>::node>& solver_impl<Policy>::get_frontier() const {
    return frontier;
}


///////////////////////////
// EXPLICIT INSTANTIATIONS
///////////////////////////

#if defined(SOLVITAIRE_LRU_ONLY)
template class solver_impl<LRUPolicy>;
#elif defined(SOLVITAIRE_FLAT_ONLY)
template class solver_impl<FlatPolicy>;
template class solver_impl<PredecessorPolicy>;
#elif defined(SOLVITAIRE_HASH_ONLY)
template class solver_impl<HashOnlyPolicy>;
#else
template class solver_impl<FlatPolicy>;
template class solver_impl<HashOnlyPolicy>;
template class solver_impl<PredecessorPolicy>;
template class solver_impl<LRUPolicy>;
template class solver_impl<MultiplicityPolicy>;
#endif
