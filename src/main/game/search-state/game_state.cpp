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

// Static predecessor Zobrist table
template <typename Policy> uint64_t game_state_impl<Policy>::Z_pred[52][110];
template <typename Policy> bool     game_state_impl<Policy>::Z_pred_initialised = false;

//////////////////
// CONSTRUCTORS //
//////////////////

// A private constructor used by both of the public ones. Initializes all of the
// piles and pile refs specified by the rules
template <typename Policy>
game_state_impl<Policy>::game_state_impl(const sol_rules& s_rules, streamliner_options stream_opts_)
        : rules(s_rules)
        , stream_opts(stream_opts_)
        , foundations_base(card::rank_t(1))
        , predecessor_zobrist_hash(0)
        , stock(255)
        , waste(255)
        , hole (255) {
    std::memset(predecessor_array, 0, 52);
    skip_pile_ordering = Policy::skip_pile_ordering;

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

}

// Constructs an initial game state from a JSON doc
template <typename Policy>
game_state_impl<Policy>::game_state_impl(const sol_rules& s_rules, const Document& doc,
                                          streamliner_options s_opts,
                                          bool /*force_lru*/, const std::string& /*cache_type*/)
        : game_state_impl(s_rules, s_opts) {
    deal_parser::parse(*this, doc);
    if constexpr (Policy::computes_hash) {
        init_payload_and_hash();
        init_initially_face_up();
    }
    if (rules.accordion_size > 0) {
        init_predecessor_zobrist();
        init_predecessor_state();
    }
}

// Constructs an initial game state from a seed
template <typename Policy>
game_state_impl<Policy>::game_state_impl(const sol_rules& s_rules, int seed,
                                          streamliner_options s_opts,
                                          bool /*force_lru*/, const std::string& /*cache_type*/)
        : game_state_impl(s_rules, s_opts) {
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

    if constexpr (Policy::computes_hash) {
        init_payload_and_hash();
    }
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

    if constexpr (Policy::computes_hash) {
        init_initially_face_up();
    }

    // Re-run multiplicity recompute after face-up turning.
    // init_payload_and_hash() ran before turn_face_up, so multiplicity
    // descriptors have stale face_down flags. With incremental updates,
    // only changed cards are re-read, so the stale flags would persist.
    if constexpr (Policy::computes_multiplicity_descriptor) {
        desc_engine.recompute_all(make_desc_ctx());
    }

    // The size of all piles must equal the deck size
    int piles_sz = 0;
    for (auto& p : piles) piles_sz += p.size();
    if (piles_sz != rules.max_rank * (rules.two_decks ? 8:4)) {
        throw runtime_error("Error: incorrect number of cards in starting piles");
    }
}

template <typename Policy>
game_state_impl<Policy>::game_state_impl(const sol_rules& s_rules,
                       std::initializer_list<std::initializer_list<string>> il)
        : game_state_impl(s_rules, streamliner_options::NONE) {pile::ref pr = 0;
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

    if constexpr (Policy::computes_hash) {
        init_payload_and_hash();
        init_initially_face_up();
    }
    if (rules.accordion_size > 0) {
        init_predecessor_zobrist();
        init_predecessor_state();
    }
}

// Generates a randomly ordered vector of cards
template <typename Policy>
vector<card> game_state_impl<Policy>::gen_shuffled_deck(card::rank_t max_rank,
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

    game_state_impl<Policy>::shuffle(begin(deck), end(deck), rng);
    return deck;
}

