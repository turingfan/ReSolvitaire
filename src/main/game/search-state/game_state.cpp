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
#include "../cache_interface.h"

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

// SOLVITAIRE_COMPUTES_FLAT_HASH is derived in game_state.h (included above).

// Static predecessor Zobrist table
uint64_t game_state::Z_pred[52][110];
bool game_state::Z_pred_initialised = false;

//////////////////
// CONSTRUCTORS //
//////////////////

// A private constructor used by both of the public ones. Initializes all of the
// piles and pile refs specified by the rules
game_state::game_state(const sol_rules& s_rules, streamliner_options stream_opts_, bool force_lru,
                       const std::string& cache_type)
        : rules(s_rules)
        , stream_opts(stream_opts_)
        , foundations_base(card::rank_t(1))
        , predecessor_zobrist_hash(0)
        , stock(255)
        , waste(255)
        , hole (255) {
    std::memset(predecessor_array, 0, 52);
    bool suit_sym = stream_opts_ == streamliner_options::SUIT_SYMMETRY
                 || stream_opts_ == streamliner_options::BOTH;
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    computing_flat_hash    = needs_flat_hash(s_rules, suit_sym, force_lru, cache_type);
    computing_flat_payload = needs_flat_payload(s_rules, suit_sym, force_lru, cache_type);
#else
    (void)cache_type;
#endif
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

    // Initialize Zobrist hash to zero (will be filled by init_payload_and_hash if needed)
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    zobrist_hash_value = 0;
#endif
    skip_pile_ordering = use_new_cache(s_rules, suit_sym) && !force_lru;
}

// Constructs an initial game state from a JSON doc
game_state::game_state(const sol_rules& s_rules, const Document& doc, streamliner_options s_opts, bool force_lru,
                       const std::string& cache_type)
        : game_state(s_rules, s_opts, force_lru, cache_type) {
    deal_parser::parse(*this, doc);
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    if (computing_flat_hash) init_payload_and_hash();
    init_initially_face_up();
#endif
    if (rules.accordion_size > 0) {
        init_predecessor_zobrist();
        init_predecessor_state();
    }
}

// Constructs an initial game state from a seed
game_state::game_state(const sol_rules& s_rules, int seed, streamliner_options s_opts, bool force_lru,
                       const std::string& cache_type)
        : game_state(s_rules, s_opts, force_lru, cache_type) {
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

    // Deals to the tableau piles (row-by-row) if any exist
    if (rules.tableau_pile_count > 0) {
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
    }

#if SOLVITAIRE_COMPUTES_FLAT_HASH
    if (computing_flat_hash) init_payload_and_hash();
#endif
    if (rules.accordion_size > 0) {
        init_predecessor_zobrist();
        init_predecessor_state();
    }

    if (rules.tableau_pile_count == 0) return;

    // Now if necessary, turns the top cards face up
    if (rules.face_up == fu::TOP_CARDS)
        for (pile::ref pr = 0; pr < static_cast<pile::ref>(piles.size()); ++pr)
            if (!piles[pr].empty()) {
                piles[pr][0].turn_face_up();
            }

#if SOLVITAIRE_COMPUTES_FLAT_HASH
    init_initially_face_up();
#endif

    // The size of all piles must equal the deck size
    int piles_sz = 0;
    for (auto& p : piles) piles_sz += p.size();
    if (piles_sz != rules.max_rank * (rules.two_decks ? 8:4)) {
        throw runtime_error("Error: incorrect number of cards in starting piles");
    }
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

#if SOLVITAIRE_COMPUTES_FLAT_HASH
    if (computing_flat_hash) init_payload_and_hash();
    init_initially_face_up();
#endif
    if (rules.accordion_size > 0) {
        init_predecessor_zobrist();
        init_predecessor_state();
    }
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

    uint8_t from_fs = 255;
    if (is_foundation_pile(m.from)) {
        from_fs = get_foundation_suit(m.from);
    }
    uint8_t to_fs = 255;
    if (is_foundation_pile(m.to)) {
        to_fs = get_foundation_suit(m.to);
    }
    uint8_t old_ht = 255;
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    if (m.to == hole) {
#ifdef SOLVITAIRE_HASH_ONLY
        if (computing_flat_hash) old_ht = hash_desc.get_hole_top();
#else
        if (computing_flat_hash) old_ht = payload.get_hole_top();
#endif
    }
#endif

    // Pile operations
    place_card(m.to, take_card(m.from));

#if SOLVITAIRE_COMPUTES_FLAT_HASH
    // Update moved card's descriptor
    uint8_t new_desc = determine_destination_descriptor(m.to, moved);
    update_card_descriptor(cid, new_desc);
#endif

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

    // Update waste pointer when playing from waste (top card removed, pointer changes)
    if (m.from == waste) {
        update_waste_ptr_in_hash(effective_waste_ptr());
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
            ? card_descriptor::IN_SPACE
            : card_descriptor::STARTING_FACE_UP;
        update_card_descriptor(rev_cid, rev_desc);
    }

}

