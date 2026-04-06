#ifndef SOLVITAIRE_HASH_ONLY_CACHE_H
#define SOLVITAIRE_HASH_ONLY_CACHE_H

#include "cache_interface.h"
#include <vector>
#include <cstdint>
#include <algorithm>

/**
 * hash_only_cache: A flat, open-addressed hash table with two-slot clusters,
 * storing only the 64-bit Zobrist hash — no payload.
 *
 * Each cluster holds two uint64_t hashes (16 bytes total).
 * Multiple clusters share a cache line naturally.
 *
 * Empty sentinel: hash value 0 means empty slot.
 * If the actual Zobrist hash is 0, we store 1 instead (collision probability
 * is negligible and the alternative is a separate occupancy bit per slot).
 *
 * Replacement policy:
 * - Slot 0: preferred (only replaced when both slots are full, never directly
 *   replaced by incoming when slot 1 is available).
 * - Slot 1: always-replace when both slots are full.
 *
 * This is intentionally simpler than flat_cache's depth-aware TwoBig1 policy
 * because hash_only_cache carries no depth information.
 */
class hash_only_cache : public cache_interface {
public:
    struct cluster {
        uint64_t hashes[2];  // 0 = empty sentinel
    };

    // max_entries: approximate number of entries the cache should hold.
    // Internally sets num_clusters = max(1, max_entries / 2).
    explicit hash_only_cache(uint64_t max_entries);

    // cache_interface implementation
    bool insert(const game_state& gs) override;
    bool contains(const game_state& gs) const override;
    void clear() override;
    uint64_t size() const override;
    uint64_t get_states_removed_from_cache() const override;
    uint64_t bucket_count() const override;

private:
    // Maps a 64-bit hash to a cluster index in [0, num_clusters)
    uint64_t cluster_index(uint64_t hash) const;

    // Normalise hash: map 0 -> 1 to keep 0 as the empty sentinel
    static uint64_t normalise(uint64_t hash) {
        return hash == 0u ? 1u : hash;
    }

    std::vector<cluster> clusters;
    uint64_t num_clusters;
    uint64_t occupied_count;
    uint64_t eviction_count;
};

#endif // SOLVITAIRE_HASH_ONLY_CACHE_H
