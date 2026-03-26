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

#include <vector>
#include <ostream>
#include <algorithm>
#include <functional>

#include <boost/functional/hash.hpp>
#include <boost/optional.hpp>
#include <boost/random.hpp>

#include "game_state.h"
#include "document.h"
#include "../../input-output/input/json-parsing/deal_parser.h"
#include "../../input-output/output/state_printer.h"
#include "../../input-output/output/log_helper.h"
#include "../move.h"
#include "../sol_rules.h"

using namespace rapidjson;
using std::vector;
using std::list;
using std::find;
using std::begin;
using std::end;
using std::rbegin;
using std::rend;
using std::runtime_error;
using std::max;
using std::ostream;
using std::mt19937;
using std::string;

typedef sol_rules::build_policy pol;
typedef sol_rules::stock_deal_type sdt;
typedef sol_rules::face_up_policy fu;
typedef game_state::streamliner_options sos;
typedef sol_rules::foundations_init_type fit;

//////////////////
// CONSTRUCTORS //
//////////////////

// A private constructor used by both of the public ones. Initializes all of the
// piles and pile refs specified by the rules
game_state::game_state(const sol_rules& s_rules, streamliner_options stream_opts_)
        : rules(s_rules)
        , stream_opts(stream_opts_)
        , foundations_base(card::rank_t(1))
        , stock(255)
        , waste(255)
        , hole (255) {
    // If there is a hole, creates pile
    if (rules.hole) {
        piles.emplace_back();
        hole = static_cast<pile::ref>(piles.size() - 1);
    }

    // If there are foundation piles, creates the relevant pile vectors
    if (rules.foundations_present) {
        for (uint8_t i = 0; i < 4*(rules.two_decks ? 2:1); i++) {
            piles.emplace_back();
            foundations.push_back(static_cast<pile::ref>(piles.size() - 1));
        }
    }

    // If there are cell piles, creates the relevant cell vectors
    if (rules.cells > 0) {
        for (uint8_t i = 0; i < rules.cells; i++) {
            piles.emplace_back();
            cells.push_back(static_cast<pile::ref>(piles.size() - 1));
            original_cells.push_back(static_cast<pile::ref>(piles.size() - 1));
        }
    }

    // If there is a stock, creates a pile
    if (rules.stock_size > 0) {
        piles.emplace_back();
        stock = static_cast<pile::ref>(piles.size() - 1);

        // If the stock deals to a waste, create the waste
        if (rules.stock_deal_t == sdt::WASTE) {
            piles.emplace_back();
            waste = static_cast<pile::ref>(piles.size() - 1);
        }
    }

    // If there is a reserve, creates piles
    if (rules.reserve_size > 0) {
        uint8_t pile_count = rules.reserve_stacked ?
                             uint8_t(1) : rules.reserve_size;
        for (uint8_t i = 0; i < pile_count; i++) {
            piles.emplace_back();
            reserve.push_back(static_cast<pile::ref>(piles.size() - 1));
            original_reserve.push_back(static_cast<pile::ref>(piles.size() - 1));
        }
    }

    // If there is a reserve, creates piles
    if (rules.accordion_size > 0) {
        uint8_t pile_count = rules.accordion_size;
        for (uint8_t i = 0; i < pile_count; i++) {
            piles.emplace_back();
            accordion.push_back(static_cast<pile::ref>(piles.size() - 1));
        }
    }

    // Creates the tableau piles
    for (uint8_t i = 0; i < rules.tableau_pile_count; i++) {
        piles.emplace_back();
        tableau_piles.push_back(static_cast<pile::ref>(piles.size() - 1));
        original_tableau_piles.push_back(static_cast<pile::ref>(piles.size() - 1));
    }

    // Creates the sequence piles
    for (uint8_t i = 0; i < rules.sequence_count; i++) {
        piles.emplace_back();
        sequences.push_back(static_cast<pile::ref>(piles.size() - 1));
    }

    // Initialize Zobrist hash and payload to zero (will be filled by subclasses)
    zobrist_hash_value = 0;
}

// Constructs an initial game state from a JSON doc
game_state::game_state(const sol_rules& s_rules, const Document& doc, streamliner_options s_opts)
        : game_state(s_rules, s_opts) {
    deal_parser::parse(*this, doc);
    init_payload_and_hash();
}

