#include "hash_only_cache.h"
#include "search-state/game_state.h"

// For Fibonacci hashing (multiply-high)
#ifdef __SIZEOF_INT128__
    typedef __uint128_t uint128_t;
#else
    // Fall back to modulo if 128-bit not available
#endif

hash_only_cache::hash_only_cache(uint64_t max_entries)
    : num_clusters(std::max<uint64_t>(1, max_entries / 2))
    , occupied_count(0)
    , eviction_count(0)
{
    clusters.resize(num_clusters);  // zero-initialised; 0 == empty sentinel
}

uint64_t hash_only_cache::cluster_index(uint64_t hash) const {
#ifdef __SIZEOF_INT128__
    return (uint64_t)((uint128_t)hash * num_clusters >> 64);
#else
    return hash % num_clusters;
#endif
}

bool hash_only_cache::insert(const game_state& gs) {
    const uint64_t raw_hash = gs.get_zobrist_hash();
    const uint64_t h = normalise(raw_hash);

    uint64_t idx = cluster_index(h);
    cluster& cl = clusters[idx];

    // Check if already present
    if (cl.hashes[0] == h || cl.hashes[1] == h) {
        return false;
    }

    // Replacement policy:
    // Prefer slot 0; only evict slot 1 (always-replace) when both are occupied.
    if (cl.hashes[0] == 0u) {
        // Slot 0 is empty
        cl.hashes[0] = h;
        occupied_count++;
    } else if (cl.hashes[1] == 0u) {
        // Slot 1 is empty, slot 0 is occupied — fill slot 1
        cl.hashes[1] = h;
        occupied_count++;
    } else {
        // Both slots full — always evict slot 1
        cl.hashes[1] = h;
        eviction_count++;
    }

    return true;
}

bool hash_only_cache::contains(const game_state& gs) const {
    const uint64_t raw_hash = gs.get_zobrist_hash();
    const uint64_t h = normalise(raw_hash);

    uint64_t idx = cluster_index(h);
    const cluster& cl = clusters[idx];

    return (cl.hashes[0] == h || cl.hashes[1] == h);
}

void hash_only_cache::clear() {
    for (auto& cl : clusters) {
        cl.hashes[0] = 0u;
        cl.hashes[1] = 0u;
    }
    occupied_count = 0;
    eviction_count = 0;
}

uint64_t hash_only_cache::size() const {
    return occupied_count;
}

uint64_t hash_only_cache::get_states_removed_from_cache() const {
    return eviction_count;
}

uint64_t hash_only_cache::bucket_count() const {
    return num_clusters * 2;
}
