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
// Created by thecharlesblake on 1/11/18.
//

#include <gtest/gtest.h>

#include "../test_helper.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/game/global_cache.h"
#include "../../main/game/cache_policy.h"

typedef sol_rules::build_policy pol;
typedef std::initializer_list<std::initializer_list<std::string>> string_il;
using lru_gs = game_state_impl<LRUPolicy>;

TEST(GlobalCache, CommutativeTableauPiles) {
    sol_rules rules;
    rules.tableau_pile_count = 3;
    rules.build_pol = sol_rules::build_policy::SAME_SUIT;
    rules.two_decks = true;  // LRU commutativity requires pile ordering; two_decks makes use_new_cache return false
    lru_gs gs(rules, string_il{{},{},{}});
    lru_cache cache(gs, 1000);

    cache.insert_t               (lru_gs(rules, {{"AC"},{"2D"},{"3H"}}));
    ASSERT_TRUE (cache.contains_t(lru_gs(rules, {{"2D"},{"3H"},{"AC"}})));
    ASSERT_FALSE(cache.contains_t(lru_gs(rules, {{"3H"},{"2D"},{"3H"}})));

    // Test with empty piles
    cache.clear();
    cache.insert_t               (lru_gs(rules, {{"4C"},{"5D"},{}}));
    ASSERT_TRUE (cache.contains_t(lru_gs(rules, {{},{"4C"},{"5D"}})));
    ASSERT_FALSE(cache.contains_t(lru_gs(rules, {{},{"4C"},{}})));

    // Test with piles of size > 1
    cache.clear();
    cache.insert_t               (lru_gs(rules, {{"6C","7D"},{"8C"},{"9D"}}));
    ASSERT_TRUE (cache.contains_t(lru_gs(rules, {{"8C"},{"6C","7D"},{"9D"}})));
    ASSERT_FALSE(cache.contains_t(lru_gs(rules, {{"8C"},{"6C","KD"},{"9D"}})));
}

TEST(GlobalCache, CommutativeReserve) {
    sol_rules rules;
    rules.reserve_size = 3;
    rules.build_pol = sol_rules::build_policy::SAME_SUIT;
    rules.two_decks = true;  // LRU commutativity requires pile ordering; two_decks makes use_new_cache return false
    lru_gs gs(rules, string_il{{},{},{}});
    lru_cache cache(gs, 1000);

    cache.insert_t               (lru_gs(rules, {{"AC"},{"2D"},{"3H"}}));
    ASSERT_TRUE (cache.contains_t(lru_gs(rules, {{"2D"},{"3H"},{"AC"}})));
    ASSERT_FALSE(cache.contains_t(lru_gs(rules, {{"3H"},{"2D"},{"3H"}})));

    // Test with empty piles
    cache.clear();
    cache.insert_t               (lru_gs(rules, {{"4C"},{"5D"},{}}));
    ASSERT_TRUE (cache.contains_t(lru_gs(rules, {{},{"4C"},{"5D"}})));
    ASSERT_FALSE(cache.contains_t(lru_gs(rules, {{},{"4C"},{}})));

    // Test with piles of size > 1
    cache.clear();
    cache.insert_t               (lru_gs(rules, {{"6C","7D"},{"8C"},{"9D"}}));
    ASSERT_TRUE (cache.contains_t(lru_gs(rules, {{"8C"},{"6C","7D"},{"9D"}})));
    ASSERT_FALSE(cache.contains_t(lru_gs(rules, {{"8C"},{"6C","KD"},{"9D"}})));
}

TEST(GlobalCache, CommutativeCells) {
    sol_rules rules;
    rules.cells = 3;
    rules.build_pol = sol_rules::build_policy::SAME_SUIT;
    rules.two_decks = true;  // LRU commutativity requires pile ordering; two_decks makes use_new_cache return false
    lru_gs gs(rules, string_il{{},{},{}});
    lru_cache cache(gs, 1000);

    cache.insert_t               (lru_gs(rules, {{"AC"},{"2D"},{"3H"}}));
    ASSERT_TRUE (cache.contains_t(lru_gs(rules, {{"2D"},{"3H"},{"AC"}})));
    ASSERT_FALSE(cache.contains_t(lru_gs(rules, {{"3H"},{"2D"},{"3H"}})));

    // Test with empty piles
    cache.clear();
    cache.insert_t               (lru_gs(rules, {{"4C"},{"5D"},{}}));
    ASSERT_TRUE (cache.contains_t(lru_gs(rules, {{},{"4C"},{"5D"}})));
    ASSERT_FALSE(cache.contains_t(lru_gs(rules, {{},{"4C"},{}})));
}
