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
// Created by thecharlesblake on 10/21/17.
//

#include "deal_parser.h"
#include "../../../game/search-state/game_state.h"
#include "json_helper.h"
#include "../../../game/sol_rules.h"

using namespace std;
using namespace rapidjson;

typedef sol_rules::build_policy pol;
typedef sol_rules::stock_deal_type sdt;
typedef sol_rules::face_up_policy fu;
typedef sol_rules::foundations_init_type fit;

template <typename Policy>
void deal_parser::parse(game_state_impl<Policy> &gs, const rapidjson::Document& doc) {
    // There are two stages to reading in the supplied deal. It is both put
    // through the deal schema validator, and also parsed into
    // the game state object (with some further checking done). The parsing is
    // done first because the errors it detects tend to give clearer error
    // messages than the schema validator

    // Construct tableau piles
    if (gs.rules.tableau_pile_count > 0) {
        parse_tableau_piles(gs, doc);
    }

    // Construct hole card
    if (gs.rules.hole) {
        parse_hole(gs, doc);
    }

    // Construct filled cells
    if (gs.rules.cells) {
        parse_cells(gs, doc);
    }

    // Construct stock
    if (gs.rules.stock_size > 0) {
        parse_stock(gs, doc);
    }

    // Construct waste
    if (gs.rules.stock_deal_t == sdt::WASTE) {
        parse_waste(gs, doc);
    }

    // Construct reserve
    if (gs.rules.reserve_size > 0) {
        parse_reserve(gs, doc);
    }

    // Construct sequences
    if (gs.rules.sequence_count > 0) {
        parse_sequences(gs, doc);
    }

    // Construct accordion
    if (gs.rules.accordion_size > 0) {
        parse_accordion(gs, doc);
    }

    // If the foundations begin with cards in them, fill them
    if (gs.rules.foundations_present) {
        bool supplied_foundations = parse_foundations(gs, doc);

        if (!supplied_foundations && gs.rules.foundations_init_cards == fit::ALL) {
            fill_foundations(gs);
        }
    }

    Document d;
    d.Parse(deal_schema_json().c_str());
    assert(!d.HasParseError());
    SchemaDocument sd(d);
    SchemaValidator validator(sd);
    if(!doc.Accept(validator)) {
        throw runtime_error(json_helper::schema_err_str(validator));
    }
}

string deal_parser::deal_schema_json() {
    string schema_json = R"(
{
  "$schema": "http://json-schema.org/draft-04/schema#",
  "description": "JSON Schema representing a solitaire game state to solve",

  "definitions": {
    "card": {"type": "string", "pattern": "^(([0-9]|1[0-3]|a|A|j|J|q|Q|k|K)(c|C|d|D|h|H|s|S))$"},
    "cardarray": {
      "type": "array", "items": {"$ref": "#/definitions/card"}
    },
    "cardoremp": {
      "oneOf": [{"$ref": "#/definitions/card"}, {"enum": [""]}]
    },
    "cardarraywithempty": {
      "type": "array", "items": {
        "anyOf": [{"$ref": "#/definitions/card"}, {"enum": ["AS", ""]}]
      }
    }
  },

  "type": "object", "properties": {

    "tableau piles": {
      "type": "array", "items": {"$ref": "#/definitions/cardarray"}
    },
    "foundations": {"$ref": "#/definitions/cardarray"},
    "cells": {
      "type": "array", "items": {"$ref": "#/definitions/cardoremp"}
    },
    "sequences": {
      "type": "array", "items": {"$ref": "#/definitions/cardarraywithempty"}
    },
    "hole": {"$ref": "#/definitions/card"},
    "stock": {"$ref": "#/definitions/cardarray"},
    "waste": {"$ref": "#/definitions/cardarray"},
    "reserve": {
      "type": "array", "items": {"$ref": "#/definitions/cardoremp"}
    },
    "accordion": {
      "type": "array", "items": {"$ref": "#/definitions/cardoremp"}
    }

  }, "additionalProperties": false
}
)";
    return schema_json;
}

template <typename Policy>
void deal_parser::parse_tableau_piles(game_state_impl<Policy> &gs, const rapidjson::Document& doc) {
    assert(doc.HasMember("tableau piles"));
    const Value& json_tab_piles = doc["tableau piles"];
    assert(json_tab_piles.IsArray());

    if (json_tab_piles.Size() != gs.rules.tableau_pile_count ) {
        json_helper::json_parse_err("Incorrect number of tableau piles");
    }

    for (auto p = std::make_pair(begin(json_tab_piles.GetArray()), begin(gs.original_tableau_piles));
         p.second != end(gs.original_tableau_piles);
         ++p.first, ++p.second) {

        for (auto& json_card : p.first->GetArray()) {
            assert(json_card.IsString());
            gs.place_card(*p.second, card(json_card.GetString(), gs.rules.face_up != fu::ALL));
        }
    }
}

