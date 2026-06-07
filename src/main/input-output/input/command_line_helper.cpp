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
// Created by thecharlesblake on 10/16/17.
//


#include "command_line_helper.h"
#include <iostream>
#include <string>
#include <boost/program_options.hpp>

#include "benchmark.h"
#include "../output/log_helper.h"
#include "sol_preset_types.h"

using namespace std;

namespace po = boost::program_options;

command_line_helper::command_line_helper()
        : main_options("options") {

    main_options.add_options()
            ("help", "produce help message")
            ("version", "the version of Solvitaire being used")
            ("type", po::value<string>(),
             "specify the type of the solitaire game to be solved from the list "
                     "of preset games. Must supply either this 'type' option, "
                     "or the 'custom-rules' option")
            ("available-game-types", "outputs a list of the different preset "
                    "game types that can be solved")
            ("describe-game-rules", po::value<string>(),
                    "outputs the JSON that describes the rules of the supplied "
                    "preset game type")
            ("custom-rules", po::value<string>(),
             "the path to a JSON file describing the rules of the solitaire "
                     "to be solved. Must supply either 'type' or 'custom-rules' option")
            ("random", po::value<int>(), "create and solve a random solitaire "
                    "deal based on a seed. Must supply either 'random',"
                    "'solvability', 'benchmark' or list of deals to be solved.")
            ("classify", "outputs a simple 'solvable/not solvable' "
                         "classification")
            ("cache-capacity", po::value<uint64_t>(), "sets an upper bound on the number of states allowed in "
                               "the cache")
            ("solvability", po::value<int>(), "calculates the solvability "
                    "percentage of the supplied solitaire game, given a limit for the number of seeds. Must supply "
                    "either 'random', 'benchmark', 'solvability' or list of deals to be solved.")
            ("timeout", po::value<uint64_t>(), "adds a timeout to searches")
            ("initial-depth-bound", po::value<uint64_t>(),
                    "depth bound L0 for a single bounded search pass. When absent "
                    "the search is unbounded (L = infinity) and behaves identically "
                    "to a normal run. A node whose depth reaches L is treated as a "
                    "truncated leaf and not expanded.")
            ("depth-grow", po::value<uint64_t>()->default_value(2),
                    "factor by which the depth bound grows between iterative-deepening "
                    "passes (parsed and stored now; the outer loop is added in a later "
                    "change and does not yet consume this).")
            ("max-depth-bound", po::value<uint64_t>(),
                    "upper limit on the depth bound for iterative deepening (parsed and "
                    "stored now; the outer loop is added in a later change and does not "
                    "yet consume this). Defaults to being tied to --timeout.")
            ("resume", po::value<vector<int>>()->multitoken(), "resumes the solvability percentage calculation from a "
                                                    "previous run. Must be supplied with the solvability option. "
                                                    "Syntax: [sol unsol intract in-progress-1 in-progress-2 ...]")
            ("cores", po::value<uint>(), "the number of cores for the solvability percentages to be run across. "
                                         "Must be supplied with the solvability option.")
            ("streamliners", po::value<string>(),
                    "Applies streamliners to the search. Options include 'none', 'both', 'suit-symmetry',"
                    " 'auto-foundations', and 'smart-solvability'. Defaults to 'none', unless '--solvability' is"
                    " also supplied, in which case defaults to 'smart-solvability'. 'smart-solvability' mode"
                    " runs first with both streamliners and a 10% timeout. If this"
                    " is unsuccessful (unsolvable or timeout), then runs again without streamliners.")
            ("benchmark", "outputs performance statistics for the solver on the "
                          "supplied solitaire game. Must supply "
                          "either 'random', 'benchmark', 'solvability' or list of deals to be "
                          "solved.")
            ("benchmark-seeds", po::value<vector<int>>()->multitoken(), "supply start and end seeds for benchmarking")
            ("benchmark-iterations", po::value<int>()->default_value(1), "iterations per seed")
            ("benchmark-warmup", po::value<bool>()->default_value(true), "run warmup pass before benchmarking")
            ("benchmark-json", po::value<string>(), "path to a regression json file to benchmark multiple instances")
            ("deal-only", "outputs the starting deal for a given game type & random seed as json")
            ("json", "outputs the result of the search as a machine-readable JSON object")
            ("reveal-hidden", "reveals identity of face-down cards in JSON output (e.g. 'ah' instead of '##')")
            ("debug", "outputs debug information to clog")
            ("force-lru", "Force use of LRU cache even for flat-cache games")
            ("cache-type", po::value<string>()->default_value("auto"),
             "Select cache implementation. Options: 'auto' (default, chooses flat or lru based on game), "
             "'hash-only' (stores only Zobrist hash — no payload, faster but weaker deduplication), "
             "'multiplicity' (experimental multiplicity encoding Stage 1, single-deck no-symmetry games).")
            ("trace", po::value<string>(),
             "write search trace to file (requires SOLVITAIRE_SEARCH_TRACE build)")
            ("trace-break-at", po::value<uint64_t>(),
             "halt and print game state at trace operation N")
            ("trace-find-hash", po::value<string>(),
             "halt and print game state at first new-state insertion with this Zobrist hash (hex, e.g. 0xdeadbeef)");

    po::options_description hidden_options("Hidden options");
    hidden_options.add_options()
            ("input-files", po::value<vector<string>>(),
             "the file containing the deal to be solved");

    cmdline_options.add(main_options).add(hidden_options);

    p.add("input-files", -1);
}

