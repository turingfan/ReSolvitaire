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
// Created by thecharlesblake on 11/13/17.
//

#include <fstream>
#include <sstream>
#include <iostream>

#include "json_helper.h"
#include "../../../../../lib/rapidjson/stringbuffer.h"
#include "../../../../../lib/rapidjson/writer.h"
#include "../../output/log_helper.h"

using namespace std;
using namespace rapidjson;

Document json_helper::get_file_json(const string& filename) {
    // Reads the file into a string
    std::ifstream ifstr(filename);
    std::stringstream buf;
    buf << ifstr.rdbuf();

    if (ifstr.fail()) {
        throw runtime_error("could not read file " + filename);
    } else {
        Document d;

        d.Parse(buf.str().c_str());
        if (d.HasParseError()) {
            throw runtime_error(filename + " not valid json");
        } else {
            return d;
        }
    }
}

void json_helper::json_parse_err(const string& msg) {
    string err_msg = "Error in JSON doc: " + msg;
    LOG_DEBUG(err_msg);
    throw runtime_error(err_msg);
}

void json_helper::json_parse_warning(const string& msg) {
    LOG_WARNING("Error in JSON doc: " + msg);
}

const string json_helper::schema_err_str(const SchemaValidator& validator) {
    string ret = "Input JSON failed to match the required schema. Schema Validator Error = ";

    StringBuffer sb;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
    ret += validator.GetInvalidSchemaKeyword();
    ret += ": ";

    sb.Clear();
    validator.GetInvalidDocumentPointer().StringifyUriFragment(sb);

    ret += sb.GetString();
    return ret;
}

void json_helper::print_game_state_as_json(const game_state& gs, bool reveal_hidden) {
    StringBuffer sb;
    Writer<StringBuffer> writer(sb);

    writer.StartObject();

    if (!gs.tableau_piles.empty()) {
        writer.Key("tableau piles");
        writer.StartArray();
        for (auto pr : gs.tableau_piles) {
            writer.StartArray();
            const auto& p = gs.piles[pr];
            for (pile::size_type i = p.size(); i-->0; ) {
                writer.String(p[i].to_string(reveal_hidden).c_str());
            }
            writer.EndArray();
        }
        writer.EndArray();
    }

    if (!gs.foundations.empty()) {
        writer.Key("foundations");
        writer.StartArray();
        for (auto pr : gs.foundations) {
            const auto& p = gs.piles[pr];
            for (pile::size_type i = p.size(); i-->0; ) {
                writer.String(p[i].to_string(reveal_hidden).c_str());
            }
        }
        writer.EndArray();
    }

    if (!gs.cells.empty()) {
        writer.Key("cells");
        writer.StartArray();
        for (auto pr : gs.cells) {
            const auto& p = gs.piles[pr];
            if (p.empty()) writer.String("");
            else writer.String(p.top_card().to_string(reveal_hidden).c_str());
        }
        writer.EndArray();
    }

    if (!gs.reserve.empty()) {
        writer.Key("reserve");
        writer.StartArray();
        if (gs.rules.reserve_stacked) {
            const auto& p = gs.piles[gs.reserve.front()];
            for (pile::size_type i = p.size(); i-->0; ) {
                writer.String(p[i].to_string(reveal_hidden).c_str());
            }
        } else {
            for (auto pr : gs.reserve) {
                const auto& p = gs.piles[pr];
                if (p.empty()) writer.String("");
                else writer.String(p.top_card().to_string(reveal_hidden).c_str());
            }
        }
        writer.EndArray();
    }

    if (!gs.sequences.empty()) {
        writer.Key("sequences");
        writer.StartArray();
        for (auto pr : gs.sequences) {
            writer.StartArray();
            const auto& p = gs.piles[pr];
            for (pile::size_type i = p.size(); i-->0; ) {
                writer.String(p[i].to_string(reveal_hidden).c_str());
            }
            writer.EndArray();
        }
        writer.EndArray();
    }

    if (!gs.accordion.empty()) {
        writer.Key("accordion");
        writer.StartArray();
        for (auto pr : gs.accordion) {
            const auto& p = gs.piles[pr];
            if (p.empty()) writer.String("");
            else writer.String(p.top_card().to_string(reveal_hidden).c_str());
        }
        writer.EndArray();
    }

    if (gs.rules.stock_size > 0) {
        writer.Key("stock");
        writer.StartArray();
        const auto& p_stock = gs.piles[gs.stock];
        for (pile::size_type i = p_stock.size(); i-->0; ) {
            writer.String(p_stock[i].to_string(reveal_hidden).c_str());
        }
        writer.EndArray();

        if (gs.rules.stock_deal_t == sol_rules::stock_deal_type::WASTE) {
            writer.Key("waste");
            writer.StartArray();
            const auto& p_waste = gs.piles[gs.waste];
            for (pile::size_type i = p_waste.size(); i-->0; ) {
                writer.String(p_waste[i].to_string(reveal_hidden).c_str());
            }
            writer.EndArray();
        }
    }

    if (gs.rules.hole) {
        writer.Key("hole");
        writer.String(gs.piles[gs.hole][0].to_string(reveal_hidden).c_str());
    }

    writer.EndObject();

    cout << sb.GetString() << endl;
}