void game_state::undo_regular_move(const move m) {
    assert(m.to < piles.size());

    // Identify moved card BEFORE pile undo (it's at m.to)
    card moved = piles[m.to].top_card();
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    uint8_t cid = zobrist_hash::card_id(moved.get_suit(), moved.get_rank());
#endif

    // === PILE OPERATIONS (restore physical state) ===

    // Undo reveal: turn revealed card face-down BEFORE returning moved card
    // (revealed card is at piles[m.from][0] while moved card is still at m.to)
    if (m.reveal_move) {
        assert(!piles[m.from].empty());
        assert(!piles[m.from][0].is_face_down());
        piles[m.from][0].turn_face_down();
    }

    // Return card to source pile
    place_card(m.from, take_card(m.to));

    // === HASH/PAYLOAD RECOVERY (all from restored pile state) ===

    // Revealed card goes back to STARTING
    // (after place_card, it is at piles[m.from][1])
    if (m.reveal_move) {
        card rev = piles[m.from][1];
        uint8_t rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
        update_card_descriptor(rev_cid, card_descriptor::STARTING);
    }

    // Destination foundation (card removed from m.to)
    if (is_foundation_pile(m.to)) {
        uint8_t suit = get_foundation_suit(m.to);
        uint8_t rank_now = piles[m.to].empty()
            ? uint8_t(0) : piles[m.to].top_card().get_rank();
        update_foundation_in_hash(suit, rank_now);
    }

    // Source foundation (card returned to m.from)
    if (is_foundation_pile(m.from)) {
        uint8_t suit = get_foundation_suit(m.from);
        update_foundation_in_hash(suit, moved.get_rank());
    }

    // Hole top (card removed from hole)
    if (m.to == hole) {
        uint8_t old_ht = piles[hole].empty()
            ? uint8_t(0)
            : zobrist_hash::card_id(piles[hole].top_card().get_suit(),
                                     piles[hole].top_card().get_rank());
        update_hole_top_in_hash(old_ht);
    }

    // Waste pointer (card returned to waste)
    if (m.from == waste) {
        update_waste_ptr_in_hash(effective_waste_ptr());
    }

#if SOLVITAIRE_COMPUTES_FLAT_HASH
    // Moved card's old descriptor (recovered from restored pile state).
    // For tableau piles with a face-down card now below the returned card,
    // use the static initial-face-up table to distinguish:
    //   - initially face-up (Klondike top-card deal): descriptor was STARTING=0
    //   - initially face-down (revealed during play): descriptor was STARTING_FACE_UP=1
    // All other positions are handled correctly by determine_destination_descriptor.
    uint8_t old_desc = determine_destination_descriptor(m.from, moved);
    if (!original_tableau_piles.empty()) {
        pile::ref first_tab = original_tableau_piles.front();
        pile::ref last_tab = original_tableau_piles.back();
        if (m.from >= first_tab && m.from <= last_tab
                && piles[m.from].size() >= 2 && piles[m.from][1].is_face_down()) {
            old_desc = initially_face_up[cid]
                ? card_descriptor::STARTING
                : card_descriptor::STARTING_FACE_UP;
        }
    }
    update_card_descriptor(cid, old_desc);
#endif
}

void game_state::make_built_group_move(move m) {
    assert(m.from  <  piles.size()  );
    assert(m.to    <  piles.size()  );

    // Adds the cards to the 'to' pile
    for (auto pile_idx = m.count; pile_idx-- > 0;) {
        place_card(m.to, piles[m.from][pile_idx]);
    }

    // Removes the cards from the 'from' pile
    for (uint8_t rem_count = 0; rem_count < m.count; rem_count++) {
        take_card(m.from);
    }

#if SOLVITAIRE_COMPUTES_FLAT_HASH
    {
        // After placement, bottom card is at piles[m.to][m.count - 1]
        card bottom = piles[m.to][m.count - 1];
        uint8_t bottom_cid = zobrist_hash::card_id(bottom.get_suit(), bottom.get_rank());
        // Update bottom card's descriptor
        // Parent (if any) is at piles[m.to][m.count]
        uint8_t new_desc;
        if (static_cast<pile::size_type>(piles[m.to].size()) == m.count) {
            new_desc = card_descriptor::IN_SPACE;
        } else {
            card parent_card = piles[m.to][m.count];
            uint8_t parent_cid = zobrist_hash::card_id(
                parent_card.get_suit(), parent_card.get_rank());
            uint8_t desc = parent_table::get_descriptor_for_parent(
                bottom_cid, parent_cid, rules.build_pol,
                foundations_base, rules.max_rank);
            new_desc = (desc != 0) ? desc
                : static_cast<uint8_t>(card_descriptor::ROOT);
        }
        update_card_descriptor(bottom_cid, new_desc);
    }
#endif

    // Handle reveal
    if (m.reveal_move) {
        assert(!piles[m.from].empty());
        assert(piles[m.from][0].is_face_down());
        piles[m.from][0].turn_face_up();
#if SOLVITAIRE_COMPUTES_FLAT_HASH
        card rev = piles[m.from][0];
        uint8_t rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
        // Bottom of pile → IN_SPACE; otherwise STARTING_FACE_UP
        uint8_t rev_desc = (piles[m.from].size() == 1)
            ? card_descriptor::IN_SPACE
            : card_descriptor::STARTING_FACE_UP;
        update_card_descriptor(rev_cid, rev_desc);
#endif
    }

}