// Returns true if we can continue solving the supplied solitaire(s)
bool command_line_helper::parse(int argc, const char* argv[]) {
    po::variables_map vm;
    // Attempts to parse cmdln options. Catches basic errors
    try {
        store(po::command_line_parser(argc, argv).options(cmdline_options)
                      .positional(p).run(), vm);
    } catch(po::error& e) {
        LOG_ERROR ("Error: " << e.what());
        return false;
    }

    // Updates the variables map which the user's options are stored in
    notify(vm);

    // Converts the variables map into flags and values

    help = (vm.count("help") != 0);

    available_game_types = (vm.count("available-game-types") != 0);

    version = (vm.count("version") != 0);

    if (vm.count("describe-game-rules")) {
        describe_game_rules = vm["describe-game-rules"].as<string>();
    }

    classify = (vm.count("classify") != 0);

    deal_only = (vm.count("deal-only") != 0);

    if (vm.count("input-files")) {
        input_files = vm["input-files"].as<vector<string>>();
    }

    if (vm.count("type")) {
        solitaire_type = vm["type"].as<string>();
    }

    if (vm.count("custom-rules")) {
        rules_file = vm["custom-rules"].as<string>();
    }

    if (vm.count("random")) {
        random_deal = vm["random"].as<int>();
    } else {
        random_deal = -1;
    }

    if (vm.count("cache-capacity")) {
        cache_capacity = vm["cache-capacity"].as<uint64_t>();
    } else {
        cache_capacity = 100000000; // One hundred-million
    }

    if (vm.count("solvability")) {
        solvability = vm["solvability"].as<int>();
    } else {
        solvability = -1;
    }

    if (vm.count("timeout")) {
        timeout = vm["timeout"].as<uint64_t>();
    } else {
        timeout = 604800000; // 1 week in milliseconds
    }

    // Depth-bounded search (Stage 1). The bound is OFF unless --initial-depth-bound
    // is supplied, so a normal run is byte-identical to before.
    if (vm.count("initial-depth-bound")) {
        has_initial_depth_bound_ = true;
        initial_depth_bound = vm["initial-depth-bound"].as<uint64_t>();
    } else {
        has_initial_depth_bound_ = false;
        initial_depth_bound = 0;
    }

    // Parsed and stored now; consumed only by the PR2 outer iterative-deepening loop.
    depth_grow = vm["depth-grow"].as<uint64_t>();

    if (vm.count("max-depth-bound")) {
        has_max_depth_bound_ = true;
        max_depth_bound = vm["max-depth-bound"].as<uint64_t>();
    } else {
        // Default tied to --timeout per the plan; the PR2 loop will refine this.
        has_max_depth_bound_ = false;
        max_depth_bound = 0;
    }

    if (vm.count("cores")) {
        cores = vm["cores"].as<uint>();
    } else {
        cores = 1;
    }

    if (vm.count("resume")) {
        resume = vm["resume"].as<vector<int>>();
    } else {
        resume = {0, 0, 0};
        for (uint i = 0; i < cores; i++) resume.push_back(i);
    }

    if (vm.count("streamliners")) {
        auto& s = vm["streamliners"].as<string>();

        if (s == "auto-foundations" ) streamliners = streamliner_opt::AUTO_FOUNDATIONS;
        else if (s == "suit-symmetry") streamliners = streamliner_opt::SUIT_SYMMETRY;
        else if (s == "both") streamliners = streamliner_opt::BOTH;
        else if (s == "smart-solvability") streamliners = streamliner_opt::SMART;
        else if (s == "none") streamliners = streamliner_opt::NONE;
        else {
            print_streamliner_error(s);
            return false;
        }
    } else {
        if (solvability == -1) streamliners = streamliner_opt::NONE;
        else streamliners = streamliner_opt::SMART;
    }

    benchmark = (vm.count("benchmark") != 0);

    if (vm.count("benchmark-seeds")) {
        auto seeds = vm["benchmark-seeds"].as<vector<int>>();
        if (seeds.size() >= 2) {
            benchmark_seeds = {seeds[0], seeds[1]};
        } else if (seeds.size() == 1) {
            benchmark_seeds = {seeds[0], seeds[0]};
        }
        is_benchmark = true;
    }

    if (benchmark && !is_benchmark) {
        benchmark_seeds = {1, 100};
        is_benchmark = true;
    }

    if (vm.count("benchmark-iterations")) {
        benchmark_iterations = vm["benchmark-iterations"].as<int>();
    }

    if (vm.count("benchmark-warmup")) {
        benchmark_warmup = vm["benchmark-warmup"].as<bool>();
    }
    
    if (vm.count("benchmark-json")) {
        benchmark_json = vm["benchmark-json"].as<string>();
        is_benchmark = true;
    }

    if (is_benchmark) {
        json_output = true;
    } else {
        json_output = (vm.count("json") != 0);
    }

    reveal_hidden = (vm.count("reveal-hidden") != 0);
    debug = (vm.count("debug") != 0);
    force_lru_cache = (vm.count("force-lru") != 0);

    if (vm.count("trace")) {
        trace_path = vm["trace"].as<string>();
    }
    if (vm.count("trace-break-at")) {
        has_break_at_ = true;
        break_at_n = vm["trace-break-at"].as<uint64_t>();
    }
    if (vm.count("trace-find-hash")) {
        has_find_hash_ = true;
        find_hash_val = std::stoull(vm["trace-find-hash"].as<string>(), nullptr, 16);
    }

    cache_type = vm["cache-type"].as<string>();
    if (cache_type != "auto" && cache_type != "hash-only" && cache_type != "multiplicity") {
        LOG_ERROR ("Error: invalid --cache-type: " + cache_type + ". Must be 'auto', 'hash-only', or 'multiplicity'.");
        return false;
    }

    // Handle logic error scenarios
    return assess_errors();
}

