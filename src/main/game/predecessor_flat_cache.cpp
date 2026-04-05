#include "predecessor_flat_cache.h"
#include "search-state/game_state.h"

#ifdef __SIZEOF_INT128__
    typedef __uint128_t uint128_t;
#endif

predecessor_flat_cache::predecessor_flat_cache(uint64_t max_entries)
    : num_clusters(std::max<uint64_t>(1, max_entries / 2))
    , occupied_count(0)
    , eviction_count(0)
{
    clusters.resize(num_clusters);  // zero-initialised
}

uint64_t predecessor_flat_cache::cluster_index(uint64_t hash) const {
#ifdef __SIZEOF_INT128__
    return static_cast<uint64_t>(static_cast<uint128_t>(hash) * num_clusters >> 64);
#else
    return hash % num_clusters;
#endif
}

void predecessor_flat_cache::pack_payload(const predecessor_state& ps, uint8_t* payload) {
    // Copy bytes 0-55 from predecessor_state data into payload
    // Byte 0: occupied, Byte 1: depth, Bytes 2-53: predecessor array, Bytes 54-55: spare
    std::memcpy(payload, ps.data, 56);
}

bool predecessor_flat_cache::payload_matches(const predecessor_state& ps, const uint8_t* payload) {
    // Compare bytes 2-53 (predecessor array only)
    return std::memcmp(ps.data + 2, payload + 2, 52) == 0;
}

bool predecessor_flat_cache::payload_is_occupied(const uint8_t* payload) {
    return payload[0] != 0;
}

uint8_t predecessor_flat_cache::payload_get_depth(const uint8_t* payload) {
    return payload[1];
}

bool predecessor_flat_cache::insert(const game_state& gs) {
    const uint64_t hash = gs.get_predecessor_zobrist_hash();
    const predecessor_state& ps = gs.get_predecessor_payload();

    uint64_t idx = cluster_index(hash);
    cluster& cl = clusters[idx];

    // Check if state is already present in slot 0
    if (payload_is_occupied(cl.lines[0].payload) && payload_matches(ps, cl.lines[0].payload)) {
        return false;
    }
    // Check slot 1: use hash guard first to avoid unnecessary payload comparison
    if (payload_is_occupied(cl.lines[1].payload)) {
        if (cl.lines[0].other_hash == hash && payload_matches(ps, cl.lines[1].payload)) {
            return false;
        }
    }

    // Prepare new payload
    uint8_t new_payload[56];
    std::memset(new_payload, 0, 56);
    pack_payload(ps, new_payload);
    new_payload[0] = 1;  // occupied

    uint8_t new_depth = ps.get_depth();

    // Replacement Policy (TwoBig1):
    if (!payload_is_occupied(cl.lines[0].payload)) {
        // Slot 0 is empty
        std::memcpy(cl.lines[0].payload, new_payload, 56);
        cl.lines[0].other_hash = 0;
        occupied_count++;
    } else if (!payload_is_occupied(cl.lines[1].payload)) {
        // Slot 1 is empty, Slot 0 is occupied
        if (new_depth <= payload_get_depth(cl.lines[0].payload)) {
            // New entry has lower/equal depth, promote to slot 0
            std::memcpy(cl.lines[1].payload, cl.lines[0].payload, 56);
            cl.lines[1].other_hash = hash;
            std::memcpy(cl.lines[0].payload, new_payload, 56);
            cl.lines[0].other_hash = 0;
        } else {
            std::memcpy(cl.lines[1].payload, new_payload, 56);
            cl.lines[1].other_hash = 0;
            cl.lines[0].other_hash = hash;
        }
        occupied_count++;
    } else if (new_depth <= payload_get_depth(cl.lines[0].payload)) {
        // Both slots full, new entry depth-wins against slot 0
        // Cascade slot 0 -> slot 1 (evicting old slot 1)
        std::memcpy(cl.lines[1].payload, cl.lines[0].payload, 56);
        cl.lines[1].other_hash = hash;
        std::memcpy(cl.lines[0].payload, new_payload, 56);
        cl.lines[0].other_hash = 0;
        eviction_count++;
    } else {
        // Both slots full, new entry depth-loses against slot 0
        // Overwrite slot 1 (evicting old slot 1)
        std::memcpy(cl.lines[1].payload, new_payload, 56);
        cl.lines[0].other_hash = hash;
        cl.lines[1].other_hash = 0;
        eviction_count++;
    }

    return true;
}

bool predecessor_flat_cache::contains(const game_state& gs) const {
    const uint64_t hash = gs.get_predecessor_zobrist_hash();
    const predecessor_state& ps = gs.get_predecessor_payload();

    uint64_t idx = cluster_index(hash);
    const cluster& cl = clusters[idx];

    // Check slot 0 (always fetched — same cache line)
    if (payload_is_occupied(cl.lines[0].payload) && payload_matches(ps, cl.lines[0].payload)) {
        return true;
    }

    // Use hash guard: only check slot 1 if its hash matches our probe hash
    if (payload_is_occupied(cl.lines[1].payload)) {
        if (cl.lines[0].other_hash == hash && payload_matches(ps, cl.lines[1].payload)) {
            return true;
        }
    }

    return false;
}

void predecessor_flat_cache::clear() {
    for (auto& cl : clusters) {
        std::memset(&cl, 0, sizeof(cluster));
    }
    occupied_count = 0;
    eviction_count = 0;
}

uint64_t predecessor_flat_cache::size() const {
    return occupied_count;
}

uint64_t predecessor_flat_cache::get_states_removed_from_cache() const {
    return eviction_count;
}

uint64_t predecessor_flat_cache::bucket_count() const {
    return num_clusters * 2;
}