void game_state::undo_built_group_move(move m) {
    assert(m.to < piles.size());

#if SOLVITAIRE_COMPUTES_FLAT_HASH
    // Capture bottom card identity before pile undo (still at m.to)
    card bottom_pre_undo = piles[m.to][m.count - 1];
    uint8_t bottom_cid = zobrist_hash::card_id(bottom_pre_undo.get_suit(), bottom_pre_undo.get_rank());
#endif

    // === PILE OPERATIONS (restore physical state) ===

    // Return group to source pile
    for (auto pile_idx = m.count; pile_idx-- > 0;) {
        place_card(m.from, piles[m.to][pile_idx]);
    }
    for (uint8_t rem_count = 0; rem_count < m.count; rem_count++) {
        take_card(m.to);
    }

    // Undo reveal: turn card at piles[m.from][m.count] face-down.
    // Must happen BEFORE descriptor recovery — face-down check reads piles[m.from][m.count].
    if (m.reveal_move) {
        assert(piles[m.from].size() > static_cast<pile::size_type>(m.count));
        assert(!piles[m.from][m.count].is_face_down());
        piles[m.from][m.count].turn_face_down();
#if SOLVITAIRE_COMPUTES_FLAT_HASH
        {
            card rev = piles[m.from][m.count];
            uint8_t rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
            update_card_descriptor(rev_cid, card_descriptor::STARTING);
        }
#endif
    }

#if SOLVITAIRE_COMPUTES_FLAT_HASH
    {
        // === HASH/PAYLOAD RECOVERY (all from restored pile state) ===

        // Recover bottom card's old descriptor.
        // For tableau piles with a face-down card below the group, use the static
        // initial-face-up table (same logic as undo_regular_move, adjusted for depth m.count):
        //   - initially face-up (Klondike top-card deal): descriptor was STARTING=0
        //   - initially face-down (revealed during play): descriptor was STARTING_FACE_UP=1
        uint8_t old_desc = 0;
        if (static_cast<pile::size_type>(piles[m.from].size()) == m.count) {
            // Group fills the entire pile: bottom card was placed on an empty pile
            old_desc = card_descriptor::IN_SPACE;
        } else if (!original_tableau_piles.empty()) {
            pile::ref first_tab = original_tableau_piles.front();
            pile::ref last_tab = original_tableau_piles.back();
            if (m.from >= first_tab && m.from <= last_tab
                    && piles[m.from][m.count].is_face_down()) {
                old_desc = initially_face_up[bottom_cid]
                    ? card_descriptor::STARTING
                    : card_descriptor::STARTING_FACE_UP;
            } else {
                card parent_card = piles[m.from][m.count];
                uint8_t parent_cid = zobrist_hash::card_id(
                    parent_card.get_suit(), parent_card.get_rank());
                uint8_t desc = parent_table::get_descriptor_for_parent(
                    bottom_cid, parent_cid, rules.build_pol,
                    foundations_base, rules.max_rank);
                old_desc = (desc != 0) ? desc : static_cast<uint8_t>(card_descriptor::ROOT);
            }
        } else {
            card parent_card = piles[m.from][m.count];
            uint8_t parent_cid = zobrist_hash::card_id(
                parent_card.get_suit(), parent_card.get_rank());
            uint8_t desc = parent_table::get_descriptor_for_parent(
                bottom_cid, parent_cid, rules.build_pol,
                foundations_base, rules.max_rank);
            old_desc = (desc != 0) ? desc : static_cast<uint8_t>(card_descriptor::ROOT);
        }
        update_card_descriptor(bottom_cid, old_desc);
    }
#endif
}

void game_state::make_stock_k_plus_move(const move m) {
#ifndef NDEBUG
    assert(rules.stock_deal_t == sdt::WASTE);
    assert(m.from == stock);
    if (rules.stock_redeal) assert(m.count <= piles[stock].size() && m.count > -piles[waste].size());
    assert(!rules.stock_redeal || m.to != waste);
    auto sz_before = piles[stock].size() + piles[waste].size();
#endif

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

    // Moves the card on top of the waste to the target pile
    place_card(m.to,  take_card(waste));

    // Flips the waste back on to the empty stock (if necessary)
    if (m.flip_waste) {
        assert(rules.stock_redeal && piles[stock].empty());
        while (!piles[waste].empty()) {
            place_card(stock, take_card(waste));
        }
    }

#if SOLVITAIRE_COMPUTES_FLAT_HASH
    // Update descriptor for played card
    uint8_t new_desc = determine_destination_descriptor(m.to, played);
    update_card_descriptor(played_cid, new_desc);
#endif

    // Update foundation/hole headers
    uint8_t to_fs = 255;
    if (is_foundation_pile(m.to)) {
        to_fs = get_foundation_suit(m.to);
        update_foundation_in_hash(to_fs, played.get_rank());
    }
    if (m.to == hole) {
        update_hole_top_in_hash(played_cid);
    }

    // Update waste pointer to current waste size
    // (applying waste-deal symmetry if applicable)
    update_waste_ptr_in_hash(effective_waste_ptr());

#ifndef NDEBUG
    auto sz_after = piles[stock].size() + piles[waste].size();
    assert(sz_before == sz_after + 1);
    assert(!(rules.stock_size > 0 && rules.stock_redeal && piles[stock].empty() && !piles[waste].empty()));
    assert(piles[stock].size() <= rules.stock_size);
#endif
}

