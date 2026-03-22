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
#include <cmath>
#include <algorithm>

#include "benchmark.h"
#include "../game/search-state/game_state.h"
#include "../solver/solver.h"

#include "../../../lib/rapidjson/rapidjson.h"
#include "../../../lib/rapidjson/document.h"
#include "../../../lib/rapidjson/writer.h"
#include "../../../lib/rapidjson/stringbuffer.h"
#include "../../../lib/rapidjson/filewritestream.h"
#include <cstdio>

using namespace std;
typedef chrono::microseconds microsec;

void benchmark::run(const sol_rules &rules, uint64_t cache_capacity, game_state::streamliner_options streamliners, std::pair<int, int> seeds, int iterations, bool warmup) {
    char writeBuffer[65536];
    rapidjson::FileWriteStream os(stdout, writeBuffer, sizeof(writeBuffer));
    rapidjson::Writer<rapidjson::FileWriteStream, rapidjson::UTF8<>, rapidjson::UTF8<>, rapidjson::CrtAllocator, rapidjson::kWriteDefaultFlags> writer(os);

    vector<double> all_times;
    vector<double> all_nodes;

    writer.StartObject();

    // Warmup: solve first seed once to wake up CPU
    if (warmup) {
        int warmup_seed = seeds.first;
        game_state gs_warmup(rules, warmup_seed, streamliners);
        solver sol_warmup(gs_warmup, cache_capacity);
        sol_warmup.run();
    }

    writer.Key("seed_data");
    writer.StartObject();

    for (int current_seed = seeds.first; current_seed <= seeds.second; current_seed++) {
        game_state gs(rules, (int)current_seed, streamliners);
        
        writer.Key(std::to_string(current_seed).c_str());
        writer.StartArray();

        for (int i = 0; i < iterations; ++i) {
            solver sol(gs, cache_capacity);

            auto start = chrono::steady_clock::now();
            solver::result res = sol.run();
            auto end = chrono::steady_clock::now();
            
            double duration = chrono::duration_cast<microsec>(end - start).count();
            double nodes = (double)res.states_searched;

            writer.StartObject();
            writer.Key("time_us"); writer.Double(duration);
            writer.Key("nodes"); writer.Double(nodes);
            writer.EndObject();

            all_times.push_back(duration);
            all_nodes.push_back(nodes);
        }
        writer.EndArray();
    }
    writer.EndObject(); // End of seed_data

    writer.Key("aggregate_stats");
    writer.StartObject();
    
    if (!all_times.empty()) {
        // Time stats
        double total_time = std::accumulate(all_times.begin(), all_times.end(), 0.0);
        double mean_time = total_time / all_times.size();
        
        vector<double> sorted_times = all_times;
        std::sort(sorted_times.begin(), sorted_times.end());
        double median_time = sorted_times[sorted_times.size() / 2];
        if (sorted_times.size() % 2 == 0) {
            median_time = (sorted_times[sorted_times.size() / 2 - 1] + sorted_times[sorted_times.size() / 2]) / 2.0;
        }

        double sq_sum_time = std::accumulate(all_times.begin(), all_times.end(), 0.0, [mean_time](double acc, double val) {
            return acc + (val - mean_time) * (val - mean_time);
        });
        double sd_time = std::sqrt(sq_sum_time / (double)all_times.size());

        // Node stats
        double total_nodes = std::accumulate(all_nodes.begin(), all_nodes.end(), 0.0);
        double mean_nodes = total_nodes / all_nodes.size();
        
        vector<double> sorted_nodes = all_nodes;
        std::sort(sorted_nodes.begin(), sorted_nodes.end());
        double median_nodes = sorted_nodes[sorted_nodes.size() / 2];
        if (sorted_nodes.size() % 2 == 0) {
            median_nodes = (sorted_nodes[sorted_nodes.size() / 2 - 1] + sorted_nodes[sorted_nodes.size() / 2]) / 2.0;
        }

        double sq_sum_nodes = std::accumulate(all_nodes.begin(), all_nodes.end(), 0.0, [mean_nodes](double acc, double val) {
            return acc + (val - mean_nodes) * (val - mean_nodes);
        });
        double sd_nodes = std::sqrt(sq_sum_nodes / (double)all_nodes.size());

        // Nodes per second calculation
        double total_time_s = total_time / 1000000.0;
        double nodes_per_second = (total_time_s > 0) ? (total_nodes / total_time_s) : 0;

        writer.Key("mean_time_us"); writer.Double(mean_time);
        writer.Key("median_time_us"); writer.Double(median_time);
        writer.Key("sd_time_us"); writer.Double(sd_time);
        
        writer.Key("mean_nodes"); writer.Double(mean_nodes);
        writer.Key("median_nodes"); writer.Double(median_nodes);
        writer.Key("sd_nodes"); writer.Double(sd_nodes);
        
        writer.Key("nodes_per_second"); writer.Double(nodes_per_second);

        writer.Key("min_time_us"); writer.Double(*std::min_element(all_times.begin(), all_times.end()));
        writer.Key("max_time_us"); writer.Double(*std::max_element(all_times.begin(), all_times.end()));
    }
    writer.EndObject(); // End of aggregate_stats
    writer.EndObject(); // End of root
    
    printf("\n");
}
