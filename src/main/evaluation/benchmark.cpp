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
#include <fstream>
#include <numeric>
#include <sys/resource.h>
#include <cmath>

#include "benchmark.h"
#include "../game/search-state/game_state.h"
#include "../solver/solver.h"
#include "../input-output/input/json-parsing/rules_parser.h"
#include "../input-output/input/sol_preset_types.h"

#include "../../../lib/rapidjson/document.h"
#include "../../../lib/rapidjson/writer.h"
#include "../../../lib/rapidjson/stringbuffer.h"
#include "../../../lib/rapidjson/filewritestream.h"

using namespace std;
typedef chrono::microseconds microsec;

static char benchmark_buffer[65536];

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
    (void)timeout_ms; // Not used in minimal version

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

        // Deal loading resolution
        string full_path = "";
        vector<string> search_paths = {
            instance_path,
            "tests/" + instance_path,
            "tests/resources/" + instance_path
        };

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

        for (int i = 0; i < iterations + (warmup ? 1 : 0); ++i) {
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
                gs = unique_ptr<game_state>(new game_state(rules, deal_doc, game_state::streamliner_options::NONE));
            } catch (const exception& e) {
                cerr << "Error evaluating instance " << full_path << ": " << e.what() << endl;
                return;
            } catch (...) {
                cerr << "Unknown error evaluating instance " << full_path << endl;
                return;
            }

            solver sol(*gs, cache_capacity);
            auto start = chrono::high_resolution_clock::now();
            solver::result res = sol.run();
            auto end = chrono::high_resolution_clock::now();
            uint64_t resident_memory = get_resident_memory_bytes();

            if (!warmup || i > 0) {
                times.push_back(chrono::duration_cast<chrono::microseconds>(end - start).count());
                nodes_list.push_back((double)res.states_searched);
                memory_list.push_back(resident_memory);
            }
        }

        if (times.empty()) return;

        sort(times.begin(), times.end());
        sort(nodes_list.begin(), nodes_list.end());
        sort(memory_list.begin(), memory_list.end());

        // Median: for even-sized arrays, average the two middle elements
        double median_time;
        if (times.size() % 2 == 1) {
            median_time = times[times.size() / 2];
        } else {
            median_time = (times[times.size() / 2 - 1] + times[times.size() / 2]) / 2.0;
        }
        double mean_time = accumulate(times.begin(), times.end(), 0.0) / times.size();

        double median_nodes;
        if (nodes_list.size() % 2 == 1) {
            median_nodes = nodes_list[nodes_list.size() / 2];
        } else {
            median_nodes = (nodes_list[nodes_list.size() / 2 - 1] + nodes_list[nodes_list.size() / 2]) / 2.0;
        }
        double mean_nodes = accumulate(nodes_list.begin(), nodes_list.end(), 0.0) / nodes_list.size();
        uint64_t max_memory = memory_list.back();
        uint64_t median_memory;
        if (memory_list.size() % 2 == 1) {
            median_memory = memory_list[memory_list.size() / 2];
        } else {
            median_memory = (memory_list[memory_list.size() / 2 - 1] + memory_list[memory_list.size() / 2]) / 2;
        }

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
