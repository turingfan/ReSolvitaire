#ifndef SOLVITAIRE_FLAT_CACHE_H
#define SOLVITAIRE_FLAT_CACHE_H

#include "cache_interface.h"
#include "compact_state.h"
#include <vector>
#include <cstdint>
#include <algorithm>
#include "platform_memory.h"

/**
 * flat_cache: A flat, open-addressed hash table with two-slot clusters.
 * Each cluster is exactly 64 bytes (one cache line), holding two 32-byte 
 * compact_state entries.
 * 
 * Replacement policy (TwoBig1):
 * - Slot 0: Depth-preferred. Overwrite only if new entry depth <= stored depth.
 * - Slot 1: Always-replace.
 */
class flat_cache : public cache_interface {
public:
    // A cluster is exactly 64 bytes (one cache line), holding 2 entries
    struct alignas(64) cluster {
        compact_state entries[2];   // 2 × 32 bytes = 64 bytes
    };

    // max_entries: approximate number of entries the cache should hold.
    // Internally rounded to determine cluster count.
    explicit flat_cache(uint64_t max_entries);
    ~flat_cache() override;

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

#if defined(__APPLE__) || defined(__linux__)
    cluster* clusters;
    size_t   alloc_bytes;
#else
    std::vector<cluster> clusters;
#endif
    uint64_t num_clusters;
    uint64_t occupied_count;
    uint64_t eviction_count;
};

#endif // SOLVITAIRE_FLAT_CACHE_H