void game_state::undo_stock_k_plus_move(move m) {
    assert(m.to < piles.size());

#ifndef NDEBUG
    assert(rules.stock_deal_t == sdt::WASTE);
    assert(m.from == stock);
    assert(!rules.stock_redeal || m.to != waste);
    auto sz_after = piles[stock].size() + piles[waste].size();
#endif

    // Identify played card BEFORE pile undo (it's at m.to)
    card played = piles[m.to].top_card();
    uint8_t played_cid = zobrist_hash::card_id(played.get_suit(), played.get_rank());

    // === PILE OPERATIONS (exact reverse of make) ===
    if (m.flip_waste) {
        assert(rules.stock_redeal && piles[waste].empty());
        while (!piles[stock].empty()) {
            place_card(waste, take_card(stock));
        }
    }
    place_card(waste, take_card(m.to));
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

    // === HASH/PAYLOAD RECOVERY (all from restored pile state) ===

    // Destination foundation (card removed from m.to)
    if (is_foundation_pile(m.to)) {
        uint8_t suit = get_foundation_suit(m.to);
        uint8_t rank_now = piles[m.to].empty()
            ? uint8_t(0) : piles[m.to].top_card().get_rank();
        update_foundation_in_hash(suit, rank_now);
    }

    // Hole top (card removed from hole)
    if (m.to == hole) {
        uint8_t old_ht = piles[hole].empty()
            ? uint8_t(0)
            : zobrist_hash::card_id(piles[hole].top_card().get_suit(),
                                     piles[hole].top_card().get_rank());
        update_hole_top_in_hash(old_ht);
    }

    // Waste pointer (always updated — stock_k_plus always involves waste)
    update_waste_ptr_in_hash(effective_waste_ptr());

    // Played card descriptor → STARTING (stock/waste cards are always STARTING)
    update_card_descriptor(played_cid, card_descriptor::STARTING);
}

void game_state::make_stock_to_all_tableau_move(move m) {
    assert(rules.stock_deal_t == sdt::TABLEAU_PILES);
    assert(!use_new_cache(rules));  // KI-6: TABLEAU_PILES games always use LRU cache

    for (pile::ref tab_pr = original_tableau_piles.front();
         tab_pr < pile::ref(original_tableau_piles.front() + m.count);
         tab_pr++) {
        place_card(tab_pr, take_card(stock));

#if SOLVITAIRE_COMPUTES_FLAT_HASH
        // Update dealt card's descriptor: STARTING → destination descriptor
        card dealt = piles[tab_pr].top_card();
        uint8_t cid = zobrist_hash::card_id(dealt.get_suit(), dealt.get_rank());
        uint8_t new_desc = determine_destination_descriptor(tab_pr, dealt);
        update_card_descriptor(cid, new_desc);
#endif
    }
}

