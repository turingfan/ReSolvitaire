//
// Created by thecharlesblake on 1/10/18.
//

#include <algorithm>

#include <boost/functional/hash.hpp>

#include "global_cache.h"
#include "../solver/search_trace.h"
#include "../input-output/output/log_helper.h"
#include "search-state/game_state.h"
#include "cache_policy.h"

using namespace std;
using namespace boost;

typedef sol_rules::build_policy pol;
typedef sol_rules::stock_deal_type sdt;

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


///////////////////////
// CACHED GAME STATE //
///////////////////////

template <typename GS>
cached_game_state::cached_game_state(const GS& gs) : live(true) {
    data.reserve(52+18);  // Enough for each card and up to 18 piles

    if (gs.rules.hole) {
        add_card(gs.piles[gs.hole].top_card(), gs);
    }

    for (pile::ref pr : gs.cells) {
        add_pile(pr, gs);
    }
    if (gs.rules.cells > 0) {
        add_card_divider();
    }

    if (gs.rules.stock_size > 0) {
        add_pile(gs.stock, gs);

        if (gs.rules.stock_deal_t == sdt::WASTE) {
            bool waste_deal_symmetry = gs.rules.stock_redeal
                    && gs.piles[gs.waste].size() % gs.rules.stock_deal_count == 0;

            if (waste_deal_symmetry) {
                add_pile_in_reverse(gs.waste, gs);
            } else {
                add_card_divider();
                add_pile_in_reverse(gs.waste, gs);
            }
        }
        
        add_card_divider();
    }

    for (pile::ref pr : gs.reserve) {
        add_pile(pr, gs);
        }
    if (gs.rules.reserve_size > 0) {
        add_card_divider();
    }

    for (pile::ref pr : gs.tableau_piles) {
        add_pile(pr, gs);
        add_card_divider();
    }

    for (pile::ref pr : gs.sequences) {
        add_pile(pr, gs);
        add_card_divider();
    }

    for (pile::ref pr : gs.accordion) {
        add_card(gs.piles[pr].top_card(), gs);
    }
}

template <typename GS>
void cached_game_state::add_pile(pile::ref pr, const GS& gs) {
    for (card c : gs.piles[pr].pile_vec) {
        add_card(c, gs);
    }
}

template <typename GS>
void cached_game_state::add_pile_in_reverse(pile::ref pr, const GS& gs) {
    for (auto i = gs.piles[pr].pile_vec.size(); i-->0;) {
        card c = gs.piles[pr].pile_vec[i];
        add_card(c, gs);
    }
}

template <typename GS>
void cached_game_state::add_card(card c, const GS& gs) {
    auto& target = data;

    // If the game is a 'hole-based' game, or suit-reduction is on, reduces
    // the cached suit of the card where possible

    bool is_suit_symmetry = (gs.rules.foundations_present
            && (gs.stream_opts == GS::streamliner_options::SUIT_SYMMETRY
                || gs.stream_opts == GS::streamliner_options::BOTH))
            || gs.rules.inherent_suit_symmetry();

    if (is_suit_symmetry) {
        switch (gs.rules.build_pol) {
            case pol::SAME_SUIT:
                target.emplace_back(c);
                break;
            case pol::RED_BLACK:
                target.emplace_back(card(c.get_colour(), c.get_rank(), c.is_face_down()));
                break;
            default:
                target.emplace_back(card(0, c.get_rank(), c.is_face_down()));
                break;
        }
    } else {
        target.emplace_back(c);
    }
}

void cached_game_state::add_card_divider() {
    data.emplace_back(card::divider);
}

bool operator==(const cached_game_state& a, const cached_game_state& b) {
    return a.data == b.data;
}



//////////////////
// STATE HASHER //
//////////////////

// hasher constructor is now a template defined inline in global_cache.h

