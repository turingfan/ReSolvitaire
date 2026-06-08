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

#ifndef SOLVITAIRE_SOLVER_H
#define SOLVITAIRE_SOLVER_H

#include <string>
#include <vector>
#include <atomic>
#include <chrono>

#include "../game/global_cache.h"
#include "../game/generic_flat_cache.h"
#include "../game/sol_rules.h"
#include "../game/cache_policy.h"
#include "../input-output/input/command_line_helper.h"

// ─── Policy-independent types ────────────────────────────────────────────────
// Extracted from solver_impl so they are the same type across all Policy
// instantiations and can be used freely in non-templated code.

struct solver_node {
    solver_node(move) noexcept;
    const move mv;
    std::vector<move> child_moves;
    boost::optional<lru_cache::item_list::iterator> cache_state; // Optional, as dominance moves aren't cached

    // ─── Stage 2b (DFSTT3) per-node backup state. Used ONLY on the bounded LRU
    // path; untouched (and meaningless) on the unbounded/flat paths. ─────────────
    // `verified` is this node's resolved remaining-budget b (the satisficing
    // `esti`): while a node is expanding it is the running min over its children of
    // (1 + child_b); on a pruned/cycle/truncated leaf it is set directly to the
    // contributed value. UINT64_MAX is the +inf sentinel ⇔ DEAD. The parent folds
    // plus_one(child.verified) into its own `verified` when the child is popped, so
    // a forced UNCACHED edge (dominance/K+) passes its child's budget +1 up to the
    // nearest cached ancestor without ever keying on a cache iterator (B1 = A).
    // `expanded` marks the nodes WE inserted/re-opened this visit (so we own their
    // finalisation and their live bit) vs hit-pruned/cycle/dominance/truncated
    // nodes (which must NOT be written or have their live bit cleared — a cycle hit
    // points at a still-live ancestor).
    static constexpr uint64_t INF_B = UINT64_MAX;
    uint64_t verified = INF_B;
    bool expanded = false;
};

struct solver_result {
    // BOUNDED_EXHAUSTED: a depth-bounded pass exhausted the search within the
    // bound L without finding a solution, but at least one node was truncated at
    // the bound (any_truncation == true). It is therefore NOT a proof of
    // unsolvability. It is an internal per-pass outcome consumed by the outer
    // iterative-deepening loop (added in a later change); a normal unbounded run
    // can never produce it.
    enum class type { TIMEOUT, SOLVED, UNSOLVABLE, MEM_LIMIT, TERMINATED, BOUNDED_EXHAUSTED };

    type sol_type;
    uint64_t states_searched;
    uint64_t unique_states_searched;
    uint64_t backtracks;
    uint64_t dominance_moves;
    uint64_t states_removed_from_cache;
    uint64_t cache_size;
    uint64_t cache_bucket_count;
    uint64_t max_depth;
    uint64_t depth;
    std::chrono::milliseconds time;
};

template <typename Policy>
class solver_impl {
public:
    typename Policy::cache_type& cache;

    using node = solver_node;
    using result = solver_result;

    explicit solver_impl(const game_state_impl<Policy>&, typename Policy::cache_type&);

    // depth_bound is the per-pass bound L. boost::none means unbounded (L = inf),
    // in which case the depth cut can never fire and any_truncation stays false —
    // the search is byte-identical to an unbounded run.
    result run(boost::optional<std::chrono::milliseconds> = boost::none,
               boost::optional<uint64_t> depth_bound = boost::none);

    void print_solution() const;
    static void print_header(long, command_line_helper::streamliner_opt);
    static void print_result_csv(result);
    static void print_null_seed_info();
    const std::vector<node>& get_frontier() const;

    const game_state_impl<Policy> init_state;

private:
    typedef std::chrono::high_resolution_clock clock;
    typedef std::chrono::milliseconds millisec;

    typename result::type dfs(boost::optional<clock::time_point> = boost::none);

    bool revert_to_last_node_with_children(boost::optional<lru_cache::item_list::iterator> = boost::none);
    void set_to_child();

    // ─── Stage 2b helpers (bounded LRU path only) ────────────────────────────────
    // saturating 1 + child budget (plus_one(+inf) == +inf).
    static uint64_t plus_one(uint64_t b) {
        return b == solver_node::INF_B ? solver_node::INF_B : b + 1;
    }
    // Finalise an EXPANDED node's cache entry when its subtree is fully explored:
    // write DEAD (verified == +inf) or OPEN(verified), then clear its live bit. A
    // no-op for nodes we did not expand (hit-pruned/cycle/dominance/truncated) —
    // critically, it must NOT clear the live bit of a cycle target (a live ancestor
    // still on the frontier). Only ever called when depth_bound is set, on the LRU
    // (non-hash) policy.
    void finalise_node(node& n);

    game_state_impl<Policy> state;
    std::vector<node> frontier;

    // Depth-bounded search state (Stage 1). When depth_bound is boost::none the
    // bound is disabled (L = infinity): the cut can never fire. any_truncation is
    // a monotone, set-only flag — once a node is truncated at the bound it stays
    // set for the whole pass and is never cleared.
    boost::optional<uint64_t> depth_bound;
    bool any_truncation = false;

    result res;

    node root;
    typename std::vector<node>::iterator current_node;
};

// ─── solver typedef ──────────────────────────────────────────────────────────
// Preserves the solver name for callers that don't need a specific policy.
// solver::result, solver::print_header, etc. all work through this typedef.
#if defined(SOLVITAIRE_LRU_ONLY)
    using solver = solver_impl<LRUPolicy>;
#elif defined(SOLVITAIRE_FLAT_ONLY)
    using solver = solver_impl<FlatPolicy>;
#elif defined(SOLVITAIRE_HASH_ONLY)
    using solver = solver_impl<HashOnlyPolicy>;
#else
    using solver = solver_impl<FlatPolicy>;
#endif

std::ostream& operator<< (std::ostream&, const solver_result::type&);
std::ostream& operator<< (std::ostream&, const solver_result&);
void sigint_handler(int i);

#endif //SOLVITAIRE_SOLVER_H