template <typename Policy>
void deal_parser::parse_hole(game_state_impl<Policy> &gs, const Document& doc) {
    if (doc.HasMember("hole")) {
        const Value &json_hole = doc["hole"];
        assert(json_hole.IsString());

        gs.place_card(gs.hole, card(json_hole.GetString()));
    }
}

template <typename Policy>
void deal_parser::parse_cells(game_state_impl<Policy> &gs, const Document& doc) {
    if (doc.HasMember("cells")) {
        const Value &json_cells = doc["cells"];
        assert(json_cells.IsArray());

        const auto json_cell_arr = json_cells.GetArray();

        if (json_cell_arr.Empty()) return;
        else if (json_cell_arr.Size() != gs.rules.cells) {
            json_helper::json_parse_err("Incorrect number of cells");
        }

        for (auto p = std::make_pair(begin(json_cell_arr), begin(gs.original_cells));
             p.second != end(gs.original_cells);
             ++p.first, ++p.second) {

            auto json_card = p.first;
            assert(json_card->IsString());
            string card_str = json_card->GetString();
            if (!card_str.empty()) {
                gs.place_card(*p.second, card(card_str.c_str()));
            }
        }
    }
}

template <typename Policy>
void deal_parser::parse_stock(game_state_impl<Policy> &gs, const Document& doc) {
    if (doc.HasMember("stock")) {
        const Value &json_stock = doc["stock"];
        assert(json_stock.IsArray());

        for (const Value& json_card : json_stock.GetArray()) {
            assert(json_card.IsString());
            gs.place_card(gs.stock, card(json_card.GetString()));
        }
    }
}

template <typename Policy>
void deal_parser::parse_waste(game_state_impl<Policy> &gs, const Document& doc) {
    if (doc.HasMember("waste")) {
        const Value &json_waste = doc["waste"];
        assert(json_waste.IsArray());

        for (const Value& json_card : json_waste.GetArray()) {
            assert(json_card.IsString());
            gs.place_card(gs.waste, card(json_card.GetString()));
        }
    }
}

template <typename Policy>
void deal_parser::parse_reserve(game_state_impl<Policy> &gs, const Document& doc) {
    assert(doc.HasMember("reserve"));
    const Value& json_reserve_piles = doc["reserve"];
    assert(json_reserve_piles.IsArray());

    const auto& json_card_arr = json_reserve_piles.GetArray();

    // We treat a regular reserve like multiple single-card piles,
    // but a stacked reserve as a single multiple-card pile
    for (pile::ref i = 0; i < json_card_arr.Size(); i++) {
        assert(json_card_arr[i].IsString());
        string card_str = json_card_arr[i].GetString();
        if (card_str.empty()) continue;
        pile::ref pr = gs.original_reserve[0];
        if (!gs.rules.reserve_stacked) pr += i;
        gs.place_card(pr, card(card_str.c_str()));
    }
}

template <typename Policy>
void deal_parser::parse_sequences(game_state_impl<Policy>& gs, const rapidjson::Document& doc) {
    assert(doc.HasMember("sequences"));
    const Value& json_seqs = doc["sequences"];
    assert(json_seqs.IsArray());

    if (json_seqs.Size() != gs.rules.sequence_count ) {
        json_helper::json_parse_err("Incorrect number of sequences");
    }

    for (auto p = std::make_pair(begin(json_seqs.GetArray()), begin(gs.sequences));
         p.second != end(gs.sequences);
         ++p.first, ++p.second) {

        for (auto& json_card : p.first->GetArray()) {
            assert(json_card.IsString());
            string card_str = json_card.GetString();
            card c = card_str.empty() ? "AS" : card(json_card.GetString());
            gs.place_card(*p.second, c);
        }
    }
}

template <typename Policy>
void deal_parser::parse_accordion(game_state_impl<Policy> &gs, const Document& doc) {
    assert(doc.HasMember("accordion"));
    const Value& json_accordion_piles = doc["accordion"];
    assert(json_accordion_piles.IsArray());

    const auto& json_card_arr = json_accordion_piles.GetArray();

    // We treat a regular reserve like multiple single-card piles,
    // but a stacked reserve as a single multiple-card pile
    if (json_card_arr.Size() != gs.rules.accordion_size) {
        json_helper::json_parse_err("Incorrect accordion size");
    }

    auto acc_it = begin(gs.accordion);
    for (pile::ref i = 0; i < json_card_arr.Size(); i++, acc_it++) {
        assert(json_card_arr[i].IsString());
        string card_str = json_card_arr[i].GetString();
        if (card_str.empty()) continue;
        pile::ref pr = *acc_it;
        gs.place_card(pr, card(card_str.c_str()));
    }
}

