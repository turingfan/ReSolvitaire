#ifndef SOLVITAIRE_CACHE_FACTORY_H
#define SOLVITAIRE_CACHE_FACTORY_H

#include "cache_interface.h"
#include "flat_cache.h"
#include "global_cache.h"
#include "hash_only_cache.h"
#include "predecessor_flat_cache.h"
#include <memory>
#include <string>

// Creates the appropriate cache for a given game configuration.
//
// Selection order:
//   1. "hash-only"  -> hash_only_cache  (explicit opt-in; collision risk, no payload)
//   2. accordion    -> predecessor_flat_cache  (unless force_lru)
//   3. flat-eligible -> flat_cache  (unless force_lru)
//   4. fallback     -> lru_cache
//
// Parameters:
//   rules        - game rules (determines cache eligibility)
//   gs           - game state (needed to construct lru_cache)
//   capacity     - number of cache slots
//   cache_type   - "auto" (default) or "hash-only"
//   force_lru    - if true, skip all specialised caches and use lru_cache
//   suit_sym     - true when suit-symmetry streamliner is active
inline std::unique_ptr<cache_interface> make_cache(
        const sol_rules& rules,
        const game_state& gs,
        uint64_t capacity,
        const std::string& cache_type = "auto",
        bool force_lru = false,
        bool suit_sym = false) {
    if (cache_type == "hash-only") {
        return std::make_unique<hash_only_cache>(capacity);
    } else if (use_predecessor_cache(rules) && !force_lru) {
        return std::make_unique<predecessor_flat_cache>(capacity);
    } else if (use_new_cache(rules, suit_sym) && !force_lru) {
        return std::make_unique<flat_cache>(capacity);
    } else {
        return std::make_unique<lru_cache>(gs, capacity);
    }
}

#endif // SOLVITAIRE_CACHE_FACTORY_H
