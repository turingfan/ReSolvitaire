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
#include <boost/program_options.hpp>
#include <boost/optional.hpp>
#include <sys/resource.h>
#include <functional>
#include <sstream>
#include <chrono>
#include <cassert>

#include "version.h"
#include "../../lib/rapidjson/document.h"
#include "../../lib/rapidjson/writer.h"
#include "../../lib/rapidjson/stringbuffer.h"
#include "input-output/input/command_line_helper.h"
#include "input-output/input/sol_preset_types.h"
#include "input-output/input/json-parsing/json_helper.h"
#include "input-output/input/json-parsing/rules_parser.h"
#include "input-output/output/log_helper.h"
#include "game/cache_interface.h"
#include "game/cache_policy.h"
#include "game/zobrist.h"
#include "solver/solver.h"
#include "solver/search_trace.h"
#include "evaluation/solvability_calc.h"
#include "evaluation/benchmark.h"
#include <memory>

using namespace rapidjson;

using namespace std;
using namespace boost;

namespace po = boost::program_options;

typedef std::chrono::milliseconds millisec;

// ─── solve_output ────────────────────────────────────────────────────────────
// Non-templated return type from the dispatch branch. Lazy callbacks capture
// the game_state_impl<Policy> inside type-erased std::function closures.
// No work is done until the caller invokes them.

struct solve_output {
    solver::result result;
    std::function<void()> print_solution;              // prints solution to cout; empty if not solved
    std::function<void(std::ostream&)> print_init_state;  // streams init_state to given ostream
};

// ─── id_options ──────────────────────────────────────────────────────────────
// Configuration for the outer iterative-deepening loop (Stage 1 item 1e). When a
// caller passes boost::none for this, the solver runs a single UNBOUNDED pass and
// the loop is never entered (preserving the L = infinity trace identity exactly).
//
// initial_bound : L0, the depth bound of the first pass.
// grow          : factor by which L grows between passes (--depth-grow; default 2).
//                 A non-progress guard below forces L = L + 1 if grow would not
//                 strictly increase L, so the loop can never spin forever.
// max_bound     : optional L_max. If set, deepening stops once L would exceed it.
//                 If unset, deepening continues until the (shared) timeout.

struct id_options {
    uint64_t initial_bound;
    uint64_t grow;
    boost::optional<uint64_t> max_bound;
};

// ─── solve_game_impl<Policy> ─────────────────────────────────────────────────
// Constructs game_state, cache, and solver for the given policy. Runs the
// solver and returns solve_output with lazy callbacks.
//
// When id_opts is boost::none the search is a single unbounded pass — byte
// identical to the pre-feature behaviour (the L = infinity identity gate enforces
// this). When id_opts is set, an outer iterative-deepening loop runs: each pass
// uses a FRESH cache and a FRESH initial game state (no cross-pass reuse — that is
// Stage 2), sharing the total timeout, growing L until SOLVED / UNSOLVABLE /
// L_max / timeout.