// Constructs an initial game state from a seed
game_state::game_state(const sol_rules& s_rules, int seed, streamliner_options s_opts)
        : game_state(s_rules, s_opts) {
    auto rng = mt19937(seed);
    vector<card> deck = gen_shuffled_deck(rules.max_rank, rules.two_decks, rng);

    if (rules.hole) {
        if(!rules.hole_base) { 	// random base card selected
            card base_card = deck.front();
            deck.erase(find(begin(deck), end(deck), base_card));
            place_card(hole, base_card);
	} else { 
	    // Note following line introduces slight bias if there is  more than one deck.
            deck.erase(find(begin(deck), end(deck), card(rules.hole_base->c_str())));
            place_card(hole, card(rules.hole_base->c_str()));
	}
    }




    // If the foundation base is random, picks the first card in the shuffled deck to be the base
    card::suit_t rand_suit = 0;
    if (!rules.foundations_base) { 
	card base_card = deck.front();
        foundations_base = base_card.get_rank();
        rand_suit = base_card.get_suit();
    }

    // If the foundations begin filled, then fills them
    uint8_t founds_to_fill = 0;
    switch (rules.foundations_init_cards) {
        case sol_rules::foundations_init_type::NONE:
            break;
        case sol_rules::foundations_init_type::ONE:
            founds_to_fill = 1; break;
        case sol_rules::foundations_init_type::ALL:
            founds_to_fill = static_cast<uint8_t>(4 * (rules.two_decks ? 2 : 1)); break;
    }
    for (uint8_t f_idx = 0; f_idx < founds_to_fill; f_idx++) {
        card c = card((f_idx + rand_suit) % uint8_t(4), foundations_base);

        deck.erase(find(begin(deck), end(deck), c));
        place_card(foundations[(f_idx + rand_suit)], c);
    }

    // If there are pre-filled cells, deals to them
    auto it = begin(original_cells);
    for (uint8_t i = 0; i < rules.cells_pre_filled; i++) {
        pile::ref r = *it;
        place_card(r, deck.back());
        deck.pop_back();
        it++;
    }

    // If there is a stock, deals to it and set up a waste pile too
    if (rules.stock_size > 0) {
        for (unsigned int i = 0; i < rules.stock_size; i++) {
            place_card(stock, deck.back());
            deck.pop_back();
        }
    }

    // If there is a reserve, deals to it.
    // We treat a regular reserve like multiple single-card piles,
    // but a stacked reserve as a single multiple-card pile.
    if (rules.reserve_size > 0) {
        for (unsigned int i = 0; i < rules.reserve_size; i++) {
            int idx = rules.reserve_stacked ? 0 : i;
            place_card(original_reserve[idx], deck.back());
            deck.pop_back();
        }
    }

    // Deals to the sequence piles
    if (rules.sequence_count > 0) {
        for (int t = 0; !deck.empty(); t++) {
            // Adds the randomly generated card to the tableau piles
            auto s = t % sequences.size();

            card c = deck.back();
            if (c.get_rank() == 1) c = "AS"; // The dummy "gap" placeholder

            pile::ref seq_pile = sequences[s];
            place_card(seq_pile, c);
            deck.pop_back();
        }
    }

    // Deals to the accordion
    if (rules.accordion_size > 0) {
        for (auto pr : accordion) {
            place_card(pr, deck.back());
            deck.pop_back();
        }
    }

    // This only occurs during testing
    if (rules.tableau_pile_count == 0) return;

    // Deals to the tableau piles (row-by-row)
    for (int t = 0; !deck.empty(); t++) {
        card c = deck.back();

        // If only the top cards are face up, initially deals all face down
        if (rules.face_up == fu::TOP_CARDS) c.turn_face_down();

        // Adds the randomly generated card to the tableau piles
        auto p = t % original_tableau_piles.size();

        // If we are doing a diagonal deal, each row should have one fewer card.
        // Leftover cards are dealt normally in full rows.
        auto row_idx = t / original_tableau_piles.size();
        if (rules.diagonal_deal && row_idx < original_tableau_piles.size()) {
            p = original_tableau_piles.size()-p-1;
            pile::ref tableau_pile = original_tableau_piles[p];

            if (p >= row_idx) {
                place_card(tableau_pile, c);
                deck.pop_back();
            }
        } else {
            pile::ref tableau_pile = original_tableau_piles[p];
            place_card(tableau_pile, c);
            deck.pop_back();
        }
    }

    // Now if necessary, turns the top cards face up
    if (rules.face_up == fu::TOP_CARDS)
        for (pile::ref pr = 0; pr < static_cast<pile::ref>(piles.size()); ++pr)
            if (!piles[pr].empty()) {
                piles[pr][0].turn_face_up();
            }

    // The size of all piles must equal the deck size
    int piles_sz = 0;
    for (auto& p : piles) piles_sz += p.size();
    if (piles_sz != rules.max_rank * (rules.two_decks ? 8:4)) {
        throw runtime_error("Error: incorrect number of cards in starting piles");
    }

    init_payload_and_hash();
}

game_state::game_state(const sol_rules& s_rules,
                       std::initializer_list<std::initializer_list<string>> il)
        : game_state(s_rules, sos::NONE) {pile::ref pr = 0;
    for (auto& p_il : il) {
        for (const string& card_str : p_il) {
            card c(card_str.c_str(), s_rules.face_up == fu::TOP_CARDS);
            place_card(pr, c);
        }
        pr++;
    }

    // Sets foundations base appropriately
    if (rules.foundations_present && rules.foundations_base == boost::none) {
        for (auto f : foundations) {
            if (!piles[f].empty()) {
                foundations_base = piles[f].top_card().get_rank();
                break;
            }
        }
    }

    init_payload_and_hash();
}

// Generates a randomly ordered vector of cards
vector<card> game_state::gen_shuffled_deck(card::rank_t max_rank,
                                           bool two_decks, mt19937 rng) {
    vector<card> deck;

    for (int deck_count = 1; deck_count <= (two_decks ? 2 : 1); deck_count++) {
        for (card::rank_t rank = 1; rank <= max_rank; rank++) {
            for (card::suit_t suit = 0 ; suit < 4; suit++) {
                deck.emplace_back(suit, rank);
            }
        }
    }

    assert(deck.size() == pile::size_type(max_rank * (two_decks ? 8 : 4)));

    game_state::shuffle(begin(deck), end(deck), rng);
    return deck;
}

