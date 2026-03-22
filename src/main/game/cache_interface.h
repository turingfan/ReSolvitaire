#ifndef SOLVITAIRE_CACHE_INTERFACE_H
#define SOLVITAIRE_CACHE_INTERFACE_H

#include <cstdint>
#include "sol_rules.h"

class game_state;

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
};

// Helper function to determine if a game should use the new cache (compact_state + descriptor zobrist)
// vs the old cache (lru_cache with cached_game_state).
// The new cache is only used for single-deck games with no special sequence or accordion mechanics.
inline bool use_new_cache(const sol_rules& rules) {
    return !rules.two_decks
        && rules.sequence_count == 0
        && rules.accordion_size == 0;
}

#endif
