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
// solver_node carries the per-frame search state on the DFS frontier. The
// cache_state field (the LRU live-bit iterator) is ONLY used by the LRU cache
// path (Policy::computes_hash == false; see solver.cpp set_non_live). The
// flat-cache family never touches it, so we carry it via an empty-base
// specialisation that removes it entirely from flat/multiplicity frames —
// saving 16 B per frontier node, which matters on deep searches (KI-26).

// Holds the LRU live-bit iterator. The <false> specialisation is empty, so via
// empty-base optimisation it contributes 0 bytes to the node.
template <bool WithCacheState>
struct cache_state_holder {
    boost::optional<lru_cache::item_list::iterator> cache_state; // dominance moves aren't cached
};
template <>
struct cache_state_holder<false> {};

template <bool WithCacheState>
struct solver_node_t : cache_state_holder<WithCacheState> {
    explicit solver_node_t(move m) noexcept : mv(m), child_moves() {}
    const move mv;
    std::vector<move> child_moves;
};

struct solver_result {
    enum class type { TIMEOUT, SOLVED, UNSOLVABLE, MEM_LIMIT, TERMINATED };

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

    // Flat-cache policies (computes_hash) don't need the LRU live-bit iterator,
    // so they use the empty-base node variant (no cache_state field).
    using node = solver_node_t<!Policy::computes_hash>;
    using result = solver_result;

    explicit solver_impl(const game_state_impl<Policy>&, typename Policy::cache_type&);

    // cpu_timeout: CPU-time (user+system) search budget. The solver also keeps a
    // wall safety-cap of wall_cap_mult x cpu_timeout so it always self-terminates
    // even if badly descheduled. max_states (0 = off) is a deterministic hard cap on
    // states searched, for reproducible cutoffs. All three map to a TIMEOUT result.
    result run(boost::optional<std::chrono::milliseconds> cpu_timeout = boost::none,
               uint64_t wall_cap_mult = 10,
               uint64_t max_states = 0);

    void print_solution() const;
    static void print_header(long, command_line_helper::streamliner_opt);
    static void print_result_csv(result);
    static void print_null_seed_info();
    const std::vector<node>& get_frontier() const;

    const game_state_impl<Policy> init_state;

private:
    typedef std::chrono::high_resolution_clock clock;
    typedef std::chrono::milliseconds millisec;

    typename result::type dfs(uint64_t start_cpu_ns,
                              boost::optional<uint64_t> cpu_budget_ns,
                              boost::optional<clock::time_point> wall_deadline,
                              uint64_t max_states);

    bool revert_to_last_node_with_children(boost::optional<lru_cache::item_list::iterator> = boost::none);
    void set_to_child();

    game_state_impl<Policy> state;
    std::vector<node> frontier;

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