template<class RandomIt, class URBG>
void game_state::shuffle(RandomIt first, RandomIt last, URBG&& g) {
    typedef typename std::iterator_traits<RandomIt>::difference_type diff_t;
    typedef boost::random::uniform_int_distribution<diff_t> distr_t;
    typedef typename distr_t::param_type param_t;

    distr_t D;
    diff_t n = last - first;
    for (diff_t i = n-1; i > 0; --i) {
        using std::swap;
        swap(first[i], first[D(g, param_t(0, i))]);
    }
}



////////////////////
// ALTERING STATE //
////////////////////

void game_state::make_move(const move m) {
    switch (m.type) {
        case move::mtype::regular:
	    make_regular_move(m); 
            break;
        case move::mtype::built_group:
            make_built_group_move(m);
            break;
        case move::mtype::stock_k_plus:
            make_stock_k_plus_move(m);
            break;
        case move::mtype::stock_to_all_tableau:
            make_stock_to_all_tableau_move(m);
            break;
        case move::mtype::sequence:
            make_sequence_move(m);
            break;
        case move::mtype::accordion:
            make_accordion_move(m);
            break;
        case move::mtype::null:
            assert(false);
            break;
    }

#ifndef NDEBUG
    check_face_down_consistent();
#endif
}

void game_state::undo_move(const move m) {
    switch (m.type) {
        case move::mtype::regular:
	    undo_regular_move(m); 
            break;
        case move::mtype::built_group:
            undo_built_group_move(m);
            break;
        case move::mtype::stock_k_plus:
            undo_stock_k_plus_move(m);
            break;
        case move::mtype::stock_to_all_tableau:
            undo_stock_to_all_tableau_move(m);
            break;
        case move::mtype::sequence:
            undo_sequence_move(m);
            break;
        case move::mtype::accordion:
            undo_accordion_move(m);
            break;
        case move::mtype::null:
            assert(false);
            break;
    }

#ifndef NDEBUG
    check_face_down_consistent();
#endif
}

void game_state::make_regular_move(const move m) {
    assert(m.from < piles.size());
    assert(m.to   < piles.size());

    // Capture pre-move state for descriptor updates
    card moved = piles[m.from].top_card();
    uint8_t cid = zobrist_hash::card_id(moved.get_suit(), moved.get_rank());
    uint8_t old_desc = payload.get_descriptor(cid);

    uint8_t from_fs = 255, old_from_fr = 255;
    if (is_foundation_pile(m.from)) {
        from_fs = get_foundation_suit(m.from);
        old_from_fr = payload.get_foundation(from_fs);
    }
    uint8_t to_fs = 255, old_to_fr = 255;
    if (is_foundation_pile(m.to)) {
        to_fs = get_foundation_suit(m.to);
        old_to_fr = payload.get_foundation(to_fs);
    }
    uint8_t old_ht = 255;
    if (m.to == hole) {
        old_ht = payload.get_hole_top();
    }

    // Pile operations
    place_card(m.to, take_card(m.from));

    // Update moved card's descriptor
    uint8_t new_desc = determine_destination_descriptor(m.to, moved);
    update_card_descriptor(cid, new_desc);

    // Update foundation headers
    if (from_fs != 255) {
        uint8_t new_rank = piles[m.from].empty()
            ? uint8_t(0) : piles[m.from].top_card().get_rank();
        update_foundation_in_hash(from_fs, new_rank);
    }
    if (to_fs != 255) {
        update_foundation_in_hash(to_fs, moved.get_rank());
    }

    // Update hole header
    if (old_ht != 255) {
        update_hole_top_in_hash(cid);
    }

    // Handle reveal
    uint8_t rev_cid = 255;
    if (m.reveal_move) {
        assert(!piles[m.from].empty());
        assert(piles[m.from][0].is_face_down());
        piles[m.from][0].turn_face_up();
        card rev = piles[m.from][0];
        rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
        // Bottom of pile → IN_SPACE; otherwise STARTING_FACE_UP
        uint8_t rev_desc = (piles[m.from].size() == 1)
            ? compact_state::IN_SPACE
            : compact_state::STARTING_FACE_UP;
        update_card_descriptor(rev_cid, rev_desc);
    }

    // Push undo info
    zobrist_undo undo = {};
    undo.card_id = cid;
    undo.old_desc = old_desc;
    undo.revealed_card_id = rev_cid;
    undo.from_found_suit = from_fs;
    undo.old_from_found_rank = old_from_fr;
    undo.to_found_suit = to_fs;
    undo.old_to_found_rank = old_to_fr;
    undo.old_hole_top = old_ht;
    undo.old_waste_ptr = 255;
    undo.sat_count = 0;
    zobrist_undo_stack.push_back(undo);
}

void game_state::undo_regular_move(const move m) {
    assert(m.to < piles.size());

    // Pop undo info
    zobrist_undo undo = zobrist_undo_stack.back();
    zobrist_undo_stack.pop_back();

    // Undo reveal descriptor
    if (m.reveal_move) {
        assert(!piles[m.from].empty());
        assert(!piles[m.from][0].is_face_down());
        update_card_descriptor(undo.revealed_card_id, compact_state::STARTING);
        piles[m.from][0].turn_face_down();
    }

    // Undo hole header
    if (undo.old_hole_top != 255) {
        update_hole_top_in_hash(undo.old_hole_top);
    }

    // Undo destination foundation header
    if (undo.to_found_suit != 255) {
        update_foundation_in_hash(undo.to_found_suit, undo.old_to_found_rank);
    }

    // Undo source foundation header
    if (undo.from_found_suit != 255) {
        update_foundation_in_hash(undo.from_found_suit, undo.old_from_found_rank);
    }

    // Restore moved card's descriptor
    update_card_descriptor(undo.card_id, undo.old_desc);

    // Pile operations
    place_card(m.from, take_card(m.to));
}

