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
// Created by thecharlesblake on 1/10/18.
//

#ifndef SOLVITAIRE_GLOBAL_CACHE_H
#define SOLVITAIRE_GLOBAL_CACHE_H

#include <vector>
#include <unordered_set>
#include <boost/pool/pool.hpp>
#include <boost/pool/pool_alloc.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/hashed_index.hpp>

#include "sol_rules.h"
#include "search-state/game_state.h"
#include <cstdint>
#include "cache_interface.h"

struct cached_game_state {
    typedef std::vector<card> state_data;
    typedef state_data::size_type size_type;

    template <typename GS> explicit cached_game_state(const GS&);
    template <typename GS> void add_pile(pile::ref, const GS&);
    template <typename GS> void add_pile_in_reverse(pile::ref, const GS&);
    template <typename GS> void add_card(card, const GS&);
    void add_card_divider();

    state_data data;
    bool live; // Is a parent in the current search tree
    // ─── Stage 2 cache-format fields (item 2a). DORMANT until 2b reads them. ─────
    // Non-key (operator== / hasher key only on `data`), mutated in place via
    // cache.modify exactly like `live`, so they never affect hashing/equality or
    // eviction (max_num_items is an entry COUNT, independent of entry size). No
    // search decision reads these yet ⇒ behaviorally inert; the L=∞ trace identity
    // and the 1f differential verify that. `dead` is the monotone DEAD bit
    // (decision Q4, option C): once set it never reverts; OPEN ⇔ !dead. `b` =
    // verified budget (OPEN(b)); `g_min` = minimum arrival depth.
    bool dead = false;
    uint32_t b = 0;
    uint32_t g_min = UINT32_MAX;
};

bool operator==(const cached_game_state&, const cached_game_state&);

struct hasher {
    // Template constructor — extracts fields from any game_state_impl<Policy>
    template <typename GS>
    explicit hasher(const GS& gs)
        : build_pol(gs.rules.build_pol)
        , is_suit_symmetry(
              (gs.rules.foundations_present
                  && (gs.stream_opts == GS::streamliner_options::SUIT_SYMMETRY
                      || gs.stream_opts == GS::streamliner_options::BOTH))
              || gs.rules.inherent_suit_symmetry()) {}

    std::size_t operator() (const cached_game_state&) const;

    std::size_t hash_value(const card&) const;
    std::size_t combine(std::size_t&, std::size_t) const;

    sol_rules::build_policy build_pol;
    bool is_suit_symmetry;  // precomputed from rules + stream_opts
};

class lru_cache : public cache_interface {
public:
    typedef boost::multi_index::multi_index_container<
            cached_game_state,
            boost::multi_index::indexed_by<
                    boost::multi_index::sequenced<>,
                    boost::multi_index::hashed_unique<
                            boost::multi_index::identity<cached_game_state>,
                            hasher
                    >
            >
    > item_list;

    template <typename GS> explicit lru_cache(const GS&, uint64_t);

    // Template insert (solver calls directly — zero virtual dispatch)
    template <typename GS>
    std::pair<item_list::iterator, bool> insert_with_iterator(const GS&);

    // Template insert/contains (for use with any game_state_impl<Policy>)
    template <typename GS>
    bool insert_t(const GS& gs) {
        return insert_with_iterator(gs).second;
    }
    template <typename GS>
    bool contains_t(const GS& gs) const {
        return cache.template get<1>().count(cached_game_state(gs)) > 0;
    }

    // cache_interface virtual overrides (thin wrappers for dual_cache tests)
    bool insert(const game_state&) override;
    bool contains(const game_state&) const override;
    void clear() override;
    uint64_t size() const override;
    uint64_t bucket_count() const override;
    uint64_t get_states_removed_from_cache() const override;
    std::string get_diagnostic_info(const game_state& gs) const override;

    void set_non_live(item_list::iterator);
    // Stage 2 substrate (item 2a): mutate the dormant fields in place, mirroring
    // set_non_live's cache.modify pattern. Declared now, called in 2b. set_dead is
    // monotone (upholds the DEAD-bit invariant: once dead, always dead).
    void set_dead(item_list::iterator);
    void update_open(item_list::iterator, uint32_t b, uint32_t g_min);
    item_list::size_type cached_size() const;

private:
    template <typename GS>
    static item_list::ctor_args_list get_init_tuple(const GS&);

    uint64_t max_num_items;
    item_list cache;
    uint64_t states_removed_from_cache;
};

#endif //SOLVITAIRE_GLOBAL_CACHE_H