template <typename Policy>
solve_output solve_game_impl(const sol_rules& rules, uint64_t timeout, uint64_t cache_capacity,
                              game_state::streamliner_options str_opts,
                              boost::optional<int> seed,
                              boost::optional<const Document&> in_doc,
                              boost::optional<id_options> id_opts) {
    // Builds a solve_output (result + lazy solution/init-state closures) from a
    // finished pass's solver. Shared by the unbounded and iterative-deepening
    // paths so the printing behaviour is identical regardless of which produced
    // the final verdict.
    auto build_output = [](solver_impl<Policy>& sol, solver::result res) -> solve_output {
        solve_output out;
        out.result = res;

        // Capture init_state for lazy printing (one copy; type-erased inside std::function)
        auto init_copy = sol.init_state;
        out.print_init_state = [init_copy](std::ostream& os) { os << init_copy; };

        if (res.sol_type == solver_impl<Policy>::result::type::SOLVED) {
            // Extract just the move sequence (vector<::move> is non-templated) — cheap
            std::vector<::move> solution_moves;
            auto it = sol.get_frontier().begin();
            ++it;  // skip root node (null move)
            for (; it != sol.get_frontier().end(); ++it)
                solution_moves.push_back(it->mv);
            uint64_t n = res.states_searched;

            out.print_solution = [init_copy, solution_moves, n]() {
                game_state_impl<Policy> state_copy = init_copy;
                std::cout << "Solution:\n" << state_copy << "\n";
                if (n > 1) {
                    for (const auto& m : solution_moves) {
                        state_copy.make_move(m);
                        std::cout << state_copy << "\n";
                    }
                }
                std::cout << "\n";
            };
        }
        return out;
    };

    // Builds a fresh initial game state for a pass.
    auto make_gs = [&]() {
        return seed
            ? game_state_impl<Policy>(rules, *seed, static_cast<typename game_state_impl<Policy>::streamliner_options>(str_opts))
            : game_state_impl<Policy>(rules, *in_doc, static_cast<typename game_state_impl<Policy>::streamliner_options>(str_opts));
    };

    // Builds a fresh cache for a pass (Stage 1: never reused across passes).
    auto make_cache = [&](const game_state_impl<Policy>& gs) {
        if constexpr (std::is_same_v<typename Policy::cache_type, lru_cache>)
            return lru_cache(gs, cache_capacity);
        else
            return typename Policy::cache_type(cache_capacity);
    };

    // ─── Unbounded path (flag absent) ────────────────────────────────────────
    // Verbatim single unbounded pass. MUST stay byte-identical to the pre-feature
    // behaviour: the loop below is never entered, so the L = infinity identity
    // gate holds.
    if (!id_opts) {
        game_state_impl<Policy> gs = make_gs();
        typename Policy::cache_type cache = make_cache(gs);
        solver_impl<Policy> sol(gs, cache);
        auto res = sol.run(std::chrono::milliseconds(timeout), boost::none);
        return build_output(sol, res);
    }

    // ─── Iterative-deepening path (flag present) ─────────────────────────────
    using clock = std::chrono::high_resolution_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeout);

    uint64_t L = id_opts->initial_bound;
    const uint64_t grow = id_opts->grow;

    // The result of the most recent pass — surfaced if the loop stops without a
    // definitive SOLVED/UNSOLVABLE verdict (so we never lose the pass's stats).
    boost::optional<solve_output> last_out;

    while (true) {
        // Share the total timeout across passes: each pass gets the remaining time.
        const auto now = clock::now();
        if (now >= deadline) {
            // Out of time before this pass could start. SOUNDNESS: the last pass
            // (if any) was BOUNDED_EXHAUSTED — unresolved, NOT a proof — so we
            // surface TIMEOUT, never UNSOLVABLE. last_out always exists here
            // because the first pass runs unconditionally (deadline is in the
            // future at entry for any sane timeout).
            if (last_out) {
                last_out->result.sol_type = solver::result::type::TIMEOUT;
                return *last_out;
            }
            // Defensive: no pass ran at all (timeout already elapsed). Build an
            // empty TIMEOUT result from a fresh solver so callers have stats.
            game_state_impl<Policy> gs = make_gs();
            typename Policy::cache_type cache = make_cache(gs);
            solver_impl<Policy> sol(gs, cache);
            solver::result empty{};
            empty.sol_type = solver::result::type::TIMEOUT;
            return build_output(sol, empty);
        }
        const auto remaining = std::chrono::duration_cast<millisec>(deadline - now);

        // Fresh cache + fresh initial state every pass (no cross-pass reuse — Stage 2).
        game_state_impl<Policy> gs = make_gs();
        typename Policy::cache_type cache = make_cache(gs);
        solver_impl<Policy> sol(gs, cache);
        auto res = sol.run(remaining, boost::optional<uint64_t>(L));

        using rtype = solver::result::type;
        switch (res.sol_type) {
            case rtype::SOLVED:
                // Winnable: a shallow solution within L. Monotone in budget — valid.
                return build_output(sol, res);
            case rtype::UNSOLVABLE:
                // Sound proof: this pass exhausted with NO truncation (any_truncation
                // == false), so the bound did not restrict the proof. Return it.
                return build_output(sol, res);
            case rtype::TIMEOUT:
            case rtype::MEM_LIMIT:
            case rtype::TERMINATED:
                // Stop conditions independent of the bound. Surface as-is (the JSON
                // mapping turns these into timeout/failed — never unsolvable).
                return build_output(sol, res);
            case rtype::BOUNDED_EXHAUSTED:
            default:
                // No win within L and a truncation occurred ⇒ deepen and retry.
                // Keep this pass's output in case the NEXT growth/limit check stops
                // the loop (so we can surface its stats under a TIMEOUT verdict).
                last_out = build_output(sol, res);
                break;
        }

        // ─── Growth + non-progress guard ─────────────────────────────────────
        // Next bound = L * grow. GUARD: if grow <= 1 or the product would not
        // strictly exceed L (e.g. overflow), force L = L + 1 so the loop always
        // makes progress and can never spin forever. (--depth-grow < 2 is also
        // rejected up front in solve_game(), so this is belt-and-braces.)
        uint64_t next_L;
        if (grow <= 1) {
            next_L = L + 1;
        } else {
            next_L = L * grow;
            if (next_L <= L) next_L = L + 1;  // overflow or no progress
        }

        // ─── L_max handling ──────────────────────────────────────────────────
        // If --max-depth-bound M is set, stop deepening once the next bound would
        // exceed M. SOUNDNESS RED LINE: the loop is stopping while the last pass
        // was BOUNDED_EXHAUSTED (unresolved) — the verdict is TIMEOUT / unknown,
        // NEVER unsolvable. (last_out is guaranteed set: we only reach here after
        // the BOUNDED_EXHAUSTED arm above stored it.)
        if (id_opts->max_bound && next_L > *id_opts->max_bound) {
            assert(last_out);
            last_out->result.sol_type = solver::result::type::TIMEOUT;
            return *last_out;
        }

        L = next_L;
    }
}