void game_state::make_built_group_move(move m) {
    assert(m.from  <  piles.size()  );
    assert(m.to    <  piles.size()  );

    // Capture bottom card of group (the one whose descriptor changes)
    card bottom = piles[m.from][m.count - 1];
    uint8_t bottom_cid = zobrist_hash::card_id(bottom.get_suit(), bottom.get_rank());
    uint8_t old_desc = payload.get_descriptor(bottom_cid);

    // Adds the cards to the 'to' pile
    for (auto pile_idx = m.count; pile_idx-- > 0;) {
        place_card(m.to, piles[m.from][pile_idx]);
    }

    // Removes the cards from the 'from' pile
    for (uint8_t rem_count = 0; rem_count < m.count; rem_count++) {
        take_card(m.from);
    }

    // Update bottom card's descriptor
    // After placement: bottom card is at piles[m.to][m.count - 1]
    // Parent (if any) is at piles[m.to][m.count]
    uint8_t new_desc;
    if (static_cast<pile::size_type>(piles[m.to].size()) == m.count) {
        new_desc = compact_state::IN_SPACE;
    } else {
        card parent_card = piles[m.to][m.count];
        uint8_t parent_cid = zobrist_hash::card_id(
            parent_card.get_suit(), parent_card.get_rank());
        uint8_t desc = parent_table::get_descriptor_for_parent(
            bottom_cid, parent_cid, rules.build_pol);
        new_desc = (desc != 0) ? desc : compact_state::ROOT;
    }
    update_card_descriptor(bottom_cid, new_desc);

    // Handle reveal
    uint8_t rev_cid = 255;
    if (m.reveal_move) {
        assert(!piles[m.from].empty());
        assert(piles[m.from][0].is_face_down());
        piles[m.from][0].turn_face_up();
        card rev = piles[m.from][0];
        rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
        // Bottom of pile → IN_SPACE; otherwise STARTING_FACE_UP
        uint8_t rev_desc = (piles[m.from].size() == 1)
            ? compact_state::IN_SPACE
            : compact_state::STARTING_FACE_UP;
        update_card_descriptor(rev_cid, rev_desc);
    }

    // Push undo
    zobrist_undo undo = {};
    undo.card_id = bottom_cid;
    undo.old_desc = old_desc;
    undo.revealed_card_id = rev_cid;
    undo.from_found_suit = 255;
    undo.old_from_found_rank = 255;
    undo.to_found_suit = 255;
    undo.old_to_found_rank = 255;
    undo.old_hole_top = 255;
    undo.old_waste_ptr = 255;
    undo.sat_count = 0;
    zobrist_undo_stack.push_back(undo);
}

void game_state::undo_built_group_move(move m) {
    assert(m.to < piles.size());

    // Pop undo info
    zobrist_undo undo = zobrist_undo_stack.back();
    zobrist_undo_stack.pop_back();

    // Undo reveal descriptor
    if (m.reveal_move) {
        assert(!piles[m.from].empty());
        assert(!piles[m.from][0].is_face_down());
        update_card_descriptor(undo.revealed_card_id, compact_state::STARTING);
        piles[m.from][0].turn_face_down();
    }

    // Restore bottom card's descriptor
    update_card_descriptor(undo.card_id, undo.old_desc);

    // Adds the cards to the 'from' pile
    for (auto pile_idx = m.count; pile_idx-- > 0;) {
        place_card(m.from, piles[m.to][pile_idx]);
    }

    // Removes the cards from the 'to' pile
    for (uint8_t rem_count = 0; rem_count < m.count; rem_count++) {
        take_card(m.to);
    }
}

