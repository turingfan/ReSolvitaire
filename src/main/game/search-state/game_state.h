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

#ifndef SOLVITAIRE_GAME_STATE_H
#define SOLVITAIRE_GAME_STATE_H

// Derived compile-time flag: 1 when the flat-cache Zobrist hash is compiled into
// game_state. True for both SOLVITAIRE_FLAT_ONLY and SOLVITAIRE_HASH_ONLY.
// compact_state is only present when SOLVITAIRE_HASH_ONLY is NOT defined.
#if defined(SOLVITAIRE_FLAT_ONLY) || defined(SOLVITAIRE_HASH_ONLY)
#  define SOLVITAIRE_COMPUTES_FLAT_HASH 1
#elif defined(SOLVITAIRE_LRU_ONLY)
#  define SOLVITAIRE_COMPUTES_FLAT_HASH 0
#else
#  define SOLVITAIRE_COMPUTES_FLAT_HASH 1   /* default binary: compile everything */
#endif

#include <vector>
#include <list>
#include <set>
#include <string>
#include <random>
#include <functional>

#include <boost/functional/hash.hpp>
#include <boost/optional/optional.hpp>

#include "document.h"
#include "../card.h"
#include "../pile.h"
#include "../sol_rules.h"
#include "../move.h"
#include "../zobrist.h"
#include "../descriptor.h"
#ifndef SOLVITAIRE_HASH_ONLY
#  include "../compact_state.h"
#else
#  include "../hash_descriptor_store.h"
#endif
#include "../predecessor_state.h"
#include "../parent_table.h"

class game_state {
    friend struct hasher;
    friend class global_cache;
    friend struct cached_game_state;
    friend class deal_parser;
    friend class state_printer;
    friend class test_helper;
    friend class json_helper;
public:
    enum class streamliner_options {NONE, AUTO_FOUNDATIONS, SUIT_SYMMETRY, BOTH};

    /* Constructors */

    // Creates a game state representation from a JSON doc
    explicit game_state(const sol_rules&, const rapidjson::Document&, streamliner_options, bool force_lru = false, const std::string& cache_type = "");
    // Does the same from a seed
    game_state(const sol_rules&, int seed, streamliner_options, bool force_lru = false, const std::string& cache_type = "");
    // Does the same but with an initialiser list (useful for testing)
    game_state(const sol_rules&, std::initializer_list<std::initializer_list<std::string>>);

    /* Altering state */

    void make_move(move);
    void undo_move(move);
    void place_card(pile::ref, card);
    card take_card(pile::ref);

    /* Legal move generation */

    std::vector<move> get_legal_moves(move = move(move::mtype::regular));
    boost::optional<move> get_dominance_move() const;

#if SOLVITAIRE_COMPUTES_FLAT_HASH
    /* Runtime policy flags (public: read by solver to avoid dead work) */
    bool computing_flat_hash;     // true: maintain Zobrist hash + payload descriptor store
    bool computing_flat_payload;  // true: cache uses full compact_state payload as key
#endif

    /* State inspection */

    bool is_solved() const;
    const std::vector<pile>& get_data() const;
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    uint64_t get_zobrist_hash() const { return zobrist_hash_value; }
#ifndef SOLVITAIRE_HASH_ONLY
    const compact_state& get_payload() const { return payload; }
    void set_payload_depth(uint16_t depth);
    void compute_hash_from_scratch();  // For testing: recompute hash from payload
#endif
#endif

    /* Predecessor-based Zobrist (accordion games) */
    uint64_t get_predecessor_zobrist_hash() const { return predecessor_zobrist_hash; }
    const predecessor_state& get_predecessor_payload() const { return pred_payload; }
    void set_predecessor_payload_depth(uint8_t depth);
    bool uses_predecessor_cache() const { return rules.accordion_size > 0; }

#if SOLVITAIRE_COMPUTES_FLAT_HASH && !defined(NDEBUG) && !defined(SOLVITAIRE_HASH_ONLY)
    compact_state recompute_payload_from_scratch() const;  // Debug: rebuild payload from board state
    void assert_payload_consistent() const;                // Debug: assert incremental payload matches recomputed
#endif

    /* Printing */

    friend std::ostream& operator<< (std::ostream&, const game_state&);

private:
    /* Constructors (& helper function) */

    explicit game_state(const sol_rules&, streamliner_options, bool force_lru = false, const std::string& cache_type = "");
    static std::vector<card> gen_shuffled_deck(card::rank_t, bool, std::mt19937);
    template<class RandomIt, class URBG> static void shuffle(RandomIt, RandomIt, URBG&&);

    /* Pile order logic */

    void eval_pile_order(pile::ref, bool);
    void eval_pile_order(std::list<pile::ref>&, pile::ref, bool);

    /* Altering state */

    void make_regular_move(move move);
    void undo_regular_move(move move);
    void make_built_group_move(move move);
    void undo_built_group_move(move move);
    void make_stock_k_plus_move(move move);
    void undo_stock_k_plus_move(move move);
    void make_stock_to_all_tableau_move(move move);
    void undo_stock_to_all_tableau_move(move move);
    void make_sequence_move(move move);
    void undo_sequence_move(move move);
    void make_accordion_move(move move);
    void undo_accordion_move(move move);

#ifndef NDEBUG
    void check_face_down_consistent() const;
#endif

    /* Legal move generation */

    bool stock_can_deal_all_tableau() const;
    move get_stock_to_all_tableau_move() const;