template <typename Policy>
bool deal_parser::parse_foundations(game_state_impl<Policy> &gs, const rapidjson::Document& doc) {
    if (!doc.HasMember("foundations")) return false;

    const Value& json_foundations = doc["foundations"];
    assert(json_foundations.IsArray());

    card c;
    for (auto j = begin(json_foundations.GetArray()); j != end(json_foundations.GetArray()); j++) {
        assert(j->IsString());
        c = card(j->GetString());
        gs.place_card(gs.foundations[c.get_suit()], c);
    }

    // If the game uses a random base for foundations, assume that the first card in the first foundation is that base
    if (!gs.rules.foundations_base) {
        auto& first_found = gs.piles[gs.foundations[c.get_suit()]];
        gs.foundations_base = first_found[first_found.size() - 1].get_rank();
    }

    return true;
}

template <typename Policy>
void deal_parser::fill_foundations(game_state_impl<Policy> &gs) {
    auto foundations_count = uint8_t(4 * (gs.rules.two_decks ? 2:1));
    assert(gs.foundations.size() == foundations_count);

    for (uint8_t f_idx = 0; f_idx < foundations_count; f_idx++) {
        gs.place_card(gs.foundations[f_idx], card(f_idx % uint8_t(4), 1));
    }
}

// ─── Explicit instantiations ──────────────────────────────────────────────────

#if defined(SOLVITAIRE_LRU_ONLY)
template void deal_parser::parse<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_tableau_piles<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_hole<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_cells<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_stock<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_waste<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_reserve<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_sequences<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_accordion<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template bool deal_parser::parse_foundations<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::fill_foundations<LRUPolicy>(game_state_impl<LRUPolicy>&);

#elif defined(SOLVITAIRE_FLAT_ONLY)
template void deal_parser::parse<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_tableau_piles<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_hole<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_cells<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_stock<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_waste<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_reserve<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_sequences<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_accordion<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template bool deal_parser::parse_foundations<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::fill_foundations<FlatPolicy>(game_state_impl<FlatPolicy>&);
template void deal_parser::parse<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_tableau_piles<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_hole<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_cells<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_stock<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_waste<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_reserve<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_sequences<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_accordion<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template bool deal_parser::parse_foundations<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::fill_foundations<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&);

#elif defined(SOLVITAIRE_HASH_ONLY)
template void deal_parser::parse<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_tableau_piles<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_hole<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_cells<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_stock<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_waste<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_reserve<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_sequences<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_accordion<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template bool deal_parser::parse_foundations<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::fill_foundations<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&);

#else   // default binary — all four policies
template void deal_parser::parse<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_tableau_piles<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_hole<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_cells<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_stock<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_waste<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_reserve<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_sequences<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_accordion<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template bool deal_parser::parse_foundations<FlatPolicy>(game_state_impl<FlatPolicy>&, const rapidjson::Document&);
template void deal_parser::fill_foundations<FlatPolicy>(game_state_impl<FlatPolicy>&);
template void deal_parser::parse<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_tableau_piles<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_hole<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_cells<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_stock<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_waste<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_reserve<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_sequences<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_accordion<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template bool deal_parser::parse_foundations<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&, const rapidjson::Document&);
template void deal_parser::fill_foundations<HashOnlyPolicy>(game_state_impl<HashOnlyPolicy>&);
template void deal_parser::parse<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_tableau_piles<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_hole<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_cells<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_stock<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_waste<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_reserve<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_sequences<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_accordion<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template bool deal_parser::parse_foundations<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&, const rapidjson::Document&);
template void deal_parser::fill_foundations<PredecessorPolicy>(game_state_impl<PredecessorPolicy>&);
template void deal_parser::parse<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_tableau_piles<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_hole<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_cells<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_stock<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_waste<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_reserve<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_sequences<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_accordion<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template bool deal_parser::parse_foundations<LRUPolicy>(game_state_impl<LRUPolicy>&, const rapidjson::Document&);
template void deal_parser::fill_foundations<LRUPolicy>(game_state_impl<LRUPolicy>&);
template void deal_parser::parse<MultiplicityPolicy>(game_state_impl<MultiplicityPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_tableau_piles<MultiplicityPolicy>(game_state_impl<MultiplicityPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_hole<MultiplicityPolicy>(game_state_impl<MultiplicityPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_cells<MultiplicityPolicy>(game_state_impl<MultiplicityPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_stock<MultiplicityPolicy>(game_state_impl<MultiplicityPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_waste<MultiplicityPolicy>(game_state_impl<MultiplicityPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_reserve<MultiplicityPolicy>(game_state_impl<MultiplicityPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_sequences<MultiplicityPolicy>(game_state_impl<MultiplicityPolicy>&, const rapidjson::Document&);
template void deal_parser::parse_accordion<MultiplicityPolicy>(game_state_impl<MultiplicityPolicy>&, const rapidjson::Document&);
template bool deal_parser::parse_foundations<MultiplicityPolicy>(game_state_impl<MultiplicityPolicy>&, const rapidjson::Document&);
template void deal_parser::fill_foundations<MultiplicityPolicy>(game_state_impl<MultiplicityPolicy>&);
#endif