void game_state::make_stock_k_plus_move(const move m) {
#ifndef NDEBUG
    assert(rules.stock_deal_t == sdt::WASTE);
    assert(m.from == stock);
    if (rules.stock_redeal) assert(m.count <= piles[stock].size() && m.count > -piles[waste].size());
    assert(!rules.stock_redeal || m.to != waste);
    auto sz_before = piles[stock].size() + piles[waste].size();
#endif

    // Capture pre-move state
    uint8_t old_waste_ptr = payload.get_waste_ptr();

    // Transfers count cards from the stock to the waste
    if (m.count > 0) {
        for (int i = 0; i < m.count; i++) {
            place_card(waste, take_card(stock));
        }
    } else {
        for (int i = 0; i > m.count; i--) {
            place_card(stock, take_card(waste));
        }
    }

    // Capture the card about to be played from waste
    card played = piles[waste].top_card();
    uint8_t played_cid = zobrist_hash::card_id(played.get_suit(), played.get_rank());
    uint8_t played_old_desc = payload.get_descriptor(played_cid);

    // Moves the card on top of the waste to the target pile
    place_card(m.to,  take_card(waste));

    // Flips the waste back on to the empty stock (if necessary)
    if (m.flip_waste) {
        assert(rules.stock_redeal && piles[stock].empty());
        while (!piles[waste].empty()) {
            place_card(stock, take_card(waste));
        }
    }

    // Update descriptor for played card
    uint8_t new_desc = determine_destination_descriptor(m.to, played);
    update_card_descriptor(played_cid, new_desc);

    // Update foundation/hole headers
    uint8_t to_fs = 255, old_to_fr = 255;
    if (is_foundation_pile(m.to)) {
        to_fs = get_foundation_suit(m.to);
        old_to_fr = payload.get_foundation(to_fs);
        update_foundation_in_hash(to_fs, played.get_rank());
    }
    uint8_t old_ht = 255;
    if (m.to == hole) {
        old_ht = payload.get_hole_top();
        update_hole_top_in_hash(played_cid);
    }

    // Update waste pointer to current waste size
    // (applying waste-deal symmetry if applicable)
    update_waste_ptr_in_hash(effective_waste_ptr());

    // Push undo
    zobrist_undo undo = {};
    undo.card_id = played_cid;
    undo.old_desc = played_old_desc;
    undo.revealed_card_id = 255;
    undo.from_found_suit = 255;
    undo.old_from_found_rank = 255;
    undo.to_found_suit = to_fs;
    undo.old_to_found_rank = old_to_fr;
    undo.old_hole_top = old_ht;
    undo.old_waste_ptr = old_waste_ptr;
    undo.sat_count = 0;
    zobrist_undo_stack.push_back(undo);

#ifndef NDEBUG
    auto sz_after = piles[stock].size() + piles[waste].size();
    assert(sz_before == sz_after + 1);
    assert(!(rules.stock_size > 0 && rules.stock_redeal && piles[stock].empty() && !piles[waste].empty()));
    assert(piles[stock].size() <= rules.stock_size);
#endif
}

void game_state::undo_stock_k_plus_move(move m) {
#ifndef NDEBUG
    assert(rules.stock_deal_t == sdt::WASTE);
    assert(m.from == stock);
    assert(!rules.stock_redeal || m.to != waste);
    auto sz_after = piles[stock].size() + piles[waste].size();
#endif

    // Pop undo info
    zobrist_undo undo = zobrist_undo_stack.back();
    zobrist_undo_stack.pop_back();

    // Restore waste pointer
    update_waste_ptr_in_hash(undo.old_waste_ptr);

    // Restore hole header
    if (undo.old_hole_top != 255) {
        update_hole_top_in_hash(undo.old_hole_top);
    }

    // Restore foundation header
    if (undo.to_found_suit != 255) {
        update_foundation_in_hash(undo.to_found_suit, undo.old_to_found_rank);
    }

    // Restore played card's descriptor
    update_card_descriptor(undo.card_id, undo.old_desc);

    // Pile operations
    if (m.flip_waste) {
        assert(rules.stock_redeal && piles[waste].empty());
        while (!piles[stock].empty()) {
            place_card(waste, take_card(stock));
        }
    }

    place_card(waste,  take_card(m.to));

    if (m.count > 0) {
        for (int i = 0; i < m.count; i++) {
            place_card(stock, take_card(waste));
        }
    } else {
        for (int i = 0; i > m.count; i--) {
            place_card(waste, take_card(stock));
        }
    }

#ifndef NDEBUG
    auto sz_before = piles[stock].size() + piles[waste].size();
    assert(sz_before == sz_after + 1);
    if (rules.stock_redeal) assert(m.count <= piles[stock].size() && m.count > -piles[waste].size());
    assert(!(rules.stock_size > 0 && rules.stock_redeal && piles[stock].empty() && !piles[waste].empty()));
    assert(piles[stock].size() <= rules.stock_size);
#endif
}

void game_state::make_stock_to_all_tableau_move(move m) {
    assert(rules.stock_deal_t == sdt::TABLEAU_PILES);

    for (pile::ref tab_pr = original_tableau_piles.front();
         tab_pr < pile::ref(original_tableau_piles.front() + m.count);
         tab_pr++) {
        // Capture card before dealing (it's on top of stock)
        card dealt = piles[stock].top_card();

        place_card(tab_pr, take_card(stock));

        // Update dealt card's descriptor: STARTING → destination descriptor
        uint8_t cid = zobrist_hash::card_id(dealt.get_suit(), dealt.get_rank());
        uint8_t new_desc = determine_destination_descriptor(tab_pr, dealt);
        update_card_descriptor(cid, new_desc);
    }

    // Push undo with sat_count for the undo path
    zobrist_undo undo = {};
    undo.card_id = 255;
    undo.old_desc = 0;
    undo.revealed_card_id = 255;
    undo.from_found_suit = 255;
    undo.old_from_found_rank = 255;
    undo.to_found_suit = 255;
    undo.old_to_found_rank = 255;
    undo.old_hole_top = 255;
    undo.old_waste_ptr = 255;
    undo.sat_count = m.count;
    zobrist_undo_stack.push_back(undo);
}

void game_state::undo_stock_to_all_tableau_move(move) {
    assert(rules.stock_deal_t == sdt::TABLEAU_PILES);

    // Pop undo info
    zobrist_undo undo = zobrist_undo_stack.back();
    zobrist_undo_stack.pop_back();

    // Restore each dealt card's descriptor back to STARTING before pile ops
    for (pile::ref tab_pr = original_tableau_piles.front() + undo.sat_count;
         tab_pr-- > original_tableau_piles.front();
            ) {
        card c = piles[tab_pr].top_card();
        uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
        update_card_descriptor(cid, compact_state::STARTING);

        place_card(stock, take_card(tab_pr));
    }
}