bool command_line_helper::assess_errors() {
    if (help) {
        print_help();
        return false;
    }

    if (version) { return true; } 

    if (available_game_types || !describe_game_rules.empty()) return true;

    if (solvability > 0 && resume.size() != 3 + cores) {
        print_resume_error();
        return false;
    }

    // The user must either supply input files, a random seed, or ask for the
    // solvability percentage, or benchmark
    int opt_count = (random_deal != -1) + !input_files.empty() + (solvability > 0) + is_benchmark;

    if (opt_count > 1) {
        print_too_many_opts_error();
        return false;
    } else if (opt_count < 1) {
        print_no_opts_error();
        return false;
    }

    // The user must supply either a solitaire type or a rules file, 
    // unless benchmark-json is used (which handles rules per instance)
    if (benchmark_json.empty() && ((solitaire_type.empty() && rules_file.empty())
            || (!solitaire_type.empty() && !rules_file.empty()))) {
        print_sol_type_rules_error();
        return false;
    }

    if (!solitaire_type.empty()) {
        return assess_sol_type();
    } else {
        return true;
    }
}

// Checks if the supplied solitaire type is in the list of valid solitaires
bool command_line_helper::assess_sol_type() {
    if (sol_preset_types::is_valid_preset(solitaire_type)) {
        return true;

    } else {
        LOG_ERROR ("Error: Solitaire type is not valid: " << solitaire_type);
        print_help();
        return false;
    }
}

void command_line_helper::print_help() {
    cerr <<  "Usage: solvitaire [options] input-file1 input-file2 ...\n"
            << main_options << "\n";
}

void command_line_helper::print_no_opts_error() {
    LOG_ERROR ("Error: User must supply input file(s), the '--random' "
            "option, the 'benchmark' option, or the '--solvability' option");
    print_help();
}

void command_line_helper::print_sol_type_rules_error() {
    LOG_ERROR ("Error: User must supply either a solitaire type, or a 'rules' "
                       "file");
    print_help();
}