size_t hasher::operator()(const cached_game_state& cgs) const {
    size_t seed = 0;

    for (card d : cgs.data) {
        combine(seed, hash_value(d));
    }
    return seed;
}

std::size_t hasher::combine(std::size_t& seed, std::size_t value) const {
    return seed ^= value + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

size_t hasher::hash_value(card const& c) const {
    boost::hash<uint8_t> boost_hasher;

    uint8_t suit_val;
    if (is_suit_symmetry) {
        switch (build_pol) {
            case pol::SAME_SUIT:
                suit_val = c.get_suit();
                break;
            case pol::RED_BLACK:
                suit_val = c.get_colour();
                break;
            default:
                suit_val = 0;
        }
    } else {
        suit_val = c.get_suit();
    }

    auto raw_val = static_cast<uint8_t>(suit_val * 26 + 2*c.get_rank() + c.is_face_down());
    return boost_hasher(raw_val);
}


///////////////
// LRU CACHE //
///////////////

// Based on https://www.boost.org/doc/libs/1_67_0/libs/multi_index/example/serialization.cpp
/* Boost.MultiIndex example of serialization of a MRU list.
 *
 * Copyright 2003-2008 Joaquin M Lopez Munoz.
 * Distributed under the Boost Software License, Version 1.0.
 * (See accompanying file LICENSE_1_0.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt)
 *
 * See http://www.boost.org/libs/multi_index for library home page.
 */

template <typename GS>
item_list::ctor_args_list lru_cache::get_init_tuple(const GS& gs) {
    return boost::make_tuple(
            item_list::nth_index<0>::type::ctor_args(),
            boost::make_tuple(
                    size_t(0),
                    multi_index::identity<cached_game_state>(),
                    hasher(gs),
                    equal_to<cached_game_state>()
                    )
            );
}

template <typename GS>
lru_cache::lru_cache(const GS& gs, uint64_t max_num_items_)
        : max_num_items(max_num_items_), cache(get_init_tuple(gs)), states_removed_from_cache(0) {
}

template <typename GS>
pair<item_list::iterator, bool> lru_cache::insert_with_iterator(const GS& gs) {
    pair<item_list::iterator, bool> p = cache.push_front(cached_game_state(gs));

    if(!p.second){                              /* duplicate item */
        cache.relocate(cache.begin(), p.first); /* put in front */
    } else if(cache.size() > max_num_items){    /* keep the length <= max_num_items */

        // If the least recently used node is 'live' (i.e. a parent), relocates
        // it to the head of the list until this is no longer the case
        for (uint64_t i = 0; prev(cache.end())->live; i++) {
            cache.relocate(cache.begin(), prev(cache.end()));

            if (i == max_num_items) {
#ifndef NDEBUG
                LOG_ERROR("All items in cache are live and cache is full");
#endif
                throw runtime_error("All items in cache are live and cache is full");
            }
        }
        STRACE_EVICT();
        cache.pop_back();
        states_removed_from_cache++;
    }
    return p;
}

bool lru_cache::insert(const game_state& gs) {
    return insert_with_iterator(gs).second;
}

bool lru_cache::contains(const game_state& gs) const {
    return cache.get<1>().count(cached_game_state(gs)) > 0;
}

void lru_cache::clear() {
    cache.clear();
}

uint64_t lru_cache::size() const {
    return static_cast<uint64_t>(cache.size());
}

item_list::size_type lru_cache::cached_size() const {
    return cache.size();
}

uint64_t lru_cache::bucket_count() const {
    return static_cast<uint64_t>(cache.get<1>().bucket_count());
}

void lru_cache::set_non_live(item_list::iterator state_iter) {
#ifndef NDEBUG
    bool succ =
#endif
    cache.modify(state_iter, [](auto& v){ v.live = false; });
#ifndef NDEBUG
    assert(succ);
#endif
}

uint64_t lru_cache::get_states_removed_from_cache() const {
    return states_removed_from_cache;
}

std::string lru_cache::get_diagnostic_info(const game_state& gs) const {
    std::string res = "LRU Cache Diagnostic:\n";
    cached_game_state cgs(gs);
    bool present = cache.get<1>().count(cgs) > 0;
    res += "  Status: " + std::string(present ? "HIT" : "MISS") + "\n";
    res += "  Stored Data (Card IDs): ";
    for (card c : cgs.data) {
        if (c == card::divider) res += "| ";
        else res += std::to_string((int)zobrist_hash::card_id(c.get_suit(), c.get_rank())) + " ";
    }
    res += "\n";
    return res;
}


///////////////////////////
// EXPLICIT INSTANTIATIONS
///////////////////////////

// game_state typedef = game_state_impl<FlatPolicy> in the default binary.
// These instantiations cover the virtual override path (dual_cache tests)
// and any direct use of lru_cache with game_state.

#if defined(SOLVITAIRE_LRU_ONLY)
// LRU-only: game_state = game_state_impl<LRUPolicy>
template cached_game_state::cached_game_state(const game_state_impl<LRUPolicy>&);
template void cached_game_state::add_pile(pile::ref, const game_state_impl<LRUPolicy>&);
template void cached_game_state::add_pile_in_reverse(pile::ref, const game_state_impl<LRUPolicy>&);
template void cached_game_state::add_card(card, const game_state_impl<LRUPolicy>&);
template lru_cache::lru_cache(const game_state_impl<LRUPolicy>&, uint64_t);
template std::pair<item_list::iterator, bool> lru_cache::insert_with_iterator(const game_state_impl<LRUPolicy>&);

#elif defined(SOLVITAIRE_FLAT_ONLY) || defined(SOLVITAIRE_HASH_ONLY)
// Flat-only / Hash-only: game_state = game_state_impl<FlatPolicy/HashOnlyPolicy>
// lru_cache is not used in the solver, but virtual overrides are compiled
// since lru_cache inherits cache_interface. Instantiate with game_state.
template cached_game_state::cached_game_state(const game_state&);
template void cached_game_state::add_pile(pile::ref, const game_state&);
template void cached_game_state::add_pile_in_reverse(pile::ref, const game_state&);
template void cached_game_state::add_card(card, const game_state&);
template lru_cache::lru_cache(const game_state&, uint64_t);
template std::pair<item_list::iterator, bool> lru_cache::insert_with_iterator(const game_state&);

#else
// Default binary: all four policies. lru_cache used by LRUPolicy solver.
// Virtual overrides use game_state = game_state_impl<FlatPolicy>.
template cached_game_state::cached_game_state(const game_state_impl<FlatPolicy>&);
template void cached_game_state::add_pile(pile::ref, const game_state_impl<FlatPolicy>&);
template void cached_game_state::add_pile_in_reverse(pile::ref, const game_state_impl<FlatPolicy>&);
template void cached_game_state::add_card(card, const game_state_impl<FlatPolicy>&);
template lru_cache::lru_cache(const game_state_impl<FlatPolicy>&, uint64_t);
template std::pair<item_list::iterator, bool> lru_cache::insert_with_iterator(const game_state_impl<FlatPolicy>&);

// LRUPolicy solver path (game_state_impl<LRUPolicy>)
template cached_game_state::cached_game_state(const game_state_impl<LRUPolicy>&);
template void cached_game_state::add_pile(pile::ref, const game_state_impl<LRUPolicy>&);
template void cached_game_state::add_pile_in_reverse(pile::ref, const game_state_impl<LRUPolicy>&);
template void cached_game_state::add_card(card, const game_state_impl<LRUPolicy>&);
template lru_cache::lru_cache(const game_state_impl<LRUPolicy>&, uint64_t);
template std::pair<item_list::iterator, bool> lru_cache::insert_with_iterator(const game_state_impl<LRUPolicy>&);
#endif