void game_state::make_sequence_move(const move m) {
    // Note: sequence moves directly modify pile_vec entries without going through
    // place_card/take_card, so the Zobrist hash is NOT updated. This is acceptable
    // because sequence-based games will not use the new Zobrist-based cache (M5).
    pile::ref from_seq_ref = m.from / rules.max_rank;
    pile::size_type from_card_idx = m.from % rules.max_rank;
    card from_card = piles[from_seq_ref][from_card_idx];
    piles[from_seq_ref][from_card_idx] = "AS";

    pile::ref to_seq_ref = m.to / rules.max_rank;
    pile::size_type to_card_idx = m.to % rules.max_rank;
    assert(piles[to_seq_ref][to_card_idx] == "AS");
    piles[to_seq_ref][to_card_idx] = from_card;
}

void game_state::undo_sequence_move(const move m) {
    pile::ref to_seq_ref = m.to / rules.max_rank;
    pile::size_type to_card_idx = m.to % rules.max_rank;
    card to_card = piles[to_seq_ref][to_card_idx];
    piles[to_seq_ref][to_card_idx] = "AS";

    pile::ref from_seq_ref = m.from / rules.max_rank;
    pile::size_type from_card_idx = m.from % rules.max_rank;
    assert(piles[from_seq_ref][from_card_idx] == "AS");
    piles[from_seq_ref][from_card_idx] = to_card;
}

void game_state::make_accordion_move(move m) {
    make_built_group_move(m);
    accordion.remove(m.from);
}

void game_state::undo_accordion_move(move m) {
    accordion.insert(upper_bound(begin(accordion), end(accordion), m.from), m.from);
    undo_built_group_move(m);
}

// Places a card on a pile and if it is on a tableau, cell or reserve pile,
// reorders the pile refs so that the largest pile is first
void game_state::place_card(pile::ref pr, card c) {
    piles[pr].place(c);

#ifndef NO_PILE_SYMMETRY
    // If the stock deals to the tableau piles, there is no pile symmetry
    if (rules.stock_size == 0 || rules.stock_deal_t != sdt::TABLEAU_PILES) {
        eval_pile_order(pr, true);
    }
#endif
}

// Same as above but for taking cards
card game_state::take_card(pile::ref pr) {
    card c = piles[pr].take();
#ifndef NO_PILE_SYMMETRY
    // If the stock deals to the tableau piles, there is no pile symmetry
    if (rules.stock_size == 0 || rules.stock_deal_t != sdt::TABLEAU_PILES) {
        eval_pile_order(pr, false);
    }
#endif
    return c;
}

#ifndef NDEBUG
void game_state::check_face_down_consistent() const {
    for (auto& p : original_tableau_piles) {
        if (piles[p].empty()) continue;
        // Makes sure the top cards of each tableau pile are face up
        assert(!piles[p].top_card().is_face_down());

        // Makes sure face down cards are never above face down ones
        bool seen_face_up = false;
        for (auto& c : piles[p].pile_vec) {
            seen_face_up = seen_face_up || !c.is_face_down();
            assert(!(c.is_face_down() && seen_face_up));
        }

        // Check that face-down cards have STARTING descriptor
        for (auto& c : piles[p].pile_vec) {
            if (c.is_face_down()) {
                uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
                uint8_t desc = payload.get_descriptor(cid);
                if (desc != compact_state::STARTING) {
                    std::cerr << "DESCRIPTOR BUG: face-down card " << (int)cid
                              << " has descriptor " << (int)desc
                              << " (expected STARTING=0)" << std::endl;
                    assert(false);
                }
            }
        }
    }
}
#endif

////////////////////////
// ZOBRIST HASHING    //
////////////////////////

void game_state::init_payload_and_hash() {
    payload.clear();
    zobrist_hash_value = 0;

    // All cards start with descriptor STARTING (0)
    // XOR in Z_card[c][0] for all 52 cards
    for (uint8_t c = 0; c < 52; ++c) {
        zobrist_hash_value ^= zobrist_hash::card_key(c, compact_state::STARTING);
    }

    // Foundation tops
    if (rules.foundations_present) {
        for (uint8_t s = 0; s < 4; ++s) {
            uint8_t top_rank = 0;
            if (s < foundations.size()) {
                top_rank = piles[foundations[s]].empty() ? 0 : piles[foundations[s]].top_card().get_rank();
            }
            payload.set_foundation(s, top_rank);
            zobrist_hash_value ^= zobrist_hash::foundation_key(s, top_rank);
        }
    }

    // Hole top
    if (rules.hole && !piles[hole].empty()) {
        card top = piles[hole].top_card();
        uint8_t cid = zobrist_hash::card_id(top.get_suit(), top.get_rank());
        payload.set_hole_top(cid);
        zobrist_hash_value ^= zobrist_hash::hole_top_key(cid);
    }

    // Waste pointer: initial position = size of waste pile
    // When waste-deal symmetry holds (redeal enabled and waste size divisible
    // by deal count), use 0 to match LRU's position-independent encoding.
    if (rules.stock_size > 0 && rules.stock_deal_t == sol_rules::stock_deal_type::WASTE) {
        uint8_t ptr = effective_waste_ptr();
        payload.set_waste_ptr(ptr);
        zobrist_hash_value ^= zobrist_hash::waste_key(ptr);
    }

    // Set positional descriptors for face-up tableau cards.
    // This must match what determine_destination_descriptor() would compute,
    // so that a card moved away and returned to the same position gets the
    // same descriptor as it had initially.
    for (auto tab_ref : original_tableau_piles) {
        const pile& p = piles[tab_ref];
        for (pile::size_type i = 0; i < p.size(); ++i) {
            card c = p[i];
            if (c.is_face_down()) continue;

            uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
            uint8_t new_desc;
            if (i == p.size() - 1) {
                // Bottom of pile
                new_desc = compact_state::IN_SPACE;
            } else {
                // Card below this one is p[i+1]
                card parent_card = p[i + 1];
                uint8_t parent_cid = zobrist_hash::card_id(
                    parent_card.get_suit(), parent_card.get_rank());
                uint8_t desc = parent_table::get_descriptor_for_parent(
                    cid, parent_cid, rules.build_pol);
                new_desc = (desc != 0) ? desc : compact_state::ROOT;
            }
            update_card_descriptor(cid, new_desc);
        }
    }

    // Pre-filled cells: IN_CELL
    for (auto c_ref : original_cells) {
        if (!piles[c_ref].empty()) {
            card c = piles[c_ref].top_card();
            uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
            update_card_descriptor(cid, compact_state::IN_CELL);
        }
    }

    // Hole cards
    if (rules.hole && !piles[hole].empty()) {
        for (pile::size_type i = 0; i < piles[hole].size(); ++i) {
            card c = piles[hole][i];
            uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
            update_card_descriptor(cid, compact_state::IN_HOLE);
        }
    }
}