void game_state::undo_stock_to_all_tableau_move(move m) {
    assert(rules.stock_deal_t == sdt::TABLEAU_PILES);
    assert(!use_new_cache(rules));  // KI-6: TABLEAU_PILES games always use LRU cache

    // Restore each dealt card's descriptor back to STARTING before pile ops
    for (pile::ref tab_pr = original_tableau_piles.front() + m.count;
         tab_pr-- > original_tableau_piles.front();
            ) {
        card c = piles[tab_pr].top_card();
        uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
        update_card_descriptor(cid, card_descriptor::STARTING);

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
    // --- Predecessor updates (before pile move changes the board) ---
    predecessor_undo_frame frame = {0};

    card from_top = piles[m.from].top_card();
    uint8_t from_cid = zobrist_hash::card_id(from_top.get_suit(), from_top.get_rank());
    card to_top = piles[m.to].top_card();
    uint8_t to_cid = zobrist_hash::card_id(to_top.get_suit(), to_top.get_rank());

    // 1. to's top card becomes buried → FINAL
    uint8_t old_to_pred = predecessor_array[to_cid];
    update_predecessor(to_cid, predecessor_state::FINAL);
    pred_undo_entries.push_back({to_cid, old_to_pred});
    frame.count++;

    // 2. from's top card inherits to's old predecessor (takes to's chain position)
    uint8_t old_from_pred = predecessor_array[from_cid];
    update_predecessor(from_cid, old_to_pred);
    pred_undo_entries.push_back({from_cid, old_from_pred});
    frame.count++;

    // 3. If there's a pile to the right of from (pile 'N'), its top card pointed at from's card.
    //    If from moves to its immediate left neighbor (pile 'to'), it is still N's left neighbor.
    //    If from moves further (3-left), N now points to from's old left neighbor.
    auto from_it = std::find(accordion.begin(), accordion.end(), m.from);
    assert(from_it != accordion.end());
    auto right_it = std::next(from_it);
    if (right_it != accordion.end()) {
        auto from_left_it = (from_it == accordion.begin()) ? accordion.end() : std::prev(from_it);
        if (from_left_it == accordion.end() || *from_left_it != m.to) {
            // from did NOT move to its immediate left neighbor — its right neighbor's pred changes
            card right_top = piles[*right_it].top_card();
            uint8_t right_cid = zobrist_hash::card_id(right_top.get_suit(), right_top.get_rank());
            uint8_t old_right_pred = predecessor_array[right_cid];
            update_predecessor(right_cid, old_from_pred);
            pred_undo_entries.push_back({right_cid, old_right_pred});
            frame.count++;
        }
    }

    // Also: if to is to the right of from and they are not adjacent,
    // the pile right of to still points at to's old top card, which is now buried.
    // After the merge, from's card is on top of to, so the pile right of to
    // should point to from's card. But we need to check this case.
    if (m.to != m.from) {
        auto to_it = std::find(accordion.begin(), accordion.end(), m.to);
        auto to_right_it = std::next(to_it);
        if (to_right_it != accordion.end() && *to_right_it != m.from) {
            card to_right_top = piles[*to_right_it].top_card();
            uint8_t to_right_cid = zobrist_hash::card_id(to_right_top.get_suit(), to_right_top.get_rank());
            if (predecessor_array[to_right_cid] == to_cid) {
                uint8_t old_tr_pred = predecessor_array[to_right_cid];
                update_predecessor(to_right_cid, from_cid);
                pred_undo_entries.push_back({to_right_cid, old_tr_pred});
                frame.count++;
            }
        }
    }

    pred_undo_frames.push_back(frame);

    // Update predecessor payload
    pred_payload.clear();
    pred_payload.set_occupied(true);
    for (uint8_t i = 0; i < 52; i++) {
        pred_payload.set_predecessor(i, predecessor_array[i]);
    }

    // --- Standard accordion move (pile operations + existing Zobrist) ---
    make_built_group_move(m);
    accordion.remove(m.from);
}

void game_state::undo_accordion_move(move m) {
    // --- Standard accordion undo (pile operations + existing Zobrist) ---
    accordion.insert(upper_bound(begin(accordion), end(accordion), m.from), m.from);
    undo_built_group_move(m);

    // --- Predecessor undo ---
    assert(!pred_undo_frames.empty());
    predecessor_undo_frame frame = pred_undo_frames.back();
    pred_undo_frames.pop_back();

    // Replay undo entries in reverse order
    for (uint8_t i = 0; i < frame.count; i++) {
        assert(!pred_undo_entries.empty());
        predecessor_undo entry = pred_undo_entries.back();
        pred_undo_entries.pop_back();
        update_predecessor(entry.card_id, entry.old_pred);
    }

    // Rebuild predecessor payload from array
    pred_payload.clear();
    pred_payload.set_occupied(true);
    for (uint8_t i = 0; i < 52; i++) {
        pred_payload.set_predecessor(i, predecessor_array[i]);
    }
}

// Places a card on a pile and if it is on a tableau, cell or reserve pile,
// reorders the pile refs so that the largest pile is first
void game_state::place_card(pile::ref pr, card c) {
    piles[pr].place(c);

#ifndef NO_PILE_SYMMETRY
    // If the stock deals to the tableau piles, there is no pile symmetry
    if (!skip_pile_ordering
        && (rules.stock_size == 0 || rules.stock_deal_t != sdt::TABLEAU_PILES)) {
        eval_pile_order(pr, true);
    }
#endif
}

// Same as above but for taking cards
card game_state::take_card(pile::ref pr) {
    card c = piles[pr].take();
#ifndef NO_PILE_SYMMETRY
    // If the stock deals to the tableau piles, there is no pile symmetry
    if (!skip_pile_ordering
        && (rules.stock_size == 0 || rules.stock_deal_t != sdt::TABLEAU_PILES)) {
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
#if SOLVITAIRE_COMPUTES_FLAT_HASH
        for (auto& c : piles[p].pile_vec) {
            if (c.is_face_down()) {
                uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
                uint8_t desc = payload.get_descriptor(cid);
                if (desc != card_descriptor::STARTING) {
                    std::cerr << "DESCRIPTOR BUG: face-down card " << (int)cid
                              << " has descriptor " << (int)desc
                              << " (expected STARTING=0)" << std::endl;
                    assert(false);
                }
            }
        }
#endif
    }
}
#endif

////////////////////////
// ZOBRIST HASHING    //
////////////////////////

#if SOLVITAIRE_COMPUTES_FLAT_HASH

// Record which cards are face-up after the initial deal (including after turn_face_up()).
// Also fixes up IN_SPACE descriptors for single-card tableau piles: when
// init_payload_and_hash() runs before turn_face_up() (seed constructor), the top card
// of a single-card pile is face-down at init time and gets STARTING=0. But a single-card
// pile is semantically IN_SPACE (consistent with determine_destination_descriptor), so
// we correct those here.
void game_state::init_initially_face_up() {
    memset(initially_face_up, 0, sizeof(initially_face_up));
    for (const auto& p : piles) {
        for (pile::size_type i = 0; i < p.size(); ++i) {
            card c = p[i];
            if (!c.is_face_down()) {
                uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
                initially_face_up[cid] = true;
            }
        }
    }

    // Fix up single-card tableau piles whose top card is face-up but still has
    // STARTING=0 (because init_payload_and_hash ran while it was face-down).
    // No-op for init-list/JSON constructors where the positional loop already set IN_SPACE.
    // Guarded for predecessor-cache (accordion) games which manage descriptors differently.
    if (uses_predecessor_cache()) return;
    for (auto tab_ref : original_tableau_piles) {
        if (piles[tab_ref].size() == 1 && !piles[tab_ref][0].is_face_down()) {
            card c = piles[tab_ref][0];
            uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
#ifdef SOLVITAIRE_HASH_ONLY
            if (computing_flat_hash && hash_desc.get_descriptor(cid) == card_descriptor::STARTING) {
#else
            if (computing_flat_hash && payload.get_descriptor(cid) == card_descriptor::STARTING) {
#endif
                update_card_descriptor(cid, card_descriptor::IN_SPACE);
            }
        }
    }
}

void game_state::init_payload_and_hash() {
    if (!computing_flat_hash) return;
#ifdef SOLVITAIRE_HASH_ONLY
    hash_desc.clear();
#else
    payload.clear();
#endif
    zobrist_hash_value = 0;

    // All cards start with descriptor STARTING (0)
    // XOR in Z_card[c][0] for all 52 cards
    for (uint8_t c = 0; c < 52; ++c) {
        zobrist_hash_value ^= zobrist_hash::card_key(c, card_descriptor::STARTING);
    }

    // Foundation tops
    if (rules.foundations_present) {
        for (uint8_t s = 0; s < 4; ++s) {
            uint8_t top_rank = 0;
            if (s < foundations.size()) {
                top_rank = piles[foundations[s]].empty() ? 0 : piles[foundations[s]].top_card().get_rank();
            }
#ifdef SOLVITAIRE_HASH_ONLY
            hash_desc.set_foundation(s, top_rank);
#else
            payload.set_foundation(s, top_rank);
#endif
            zobrist_hash_value ^= zobrist_hash::foundation_key(s, top_rank);
        }
    }

    // Hole top
    if (rules.hole && !piles[hole].empty()) {
        card top = piles[hole].top_card();
        uint8_t cid = zobrist_hash::card_id(top.get_suit(), top.get_rank());
#ifdef SOLVITAIRE_HASH_ONLY
        hash_desc.set_hole_top(cid);
#else
        payload.set_hole_top(cid);
#endif
        zobrist_hash_value ^= zobrist_hash::hole_top_key(cid);
    }

    // Waste pointer: initial position = size of waste pile
    // When waste-deal symmetry holds (redeal enabled and waste size divisible
    // by deal count), use 0 to match LRU's position-independent encoding.
    if (rules.stock_size > 0 && rules.stock_deal_t == sol_rules::stock_deal_type::WASTE) {
        uint8_t ptr = effective_waste_ptr();
#ifdef SOLVITAIRE_HASH_ONLY
        hash_desc.set_waste_ptr(ptr);
#else
        payload.set_waste_ptr(ptr);
#endif
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
                new_desc = card_descriptor::IN_SPACE;
            } else {
                // Card below this one is p[i+1]
                card parent_card = p[i + 1];
                // Face-down parent: card is above an unrevealed card at deal time.
                // Descriptor stays STARTING (0) — no valid build relationship visible.
                if (parent_card.is_face_down()) continue;
                uint8_t parent_cid = zobrist_hash::card_id(
                    parent_card.get_suit(), parent_card.get_rank());
                uint8_t desc = parent_table::get_descriptor_for_parent(
                    cid, parent_cid, rules.build_pol,
                    foundations_base, rules.max_rank);
                new_desc = (desc != 0) ? desc
                    : static_cast<uint8_t>(card_descriptor::ROOT);
            }
            update_card_descriptor(cid, new_desc);
        }
    }

    // Pre-filled cells: IN_CELL
    for (auto c_ref : original_cells) {
        if (!piles[c_ref].empty()) {
            card c = piles[c_ref].top_card();
            uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
            update_card_descriptor(cid, card_descriptor::IN_CELL);
        }
    }

    // Hole cards
    if (rules.hole && !piles[hole].empty()) {
        for (pile::size_type i = 0; i < piles[hole].size(); ++i) {
            card c = piles[hole][i];
            uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
            update_card_descriptor(cid, card_descriptor::IN_HOLE);
        }
    }
}