template <typename Policy>
template<class RandomIt, class URBG>
void game_state_impl<Policy>::shuffle(RandomIt first, RandomIt last, URBG&& g) {
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

template <typename Policy>
void game_state_impl<Policy>::make_move(const move m) {
    // Pre-capture waste top before pile ops (stock_k_plus incremental update).
    [[maybe_unused]] uint8_t pre_move_waste_top_cid = UINT8_MAX;
    if constexpr (Policy::computes_multiplicity_descriptor) {
        if (m.type == move::mtype::stock_k_plus
                && waste != pile::ref(255) && !piles[waste].empty()) {
            card w_top = piles[waste].top_card();
            pre_move_waste_top_cid = zobrist_hash::card_id(
                w_top.get_suit(), w_top.get_rank());
        }
    }

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

    if constexpr (Policy::computes_multiplicity_descriptor) {
        // Incremental multiplicity descriptor update.
        // Pile operations are complete; determine descriptor changes by move type.
        std::pair<uint8_t, multiplicity_descriptor> mult_changes[4];
        uint8_t mult_n = 0;
        bool mult_fallback = false;

        switch (m.type) {
            case move::mtype::regular: {
                // Moved card: now at top of m.to
                uint8_t cid = zobrist_hash::card_id(
                    piles[m.to].top_card().get_suit(),
                    piles[m.to].top_card().get_rank());
                mult_changes[mult_n++] = {cid, mult_desc_at(m.to, 0)};

                // If moved to hole, old hole top becomes PERMANENT
                if (m.to == hole && piles[hole].size() > 1) {
                    card old_top = piles[hole][1];
                    uint8_t old_cid = zobrist_hash::card_id(
                        old_top.get_suit(), old_top.get_rank());
                    mult_changes[mult_n++] = {old_cid,
                        multiplicity_descriptor::make_locative(MLD_PERMANENT)};
                }

                // Revealed card: now face-up at top of m.from
                if (m.reveal_move) {
                    uint8_t rev_cid = zobrist_hash::card_id(
                        piles[m.from][0].get_suit(),
                        piles[m.from][0].get_rank());
                    mult_changes[mult_n++] = {rev_cid, mult_desc_at(m.from, 0)};
                }
                break;
            }
            case move::mtype::built_group: {
                // Bottom of group: at piles[m.to][m.count - 1]
                card bottom = piles[m.to][m.count - 1];
                uint8_t bottom_cid = zobrist_hash::card_id(
                    bottom.get_suit(), bottom.get_rank());
                mult_changes[mult_n++] = {bottom_cid,
                    mult_desc_at(m.to, m.count - 1)};

                // Revealed card
                if (m.reveal_move) {
                    uint8_t rev_cid = zobrist_hash::card_id(
                        piles[m.from][0].get_suit(),
                        piles[m.from][0].get_rank());
                    mult_changes[mult_n++] = {rev_cid, mult_desc_at(m.from, 0)};
                }
                break;
            }
            case move::mtype::stock_k_plus: {
                // O(1) incremental: only top-of-waste changes descriptor.
                card played = piles[m.to].top_card();
                uint8_t played_cid = zobrist_hash::card_id(
                    played.get_suit(), played.get_rank());
                mult_changes[mult_n++] = {played_cid, mult_desc_at(m.to, 0)};

                uint8_t post_waste_top_cid = UINT8_MAX;
                if (!piles[waste].empty()) {
                    card nt = piles[waste].top_card();
                    post_waste_top_cid = zobrist_hash::card_id(
                        nt.get_suit(), nt.get_rank());
                }
                if (pre_move_waste_top_cid != UINT8_MAX
                        && pre_move_waste_top_cid != post_waste_top_cid) {
                    // Old waste top moved to stock or down — now MLD_IN_STOCK
                    mult_changes[mult_n++] = {pre_move_waste_top_cid,
                        multiplicity_descriptor::make_locative(MLD_IN_STOCK)};
                }
                if (post_waste_top_cid != UINT8_MAX
                        && post_waste_top_cid != pre_move_waste_top_cid) {
                    // New waste top needs correct descriptor
                    mult_changes[mult_n++] = {post_waste_top_cid,
                        mult_desc_at(waste, 0)};
                }
                break;
            }
            case move::mtype::stock_to_all_tableau:
                mult_fallback = true;
                break;
            default:
                // sequence and accordion never use MultiplicityPolicy
                break;
        }

        if (mult_fallback
#ifdef MULTIPLICITY_NO_INCREMENTAL
            || true  // Force from-scratch recompute for benchmarking
#endif
        ) {
            desc_engine.recompute_all(make_desc_ctx());
        } else if (mult_n > 0) {
            if (desc_engine.is_none_mode()) {
                desc_engine.incremental_update_none(mult_changes, mult_n);
            } else {
                desc_engine.incremental_update(mult_changes, mult_n);
            }
        }

#ifndef NDEBUG
        desc_engine.verify_against_scratch(make_desc_ctx());
#endif
    }

#ifndef NDEBUG
    check_face_down_consistent();
#endif
}

template <typename Policy>
void game_state_impl<Policy>::undo_move(const move m) {
    // Pre-capture played card and waste top before pile ops (stock_k_plus incremental).
    [[maybe_unused]] uint8_t pre_undo_played_cid    = UINT8_MAX;
    [[maybe_unused]] uint8_t pre_undo_waste_top_cid = UINT8_MAX;
    if constexpr (Policy::computes_multiplicity_descriptor) {
        if (m.type == move::mtype::stock_k_plus) {
            card played = piles[m.to].top_card();
            pre_undo_played_cid = zobrist_hash::card_id(
                played.get_suit(), played.get_rank());
            if (waste != pile::ref(255) && !piles[waste].empty()) {
                card w_top = piles[waste].top_card();
                pre_undo_waste_top_cid = zobrist_hash::card_id(
                    w_top.get_suit(), w_top.get_rank());
            }
        }
    }

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

    if constexpr (Policy::computes_multiplicity_descriptor) {
        // Incremental multiplicity descriptor update (undo direction).
        // Pile operations are restored; determine descriptor changes by move type.
        std::pair<uint8_t, multiplicity_descriptor> mult_changes[4];
        uint8_t mult_n = 0;
        bool mult_fallback = false;

        switch (m.type) {
            case move::mtype::regular: {
                // Returned card: now at top of m.from
                uint8_t cid = zobrist_hash::card_id(
                    piles[m.from].top_card().get_suit(),
                    piles[m.from].top_card().get_rank());
                mult_changes[mult_n++] = {cid, mult_desc_at(m.from, 0)};

                // If moved from hole, the new hole top was PERMANENT, now HOLE_TOP
                if (m.to == hole && !piles[hole].empty()) {
                    card new_top = piles[hole][0];
                    uint8_t top_cid = zobrist_hash::card_id(
                        new_top.get_suit(), new_top.get_rank());
                    mult_changes[mult_n++] = {top_cid, mult_desc_at(hole, 0)};
                }

                // Revealed card undone: card below returned card is now face-down.
                // After undo, it is at piles[m.from][1] (face-down).
                if (m.reveal_move) {
                    uint8_t rev_cid = zobrist_hash::card_id(
                        piles[m.from][1].get_suit(),
                        piles[m.from][1].get_rank());
                    mult_changes[mult_n++] = {rev_cid, mult_desc_at(m.from, 1)};
                }
                break;
            }
            case move::mtype::built_group: {
                // Bottom of group returned: at piles[m.from][m.count - 1]
                card bottom = piles[m.from][m.count - 1];
                uint8_t bottom_cid = zobrist_hash::card_id(
                    bottom.get_suit(), bottom.get_rank());
                mult_changes[mult_n++] = {bottom_cid,
                    mult_desc_at(m.from, m.count - 1)};

                // Revealed card undone: at piles[m.from][m.count] (face-down)
                if (m.reveal_move) {
                    uint8_t rev_cid = zobrist_hash::card_id(
                        piles[m.from][m.count].get_suit(),
                        piles[m.from][m.count].get_rank());
                    mult_changes[mult_n++] = {rev_cid,
                        mult_desc_at(m.from, m.count)};
                }
                break;
            }
            case move::mtype::stock_k_plus: {
                // O(1) incremental undo: reverse the waste-top descriptor changes.
                mult_changes[mult_n++] = {pre_undo_played_cid,
                    multiplicity_descriptor::make_locative(MLD_IN_STOCK)};

                uint8_t post_undo_waste_top_cid = UINT8_MAX;
                if (!piles[waste].empty()) {
                    card nt = piles[waste].top_card();
                    post_undo_waste_top_cid = zobrist_hash::card_id(
                        nt.get_suit(), nt.get_rank());
                    mult_changes[mult_n++] = {post_undo_waste_top_cid,
                        mult_desc_at(waste, 0)};
                }
                if (pre_undo_waste_top_cid != UINT8_MAX
                        && pre_undo_waste_top_cid != post_undo_waste_top_cid) {
                    // Pre-undo waste top (post-make top) no longer at pos 0
                    mult_changes[mult_n++] = {pre_undo_waste_top_cid,
                        multiplicity_descriptor::make_locative(MLD_IN_STOCK)};
                }
                break;
            }
            case move::mtype::stock_to_all_tableau:
                mult_fallback = true;
                break;
            default:
                break;
        }

        if (mult_fallback
#ifdef MULTIPLICITY_NO_INCREMENTAL
            || true  // Force from-scratch recompute for benchmarking
#endif
        ) {
            desc_engine.recompute_all(make_desc_ctx());
        } else if (mult_n > 0) {
            if (desc_engine.is_none_mode()) {
                desc_engine.incremental_update_none(mult_changes, mult_n);
            } else {
                desc_engine.incremental_update(mult_changes, mult_n);
            }
        }

#ifndef NDEBUG
        desc_engine.verify_against_scratch(make_desc_ctx());
#endif
    }

#ifndef NDEBUG
    check_face_down_consistent();
#endif
}

template <typename Policy>
void game_state_impl<Policy>::make_regular_move(const move m) {
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
    if constexpr (Policy::computes_hash) {
        if (m.to == hole) {
            old_ht = desc_engine.get_store().get_hole_top();
        }
    }

    // Pile operations
    place_card(m.to, take_card(m.from));

    if constexpr (Policy::computes_hash) {
        // Update moved card's descriptor
        uint8_t new_desc = desc_engine.determine_destination_descriptor(m.to, moved, make_desc_ctx());
        desc_engine.update_card(cid, new_desc);
    }

    if constexpr (Policy::computes_hash) {
        // Update foundation headers
        if (from_fs != 255) {
            uint8_t new_rank = piles[m.from].empty()
                ? uint8_t(0) : piles[m.from].top_card().get_rank();
            desc_engine.update_foundation(from_fs, new_rank);
        }
        if (to_fs != 255) {
            desc_engine.update_foundation(to_fs, moved.get_rank());
        }

        // Update hole header
        if (old_ht != 255) {
            desc_engine.update_hole_top(cid);
        }

        // Update waste pointer when playing from waste (top card removed, pointer changes)
        if (m.from == waste) {
            desc_engine.update_waste_ptr(effective_waste_ptr());
        }
    }

    // Handle reveal
    if (m.reveal_move) {
        assert(!piles[m.from].empty());
        assert(piles[m.from][0].is_face_down());
        piles[m.from][0].turn_face_up();
        if constexpr (Policy::computes_hash) {
            card rev = piles[m.from][0];
            uint8_t rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
            // Bottom of pile → IN_SPACE; otherwise STARTING_FACE_UP
            uint8_t rev_desc = (piles[m.from].size() == 1)
                ? card_descriptor::IN_SPACE
                : card_descriptor::STARTING_FACE_UP;
            desc_engine.update_card(rev_cid, rev_desc);
        }
    }

}

template <typename Policy>
void game_state_impl<Policy>::undo_regular_move(const move m) {
    assert(m.to < piles.size());

    // Identify moved card BEFORE pile undo (it's at m.to)
    card moved = piles[m.to].top_card();

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

    if constexpr (Policy::computes_hash) {
        // Revealed card goes back to STARTING
        // (after place_card, it is at piles[m.from][1])
        if (m.reveal_move) {
            card rev = piles[m.from][1];
            uint8_t rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
            desc_engine.update_card(rev_cid, card_descriptor::STARTING);
        }

        // Destination foundation (card removed from m.to)
        if (is_foundation_pile(m.to)) {
            uint8_t suit = get_foundation_suit(m.to);
            uint8_t rank_now = piles[m.to].empty()
                ? uint8_t(0) : piles[m.to].top_card().get_rank();
            desc_engine.update_foundation(suit, rank_now);
        }

        // Source foundation (card returned to m.from)
        if (is_foundation_pile(m.from)) {
            uint8_t suit = get_foundation_suit(m.from);
            desc_engine.update_foundation(suit, moved.get_rank());
        }

        // Hole top (card removed from hole)
        if (m.to == hole) {
            uint8_t old_ht = piles[hole].empty()
                ? uint8_t(0)
                : zobrist_hash::card_id(piles[hole].top_card().get_suit(),
                                         piles[hole].top_card().get_rank());
            desc_engine.update_hole_top(old_ht);
        }

        // Waste pointer (card returned to waste)
        if (m.from == waste) {
            desc_engine.update_waste_ptr(effective_waste_ptr());
        }

        uint8_t cid = zobrist_hash::card_id(moved.get_suit(), moved.get_rank());
        // Moved card's old descriptor (recovered from restored pile state).
        // For tableau piles with a face-down card now below the returned card,
        // use the static initial-face-up table to distinguish:
        //   - initially face-up (Klondike top-card deal): descriptor was STARTING=0
        //   - initially face-down (revealed during play): descriptor was STARTING_FACE_UP=1
        // All other positions are handled correctly by determine_destination_descriptor.
        uint8_t old_desc = desc_engine.determine_destination_descriptor(m.from, moved, make_desc_ctx());
        if (!original_tableau_piles.empty()) {
            pile::ref first_tab = original_tableau_piles.front();
            pile::ref last_tab = original_tableau_piles.back();
            if (m.from >= first_tab && m.from <= last_tab
                    && piles[m.from].size() >= 2 && piles[m.from][1].is_face_down()) {
                old_desc = desc_engine.was_initially_face_up(cid)
                    ? card_descriptor::STARTING
                    : card_descriptor::STARTING_FACE_UP;
            }
        }
        desc_engine.update_card(cid, old_desc);
    }
}

template <typename Policy>
void game_state_impl<Policy>::make_built_group_move(move m) {
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

    if constexpr (Policy::computes_hash) {
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
        desc_engine.update_card(bottom_cid, new_desc);
    }

    // Handle reveal
    if (m.reveal_move) {
        assert(!piles[m.from].empty());
        assert(piles[m.from][0].is_face_down());
        piles[m.from][0].turn_face_up();
        if constexpr (Policy::computes_hash) {
            card rev = piles[m.from][0];
            uint8_t rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
            // Bottom of pile → IN_SPACE; otherwise STARTING_FACE_UP
            uint8_t rev_desc = (piles[m.from].size() == 1)
                ? card_descriptor::IN_SPACE
                : card_descriptor::STARTING_FACE_UP;
            desc_engine.update_card(rev_cid, rev_desc);
        }
    }

}

template <typename Policy>
void game_state_impl<Policy>::undo_built_group_move(move m) {
    assert(m.to < piles.size());

    // Capture bottom card identity before pile undo (still at m.to)
    uint8_t bottom_cid = 0;
    if constexpr (Policy::computes_hash) {
        card bottom_pre_undo = piles[m.to][m.count - 1];
        bottom_cid = zobrist_hash::card_id(bottom_pre_undo.get_suit(), bottom_pre_undo.get_rank());
    }

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
        if constexpr (Policy::computes_hash) {
            card rev = piles[m.from][m.count];
            uint8_t rev_cid = zobrist_hash::card_id(rev.get_suit(), rev.get_rank());
            desc_engine.update_card(rev_cid, card_descriptor::STARTING);
        }
    }

    if constexpr (Policy::computes_hash) {
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
                old_desc = desc_engine.was_initially_face_up(bottom_cid)
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
        desc_engine.update_card(bottom_cid, old_desc);
    }
}