void game_state::update_card_descriptor(uint8_t cid, uint8_t new_desc) {
    uint8_t old_desc = payload.get_descriptor(cid);
    payload.set_descriptor(cid, new_desc);
    zobrist_hash_value ^= zobrist_hash::card_key(cid, old_desc)
                        ^ zobrist_hash::card_key(cid, new_desc);
}

void game_state::update_foundation_in_hash(uint8_t suit, uint8_t new_rank) {
    uint8_t old_rank = payload.get_foundation(suit);
    zobrist_hash_value ^= zobrist_hash::foundation_key(suit, old_rank)
                        ^ zobrist_hash::foundation_key(suit, new_rank);
    payload.set_foundation(suit, new_rank);
}

uint8_t game_state::effective_waste_ptr() const {
    bool waste_deal_symmetry = rules.stock_redeal
        && piles[waste].size() % rules.stock_deal_count == 0;
    if (waste_deal_symmetry) {
        return 0;
    }
    return static_cast<uint8_t>(piles[waste].size());
}

void game_state::update_waste_ptr_in_hash(uint8_t new_ptr) {
    uint8_t old_ptr = payload.get_waste_ptr();
    zobrist_hash_value ^= zobrist_hash::waste_key(old_ptr)
                        ^ zobrist_hash::waste_key(new_ptr);
    payload.set_waste_ptr(new_ptr);
}

void game_state::update_hole_top_in_hash(uint8_t new_cid) {
    uint8_t old_cid = payload.get_hole_top();
    zobrist_hash_value ^= zobrist_hash::hole_top_key(old_cid)
                        ^ zobrist_hash::hole_top_key(new_cid);
    payload.set_hole_top(new_cid);
}

bool game_state::is_foundation_pile(pile::ref pr) const {
    return !foundations.empty()
        && pr >= foundations.front()
        && pr < foundations.front() + foundations.size();
}

uint8_t game_state::get_foundation_suit(pile::ref pr) const {
    return pr - foundations.front();
}

uint8_t game_state::determine_destination_descriptor(pile::ref dest, card moved_card) const {
    // Foundation: card descriptor set to STARTING (0)
    if (is_foundation_pile(dest)) {
        return compact_state::STARTING;
    }

    // Hole
    if (dest == hole) {
        return compact_state::IN_HOLE;
    }

    // Cell
    if (!original_cells.empty()
        && dest >= original_cells.front()
        && dest <= original_cells.back()) {
        return compact_state::IN_CELL;
    }

    // Tableau: ROOT if placed on empty pile, PARENT_i if placed on a parent card
    if (!original_tableau_piles.empty()) {
        pile::ref first_tab = original_tableau_piles.front();
        pile::ref last_tab = original_tableau_piles.back();
        if (dest >= first_tab && dest <= last_tab) {
            // After place_card, size==1 means the pile was empty before
            if (piles[dest].size() == 1) {
                return compact_state::IN_SPACE;
            }
            // Card below the moved card is the parent
            card parent_card = piles[dest][1];
            uint8_t moved_cid = zobrist_hash::card_id(
                moved_card.get_suit(), moved_card.get_rank());
            uint8_t parent_cid = zobrist_hash::card_id(
                parent_card.get_suit(), parent_card.get_rank());
            uint8_t desc = parent_table::get_descriptor_for_parent(
                moved_cid, parent_cid, rules.build_pol);
            if (desc != 0) return desc;
            // Non-legal-build parent below — ROOT
            return compact_state::ROOT;
        }
    }

    // Reserve, stock, waste: keep STARTING
    return compact_state::STARTING;
}

void game_state::set_payload_depth(uint16_t depth) {
    payload.set_depth(depth);
}

