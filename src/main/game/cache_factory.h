#ifndef SOLVITAIRE_CACHE_FACTORY_H
#define SOLVITAIRE_CACHE_FACTORY_H

#include "cache_interface.h"
#include "flat_cache.h"
#include "global_cache.h"
#include "hash_only_cache.h"
#if !defined(SOLVITAIRE_LRU_ONLY)
#include "predecessor_flat_cache.h"
#endif
#include <memory>
#include <string>

#if defined(USE_GENERIC_CACHE) || defined(SOLVITAIRE_FLAT_ONLY) || defined(SOLVITAIRE_HASH_ONLY)
#include "generic_flat_cache.h"
#endif

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
#if defined(SOLVITAIRE_LRU_ONLY)
    // Accordion games use the predecessor flat cache — they are not LRU games.
    // Flat-cache-eligible games require --force-lru as an explicit benchmarking opt-in.
    (void)cache_type;
    if (use_predecessor_cache(rules))
        throw std::runtime_error(
            "lru-only binary: accordion requires predecessor cache; use solvitaire-flat");
    if (use_new_cache(rules, suit_sym) && !force_lru)
        throw std::runtime_error(
            "lru-only binary: game is flat-cache-eligible; "
            "pass --force-lru to run under lru_cache for benchmarking");
    return std::make_unique<lru_cache>(gs, capacity);
#elif defined(SOLVITAIRE_FLAT_ONLY)
    (void)gs;
    (void)cache_type;
    if (force_lru)
        throw std::runtime_error("flat-only binary: --force-lru is not supported");
    if (use_predecessor_cache(rules))
        return std::make_unique<predecessor_flat_cache>(capacity);
    if (!use_new_cache(rules, suit_sym))
        throw std::runtime_error("flat-only binary: game requires LRU cache; use default solvitaire binary");
    return std::make_unique<generic_flat_cache<CompactStatePolicy>>(capacity);
#elif defined(SOLVITAIRE_HASH_ONLY)
    (void)gs;
    (void)cache_type;
    if (force_lru)
        throw std::runtime_error("hash-only binary: --force-lru is not supported");
    if (use_predecessor_cache(rules) || !use_new_cache(rules, suit_sym))
        throw std::runtime_error("hash-only binary: game not eligible for hash-only cache");
    return std::make_unique<generic_flat_cache<HashOnlyPolicy>>(capacity);
#else
    if (cache_type == "hash-only") {
#ifdef USE_GENERIC_CACHE
        return std::make_unique<generic_flat_cache<HashOnlyPolicy>>(capacity);
#else
        return std::make_unique<hash_only_cache>(capacity);
#endif
    } else if (use_predecessor_cache(rules) && !force_lru) {
#ifdef USE_GENERIC_CACHE
        return std::make_unique<generic_flat_cache<PredecessorPolicy>>(capacity);
#else
        return std::make_unique<predecessor_flat_cache>(capacity);
#endif
    } else if (use_new_cache(rules, suit_sym) && !force_lru) {
#ifdef USE_GENERIC_CACHE
        return std::make_unique<generic_flat_cache<CompactStatePolicy>>(capacity);
#else
        return std::make_unique<flat_cache>(capacity);
#endif
    } else {
        return std::make_unique<lru_cache>(gs, capacity);
    }
#endif
}

#endif // SOLVITAIRE_CACHE_FACTORY_H