template <typename Policy>
void game_state_impl<Policy>::make_stock_k_plus_move(const move m) {
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

    if constexpr (Policy::computes_hash) {
        // Update descriptor for played card
        uint8_t new_desc = desc_engine.determine_destination_descriptor(m.to, played, make_desc_ctx());
        desc_engine.update_card(played_cid, new_desc);
    }

    if constexpr (Policy::computes_hash) {
        // Update foundation/hole headers
        if (is_foundation_pile(m.to)) {
            uint8_t to_fs = get_foundation_suit(m.to);
            desc_engine.update_foundation(to_fs, played.get_rank());
        }
        if (m.to == hole) {
            desc_engine.update_hole_top(played_cid);
        }

        // Update waste pointer to current waste size
        // (applying waste-deal symmetry if applicable)
        desc_engine.update_waste_ptr(effective_waste_ptr());
    }

#ifndef NDEBUG
    auto sz_after = piles[stock].size() + piles[waste].size();
    assert(sz_before == sz_after + 1);
    assert(!(rules.stock_size > 0 && rules.stock_redeal && piles[stock].empty() && !piles[waste].empty()));
    assert(piles[stock].size() <= rules.stock_size);
#endif
}

template <typename Policy>
void game_state_impl<Policy>::undo_stock_k_plus_move(move m) {
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

    if constexpr (Policy::computes_hash) {
        // Destination foundation (card removed from m.to)
        if (is_foundation_pile(m.to)) {
            uint8_t suit = get_foundation_suit(m.to);
            uint8_t rank_now = piles[m.to].empty()
                ? uint8_t(0) : piles[m.to].top_card().get_rank();
            desc_engine.update_foundation(suit, rank_now);
        }

        // Hole top (card removed from hole)
        if (m.to == hole) {
            uint8_t old_ht = piles[hole].empty()
                ? uint8_t(0)
                : zobrist_hash::card_id(piles[hole].top_card().get_suit(),
                                         piles[hole].top_card().get_rank());
            desc_engine.update_hole_top(old_ht);
        }

        // Waste pointer (always updated — stock_k_plus always involves waste)
        desc_engine.update_waste_ptr(effective_waste_ptr());

        // Played card descriptor → STARTING (stock/waste cards are always STARTING)
        desc_engine.update_card(played_cid, card_descriptor::STARTING);
    }
}

