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
// Created by thecharlesblake on 4/5/18.
//

#include <climits>
#include <iostream>
#include <numeric>
#include <sys/resource.h>
#include <cmath>

#include "benchmark.h"
#include "../game/search-state/game_state.h"
#include "../solver/solver.h"

#include "../../../lib/rapidjson/document.h"
#include "../../../lib/rapidjson/writer.h"
#include "../../../lib/rapidjson/stringbuffer.h"

using namespace std;
typedef chrono::microseconds microsec;

uint64_t get_resident_memory_bytes() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef __APPLE__
        return usage.ru_maxrss;  // macOS: already in bytes
#else
        return usage.ru_maxrss * 1024;  // Linux: in kilobytes
#endif
    }
    return 0;
}

void benchmark::run(const sol_rules &rules, uint64_t cache_capacity, game_state::streamliner_options str_opts,
                    pair<int, int> seeds, int iterations, bool warmup, uint64_t timeout_ms) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

    writer.StartObject();
    writer.Key("seed_data");
    writer.StartObject();

    vector<double> all_times;
    vector<double> all_nodes;
    vector<uint64_t> all_memory;

    for(int seed = seeds.first; seed <= seeds.second; seed++) {
        writer.Key(to_string(seed).c_str());
        writer.StartArray();

        for(int i = 0; i < iterations + (warmup ? 1 : 0); i++) {
            game_state gs(rules, seed, str_opts);
            solver sol(gs, cache_capacity);

            auto start = chrono::steady_clock::now();
            solver::result result = sol.run(chrono::milliseconds(timeout_ms));
            auto end = chrono::steady_clock::now();
            microsec elapsed_micros =
                    chrono::duration_cast<chrono::microseconds>(end - start);

            if (!warmup || i > 0) {
                double duration = static_cast<double>(elapsed_micros.count());
                uint64_t memory = get_resident_memory_bytes();

                all_times.push_back(duration);
                all_nodes.push_back(static_cast<double>(result.states_searched));
                all_memory.push_back(memory);

                writer.StartObject();
                writer.Key("time_us"); writer.Double(duration);
                writer.Key("nodes"); writer.Double(static_cast<double>(result.states_searched));
                writer.Key("resident_memory_bytes"); writer.Uint64(memory);
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
        double median_time = all_times[all_times.size() / 2];

        double sq_sum = inner_product(all_times.begin(), all_times.end(), all_times.begin(), 0.0);
        double stdev = sqrt(max(0.0, sq_sum / all_times.size() - mean_time * mean_time));

        double total_nodes = accumulate(all_nodes.begin(), all_nodes.end(), 0.0);
        double mean_nodes = total_nodes / all_nodes.size();

        sort(all_nodes.begin(), all_nodes.end());
        double median_nodes = all_nodes[all_nodes.size() / 2];

        double sq_sum_nodes = inner_product(all_nodes.begin(), all_nodes.end(), all_nodes.begin(), 0.0);
        double stdev_nodes = sqrt(max(0.0, sq_sum_nodes / all_nodes.size() - mean_nodes * mean_nodes));

        // Memory statistics
        sort(all_memory.begin(), all_memory.end());
        uint64_t max_memory = all_memory.back();
        uint64_t median_memory = all_memory[all_memory.size() / 2];

        writer.Key("mean_time_us"); writer.Double(mean_time);
        writer.Key("median_time_us"); writer.Double(median_time);
        writer.Key("sd_time_us"); writer.Double(stdev);
        writer.Key("mean_nodes"); writer.Double(mean_nodes);
        writer.Key("median_nodes"); writer.Double(median_nodes);
        writer.Key("sd_nodes"); writer.Double(stdev_nodes);
        writer.Key("nodes_per_second"); writer.Double(total_nodes / (max(1.0, total_time) / 1000000.0));
        writer.Key("min_time_us"); writer.Double(all_times.front());
        writer.Key("max_time_us"); writer.Double(all_times.back());
        writer.Key("max_resident_memory_bytes"); writer.Uint64(max_memory);
        writer.Key("median_resident_memory_bytes"); writer.Uint64(median_memory);
    }

    writer.EndObject(); // End of aggregate_stats
    writer.EndObject(); // End of root

    cout << buffer.GetString();
}

void benchmark::run_json(const string& json_path, uint64_t cache_capacity, int iterations, bool warmup, uint64_t timeout_ms) {
    // Suppress unused parameter warnings
    (void)json_path;
    (void)cache_capacity;
    (void)iterations;
    (void)warmup;
    (void)timeout_ms;

    // Placeholder for JSON instance benchmarking
    // This would load instances from a JSON file and benchmark each one
    cerr << "JSON-based benchmarking not yet implemented in minimal version" << endl;
}