#endif // SOLVITAIRE_COMPUTES_FLAT_HASH

void game_state::update_card_descriptor(uint8_t cid, uint8_t new_desc) {
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    if (computing_flat_hash) {
#ifdef SOLVITAIRE_HASH_ONLY
        uint8_t old_desc = hash_desc.get_descriptor(cid);
        hash_desc.set_descriptor(cid, new_desc);
#else
        uint8_t old_desc = payload.get_descriptor(cid);
        payload.set_descriptor(cid, new_desc);
#endif
        zobrist_hash_value ^= zobrist_hash::card_key(cid, old_desc)
                            ^ zobrist_hash::card_key(cid, new_desc);
    }
#else
    (void)cid; (void)new_desc;
#endif
}

void game_state::update_foundation_in_hash(uint8_t suit, uint8_t new_rank) {
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    if (computing_flat_hash) {
#ifdef SOLVITAIRE_HASH_ONLY
        uint8_t old_rank = hash_desc.get_foundation(suit);
        hash_desc.set_foundation(suit, new_rank);
#else
        uint8_t old_rank = payload.get_foundation(suit);
        payload.set_foundation(suit, new_rank);
#endif
        zobrist_hash_value ^= zobrist_hash::foundation_key(suit, old_rank)
                            ^ zobrist_hash::foundation_key(suit, new_rank);
    }
#else
    (void)suit; (void)new_rank;
#endif
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
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    if (computing_flat_hash) {
#ifdef SOLVITAIRE_HASH_ONLY
        uint8_t old_ptr = hash_desc.get_waste_ptr();
        hash_desc.set_waste_ptr(new_ptr);
#else
        uint8_t old_ptr = payload.get_waste_ptr();
        payload.set_waste_ptr(new_ptr);
#endif
        zobrist_hash_value ^= zobrist_hash::waste_key(old_ptr)
                            ^ zobrist_hash::waste_key(new_ptr);
    }
#else
    (void)new_ptr;
#endif
}