template <typename Policy>
void game_state_impl<Policy>::make_stock_to_all_tableau_move(move m) {
    assert(rules.stock_deal_t == sdt::TABLEAU_PILES);
    assert(!use_new_cache(rules));  // TABLEAU_PILES: not eligible for flat cache

    for (pile::ref tab_pr = original_tableau_piles.front();
         tab_pr < pile::ref(original_tableau_piles.front() + m.count);
         tab_pr++) {
        place_card(tab_pr, take_card(stock));

        if constexpr (Policy::computes_hash) {
            // Update dealt card's descriptor: STARTING → destination descriptor
            card dealt = piles[tab_pr].top_card();
            uint8_t cid = zobrist_hash::card_id(dealt.get_suit(), dealt.get_rank());
            uint8_t new_desc = desc_engine.determine_destination_descriptor(tab_pr, dealt, make_desc_ctx());
            desc_engine.update_card(cid, new_desc);
        }
    }
}

template <typename Policy>
void game_state_impl<Policy>::undo_stock_to_all_tableau_move(move m) {
    assert(rules.stock_deal_t == sdt::TABLEAU_PILES);
    assert(!use_new_cache(rules));  // TABLEAU_PILES: not eligible for flat cache

    // Restore each dealt card's descriptor back to STARTING before pile ops
    for (pile::ref tab_pr = original_tableau_piles.front() + m.count;
         tab_pr-- > original_tableau_piles.front();
            ) {
        if constexpr (Policy::computes_hash) {
            card c = piles[tab_pr].top_card();
            uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
            desc_engine.update_card(cid, card_descriptor::STARTING);
        }

        place_card(stock, take_card(tab_pr));
    }
}

