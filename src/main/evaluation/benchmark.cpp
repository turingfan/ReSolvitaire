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
// Created by thecharlesblake on 11/14/17.
//

#include "benchmark.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <numeric>
#include <algorithm>
#include <chrono>
#include <string> // Keep string for to_string and other string operations
#include <memory> // Keep memory for unique_ptr
#include <cmath> // Keep cmath for sqrt
#include <cstdio> // For FILE operations on Linux /proc/self/status
#include <sys/resource.h> // For getrusage() memory measurement

#include "../game/sol_rules.h" // Keep this for sol_rules
#include "../game/search-state/game_state.h" // Keep this for game_state
#include "../game/global_cache.h"
#include "../game/flat_cache.h"
#include "../game/predecessor_flat_cache.h"
#include "../solver/solver.h"
#include "../input-output/input/json-parsing/rules_parser.h"
#include "../input-output/input/json-parsing/deal_parser.h"
#include "../input-output/input/sol_preset_types.h"
#include "../input-output/output/log_helper.h" // Keep this for log_helper

#include "../../../lib/rapidjson/document.h"
#include "../../../lib/rapidjson/writer.h"
#include "../../../lib/rapidjson/stringbuffer.h"
#include "../../../lib/rapidjson/filewritestream.h"
#include "../../../lib/rapidjson/filereadstream.h"


using namespace std;

static char benchmark_buffer[65536];

// Helper function to get resident set size (actual physical memory)
static uint64_t get_resident_memory_bytes() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        // ru_maxrss is in bytes on macOS, kilobytes on Linux
        #ifdef __APPLE__
            return (uint64_t)usage.ru_maxrss;
        #else
            return (uint64_t)usage.ru_maxrss * 1024;
        #endif
    }
    return 0;
}

// Helper function to get virtual memory size
static uint64_t get_virtual_memory_bytes() {
    #ifdef __APPLE__
        // On macOS, sum up memory usage components from rusage
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            // ru_idrss (unshared data) + ru_ixrss (unshared stack) + ru_isrss (shared memory)
            // These are in units of page*seconds, so convert to bytes
            // Actually, on macOS these are deprecated. Use a simple heuristic: peak RSS is a good estimate
            return (uint64_t)usage.ru_maxrss;
        }
    #else
        // On Linux, read from /proc/self/status if available
        FILE* f = fopen("/proc/self/status", "r");
        if (f) {
            char line[256];
            uint64_t vm_peak = 0;
            while (fgets(line, sizeof(line), f)) {
                if (sscanf(line, "VmPeak: %lu", &vm_peak) == 1) {
                    fclose(f);
                    return vm_peak * 1024;  // Convert KB to bytes
                }
            }
            fclose(f);
        }
    #endif
    return 0;
}