    std::set<std::pair<int8_t, bool>, std::greater<>> generate_k_plus_moves_to_check() const;
    void add_stock_to_cell_move(std::vector<move>&, pile::ref) const;
    void add_stock_to_tableau_moves(std::vector<move>&) const;
    void add_stock_to_hole_foundation_moves(std::vector<move>&) const;
    card stock_card_from_count(int8_t) const;
    void add_foundation_complete_piles_moves(std::vector<move> &) const;
    void add_accordion_moves(std::vector<move>&) const;
    void add_stock_hole_move(std::vector<move>&) const;

    bool is_valid_tableau_move(pile::ref, pile::ref) const;
    bool is_valid_tableau_move(card, pile::ref) const;
    bool is_next_tableau_card(card, card) const;
    bool is_valid_foundations_move(pile::ref, pile::ref) const;
    bool is_valid_foundations_move(card, pile::ref) const;
    bool is_valid_hole_move(pile::ref) const;
    bool is_valid_hole_move(card) const;

    void add_valid_tableau_moves(std::vector<move>&, pile::ref) const;
    void add_built_group_moves(std::vector<move>&, bool, bool) const;
    void add_built_group_moves(std::vector<move>&, pile::ref, pile::size_type, bool, bool) const;
    void add_whole_pile_moves(std::vector<move>&) const;
    void add_whole_pile_moves(std::vector<move>&, pile::ref, pile::size_type) const;
    pile::size_type get_built_group_height(pile::ref) const;
    bool is_next_built_group_card(card, card) const;
    void add_empty_built_group_moves(std::vector<move>&, pile::ref, pile::ref, pile::size_type, bool, bool, bool) const;
    void add_kings_only_built_group_move(std::vector<move>&, pile::ref, pile::ref, pile::size_type, bool) const;
    void add_non_empty_built_group_move(std::vector<move>&, pile::ref, pile::ref, pile::size_type, bool, bool, bool) const;
    void add_sequence_moves(std::vector<move>&) const;
    bool creates_immediate_loop(pile::ref, pile::ref) const;
    bool tableau_space_and_auto_reserve() const;

    bool is_next_legal_card(sol_rules::build_policy, card, card) const;
    bool is_next_legal_card(std::vector<sol_rules::accordion_policy>, card, card) const;
    void turn_face_down_cards(std::vector<move>&) const;

    /* Auto-foundation moves */

    boost::optional<move> auto_reserve_move() const;
    boost::optional<move> auto_waste_stock_move() const;
    bool is_valid_auto_foundation_move(pile::ref) const;
    bool is_ordered_pile(pile::ref) const;
    bool dominance_blocks_foundation_move(pile::ref);
    card::rank_t foundation_base_convert(card::rank_t) const;

    /* Helper methods */

    /* Game rules */

    const sol_rules rules;
    streamliner_options stream_opts;
    bool skip_pile_ordering;
    card::rank_t foundations_base;

#if SOLVITAIRE_COMPUTES_FLAT_HASH
    /* Descriptor-aligned Zobrist hash and descriptor store */
    uint64_t zobrist_hash_value;
#ifdef SOLVITAIRE_HASH_ONLY
    hash_descriptor_store hash_desc;  // old-value store for incremental Zobrist XOR deltas
#else
    compact_state payload;            // cache key (copied into flat_cache clusters) + descriptor store
#endif
    bool initially_face_up[52];  // true = card was face-up at initial deal (after turn_face_up)

    void init_payload_and_hash();     // Called at end of constructors
    void init_initially_face_up();    // Called after turn_face_up() in constructors
#endif

    // Descriptor update helpers
    void update_card_descriptor(uint8_t cid, uint8_t new_desc);
    void update_foundation_in_hash(uint8_t suit, uint8_t new_rank);
    uint8_t effective_waste_ptr() const;
    void update_waste_ptr_in_hash(uint8_t new_ptr);
    void update_hole_top_in_hash(uint8_t new_cid);
#if SOLVITAIRE_COMPUTES_FLAT_HASH
    uint8_t determine_destination_descriptor(pile::ref dest, card moved_card) const;
#endif
    bool is_foundation_pile(pile::ref pr) const;
    uint8_t get_foundation_suit(pile::ref pr) const;

    /* Predecessor-based Zobrist hash and payload (accordion games) */
    static uint64_t Z_pred[52][110];   // card_id x predecessor_value
    static bool Z_pred_initialised;
    uint8_t predecessor_array[52];     // current predecessor for each card
    uint64_t predecessor_zobrist_hash; // incrementally maintained
    predecessor_state pred_payload;    // current payload

    void init_predecessor_zobrist();    // fill Z_pred table (once)
    void init_predecessor_state();      // compute initial predecessor array from game layout
    void update_predecessor(uint8_t card_id, uint8_t new_pred);  // XOR-based incremental update

    // Undo record for predecessor updates during accordion moves
    struct predecessor_undo {
        uint8_t card_id;
        uint8_t old_pred;
    };
    struct predecessor_undo_frame {
        uint8_t count;  // number of predecessor_undo entries in this frame
    };
    std::vector<predecessor_undo> pred_undo_entries;
    std::vector<predecessor_undo_frame> pred_undo_frames;

    /* Pile references */

    std::list<pile::ref> tableau_piles;
    std::list<pile::ref> cells;
    pile::ref stock;
    pile::ref waste;
    std::list<pile::ref> reserve;
    std::vector<pile::ref> foundations;
    std::vector<pile::ref> sequences;
    std::list<pile::ref> accordion;
    pile::ref hole;

    /* Pile references of starting/original layout */

    std::vector<pile::ref> original_tableau_piles;
    std::vector<pile::ref> original_cells;
    std::vector<pile::ref> original_reserve;

    /* Core piles */

    std::vector<pile> piles;
};

#endif //SOLVITAIRE_GAME_STATE_H