template <typename Policy>
void game_state_impl<Policy>::make_sequence_move(const move m) {
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

template <typename Policy>
void game_state_impl<Policy>::undo_sequence_move(const move m) {
    pile::ref to_seq_ref = m.to / rules.max_rank;
    pile::size_type to_card_idx = m.to % rules.max_rank;
    card to_card = piles[to_seq_ref][to_card_idx];
    piles[to_seq_ref][to_card_idx] = "AS";

    pile::ref from_seq_ref = m.from / rules.max_rank;
    pile::size_type from_card_idx = m.from % rules.max_rank;
    assert(piles[from_seq_ref][from_card_idx] == "AS");
    piles[from_seq_ref][from_card_idx] = to_card;
}

template <typename Policy>
void game_state_impl<Policy>::make_accordion_move(move m) {
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

template <typename Policy>
void game_state_impl<Policy>::undo_accordion_move(move m) {
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
template <typename Policy>
void game_state_impl<Policy>::place_card(pile::ref pr, card c) {
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
template <typename Policy>
card game_state_impl<Policy>::take_card(pile::ref pr) {
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
template <typename Policy>
void game_state_impl<Policy>::check_face_down_consistent() const {
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
        // (flat engine only; multiplicity engine encodes face-down state differently)
        if constexpr (Policy::computes_hash && !Policy::computes_multiplicity_descriptor) {
            for (auto& c : piles[p].pile_vec) {
                if (c.is_face_down()) {
                    uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
                    uint8_t desc = desc_engine.get_store().get_descriptor(cid);
                    if (desc != card_descriptor::STARTING) {
                        std::cerr << "DESCRIPTOR BUG: face-down card " << (int)cid
                                  << " has descriptor " << (int)desc
                                  << " (expected STARTING=0)" << std::endl;
                        assert(false);
                    }
                }
            }
        }
    }
}
#endif

////////////////////////
// ZOBRIST HASHING    //
////////////////////////

// Thin wrappers that delegate to the descriptor engine.

template <typename Policy>
void game_state_impl<Policy>::init_initially_face_up() {
    if constexpr (Policy::computes_hash) {
        desc_engine.init_face_up_table(make_desc_ctx());
    }
}

template <typename Policy>
void game_state_impl<Policy>::init_payload_and_hash() {
    if constexpr (Policy::computes_hash) {
        desc_engine.init(make_desc_ctx(), effective_waste_ptr());
    }
}

// ── Descriptor context construction ──────────────────────────────────────────

template <typename Policy>
descriptor_context game_state_impl<Policy>::make_desc_ctx() const {
    descriptor_context ctx = { piles, rules, foundations, original_tableau_piles,
                               original_cells, hole, foundations_base,
                               stock, waste,
                               original_reserve.empty() ? nullptr : &original_reserve };
    ctx.suit_sym = (stream_opts == streamliner_options::SUIT_SYMMETRY
                    || stream_opts == streamliner_options::BOTH);
    return ctx;
}

template <typename Policy>
uint8_t game_state_impl<Policy>::effective_waste_ptr() const {
    bool waste_deal_symmetry = rules.stock_redeal
        && piles[waste].size() % rules.stock_deal_count == 0;
    if (waste_deal_symmetry) {
        return 0;
    }
    return static_cast<uint8_t>(piles[waste].size());
}

template <typename Policy>
bool game_state_impl<Policy>::is_foundation_pile(pile::ref pr) const {
    return !foundations.empty()
        && pr >= foundations.front()
        && pr < foundations.front() + foundations.size();
}

template <typename Policy>
uint8_t game_state_impl<Policy>::get_foundation_suit(pile::ref pr) const {
    return pr - foundations.front();
}

// ── Multiplicity descriptor for a card at pile position ──────────────────────
// Determines the correct multiplicity_descriptor based on the pile type and
// the card's position within that pile. Called 1-2 times per move for
// incremental update change identification.

template <typename Policy>
multiplicity_descriptor game_state_impl<Policy>::mult_desc_at(
    pile::ref pr, pile::size_type pos) const
{
    bool fd = piles[pr][pos].is_face_down();

    // Foundation → PERMANENT
    if (is_foundation_pile(pr))
        return multiplicity_descriptor::make_locative(MLD_PERMANENT);

    // Hole → top is HOLE_TOP, rest PERMANENT
    if (pr == hole)
        return multiplicity_descriptor::make_locative(
            pos == 0 ? MLD_HOLE_TOP : MLD_PERMANENT, fd);

    // Cell → IN_CELL
    for (pile::ref cr : original_cells) {
        if (pr == cr)
            return multiplicity_descriptor::make_locative(MLD_IN_CELL, fd);
    }

    // Stock → IN_STOCK
    if (pr == stock)
        return multiplicity_descriptor::make_locative(MLD_IN_STOCK, fd);

    // Waste → IN_WASTE for top card only (pos == 0); all others use IN_STOCK.
    // Non-top waste cards are indistinguishable from stock for hashing purposes.
    if (pr == waste) {
        bool waste_deal_sym = rules.stock_redeal
            && piles[waste].size() % rules.stock_deal_count == 0;
        return multiplicity_descriptor::make_locative(
            (!waste_deal_sym && pos == 0) ? MLD_IN_WASTE : MLD_IN_STOCK, fd);
    }

    // Reserve → IN_RESERVE
    for (pile::ref rr : original_reserve) {
        if (pr == rr)
            return multiplicity_descriptor::make_locative(MLD_IN_RESERVE, fd);
    }

    // Tableau — determine space_kind and predecessor
    pile::size_type pile_size = piles[pr].size();
    if (pos + 1 == pile_size) {
        // Bottom of pile → locative with space_kind
        bool pile_sym = (rules.stock_size == 0
            || rules.stock_deal_t != sol_rules::stock_deal_type::TABLEAU_PILES);
        uint8_t space_kind = MLD_IN_SPACE;
        if (!pile_sym) {
            // Find pile index
            for (pile::size_type idx = 0; idx < original_tableau_piles.size(); idx++) {
                if (original_tableau_piles[idx] == pr) {
                    space_kind = static_cast<uint8_t>(MLD_IN_SPACE + idx);
                    break;
                }
            }
        }
        return multiplicity_descriptor::make_locative(space_kind, fd);
    } else {
        // Not bottom → predecessor of card below
        card parent = piles[pr][pos + 1];
        uint8_t parent_cid = zobrist_hash::card_id(parent.get_suit(), parent.get_rank());
        return multiplicity_descriptor::make_predecessor(parent_cid, fd);
    }
}

template <typename Policy>
void game_state_impl<Policy>::set_payload_depth(uint16_t depth) {
    if constexpr (Policy::computes_payload) {
        desc_engine.get_store().set_depth(depth);
    }
}

template <typename Policy>
void game_state_impl<Policy>::compute_hash_from_scratch() {
    if constexpr (Policy::computes_payload) {
        desc_engine.recompute_hash(rules);
    }
}

////////////////////////
// INSPECT GAME STATE //
////////////////////////

template <typename Policy>
bool game_state_impl<Policy>::is_solved() const {
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

template <typename Policy>
const std::vector<pile>& game_state_impl<Policy>::get_data() const {
    return piles;
}

#ifndef NDEBUG
template <typename Policy>
template <typename P, typename>
compact_state game_state_impl<Policy>::recompute_payload_from_scratch() const {
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

template <typename Policy>
void game_state_impl<Policy>::assert_payload_consistent() const {
    // Multiplicity engine is always-recomputed from scratch, so incremental
    // consistency checks don't apply; skip to avoid compact_state mismatch.
    if constexpr (Policy::computes_payload && !Policy::computes_multiplicity_descriptor) {
        // Cannot accurately recompute STARTING_FACE_UP for face-down games
        // (revealed cards are indistinguishable from originally-placed cards by
        // board inspection alone). Only assert for fully face-up games.
        if (rules.face_up != sol_rules::face_up_policy::ALL) return;

        compact_state recomputed = recompute_payload_from_scratch();
        assert(recomputed.matches(desc_engine.get_store()) &&
               "Incremental payload diverged from scratch-recomputed payload — "
               "make_move/undo_move descriptor update bug");
    }
}
#endif // NDEBUG

////////////////////////////////////
// PREDECESSOR ZOBRIST (ACCORDION) //
////////////////////////////////////

template <typename Policy>
void game_state_impl<Policy>::init_predecessor_zobrist() {
    if (Z_pred_initialised) return;

    std::mt19937_64 rng(0xDEADBEEF42ULL);  // Fixed seed for reproducibility
    for (int cid = 0; cid < 52; cid++) {
        for (int pred = 0; pred < 110; pred++) {
            Z_pred[cid][pred] = rng();
        }
    }
    Z_pred_initialised = true;
}

template <typename Policy>
void game_state_impl<Policy>::init_predecessor_state() {
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

template <typename Policy>
void game_state_impl<Policy>::update_predecessor(uint8_t card_id, uint8_t new_pred) {
    uint8_t old_pred = predecessor_array[card_id];
    predecessor_zobrist_hash ^= Z_pred[card_id][old_pred];
    predecessor_array[card_id] = new_pred;
    predecessor_zobrist_hash ^= Z_pred[card_id][new_pred];
}

template <typename Policy>
void game_state_impl<Policy>::set_predecessor_payload_depth(uint8_t depth) {
    pred_payload.set_depth(depth);
}

///////////
// PRINT //
///////////

template <typename Policy>
ostream& operator<< (ostream& str, const game_state_impl<Policy>& gs) {
    return state_printer::print(str, gs);
}

// ─── Explicit instantiations ──────────────────────────────────────────────────

#if defined(SOLVITAIRE_LRU_ONLY)
template class game_state_impl<LRUPolicy>;
template ostream& operator<<(ostream&, const game_state_impl<LRUPolicy>&);

#elif defined(SOLVITAIRE_FLAT_ONLY)
template class game_state_impl<FlatPolicy>;
template class game_state_impl<PredecessorPolicy>;
template ostream& operator<<(ostream&, const game_state_impl<FlatPolicy>&);
template ostream& operator<<(ostream&, const game_state_impl<PredecessorPolicy>&);

#elif defined(SOLVITAIRE_HASH_ONLY)
template class game_state_impl<HashOnlyPolicy>;
template ostream& operator<<(ostream&, const game_state_impl<HashOnlyPolicy>&);

#else   // default binary — all policies
template class game_state_impl<FlatPolicy>;
template class game_state_impl<HashOnlyPolicy>;
template class game_state_impl<PredecessorPolicy>;
template class game_state_impl<LRUPolicy>;
template class game_state_impl<MultiplicityPolicy>;
template ostream& operator<<(ostream&, const game_state_impl<FlatPolicy>&);
template ostream& operator<<(ostream&, const game_state_impl<HashOnlyPolicy>&);
template ostream& operator<<(ostream&, const game_state_impl<PredecessorPolicy>&);
template ostream& operator<<(ostream&, const game_state_impl<LRUPolicy>&);
template ostream& operator<<(ostream&, const game_state_impl<MultiplicityPolicy>&);
#endif
