#ifndef SOLVITAIRE_PREDECESSOR_FLAT_CACHE_H
#define SOLVITAIRE_PREDECESSOR_FLAT_CACHE_H

#include "cache_interface.h"
#include "predecessor_state.h"
#include <vector>
#include <cstdint>
#include <algorithm>

/**
 * predecessor_flat_cache: A flat, open-addressed hash table with two-slot clusters
 * for predecessor-encoded accordion game states.
 *
 * Each cluster is 128 bytes (two cache lines), holding two entries.
 * Each entry: 56 bytes payload + 8 bytes hash guard of the OTHER entry.
 *
 * Hash guard logic on lookup:
 *   1. Check if entry 0 matches (payload in cache line 0, already fetched)
 *   2. If no match, compare probe hash against lines[0].other_hash (entry 1's hash)
 *   3. Only if hash matches (~1/2^64 false positive), fetch cache line 1 and compare payload
 *   This avoids a second DRAM fetch for virtually all non-matching probes.
 *
 * Replacement: TwoBig1 — slot 0 is depth-preferred, slot 1 always-replace.
 */
class predecessor_flat_cache : public cache_interface {
public:
    // Payload layout in cache_line (56 bytes):
    //   Byte 0:     occupied flag
    //   Byte 1:     depth (uint8_t)
    //   Bytes 2-53: predecessor array (52 cards)
    //   Bytes 54-55: spare (zeroed)
    struct cache_line {
        uint8_t payload[56];     // predecessor_state data packed into 56 bytes
        uint64_t other_hash;     // Zobrist hash of the OTHER entry in this cluster
    };

    struct alignas(128) cluster {
        cache_line lines[2];     // 2 x 64 bytes = 128 bytes
    };

    explicit predecessor_flat_cache(uint64_t max_entries);

    // cache_interface implementation
    bool insert(const game_state& gs) override;
    bool contains(const game_state& gs) const override;
    void clear() override;
    uint64_t size() const override;
    uint64_t get_states_removed_from_cache() const override;
    uint64_t bucket_count() const override;
    std::string get_diagnostic_info(const game_state& gs) const override;

private:
    uint64_t cluster_index(uint64_t hash) const;

    // Pack a predecessor_state into a 56-byte payload
    static void pack_payload(const predecessor_state& ps, uint8_t* payload);
    // Compare a predecessor_state against a 56-byte payload (bytes 2-53 only)
    static bool payload_matches(const predecessor_state& ps, const uint8_t* payload);
    // Check if a payload slot is occupied
    static bool payload_is_occupied(const uint8_t* payload);
    // Get depth from a payload
    static uint8_t payload_get_depth(const uint8_t* payload);

    std::vector<cluster> clusters;
    uint64_t num_clusters;
    uint64_t occupied_count;
    uint64_t eviction_count;
};

#endif // SOLVITAIRE_PREDECESSOR_FLAT_CACHE_H
