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
#include "cache_interface.h"

struct cached_game_state {
    typedef std::vector<card> state_data;
    typedef state_data::size_type size_type;

    explicit cached_game_state(const game_state&);
    void add_pile(pile::ref, const game_state&);
    void add_pile_in_reverse(pile::ref, const game_state&);
    void add_card(card, const game_state&);
    void add_card_divider();

    state_data data;
    bool live; // Is a parent in the current search tree
};

bool operator==(const cached_game_state&, const cached_game_state&);

struct hasher {
    explicit hasher(const game_state&);
    std::size_t operator() (const cached_game_state&) const;

    std::size_t hash_value(const card&) const;
    std::size_t combine(std::size_t&, std::size_t) const;

    const game_state& init_gs;
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

    explicit lru_cache(const game_state&, uint64_t);

    // Original insert method (returns iterator)
    std::pair<item_list::iterator, bool> insert_with_iterator(const game_state&);

    // cache_interface implementation
    bool insert(const game_state&) override;
    bool contains(const game_state&) const override;
    void clear() override;
    uint64_t size() const override;
    uint64_t bucket_count() const override;
    uint64_t get_states_removed_from_cache() const override;
    std::string get_diagnostic_info(const game_state& gs) const override;

    void set_non_live(item_list::iterator);
    item_list::size_type cached_size() const;

private:
    static item_list::ctor_args_list get_init_tuple(const game_state&);

    uint64_t max_num_items;
    item_list cache;
    uint64_t states_removed_from_cache;
};

#endif //SOLVITAIRE_GLOBAL_CACHE_H