void game_state::compute_hash_from_scratch() {
    // Recompute hash from the payload's current descriptor values
    zobrist_hash_value = 0;
    for (uint8_t c = 0; c < 52; ++c) {
        zobrist_hash_value ^= zobrist_hash::card_key(c, payload.get_descriptor(c));
    }

    // Foundation contributions
    if (rules.foundations_present) {
        for (uint8_t s = 0; s < 4; ++s) {
            zobrist_hash_value ^= zobrist_hash::foundation_key(s, payload.get_foundation(s));
        }
    }

    // Hole top contribution
    if (rules.hole) {
        zobrist_hash_value ^= zobrist_hash::hole_top_key(payload.get_hole_top());
    }

    // Waste pointer contribution (only for games with stock+waste)
    if (rules.stock_size > 0 && rules.stock_deal_t == sol_rules::stock_deal_type::WASTE) {
        zobrist_hash_value ^= zobrist_hash::waste_key(payload.get_waste_ptr());
    }
}

////////////////////////
// INSPECT GAME STATE //
////////////////////////

bool game_state::is_solved() const {
    bool solved = true;
    if (rules.hole) {
        solved = piles[hole].size()
                 == rules.max_rank * 4 * (rules.two_decks ? 2 : 1);
    } else if (rules.foundations_present) {
        for (auto f : foundations) {
            if (piles[f].size() != rules.max_rank) {
                solved = false;
            }
        }
    } else if (rules.sequence_count > 0) {
        for (pile::ref i = 0; i < sequences.size() && solved; i++) {
            for (pile::ref j = piles[sequences[i]].size(); j-- > 2;) {
                if (!is_next_legal_card(rules.sequence_build_pol, piles[sequences[i]][j-1], piles[sequences[i]][j])) {
                    solved = false;
                    break;
                }
            }
            if (piles[sequences[i]].top_card() != "AS") solved = false;
        }
    } else if (rules.accordion_size > 0) {
        return accordion.size() == 1;
    } else {
        assert(false);
    }

    // Runs some alternative checks in debug mode to make sure the game state
    // is consistent
#ifndef NDEBUG
    bool rest_empty = true;
    for (pile::ref pr = 0; pr < piles.size(); pr++) {
        // All piles other than the hole must be empty
        if (rules.hole) {
            if (pr != hole && !piles[pr].empty()) {
                rest_empty = false;
                break;
            }
        }
            // All piles other than the foundations must be empty
        else if (rules.foundations_present) {
            bool non_foundation_ref =
                    pr < foundations.front()
                    || pr >= foundations.front() + foundations.size();
            if (non_foundation_ref && !piles[pr].empty()) {
                rest_empty = false;
                break;
            }
        }
    }
    if (rules.sequence_count > 0) {
        for (auto seq : sequences) {
            assert(piles[seq].size() == rules.max_rank);
        }
    } else {
        assert(solved == rest_empty);
    }
#endif

    return solved;
}

const std::vector<pile>& game_state::get_data() const {
    return piles;
}

#ifndef NDEBUG
compact_state game_state::recompute_payload_from_scratch() const {
    compact_state cp;
    cp.clear();

    // 1. Foundations
    if (rules.foundations_present) {
        for (uint8_t s = 0; s < 4; ++s) {
            uint8_t top_rank = 0;
            if (s < foundations.size()) {
                top_rank = piles[foundations[s]].empty() ? 0 : piles[foundations[s]].top_card().get_rank();
            }
            cp.set_foundation(s, top_rank);
        }
    }

    // 2. Hole
    if (rules.hole && !piles[hole].empty()) {
        card top = piles[hole].top_card();
        cp.set_hole_top(zobrist_hash::card_id(top.get_suit(), top.get_rank()));
    }

    // 3. Waste Pointer
    if (rules.stock_size > 0 && rules.stock_deal_t == sol_rules::stock_deal_type::WASTE) {
        cp.set_waste_ptr(static_cast<uint8_t>(piles[waste].size()));
    }

    // 4. Card Descriptors
    for (pile::ref pr = 0; pr < piles.size(); pr++) {
        const pile& p = piles[pr];

        for (uint8_t i = 0; i < p.size(); ++i) {
            card c = p[i];
            uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());

            if (is_foundation_pile(pr)) {
                cp.set_descriptor(cid, compact_state::STARTING);
            } else if (!original_cells.empty() && pr >= original_cells.front() && pr <= original_cells.back()) {
                cp.set_descriptor(cid, compact_state::IN_CELL);
            } else if (pr == hole) {
                cp.set_descriptor(cid, compact_state::IN_HOLE);
            } else if (!original_tableau_piles.empty() && pr >= original_tableau_piles.front() && pr <= original_tableau_piles.back()) {
                if (i == p.size() - 1) { // Bottom of pile
                    if (p[i].is_face_down()) {
                        cp.set_descriptor(cid, compact_state::STARTING);
                    } else {
                        cp.set_descriptor(cid, compact_state::ROOT);
                    }
                } else {
                    card parent = p[i+1];
                    if (parent.is_face_down()) {
                        cp.set_descriptor(cid, compact_state::STARTING);
                    } else {
                        uint8_t parent_cid = zobrist_hash::card_id(parent.get_suit(), parent.get_rank());
                        uint8_t desc = parent_table::get_descriptor_for_parent(cid, parent_cid, rules.build_pol);
                        cp.set_descriptor(cid, (desc != 0) ? desc : compact_state::ROOT);
                    }
                }
            } else {
                cp.set_descriptor(cid, compact_state::STARTING);
            }
        }
    }
    return cp;
}
#endif

///////////
// PRINT //
///////////

ostream& operator<< (ostream& str, const game_state& gs) {
    return state_printer::print(str, gs);
}

