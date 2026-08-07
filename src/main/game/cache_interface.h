#ifndef SOLVITAIRE_CACHE_INTERFACE_H
#define SOLVITAIRE_CACHE_INTERFACE_H

#include <cstdint>
#include <string>
#include "sol_rules.h"

#include "search-state/game_state.h"

class cache_interface {
public:
    virtual ~cache_interface() = default;

    // Returns true if the state was newly inserted (not already present)
    virtual bool insert(const game_state& gs) = 0;

    // Returns true if the state is in the cache
    virtual bool contains(const game_state& gs) const = 0;

    virtual void clear() = 0;
    virtual uint64_t size() const = 0;
    virtual uint64_t get_states_removed_from_cache() const = 0;
    virtual uint64_t bucket_count() const = 0;
    virtual std::string get_diagnostic_info(const game_state&) const {
        return "No specialized diagnostic info available for this cache.\n";
    }
};

// Helper function to determine if a game should use the new cache (compact_state + descriptor zobrist)
// vs the old cache (lru_cache with cached_game_state).
// The new cache requires single-deck games with no special sequence, accordion, or spider-type
// stock dealing mechanics. Spider-type dealing (stock_deal_type::TABLEAU_PILES) distributes cards
// across tableau piles in a way that breaks the per-card descriptor model's pile symmetry assumptions.
//
// When suit-symmetry streamliner is active, the flat cache cannot provide suit-canonical deduplication
// (it hashes on actual card identity, not suit-normalised identity). The LRU cache + pile ordering
// handles this correctly, so we fall back to it whenever suit-symmetry is in use.
inline bool use_new_cache(const sol_rules& rules, bool suit_symmetry_active = false) {
    return !suit_symmetry_active
        && !rules.two_decks
        && rules.sequence_count == 0
        && rules.accordion_size == 0
        && (rules.stock_size == 0 || rules.stock_deal_t != sol_rules::stock_deal_type::TABLEAU_PILES);
}

// Helper function to determine if a game should use the predecessor cache
// (predecessor_state + predecessor Zobrist). This applies to accordion games.
inline bool use_predecessor_cache(const sol_rules& rules) {
    return rules.accordion_size > 0;
}

// Helper function to determine if a game can use the multiplicity cache.
// Eligibility: single-deck, no sequences, no accordion.
// TABLEAU_PILES games are now supported via pile-indexed in_space(k) descriptors (Stage 2B).
// Suit-symmetry is now supported (Stage 2), so suit_symmetry_active is ignored.
inline bool use_multiplicity_cache(const sol_rules& rules,
                                    bool /*suit_symmetry_active*/ = false) {
    return !rules.two_decks
        && rules.sequence_count == 0
        && rules.accordion_size == 0;
}

// Helper function to determine if a game can use the bitmap cache.
// Eligibility: single-deck, no sequences, no accordion.
// No stock_deal_type restriction and no suit-symmetry restriction — the bitmap
// cache stores only a hash bit and doesn't need pile ordering or suit canonicalisation.
inline bool use_bitmap_cache(const sol_rules& rules) {
    return !rules.two_decks
        && rules.sequence_count == 0
        && rules.accordion_size == 0;
}

#endif
