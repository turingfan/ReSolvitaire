#ifndef SOLVITAIRE_GENERIC_FLAT_CACHE_H
#define SOLVITAIRE_GENERIC_FLAT_CACHE_H

// ─── generic_flat_cache.h ─────────────────────────────────────────────────────
//
// A single flat, open-addressed hash table template that subsumes flat_cache,
// hash_only_cache, and predecessor_flat_cache.  Each specialisation is governed
// by a Policy type (defined in generic_flat_cache_policies.h) that supplies:
//
//   Policy::cluster         — the struct stored per two-slot bucket (with alignas)
//   Policy::payload_type    — what game_state provides for identity comparisons
//   Policy::insert_strategy — tag selecting the replacement-policy overload
//   Policy::HAS_HASH_GUARD  — bool; true only for PredecessorPolicy
//
// Plus static methods:
//   hash_of(gs), payload_of(gs)            — extract hash and payload
//   is_occupied(cl, slot), matches(cl, slot, payload)
//   get_depth(cl, slot), depth_of_new(payload)  (depth-aware policies)
//   write_slot(cl, slot, payload), copy_slot(cl, dst, src)
//   get_guard_hash(cl), set_slot1_guard(cl, h),
//   set_cascade_guard(cl, h), clear_slot1_guard(cl) (PredecessorPolicy only)
//
// C++14: no if constexpr.  Compile-time branching uses tag dispatch.
// ─────────────────────────────────────────────────────────────────────────────

#include "cache_interface.h"
#include "platform_memory.h"
#include "generic_flat_cache_policies.h"

#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>

// Non-template base so that solver.cpp can recognise any generic_flat_cache<Policy>
// via a single dynamic_cast<generic_flat_cache_base*> without instantiating the template.
class generic_flat_cache_base : public cache_interface {};

template <typename Policy>
class generic_flat_cache : public generic_flat_cache_base {
public:
    // Expose the cluster type (used in the mandatory static_asserts below)
    typedef typename Policy::cluster cluster;

private:
    typedef typename Policy::payload_type payload_type;

public:
    // max_entries: approximate number of entries the cache should hold.
    // Internally sets num_clusters = max(1, max_entries / 2).
    explicit generic_flat_cache(uint64_t max_entries)
        : num_clusters(std::max<uint64_t>(1, max_entries / 2))
        , occupied_count(0)
        , eviction_count(0)
#if defined(__APPLE__) || defined(__linux__)
        , buf(num_clusters * sizeof(cluster))
        , clusters(buf.template as<cluster>())
#endif
    {
#if !defined(__APPLE__) && !defined(__linux__)
        clusters.resize(num_clusters);  // eager zero-fill on unsupported platforms
#endif
    }

    ~generic_flat_cache() override {
        // buf destructor handles release on lazy-alloc platforms.
        // std::vector destructor handles the fallback path.
    }

    // ── cache_interface ───────────────────────────────────────────────────────

    bool insert(const game_state& gs) override {
        const uint64_t hash = Policy::hash_of(gs);
        const payload_type& payload = Policy::payload_of(gs);
        const uint64_t idx = cluster_index(hash);
        cluster& cl = clusters[idx];

        // Already present in slot 0?
        if (Policy::is_occupied(cl, 0) && Policy::matches(cl, 0, payload))
            return false;

        // Already present in slot 1? (with optional hash-guard for predecessor)
        if (check_slot1(cl, payload, hash,
                typename hash_guard_select<Policy::HAS_HASH_GUARD>::type{}))
            return false;

        // Insert — delegate replacement policy to the appropriate overload
        do_replacement(cl, payload, hash, typename Policy::insert_strategy{});
        return true;
    }

    bool contains(const game_state& gs) const override {
        const uint64_t hash = Policy::hash_of(gs);
        const payload_type& payload = Policy::payload_of(gs);
        const uint64_t idx = cluster_index(hash);
        const cluster& cl = clusters[idx];

        if (Policy::is_occupied(cl, 0) && Policy::matches(cl, 0, payload))
            return true;

        return check_slot1(cl, payload, hash,
                typename hash_guard_select<Policy::HAS_HASH_GUARD>::type{});
    }