void game_state::update_hole_top_in_hash(uint8_t new_cid) {
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    if (computing_flat_hash) {
#ifdef SOLVITAIRE_HASH_ONLY
        uint8_t old_cid = hash_desc.get_hole_top();
        hash_desc.set_hole_top(new_cid);
#else
        uint8_t old_cid = payload.get_hole_top();
        payload.set_hole_top(new_cid);
#endif
        zobrist_hash_value ^= zobrist_hash::hole_top_key(old_cid)
                            ^ zobrist_hash::hole_top_key(new_cid);
    }
#else
    (void)new_cid;
#endif
}

bool game_state::is_foundation_pile(pile::ref pr) const {
    return !foundations.empty()
        && pr >= foundations.front()
        && pr < foundations.front() + foundations.size();
}

uint8_t game_state::get_foundation_suit(pile::ref pr) const {
    return pr - foundations.front();
}

#if SOLVITAIRE_COMPUTES_FLAT_HASH
uint8_t game_state::determine_destination_descriptor(pile::ref dest, card moved_card) const {
    // Foundation: card descriptor set to STARTING (0)
    if (is_foundation_pile(dest)) {
        return card_descriptor::STARTING;
    }

    // Hole
    if (dest == hole) {
        return card_descriptor::IN_HOLE;
    }

    // Cell
    if (!original_cells.empty()
        && dest >= original_cells.front()
        && dest <= original_cells.back()) {
        return card_descriptor::IN_CELL;
    }

    // Tableau: ROOT if placed on empty pile, PARENT_i if placed on a parent card
    if (!original_tableau_piles.empty()) {
        pile::ref first_tab = original_tableau_piles.front();
        pile::ref last_tab = original_tableau_piles.back();
        if (dest >= first_tab && dest <= last_tab) {
            // After place_card, size==1 means the pile was empty before
            if (piles[dest].size() == 1) {
                return card_descriptor::IN_SPACE;
            }
            // Card below the moved card is the parent
            card parent_card = piles[dest][1];
            uint8_t moved_cid = zobrist_hash::card_id(
                moved_card.get_suit(), moved_card.get_rank());
            uint8_t parent_cid = zobrist_hash::card_id(
                parent_card.get_suit(), parent_card.get_rank());
            uint8_t desc = parent_table::get_descriptor_for_parent(
                moved_cid, parent_cid, rules.build_pol,
                foundations_base, rules.max_rank);
            if (desc != 0) return desc;
            // Non-legal-build parent below — ROOT discriminated by parent suit
            return card_descriptor::ROOT;
        }
    }

    // Reserve, stock, waste: keep STARTING
    return card_descriptor::STARTING;
}
#endif // SOLVITAIRE_COMPUTES_FLAT_HASH (determine_destination_descriptor)

#if SOLVITAIRE_COMPUTES_FLAT_HASH && !defined(SOLVITAIRE_HASH_ONLY)
void game_state::set_payload_depth(uint16_t depth) {
    if (computing_flat_payload) payload.set_depth(depth);
}