// ─── dispatch_solve ──────────────────────────────────────────────────────────
// Selects the correct policy at compile time based on game rules and options.

static solve_output dispatch_solve(const sol_rules& rules, uint64_t timeout, uint64_t cache_capacity,
                             game_state::streamliner_options str_opts,
                             boost::optional<int> seed,
                             boost::optional<const Document&> in_doc,
                             bool force_lru,
                             const std::string& cache_type,
                             boost::optional<id_options> id_opts) {
    bool suit_sym = str_opts == game_state::streamliner_options::SUIT_SYMMETRY
                 || str_opts == game_state::streamliner_options::BOTH
                 || rules.inherent_suit_symmetry();

#if defined(SOLVITAIRE_LRU_ONLY)
    (void)cache_type; (void)force_lru; (void)suit_sym;
    return solve_game_impl<LRUPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc, id_opts);
#elif defined(SOLVITAIRE_FLAT_ONLY)
    (void)force_lru; (void)cache_type;
    if (use_predecessor_cache(rules))
        return solve_game_impl<PredecessorPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc, id_opts);
    if (!use_new_cache(rules, suit_sym))
        throw std::runtime_error("flat-only binary: game requires LRU cache");
    return solve_game_impl<FlatPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc, id_opts);
#elif defined(SOLVITAIRE_HASH_ONLY)
    (void)force_lru; (void)cache_type;
    if (!use_new_cache(rules, suit_sym))
        throw std::runtime_error("hash-only binary: game requires LRU cache");
    return solve_game_impl<HashOnlyPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc, id_opts);
#else
    if (force_lru) {
        return solve_game_impl<LRUPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc, id_opts);
    } else if (cache_type == "multiplicity" && use_multiplicity_cache(rules, suit_sym)) {
        return solve_game_impl<MultiplicityPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc, id_opts);
    } else if (cache_type == "hash-only" && use_new_cache(rules, suit_sym)) {
        return solve_game_impl<HashOnlyPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc, id_opts);
    } else if (use_predecessor_cache(rules)) {
        return solve_game_impl<PredecessorPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc, id_opts);
    } else if (use_new_cache(rules, suit_sym)) {
        return solve_game_impl<FlatPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc, id_opts);
    } else if (use_multiplicity_cache(rules, suit_sym)) {
        return solve_game_impl<MultiplicityPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc, id_opts);
    } else {
        return solve_game_impl<LRUPolicy>(rules, timeout, cache_capacity, str_opts, seed, in_doc, id_opts);
    }
#endif
}

// ─── Forward declarations ────────────────────────────────────────────────────