void benchmark::run(const sol_rules& rules, uint64_t cache_capacity, game_state::streamliner_options str_opts, pair<int, int> seeds, int iterations, bool warmup, uint64_t timeout_ms, bool force_lru) {
    rapidjson::FileWriteStream os(stdout, benchmark_buffer, sizeof(benchmark_buffer));
    rapidjson::Writer<rapidjson::FileWriteStream> writer(os);

    writer.StartObject();

    writer.Key("seed_data");
    writer.StartObject();

    vector<double> all_times;
    vector<double> all_nodes;
    vector<uint64_t> all_memory;
    vector<solver::result::type> all_sol_types;

    for (int seed = seeds.first; seed <= seeds.second; ++seed) {
        writer.Key(to_string(seed).c_str());
        writer.StartArray();

        for (int i = 0; i < iterations + (warmup ? 1 : 0); ++i) {
            game_state gs(rules, (int)seed, str_opts, force_lru);
            std::unique_ptr<cache_interface> cache_ptr;
            bool suit_sym = str_opts == game_state::streamliner_options::SUIT_SYMMETRY
                         || str_opts == game_state::streamliner_options::BOTH;
            if (use_predecessor_cache(rules) && !force_lru) {
                cache_ptr = std::make_unique<predecessor_flat_cache>(cache_capacity);
            } else if (use_new_cache(rules, suit_sym) && !force_lru) {
                cache_ptr = std::make_unique<flat_cache>(cache_capacity);
            } else {
                cache_ptr = std::make_unique<lru_cache>(gs, cache_capacity);
            }
            solver sol(gs, *cache_ptr);

            auto start = chrono::high_resolution_clock::now();
            solver::result res = sol.run(chrono::milliseconds(timeout_ms));
            auto end = chrono::high_resolution_clock::now();
            uint64_t resident_memory = get_resident_memory_bytes();
            uint64_t virtual_memory = get_virtual_memory_bytes();

            if (!warmup || i > 0) {
                double duration = chrono::duration_cast<chrono::microseconds>(end - start).count();
                all_times.push_back(duration);
                all_nodes.push_back((double)res.states_searched);
                all_memory.push_back(resident_memory);
                all_sol_types.push_back(res.sol_type);

                writer.StartObject();
                writer.Key("time_us"); writer.Double(duration);
                writer.Key("nodes"); writer.Double((double)res.states_searched);
                writer.Key("unique_nodes"); writer.Uint64(res.unique_states_searched);
                writer.Key("backtracks"); writer.Uint64(res.backtracks);
                writer.Key("dominance_moves"); writer.Uint64(res.dominance_moves);
                writer.Key("states_removed_from_cache"); writer.Uint64(res.states_removed_from_cache);
                writer.Key("final_cache_size"); writer.Uint64(res.cache_size);
                writer.Key("final_cache_buckets"); writer.Uint64(res.cache_bucket_count);
                writer.Key("max_search_depth"); writer.Uint64(res.max_depth);
                writer.Key("final_search_depth"); writer.Uint64(res.depth);
                writer.Key("solution_type"); writer.String(boost::lexical_cast<std::string>(res.sol_type).c_str());
                writer.Key("resident_memory_bytes"); writer.Uint64(resident_memory);
                if (virtual_memory > 0) {
                    writer.Key("virtual_memory_bytes"); writer.Uint64(virtual_memory);
                }
                writer.EndObject();
            }
        }
        writer.EndArray();
    }
    writer.EndObject(); // End of seed_data

    // Aggregate stats
    writer.Key("aggregate_stats");
    writer.StartObject();

    if (!all_times.empty()) {
        double total_time = accumulate(all_times.begin(), all_times.end(), 0.0);
        double mean_time = total_time / all_times.size();
        
        sort(all_times.begin(), all_times.end());
        // Median: for even-sized arrays, average the two middle elements
        double median_time;
        if (all_times.size() % 2 == 1) {
            median_time = all_times[all_times.size() / 2];
        } else {
            median_time = (all_times[all_times.size() / 2 - 1] + all_times[all_times.size() / 2]) / 2.0;
        }

        double sq_sum = inner_product(all_times.begin(), all_times.end(), all_times.begin(), 0.0);
        double stdev = sqrt(max(0.0, sq_sum / all_times.size() - mean_time * mean_time));

        double total_nodes = accumulate(all_nodes.begin(), all_nodes.end(), 0.0);
        double mean_nodes = total_nodes / all_nodes.size();

        sort(all_nodes.begin(), all_nodes.end());
        // Median: for even-sized arrays, average the two middle elements
        double median_nodes;
        if (all_nodes.size() % 2 == 1) {
            median_nodes = all_nodes[all_nodes.size() / 2];
        } else {
            median_nodes = (all_nodes[all_nodes.size() / 2 - 1] + all_nodes[all_nodes.size() / 2]) / 2.0;
        }

        // Geometric means
        double sum_log_time = 0;
        double sum_log_nodes = 0;
        for (double t : all_times) sum_log_time += log(max(1.0, t));
        for (double n : all_nodes) sum_log_nodes += log(max(1.0, n));
        double geomean_time = exp(sum_log_time / all_times.size());
        double geomean_nodes = exp(sum_log_nodes / all_nodes.size());

        // NPS statistics
        vector<double> indiv_nps;
        double sum_log_nps = 0;
        for (size_t i = 0; i < all_times.size(); ++i) {
            double nps = (all_nodes[i] * 1000000.0) / max(1.0, all_times[i]);
            indiv_nps.push_back(nps);
            sum_log_nps += log(max(1.0, nps));
        }
        sort(indiv_nps.begin(), indiv_nps.end());
        double mean_nps = accumulate(indiv_nps.begin(), indiv_nps.end(), 0.0) / indiv_nps.size();
        double median_nps = indiv_nps[indiv_nps.size() / 2];
        double geomean_nps = exp(sum_log_nps / indiv_nps.size());
        double aggregate_nps = (total_nodes * 1000000.0) / max(1.0, total_time);

        // PAR2 Score (penalize timeouts as 2x timeout)
        // Penalize instances where solution_type == TIMEOUT
        double par2_sum_us = 0;
        for (size_t i = 0; i < all_times.size(); ++i) {
            if (all_sol_types[i] == solver::result::type::TIMEOUT) {
                par2_sum_us += (double)timeout_ms * 1000.0 * 2.0;
            } else {
                par2_sum_us += all_times[i];
            }
        }
        double par2_score_us = par2_sum_us / all_times.size();

        // Memory statistics (resident)
        sort(all_memory.begin(), all_memory.end());
        uint64_t max_memory = all_memory.back();
        uint64_t median_memory = all_memory[all_memory.size() / 2];

        writer.Key("mean_time_us"); writer.Double(mean_time);
        writer.Key("median_time_us"); writer.Double(median_time);
        writer.Key("geometric_mean_time_us"); writer.Double(geomean_time);
        writer.Key("sd_time_us"); writer.Double(stdev);
        writer.Key("par2_score_us"); writer.Double(par2_score_us);

        writer.Key("mean_nodes"); writer.Double(mean_nodes);
        writer.Key("median_nodes"); writer.Double(median_nodes);
        writer.Key("geometric_mean_nodes"); writer.Double(geomean_nodes);
        
        writer.Key("mean_nps"); writer.Double(mean_nps);
        writer.Key("median_nps"); writer.Double(median_nps);
        writer.Key("geometric_mean_nps"); writer.Double(geomean_nps);
        writer.Key("aggregate_nps"); writer.Double(aggregate_nps);
        writer.Key("nodes_per_second"); writer.Double(aggregate_nps); // Keep for compatibility
        
        writer.Key("min_time_us"); writer.Double(all_times.front());
        writer.Key("max_time_us"); writer.Double(all_times.back());
        writer.Key("max_resident_memory_bytes"); writer.Uint64(max_memory);
        writer.Key("median_resident_memory_bytes"); writer.Uint64(median_memory);
    }

    writer.EndObject(); // End of aggregate_stats
    writer.EndObject(); // End of root
    os.Flush();
}