void game_state::compute_hash_from_scratch() {
    if (!computing_flat_hash) return;
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
#endif // SOLVITAIRE_COMPUTES_FLAT_HASH

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
#if SOLVITAIRE_COMPUTES_FLAT_HASH
compact_state game_state::recompute_payload_from_scratch() const {
    compact_state cp;
    cp.clear();  // All 52 card descriptors default to STARTING(0)

    // Foundation tops (bytes 3-4)
    if (rules.foundations_present) {
        for (uint8_t s = 0; s < 4; ++s) {
            uint8_t top_rank = 0;
            if (s < foundations.size()) {
                top_rank = piles[foundations[s]].empty() ? 0
                         : piles[foundations[s]].top_card().get_rank();
            }
            cp.set_foundation(s, top_rank);
        }
    }

    // Hole top (byte 3, mutually exclusive with foundations)
    if (rules.hole && !piles[hole].empty()) {
        card top = piles[hole].top_card();
        cp.set_hole_top(zobrist_hash::card_id(top.get_suit(), top.get_rank()));
    }

    // Waste pointer (byte 5) — must apply the same symmetry as effective_waste_ptr()
    if (rules.stock_size > 0 && rules.stock_deal_t == sol_rules::stock_deal_type::WASTE) {
        cp.set_waste_ptr(effective_waste_ptr());
    }

    // Tableau card descriptors — walk current piles explicitly
    for (auto tab_ref : original_tableau_piles) {
        const pile& p = piles[tab_ref];
        for (pile::size_type i = 0; i < p.size(); ++i) {
            card c = p[i];
            if (c.is_face_down()) continue;  // face-down cards remain STARTING

            uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
            uint8_t new_desc;

            if (i == p.size() - 1) {
                // Bottom of pile: empty space below — IN_SPACE, not ROOT
                new_desc = card_descriptor::IN_SPACE;
            } else {
                card parent_card = p[i + 1];
                if (parent_card.is_face_down()) {
                    // Face-up card above a face-down card. Cannot distinguish
                    // ROOT (original deal position) from STARTING_FACE_UP (revealed
                    // card) without move history. Assign ROOT conservatively — matches
                    // init_payload_and_hash() for the original-deal case.
                    // NOTE: this makes recompute inaccurate for revealed cards in
                    // face-down games; assert_payload_consistent() skips assertion
                    // for such games (face_up_policy::TOP_CARDS).
                    new_desc = card_descriptor::ROOT;
                } else {
                    uint8_t parent_cid = zobrist_hash::card_id(
                        parent_card.get_suit(), parent_card.get_rank());
                    uint8_t desc = parent_table::get_descriptor_for_parent(
                        cid, parent_cid, rules.build_pol,
                        foundations_base, rules.max_rank);
                    new_desc = (desc != 0) ? desc : static_cast<uint8_t>(card_descriptor::ROOT);
                }
            }
            cp.set_descriptor(cid, new_desc);
        }
    }

    // Cell cards — IN_CELL (walk all cell pile refs, not just pre-filled ones)
    for (auto c_ref : cells) {
        if (!piles[c_ref].empty()) {
            card c = piles[c_ref].top_card();
            cp.set_descriptor(zobrist_hash::card_id(c.get_suit(), c.get_rank()),
                              card_descriptor::IN_CELL);
        }
    }

    // Hole cards — IN_HOLE
    if (rules.hole) {
        for (pile::size_type i = 0; i < piles[hole].size(); ++i) {
            card c = piles[hole][i];
            cp.set_descriptor(zobrist_hash::card_id(c.get_suit(), c.get_rank()),
                              card_descriptor::IN_HOLE);
        }
    }

    return cp;
}

void game_state::assert_payload_consistent() const {
    if (!computing_flat_payload) return;
    // Cannot accurately recompute STARTING_FACE_UP for face-down games
    // (revealed cards are indistinguishable from originally-placed cards by
    // board inspection alone). Only assert for fully face-up games.
    if (rules.face_up != sol_rules::face_up_policy::ALL) return;

    compact_state recomputed = recompute_payload_from_scratch();
    assert(recomputed.matches(payload) &&
           "Incremental payload diverged from scratch-recomputed payload — "
           "make_move/undo_move descriptor update bug");
}
#endif // SOLVITAIRE_COMPUTES_FLAT_HASH
#endif // NDEBUG

////////////////////////////////////
// PREDECESSOR ZOBRIST (ACCORDION) //
////////////////////////////////////

void game_state::init_predecessor_zobrist() {
    if (Z_pred_initialised) return;

    std::mt19937_64 rng(0xDEADBEEF42ULL);  // Fixed seed for reproducibility
    for (int cid = 0; cid < 52; cid++) {
        for (int pred = 0; pred < 110; pred++) {
            Z_pred[cid][pred] = rng();
        }
    }
    Z_pred_initialised = true;
}

void game_state::init_predecessor_state() {
    // Clear everything
    std::memset(predecessor_array, 0, 52);
    predecessor_zobrist_hash = 0;
    pred_payload.clear();
    pred_payload.set_occupied(true);

    // For accordion games: walk the accordion list left to right.
    // Each pile's top card has predecessor = previous pile's top card (or PILE_0).
    // Buried cards (non-top) get FINAL.
    // Cards not in the accordion (not dealt yet) get STARTING.

    // First, mark all cards as STARTING
    for (uint8_t i = 0; i < 52; i++) {
        predecessor_array[i] = predecessor_state::STARTING;
        predecessor_zobrist_hash ^= Z_pred[i][predecessor_state::STARTING];
    }

    // Now process accordion piles
    uint8_t prev_top_cid = 255;  // no previous pile yet
    for (auto pr : accordion) {
        if (piles[pr].empty()) continue;

        // Top card: predecessor is previous pile's top card or PILE_0
        card top = piles[pr].top_card();
        uint8_t top_cid = zobrist_hash::card_id(top.get_suit(), top.get_rank());

        uint8_t new_pred = (prev_top_cid == 255)
            ? static_cast<uint8_t>(predecessor_state::PILE_0)
            : prev_top_cid;

        // Remove STARTING, add new_pred
        predecessor_zobrist_hash ^= Z_pred[top_cid][predecessor_state::STARTING];
        predecessor_array[top_cid] = new_pred;
        predecessor_zobrist_hash ^= Z_pred[top_cid][new_pred];

        // Buried cards (below top): FINAL
        for (pile::size_type idx = 1; idx < piles[pr].size(); idx++) {
            card c = piles[pr][idx];
            uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
            predecessor_zobrist_hash ^= Z_pred[cid][predecessor_state::STARTING];
            predecessor_array[cid] = predecessor_state::FINAL;
            predecessor_zobrist_hash ^= Z_pred[cid][predecessor_state::FINAL];
        }

        prev_top_cid = top_cid;
    }

    // Copy into payload
    for (uint8_t i = 0; i < 52; i++) {
        pred_payload.set_predecessor(i, predecessor_array[i]);
    }
}

void game_state::update_predecessor(uint8_t card_id, uint8_t new_pred) {
    uint8_t old_pred = predecessor_array[card_id];
    predecessor_zobrist_hash ^= Z_pred[card_id][old_pred];
    predecessor_array[card_id] = new_pred;
    predecessor_zobrist_hash ^= Z_pred[card_id][new_pred];
}

void game_state::set_predecessor_payload_depth(uint8_t depth) {
    pred_payload.set_depth(depth);
}

///////////
// PRINT //
///////////

ostream& operator<< (ostream& str, const game_state& gs) {
    return state_printer::print(str, gs);
}