const boost::optional<sol_rules> gen_rules(command_line_helper&);
void solve_random_game(int, const sol_rules&, command_line_helper&);
bool solve_input_files(vector<string>, const sol_rules&, command_line_helper&);
void solve_game(const sol_rules& rules, command_line_helper& clh, boost::optional<int> seed, boost::optional<const Document&> in_doc, string instance_name);
void print_version();

// Decides what to do given supplied command-line options
int main(int argc, const char* argv[]) {

    // Initialize Zobrist hash tables
    zobrist_hash::init();

    // Parses the command-line options
    command_line_helper clh;
    if (!clh.parse(argc, argv)) {
        return EXIT_FAILURE;
    }

#ifdef SOLVITAIRE_SEARCH_TRACE
    if (!clh.get_trace_path().empty()) {
        trace_writer::instance().open(clh.get_trace_path(), argc, argv);
        const string str_name = [&]() -> string {
            switch (clh.get_streamliners()) {
                case command_line_helper::streamliner_opt::NONE:             return "none";
                case command_line_helper::streamliner_opt::AUTO_FOUNDATIONS: return "auto-foundations";
                case command_line_helper::streamliner_opt::SUIT_SYMMETRY:   return "suit-symmetry";
                case command_line_helper::streamliner_opt::BOTH:            return "both";
                case command_line_helper::streamliner_opt::SMART:           return "smart";
                default:                                                     return "unknown";
            }
        }();
        STRACE_INIT(clh.get_solitaire_type(), clh.get_random_deal(),
                    str_name, clh.get_cache_type());
    }
    if (clh.has_break_at()) {
        trace_writer::instance().set_break_at(clh.get_break_at_n());
    }
    if (clh.has_find_hash()) {
        trace_writer::instance().set_find_hash(clh.get_find_hash());
    }
#else
    if (!clh.get_trace_path().empty() || clh.has_break_at()) {
        std::cerr << "Warning: --trace/--trace-break-at ignored "
                     "(not built with SOLVITAIRE_TRACE)\n";
    }
#endif

    // If the user has asked for the list of preset game types, prints it
    if (clh.get_available_game_types()) {
        sol_preset_types::print_available_games();
        return EXIT_SUCCESS;
    }

    // If the user has asked for the version, prints it
    if (clh.get_version()) {
        print_version();
        return EXIT_SUCCESS;
    }

    string game_rule_str = clh.get_describe_game_rules();
    if (!game_rule_str.empty()) {
        sol_preset_types::describe_game_rules(game_rule_str);
        return EXIT_SUCCESS;
    }

    // Generates the rules of the solitaire from the game type
    // Skip if we are doing a benchmark-json which handles rules per instance
    boost::optional<sol_rules> rules;
    if (clh.get_benchmark_json().empty()) {
        rules = gen_rules(clh);
        if (!rules) return EXIT_FAILURE;
    }

    if (clh.get_deal_only()) {
        game_state gs(*rules, clh.get_random_deal(), game_state::streamliner_options::NONE);
        json_helper::print_game_state_as_json(gs, clh.get_reveal_hidden());
        return EXIT_SUCCESS;
    }

    try {
        // If the user has asked for a solvability percentage, calculates it
        if (clh.get_solvability() > 0) {
            solvability_calc solv_c(*rules, clh.get_cache_capacity(), clh.get_cache_type());
            solv_c.calculate_solvability_percentage(clh.get_timeout(), clh.get_solvability(), clh.get_cores(),
                                                    clh.get_streamliners(), clh.get_resume());
        }
        // If the benchmark option has been supplied, generates it
        if (!clh.get_benchmark_json().empty()) {
            benchmark::run_json(clh.get_benchmark_json(), clh.get_cache_capacity(), clh.get_benchmark_iterations(), clh.get_benchmark_warmup(), clh.get_timeout(), clh.get_cache_type());
            return EXIT_SUCCESS;
        }

        if (clh.get_benchmark() || clh.get_is_benchmark()) {
            benchmark::run(*rules, clh.get_cache_capacity(), clh.get_streamliners_game_state(), clh.get_benchmark_seeds(), clh.get_benchmark_iterations(), clh.get_benchmark_warmup(), clh.get_timeout(), clh.get_force_lru_cache(), clh.get_cache_type());
            return EXIT_SUCCESS;
        }
        
        // If a random deal seed has been supplied, solves it
        if (clh.get_random_deal() != -1) {
            solve_random_game(clh.get_random_deal(), *rules, clh);
        }
        // Otherwise there are supplied input files which should be solved
        else {
            const vector<string> input_files = clh.get_input_files();

            // If there are no input files, solve a random deal based on the
            // supplied seed
            assert(!input_files.empty());
            if (solve_input_files(input_files, *rules, clh))
                return EXIT_FAILURE;
        }
    } catch (const std::runtime_error& error) {
        LOG_ERROR(error.what());
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        LOG_ERROR("Unexpected error: " << error.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void print_version() {
    LOG_INFO("(Solvitaire version: " << SOLVITAIRE_VERSION_H << ")");
}

// Generates the game rules given the command line options
const boost::optional<sol_rules> gen_rules(command_line_helper& clh) {
    try {
        if (!clh.get_solitaire_type().empty()) {
            return rules_parser::from_preset(clh.get_solitaire_type());
        } else {
            return rules_parser::from_file(clh.get_rules_file());
        }
    } catch (const runtime_error& error) {
        string errmsg = "Error in rules generation: ";
        errmsg += error.what();
        LOG_ERROR(errmsg);
        return none;
    }
}

void solve_random_game(int seed, const sol_rules& rules, command_line_helper& clh) {
    if (!clh.get_classify() && !clh.get_json_output())
        LOG_INFO ("Attempting to solve with seed: " << seed << "...");
    solve_game(rules, clh, seed, none, "seed_" + to_string(seed));
}

bool solve_input_files(const vector<string> input_files, const sol_rules& rules, command_line_helper& clh) {
    bool had_error = false;
    for (const string& input_file : input_files) {
        try {
            // Reads in the input file to a json doc
            const Document in_doc = json_helper::get_file_json(input_file);

            if (!clh.get_json_output())
                LOG_INFO ("Attempting to solve " << input_file << "...");
            solve_game(rules, clh, none, in_doc, input_file);

        } catch (const std::runtime_error& error) {
            LOG_ERROR(error.what());
            had_error = true;
        }
    }
    return had_error;
}

void solve_game(const sol_rules& rules, command_line_helper& clh, boost::optional<int> seed, boost::optional<const Document&> in_doc, string instance_name) {
    bool smart = clh.get_streamliners() == command_line_helper::streamliner_opt::SMART;

    uint64_t timeout;
    game_state::streamliner_options str_opt;
    if (smart) {
        timeout = clh.get_timeout() / 10;
        str_opt = game_state::streamliner_options::BOTH;
    } else {
        timeout = clh.get_timeout();
        str_opt = clh.get_streamliners_game_state();
    }
    // Build the iterative-deepening configuration from the CLI (Stage 1 item 1e).
    // boost::none means the flag --initial-depth-bound is ABSENT ⇒ a single
    // unbounded pass, byte-identical to the pre-feature behaviour (the loop is
    // never entered). When present, the outer ID loop runs in solve_game_impl.
    boost::optional<id_options> id_opts;
    if (clh.has_initial_depth_bound()) {
        id_options o;
        o.initial_bound = clh.get_initial_depth_bound();
        // Reject --depth-grow < 2 up front: a factor < 2 cannot grow a positive
        // bound geometrically. (The loop also has a belt-and-braces L = L + 1
        // non-progress guard, so a value of 1 still terminates rather than hangs.)
        o.grow = clh.get_depth_grow();
        if (o.grow < 2) {
            LOG_ERROR("Error: --depth-grow must be >= 2 (got " << o.grow << ")");
            // Fall back to the minimal progressing factor so we never spin; the
            // loop's non-progress guard would do the same, but we make it explicit.
            o.grow = 2;
        }
        // --max-depth-bound absent ⇒ no separate depth cap; deepen until timeout
        // ("tied to --timeout" per the plan, decision D6 default).
        o.max_bound = clh.has_max_depth_bound()
                ? boost::optional<uint64_t>(clh.get_max_depth_bound())
                : boost::none;
        id_opts = o;
    }
    solve_output solution = dispatch_solve(rules, timeout, clh.get_cache_capacity(), str_opt, seed, in_doc, clh.get_force_lru_cache(), clh.get_cache_type(), id_opts);

    bool run_again = smart && solution.result.sol_type != solver::result::type::SOLVED;
    cout.flush();
    if (run_again)
        if (!clh.get_classify() && !clh.get_json_output()) cout << "Unsolvable using streamliner. Running again...\n";
    boost::optional<solve_output> streamliner_solution = run_again
            ? dispatch_solve(rules, clh.get_timeout(), clh.get_cache_capacity(), game_state::streamliner_options::NONE, seed, in_doc, clh.get_force_lru_cache(), clh.get_cache_type(), id_opts)
            : boost::optional<solve_output>();

    if (clh.get_json_output()) {
        solve_output& s = run_again ? *streamliner_solution : solution;
        StringBuffer sb;
        Writer<StringBuffer> writer(sb);
        writer.StartObject();
        writer.Key("instance_name");
        writer.String(instance_name.c_str());
        writer.Key("solution_type");
        // SOUNDNESS RED LINE: a depth-bounded/iterative-deepening run reports
        // "unsolvable" ONLY via a pass that returned UNSOLVABLE (exhausted with no
        // truncation — a genuine proof). When the ID loop (item 1e) stops without a
        // SOLVED/UNSOLVABLE pass (timeout or L_max reached while the last pass was
        // BOUNDED_EXHAUSTED), it remaps the verdict to TIMEOUT before we get here —
        // so the unresolved case becomes "timeout", NEVER "unsolvable". The
        // BOUNDED_EXHAUSTED → "bounded-exhausted" arm below is therefore a
        // defensive fallback (it should not occur on the normal ID solve path, but
        // if it ever did it must still NOT be conflated with "unsolvable").
        writer.String(s.result.sol_type == solver::result::type::SOLVED ? "winnable" :
                      s.result.sol_type == solver::result::type::UNSOLVABLE ? "unsolvable" :
                      s.result.sol_type == solver::result::type::TIMEOUT ? "timeout" :
                      s.result.sol_type == solver::result::type::BOUNDED_EXHAUSTED ? "bounded-exhausted" : "failed");
        writer.Key("states_searched");
        writer.Uint64(s.result.states_searched);
        writer.Key("unique_states");
        writer.Uint64(s.result.unique_states_searched);
        writer.Key("backtracks");
        writer.Uint64(s.result.backtracks);
        writer.Key("max_depth");
        writer.Uint64(s.result.max_depth);
        writer.Key("dominance_moves");
        writer.Uint64(s.result.dominance_moves);
        writer.Key("states_removed_from_cache");
        writer.Uint64(s.result.states_removed_from_cache);
        writer.Key("cache_size");
        writer.Uint64(s.result.cache_size);
        writer.Key("cache_buckets");
        writer.Uint64(s.result.cache_bucket_count);
        writer.Key("final_depth");
        writer.Uint64(s.result.depth);
        // Memory measurement (getrusage RUSAGE_SELF)
        {
            struct rusage usage;
            uint64_t rss_bytes = 0;
            if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef __APPLE__
                rss_bytes = (uint64_t)usage.ru_maxrss;  // bytes on macOS
#else
                rss_bytes = (uint64_t)usage.ru_maxrss * 1024ULL;  // KB on Linux
#endif
            }
            writer.Key("solver_resident_bytes");
            writer.Uint64(rss_bytes);
        }
        writer.EndObject();
        cout << sb.GetString() << endl;
    } else if (clh.get_classify()) {
        if (seed) cout << *seed;
        solver::print_result_csv(solution.result);
        if (smart) {
            if (run_again) {
                solver::print_result_csv(streamliner_solution->result);
                cout << ", " << streamliner_solution->result.sol_type;
            } else {
                solver::print_null_seed_info();
                cout << ", " << solution.result.sol_type;
            }
        } else {
            cout << ", " << solution.result.sol_type;
        }
        cout << "\n";
    } else {
        solve_output& s = run_again ? *streamliner_solution : solution;

        if (s.result.sol_type == solver::result::type::SOLVED) {
            s.print_solution();   // lazy: replay happens here and only here
        } else {
            cout << "Deal:\n";
            s.print_init_state(cout);  // lazy: init_state streamed here
            cout << "\n";
        }
        cout << s.result;
    }
    cout.flush();
}