void benchmark::run_json(const string& json_path, uint64_t cache_capacity, int benchmark_iterations, bool benchmark_warmup, uint64_t timeout_ms) {
    ifstream f(json_path);
    if (!f) {
        cerr << "Error: Could not open benchmark JSON: " << json_path << endl;
        return;
    }

    string content((istreambuf_iterator<char>(f)), (istreambuf_iterator<char>()));
    rapidjson::Document doc;
    doc.Parse(content.c_str());

    if (doc.HasParseError()) {
        cerr << "Error: Failed to parse benchmark JSON." << endl;
        return;
    }

    rapidjson::FileWriteStream os(stdout, benchmark_buffer, sizeof(benchmark_buffer));
    rapidjson::Writer<rapidjson::FileWriteStream> writer(os);

    writer.StartArray();

    auto process_instance = [&](const rapidjson::Value& item) {
        if (!item.IsObject()) return;

        string instance_path = "";
        if (item.HasMember("instance") && item["instance"].IsString()) {
            instance_path = item["instance"].GetString();
        } else if (item.HasMember("instance_name") && item["instance_name"].IsString()) {
            instance_path = item["instance_name"].GetString();
        } else {
            return;
        }
        
        // Dynamic game type from filepath
        string game_type = "";
        size_t last_slash = instance_path.find_last_of("/");
        string filename = (last_slash == string::npos) ? instance_path : instance_path.substr(last_slash + 1);
        size_t first_underscore = filename.find_first_of("_");
        if (first_underscore != string::npos) {
            game_type = filename.substr(0, first_underscore);
        }

        // Rules handling
        sol_rules rules;
        bool rules_loaded = false;
        if (item.HasMember("custom_rules") && item["custom_rules"].IsString()) {
            string rules_path = item["custom_rules"].GetString();
            if (ifstream(rules_path)) {
                rules = rules_parser::from_file(rules_path);
                rules_loaded = true;
            } else if (ifstream("tests/" + rules_path)) {
                rules = rules_parser::from_file("tests/" + rules_path);
                rules_loaded = true;
            }
        }
        
        if (!rules_loaded && !game_type.empty()) {
            if (sol_preset_types::is_valid_preset(game_type)) {
                rules = rules_parser::from_preset(game_type);
                rules_loaded = true;
            }
        }

        if (!rules_loaded) return;

        // Streamliner
        game_state::streamliner_options str_opts = game_state::streamliner_options::NONE;
        if (item.HasMember("streamliner") && item["streamliner"].IsString()) {
            string s = item["streamliner"].GetString();
            if (s == "both") str_opts = game_state::streamliner_options::BOTH;
            else if (s == "suit-symmetry") str_opts = game_state::streamliner_options::SUIT_SYMMETRY;
            else if (s == "auto-foundations") str_opts = game_state::streamliner_options::AUTO_FOUNDATIONS;
        }

        // Deal loading resolution
        string full_path = "";
        vector<string> search_paths = {
            instance_path,
            "tests/" + instance_path,
            "tests/resources/" + instance_path
        };
        
        // Also try stripping "instances/" if present in the path (Level 1/3)
        size_t instances_pos = instance_path.find("instances/");
        if (instances_pos != string::npos) {
            string stripped = instance_path;
            stripped.erase(instances_pos, 10); // "instances/" is 10 chars
            search_paths.push_back("tests/resources/" + stripped);
            search_paths.push_back("tests/" + stripped);
        }

        for (const string& p : search_paths) {
            if (ifstream(p)) {
                full_path = p;
                break;
            }
        }

        if (full_path.empty()) return;

        vector<double> times;
        vector<double> nodes_list;
        vector<uint64_t> memory_list;

        for (int i = 0; i < benchmark_iterations + (benchmark_warmup ? 1 : 0); ++i) {
            unique_ptr<game_state> gs;
            try {
                ifstream deal_file(full_path);
                if (!deal_file) throw runtime_error("File not found");
                string deal_content((istreambuf_iterator<char>(deal_file)), (istreambuf_iterator<char>()));
                rapidjson::Document deal_doc;
                deal_doc.Parse(deal_content.c_str());
                if (deal_doc.HasParseError()) {
                    throw runtime_error("JSON parse error");
                }
                // Use Document for from-file constructor
                gs = unique_ptr<game_state>(new game_state(rules, deal_doc, str_opts));
            } catch (const exception& e) {
                cerr << "Error evaluating instance " << full_path << ": " << e.what() << endl;
                continue;
            } catch (...) {
                cerr << "Unknown error evaluating instance " << full_path << endl;
                continue;
            }

            std::unique_ptr<cache_interface> cache_ptr;
            bool suit_sym_json = str_opts == game_state::streamliner_options::SUIT_SYMMETRY
                              || str_opts == game_state::streamliner_options::BOTH;
            if (use_predecessor_cache(rules)) {
                cache_ptr = std::make_unique<predecessor_flat_cache>(cache_capacity);
            } else if (use_new_cache(rules, suit_sym_json)) {
                cache_ptr = std::make_unique<flat_cache>(cache_capacity);
            } else {
                cache_ptr = std::make_unique<lru_cache>(*gs, cache_capacity);
            }
            solver sol(*gs, *cache_ptr);
            auto start = chrono::high_resolution_clock::now();
            solver::result res = sol.run(chrono::milliseconds(timeout_ms));
            auto end = chrono::high_resolution_clock::now();
            uint64_t resident_memory = get_resident_memory_bytes();

            if (!benchmark_warmup || i > 0) {
                times.push_back(chrono::duration_cast<chrono::microseconds>(end - start).count());
                nodes_list.push_back((double)res.states_searched);
                memory_list.push_back(resident_memory);
            }
        }

        if (times.empty()) return;

        sort(times.begin(), times.end());
        sort(nodes_list.begin(), nodes_list.end());
        sort(memory_list.begin(), memory_list.end());

        double median_time = times[times.size() / 2];
        double mean_time = accumulate(times.begin(), times.end(), 0.0) / times.size();
        double median_nodes = nodes_list[nodes_list.size() / 2];
        double mean_nodes = accumulate(nodes_list.begin(), nodes_list.end(), 0.0) / nodes_list.size();
        uint64_t max_memory = memory_list.back();
        uint64_t median_memory = memory_list[memory_list.size() / 2];

        writer.StartObject();
        writer.Key("instance"); writer.String(filename.c_str());
        writer.Key("median_time_us"); writer.Double(median_time);
        writer.Key("mean_time_us"); writer.Double(mean_time);
        writer.Key("median_nodes"); writer.Double(median_nodes);
        writer.Key("mean_nodes"); writer.Double(mean_nodes);
        writer.Key("max_resident_memory_bytes"); writer.Uint64(max_memory);
        writer.Key("median_resident_memory_bytes"); writer.Uint64(median_memory);
        writer.EndObject();
    };

    if (doc.IsArray()) {
        for (auto& item : doc.GetArray()) {
            process_instance(item);
        }
    } else if (doc.IsObject()) {
        for (auto& m : doc.GetObject()) {
            process_instance(m.value);
        }
    }

    writer.EndArray();
    os.Flush();
}