    void clear() override {
#if defined(__APPLE__) || defined(__linux__)
        buf.reset();
#else
        for (uint64_t i = 0; i < num_clusters; ++i) {
            std::memset(&clusters[i], 0, sizeof(cluster));
        }
#endif
        occupied_count = 0;
        eviction_count = 0;
    }

    uint64_t size() const override {
        return occupied_count;
    }

    uint64_t get_states_removed_from_cache() const override {
        return eviction_count;
    }

    uint64_t bucket_count() const override {
        return num_clusters * 2;
    }

private:
    // ── Cluster indexing ──────────────────────────────────────────────────────
    // Fibonacci (multiply-high) hashing; falls back to modulo when __uint128_t
    // is unavailable.

    uint64_t cluster_index(uint64_t hash) const {
#ifdef __SIZEOF_INT128__
        typedef __uint128_t uint128_t;
        return static_cast<uint64_t>(
            static_cast<uint128_t>(hash) * num_clusters >> 64);
#else
        return hash % num_clusters;
#endif
    }

    // ── Slot-1 presence check (tag-dispatched on HAS_HASH_GUARD) ─────────────

    // No hash guard: check slot 1 directly (CompactState, HashOnly)
    bool check_slot1(const cluster& cl,
                     const payload_type& payload,
                     uint64_t /*hash*/,
                     no_hash_guard_tag) const {
        return Policy::is_occupied(cl, 1) && Policy::matches(cl, 1, payload);
    }

    // Hash guard: only compare slot-1 payload if other_hash matches probe hash.
    // This avoids fetching cache line 1 for virtually all non-matching probes.
    bool check_slot1(const cluster& cl,
                     const payload_type& payload,
                     uint64_t hash,
                     hash_guard_tag) const {
        return Policy::is_occupied(cl, 1)
            && Policy::get_guard_hash(cl) == hash
            && Policy::matches(cl, 1, payload);
    }

    // ── Replacement policies (tag-dispatched on insert_strategy) ─────────────

    // HashOnly: simple TwoBig1 — no depth comparison, always evict slot 1.
    // Matches hash_only_cache::insert exactly.
    void do_replacement(cluster& cl,
                        const payload_type& payload,
                        uint64_t /*hash*/,
                        insert_simple_tag) {
        if (!Policy::is_occupied(cl, 0)) {
            Policy::write_slot(cl, 0, payload);
            ++occupied_count;
        } else if (!Policy::is_occupied(cl, 1)) {
            Policy::write_slot(cl, 1, payload);
            ++occupied_count;
        } else {
            // Both slots full — always evict slot 1
            Policy::write_slot(cl, 1, payload);
            ++eviction_count;
        }
    }

    // CompactState: depth-preferred TwoBig1, no hash-guard updates.
    // Matches flat_cache::insert exactly.
    void do_replacement(cluster& cl,
                        const payload_type& payload,
                        uint64_t /*hash*/,
                        insert_depth_tag) {
        if (!Policy::is_occupied(cl, 0)) {
            Policy::write_slot(cl, 0, payload);
            ++occupied_count;
        } else if (!Policy::is_occupied(cl, 1)) {
            // Depth-winner goes to slot 0; loser to slot 1
            if (Policy::depth_of_new(payload) <= Policy::get_depth(cl, 0)) {
                Policy::copy_slot(cl, 1, 0);
                Policy::write_slot(cl, 0, payload);
            } else {
                Policy::write_slot(cl, 1, payload);
            }
            ++occupied_count;
        } else if (Policy::depth_of_new(payload) <= Policy::get_depth(cl, 0)) {
            // Both full; new entry wins slot 0 — cascade old slot 0 → slot 1
            Policy::copy_slot(cl, 1, 0);
            Policy::write_slot(cl, 0, payload);
            ++eviction_count;
        } else {
            // Both full; new entry loses to slot 0 — evict slot 1
            Policy::write_slot(cl, 1, payload);
            ++eviction_count;
        }
    }

