#ifndef SOLVITAIRE_BITMAP_CACHE_H
#define SOLVITAIRE_BITMAP_CACHE_H

#include "cache_interface.h"
#include "platform_memory.h"
#include <cstdint>

class bitmap_cache : public cache_interface {
    platform::lazy_buffer buffer;
    uint64_t num_bits;
    uint64_t mask;

    // mutable: probe() is const but updates these for read-side accounting
    mutable uint64_t total_probes = 0;
    mutable uint64_t hit_count    = 0;
    uint64_t         insert_count = 0;

public:
    explicit bitmap_cache(uint64_t capacity_bytes)
        : buffer(capacity_bytes == 0 ? 1 : capacity_bytes),
          num_bits(0),
          mask(0)
    {
        uint64_t total_bits = capacity_bytes <= (UINT64_MAX / 8) ? capacity_bytes * 8 : UINT64_MAX;
        if (total_bits == 0) return;
        // Round down to largest power of 2 <= total_bits
        num_bits = 1ULL << (63 - __builtin_clzll(total_bits));
        mask = num_bits - 1;
    }

    // Test-and-set. Returns true if bit was ALREADY set (hit), false if newly set (miss).
    bool probe_and_insert(uint64_t hash) {
        if (num_bits == 0) return false;
        uint64_t index    = hash & mask;
        uint64_t byte_idx = index >> 3;
        uint8_t  bit_mask = uint8_t(1) << (index & 7);

        uint8_t* bytes = buffer.as<uint8_t>();
        bool was_set = (bytes[byte_idx] & bit_mask) != 0;

        ++total_probes;
        if (was_set) {
            ++hit_count;
        } else {
            bytes[byte_idx] |= bit_mask;
            ++insert_count;
        }
        return was_set;
    }

    // Read-only containment check. Returns true if bit is set.
    bool probe(uint64_t hash) const {
        if (num_bits == 0) return false;
        uint64_t index    = hash & mask;
        uint64_t byte_idx = index >> 3;
        uint8_t  bit_mask = uint8_t(1) << (index & 7);

        const uint8_t* bytes = buffer.as<uint8_t>();
        bool is_set = (bytes[byte_idx] & bit_mask) != 0;

        ++total_probes;
        if (is_set) ++hit_count;
        return is_set;
    }

    // cache_interface overrides
    // Returns true if newly inserted (cache_interface convention: true = newly inserted)
    bool insert(const game_state& gs) override {
        return !probe_and_insert(gs.get_zobrist_hash());
    }

    bool contains(const game_state& gs) const override {
        return probe(gs.get_zobrist_hash());
    }

    void clear() override {
        buffer.reset();
        total_probes = 0;
        hit_count    = 0;
        insert_count = 0;
    }

    // Logical size: number of unique insertions
    uint64_t size() const override { return insert_count; }
    uint64_t get_states_removed_from_cache() const override { return 0; }
    uint64_t bucket_count() const override { return num_bits; }

    uint64_t get_num_bits()     const { return num_bits; }
    uint64_t get_total_probes() const { return total_probes; }
    uint64_t get_hit_count()    const { return hit_count; }
    uint64_t get_insert_count() const { return insert_count; }
};

#endif // SOLVITAIRE_BITMAP_CACHE_H
