#include "flat_cache.h"
#include "search-state/game_state.h"

// For Fibonacci hashing (multiply-high)
#ifdef __SIZEOF_INT128__
    typedef __uint128_t uint128_t;
#else
    // Fall back to 64-bit if 128-bit not available
#endif

flat_cache::flat_cache(uint64_t max_entries)
    : num_clusters(std::max<uint64_t>(1, max_entries / 2))
    , occupied_count(0)
    , eviction_count(0)
{
    clusters.resize(num_clusters);  // zero-initialised
}

uint64_t flat_cache::cluster_index(uint64_t hash) const {
#ifdef __SIZEOF_INT128__
    return (uint64_t)((uint128_t)hash * num_clusters >> 64);
#else
    return hash % num_clusters;
#endif
}

bool flat_cache::insert(const game_state& gs) {
    const uint64_t hash = gs.get_zobrist_hash();
    const compact_state& payload = gs.get_payload();

    uint64_t idx = cluster_index(hash);
    cluster& cl = clusters[idx];

    // Check if state is already present in either slot
    if (cl.entries[0].is_occupied() && cl.entries[0].matches(payload)) {
        return false;
    }
    if (cl.entries[1].is_occupied() && cl.entries[1].matches(payload)) {
        return false;
    }

    // Prepare new record
    compact_state new_state = payload;
    new_state.set_occupied(true);
    // Depth is NOT set here for Milestone 3, stays 0

    // Replacement Policy (TwoBig1):
    if (!cl.entries[0].is_occupied()) {
        // Slot 0 is empty
        cl.entries[0] = new_state;
        occupied_count++;
    } else if (!cl.entries[1].is_occupied()) {
        // Slot 1 is empty, Slot 0 is occupied
        // Compare depths: bigger (lower depth) goes to slot 0
        if (new_state.get_depth() <= cl.entries[0].get_depth()) {
            cl.entries[1] = cl.entries[0];
            cl.entries[0] = new_state;
        } else {
            cl.entries[1] = new_state;
        }
        occupied_count++;
    } else if (new_state.get_depth() <= cl.entries[0].get_depth()) {
        // Both slots full, new entry depth-wins against slot 0
        // Cascade slot 0 -> slot 1 (evicting old slot 1)
        cl.entries[1] = cl.entries[0];
        cl.entries[0] = new_state;
        eviction_count++;
    } else {
        // Both slots full, new entry depth-loses against slot 0
        // Overwrite slot 1 (evicting old slot 1)
        cl.entries[1] = new_state;
        eviction_count++;
    }

    return true;
}

bool flat_cache::contains(const game_state& gs) const {
    const uint64_t hash = gs.get_zobrist_hash();
    const compact_state& payload = gs.get_payload();

    uint64_t idx = cluster_index(hash);
    const cluster& cl = clusters[idx];

    if (cl.entries[0].is_occupied() && cl.entries[0].matches(payload)) {
        return true;
    }
    if (cl.entries[1].is_occupied() && cl.entries[1].matches(payload)) {
        return true;
    }

    return false;
}

void flat_cache::clear() {
    for (auto& cl : clusters) {
        cl.entries[0].clear();
        cl.entries[1].clear();
    }
    occupied_count = 0;
    eviction_count = 0;
}

uint64_t flat_cache::size() const {
    return occupied_count;
}

uint64_t flat_cache::get_states_removed_from_cache() const {
    return eviction_count;
}

uint64_t flat_cache::bucket_count() const {
    return num_clusters * 2;
}