    // Predecessor: depth-preferred TwoBig1 + hash-guard field maintenance.
    // Matches predecessor_flat_cache::insert exactly, including the intentional
    // design limitation where lines[0].other_hash is NOT updated after a
    // depth-win cascade (the comment in the original is preserved here).
    void do_replacement(cluster& cl,
                        const payload_type& ps,
                        uint64_t hash,
                        insert_predecessor_tag) {
        if (!Policy::is_occupied(cl, 0)) {
            Policy::write_slot(cl, 0, ps);
            Policy::clear_slot1_guard(cl);   // lines[0].other_hash = 0 (slot 1 empty)
            ++occupied_count;
        } else if (!Policy::is_occupied(cl, 1)) {
            if (Policy::depth_of_new(ps) <= Policy::get_depth(cl, 0)) {
                // New entry wins slot 0; cascade old slot 0 to slot 1
                Policy::copy_slot(cl, 1, 0);
                Policy::set_cascade_guard(cl, hash);  // lines[1].other_hash = hash
                Policy::write_slot(cl, 0, ps);
                // Note: lines[0].other_hash is NOT updated here — design
                // limitation; old slot 0's hash is not available at this point
            } else {
                // New entry goes directly to slot 1
                Policy::write_slot(cl, 1, ps);
                Policy::set_slot1_guard(cl, hash);    // lines[0].other_hash = hash
            }
            ++occupied_count;
        } else if (Policy::depth_of_new(ps) <= Policy::get_depth(cl, 0)) {
            // Both full; new entry wins slot 0 — cascade old slot 0 → slot 1
            Policy::copy_slot(cl, 1, 0);
            Policy::set_cascade_guard(cl, hash);      // lines[1].other_hash = hash
            Policy::write_slot(cl, 0, ps);
            ++eviction_count;
        } else {
            // Both full; new entry overwrites slot 1
            Policy::write_slot(cl, 1, ps);
            Policy::set_slot1_guard(cl, hash);        // lines[0].other_hash = hash
            ++eviction_count;
        }
    }

    // ── Data members ──────────────────────────────────────────────────────────
    // num_clusters must be declared before buf so it is initialised first
    // (C++ initialises members in declaration order, not MIL order).

    uint64_t num_clusters;
    uint64_t occupied_count;
    uint64_t eviction_count;
#if defined(__APPLE__) || defined(__linux__)
    platform::lazy_buffer buf;
    cluster* clusters;
#else
    std::vector<cluster> clusters;
#endif
};


// ─── Mandatory cluster-size static_asserts ────────────────────────────────────
//
// These fire in every translation unit that includes this header.  If any value
// is wrong, fix the policy struct (adjust alignas / padding), not the assert.
//
// Values are human-signed-off:
//   CompactStatePolicy: 2 × compact_state (32 B)  = 64 B, aligned 64 B
//   HashOnlyPolicy:     2 × uint64_t (8 B)         = 16 B
//   PredecessorPolicy:  2 × cache_line (64 B)      = 128 B, aligned 128 B

static_assert(sizeof(generic_flat_cache<CompactStatePolicy>::cluster) == 64,
    "generic_flat_cache<CompactStatePolicy>::cluster must be exactly 64 bytes");
static_assert(alignof(generic_flat_cache<CompactStatePolicy>::cluster) == 64,
    "generic_flat_cache<CompactStatePolicy>::cluster must be aligned to 64 bytes");

static_assert(sizeof(generic_flat_cache<HashOnlyPolicy>::cluster) == 16,
    "generic_flat_cache<HashOnlyPolicy>::cluster must be exactly 16 bytes");

static_assert(sizeof(generic_flat_cache<PredecessorPolicy>::cluster) == 128,
    "generic_flat_cache<PredecessorPolicy>::cluster must be exactly 128 bytes");
static_assert(alignof(generic_flat_cache<PredecessorPolicy>::cluster) == 128,
    "generic_flat_cache<PredecessorPolicy>::cluster must be aligned to 128 bytes");


#endif // SOLVITAIRE_GENERIC_FLAT_CACHE_H
