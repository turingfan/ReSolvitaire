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

    using node = solver_node;
    using result = solver_result;

    explicit solver_impl(const game_state_impl<Policy>&, typename Policy::cache_type&);

    result run(boost::optional<std::chrono::milliseconds> = boost::none);

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