void command_line_helper::print_too_many_opts_error() {
    LOG_ERROR ("Error: User must supply input file(s), the '--random' option, the 'benchmark' option,, "
               "or the '--solvability' option, not multiple");
    print_help();
}

void command_line_helper::print_resume_error() {
    LOG_ERROR ("Error: Resume contains " << resume.size() << " options, and should contain " << cores + 3);
    print_help();
}

void command_line_helper::print_streamliner_error(const string& str) {
    LOG_ERROR ("Error: invalid streamliner: " + str + ".\nAvailable options are: 'none', 'both', 'suit-symmetry',"
                                                      " 'auto-foundations', and 'smart-solvability'");
}

const vector<string> command_line_helper::get_input_files() {
    return input_files;
}

const string command_line_helper::get_solitaire_type() {
    return solitaire_type;
}

const string command_line_helper::get_rules_file() {
    return rules_file;
}

int command_line_helper::get_random_deal() {
    return random_deal;
}

bool command_line_helper::get_classify() {
    return classify;
}

bool command_line_helper::get_deal_only() {
    return deal_only;
}

uint64_t command_line_helper::get_cache_capacity() {
    return cache_capacity;
}

int command_line_helper::get_solvability() {
    return solvability;
}

uint64_t command_line_helper::get_timeout() {
    return timeout;
}

bool command_line_helper::has_initial_depth_bound() const {
    return has_initial_depth_bound_;
}

uint64_t command_line_helper::get_initial_depth_bound() const {
    return initial_depth_bound;
}

uint64_t command_line_helper::get_depth_grow() const {
    return depth_grow;
}

uint64_t command_line_helper::get_max_depth_bound() const {
    return max_depth_bound;
}

vector<int> command_line_helper::get_resume() {
    return resume;
}

uint command_line_helper::get_cores() {
    return cores;
}

bool command_line_helper::get_available_game_types() {
    return available_game_types;
}

string command_line_helper::get_describe_game_rules() {
    return describe_game_rules;
}

bool command_line_helper::get_benchmark() {
    return benchmark;
}

std::pair<int, int> command_line_helper::get_benchmark_seeds() const {
    if (benchmark_seeds.first > benchmark_seeds.second) {
        return {benchmark_seeds.second, benchmark_seeds.first};
    }
    return benchmark_seeds;
}

int command_line_helper::get_benchmark_iterations() const {
    return benchmark_iterations;
}

bool command_line_helper::get_benchmark_warmup() const {
    return benchmark_warmup;
}

const std::string& command_line_helper::get_benchmark_json() const {
    return benchmark_json;
}

bool command_line_helper::get_is_benchmark() const {
    return is_benchmark;
}

bool command_line_helper::get_json_output() const {
    return json_output;
}

bool command_line_helper::get_reveal_hidden() const {
    return reveal_hidden;
}

bool command_line_helper::get_debug() const {
    return debug;
}

bool command_line_helper::get_force_lru_cache() const {
    return force_lru_cache;
}

const std::string& command_line_helper::get_cache_type() const {
    return cache_type;
}

const std::string& command_line_helper::get_trace_path() const {
    return trace_path;
}

bool command_line_helper::has_break_at() const {
    return has_break_at_;
}

uint64_t command_line_helper::get_break_at_n() const {
    return break_at_n;
}

bool command_line_helper::has_find_hash() const {
    return has_find_hash_;
}

uint64_t command_line_helper::get_find_hash() const {
    return find_hash_val;
}

bool command_line_helper::get_version() {
    return version;
}

command_line_helper::streamliner_opt command_line_helper::get_streamliners() {
    return streamliners;
}

game_state::streamliner_options command_line_helper::get_streamliners_game_state() {
    return convert_streamliners(streamliners);
}

game_state::streamliner_options command_line_helper::convert_streamliners(streamliner_opt so) {
    switch (so){
        case command_line_helper::streamliner_opt::NONE: return game_state::streamliner_options::NONE;
        case command_line_helper::streamliner_opt::AUTO_FOUNDATIONS: return game_state::streamliner_options::AUTO_FOUNDATIONS;
        case command_line_helper::streamliner_opt::SUIT_SYMMETRY: return game_state::streamliner_options::SUIT_SYMMETRY;
        case command_line_helper::streamliner_opt::BOTH: return game_state::streamliner_options::BOTH;
        case command_line_helper::streamliner_opt::SMART:
        default:
            assert(false);
            throw runtime_error("attempted to convert smart streamliner mode to single streamliner");
            return game_state::streamliner_options::NONE;;
    }
}
