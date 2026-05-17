#ifndef SOLVITAIRE_GENERIC_FLAT_CACHE_POLICIES_H
#define SOLVITAIRE_GENERIC_FLAT_CACHE_POLICIES_H

// ─── generic_flat_cache_policies.h ───────────────────────────────────────────
//
// Three policies for generic_flat_cache<Policy>:
//
//   CompactStatePolicy      — wraps compact_state (32 B entries, 64 B clusters)
//                             TwoBig1 depth-preferred replacement
//
//   HashOnlyClusterPolicy   — hash-only entries (8 B each, 16 B clusters)
//                             Simple TwoBig1, no depth, 0→1 normalisation
//
//   PredecessorClusterPolicy — 56 B payload + 8 B guard hash (128 B clusters)
//                              TwoBig1 depth-preferred + hash-guard optimisation
//                              on slot 1 to avoid a second DRAM fetch
//
// C++14: no if constexpr. Tag dispatch is used throughout.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <cstring>
#include <type_traits>
#ifndef SOLVITAIRE_HASH_ONLY
#  include "compact_state.h"
#endif
#include "predecessor_state.h"
#include "multiplicity_descriptor_store.h"
#include "search-state/game_state.h"

// ─── Dispatch tag types ───────────────────────────────────────────────────────
//
// HAS_HASH_GUARD governs the slot-1 lookup in contains() and insert():
//   no_hash_guard_tag — check slot 1 unconditionally (CompactState, HashOnly)
//   hash_guard_tag    — gate slot-1 payload fetch on other_hash == probe hash
//                       (Predecessor only; avoids the second cache-line fetch)

struct no_hash_guard_tag {};
struct hash_guard_tag    {};

// Metafunction: bool → tag type for the hash-guard branch
template <bool HasGuard> struct hash_guard_select;
template <> struct hash_guard_select<false> { typedef no_hash_guard_tag type; };
template <> struct hash_guard_select<true>  { typedef hash_guard_tag    type; };

// insert_strategy selects which do_replacement() overload the template invokes:
//   insert_simple_tag      — HashOnly (no depth, always fill then evict slot 1)
//   insert_depth_tag       — CompactState (depth-preferred TwoBig1, no guard)
//   insert_predecessor_tag — Predecessor (depth-preferred TwoBig1 + guard updates)

struct insert_simple_tag      {};
struct insert_depth_tag       {};
struct insert_predecessor_tag {};


// ─── CompactStatePolicy ───────────────────────────────────────────────────────
//
// Mirrors flat_cache exactly: entries are compact_state (32 B), cluster = 64 B,
// aligned to 64 B (one cache line). TwoBig1 depth-preferred replacement.
//
// Not compiled in SOLVITAIRE_HASH_ONLY: compact_state is excluded from that path.

#ifndef SOLVITAIRE_HASH_ONLY
struct CompactStatePolicy {
    typedef compact_state payload_type;
    typedef insert_depth_tag insert_strategy;
    static const bool HAS_HASH_GUARD = false;

    // Cluster: two compact_state entries, one cache line
    struct alignas(64) cluster {
        compact_state entries[2];
    };

    template <typename GS>
    static uint64_t hash_of(const GS& gs) {
        return gs.get_zobrist_hash();
    }
    template <typename GS>
    static const compact_state& payload_of(const GS& gs) {
        return gs.get_payload();
    }

    static bool is_occupied(const cluster& cl, int slot) {
        return cl.entries[slot].is_occupied();
    }
    // Compare bytes 3-31 (game-state data only, excludes occupied flag and depth)
    static bool matches(const cluster& cl, int slot, const compact_state& payload) {
        return cl.entries[slot].matches(payload);
    }
    // Depth of an existing slot entry (bytes 1-2 of compact_state)
    static uint16_t get_depth(const cluster& cl, int slot) {
        return cl.entries[slot].get_depth();
    }
    // Depth of the incoming payload (used for depth comparison before insertion)
    static uint16_t depth_of_new(const compact_state& payload) {
        return payload.get_depth();
    }
    // Write payload into slot, marking it occupied
    static void write_slot(cluster& cl, int slot, const compact_state& payload) {
        cl.entries[slot] = payload;
        cl.entries[slot].set_occupied(true);
    }
    // Copy one slot to another (full compact_state copy, including occupied flag)
    static void copy_slot(cluster& cl, int dst, int src) {
        cl.entries[dst] = cl.entries[src];
    }
};
#endif // !SOLVITAIRE_HASH_ONLY


// ─── HashOnlyClusterPolicy ───────────────────────────────────────────────────
//
// Mirrors hash_only_cache exactly: entries are uint64_t hashes (8 B each),
// cluster = 16 B (no alignment requirement beyond natural uint64_t alignment).
// Empty sentinel = 0; if the actual hash is 0, store 1 (0→1 normalisation).
// Simple TwoBig1 — no depth comparison; slot 1 is always-replace.

struct HashOnlyClusterPolicy {
    typedef uint64_t payload_type;
    typedef insert_simple_tag insert_strategy;
    static const bool HAS_HASH_GUARD = false;

    struct cluster {
        uint64_t hashes[2];  // 0 = empty sentinel
    };

    // Map hash 0 → 1 so that 0 remains the unambiguous "empty" sentinel.
    // Collision probability of the 0→1 remapping is negligible (1/2^64).
    static uint64_t normalise(uint64_t h) {
        return h == 0u ? 1u : h;
    }
    // Both hash_of and payload_of return the normalised hash: the stored value
    // IS the identity of the state, and it is also used for cluster indexing.
    template <typename GS>
    static uint64_t hash_of(const GS& gs) {
        return normalise(gs.get_zobrist_hash());
    }
    template <typename GS>
    static uint64_t payload_of(const GS& gs) {
        return normalise(gs.get_zobrist_hash());
    }

    static bool is_occupied(const cluster& cl, int slot) {
        return cl.hashes[slot] != 0u;
    }
    static bool matches(const cluster& cl, int slot, uint64_t h) {
        return cl.hashes[slot] == h;
    }
    static void write_slot(cluster& cl, int slot, uint64_t h) {
        cl.hashes[slot] = h;
    }
    // No get_depth / depth_of_new / copy_slot — not needed by insert_simple_tag
};


// ─── PredecessorClusterPolicy ────────────────────────────────────────────────
//
// Mirrors predecessor_flat_cache exactly: each cache_line is 64 B
// (56 B payload + 8 B other_hash guard), two lines per cluster → 128 B,
// aligned to 128 B (two cache lines).
//
// Hash-guard optimisation: lines[0].other_hash stores the hash of the entry in
// slot 1. Before fetching slot 1's payload (cache line 1), we compare
// lines[0].other_hash against the probe hash; if they differ, slot 1 cannot
// match and we skip the cache-line-1 fetch entirely.
//
// Replacement: TwoBig1 depth-preferred (same logic as CompactStatePolicy).

struct PredecessorClusterPolicy {
    typedef predecessor_state payload_type;
    typedef insert_predecessor_tag insert_strategy;
    static const bool HAS_HASH_GUARD = true;

    // Payload layout (56 bytes):
    //   Byte 0:     occupied flag (0 = empty, 1 = occupied)
    //   Byte 1:     depth (uint8_t)
    //   Bytes 2-53: predecessor array (52 cards × 8 bits)
    //   Bytes 54-55: spare (zeroed)
    struct cache_line {
        uint8_t  payload[56];
        uint64_t other_hash;   // Zobrist hash of the OTHER slot's entry
    };

    // Two 64-byte cache lines = 128 bytes, aligned to 128 B
    struct alignas(128) cluster {
        cache_line lines[2];
    };

    template <typename GS>
    static uint64_t hash_of(const GS& gs) {
        return gs.get_predecessor_zobrist_hash();
    }
    template <typename GS>
    static const predecessor_state& payload_of(const GS& gs) {
        return gs.get_predecessor_payload();
    }

    static bool is_occupied(const cluster& cl, int slot) {
        return cl.lines[slot].payload[0] != 0;
    }
    // Compare bytes 2-53 (predecessor array only; excludes occupied flag and depth)
    static bool matches(const cluster& cl, int slot, const predecessor_state& ps) {
        return std::memcmp(ps.data + 2, cl.lines[slot].payload + 2, 52) == 0;
    }
    // Depth of an existing slot entry (byte 1 of the payload)
    static uint8_t get_depth(const cluster& cl, int slot) {
        return cl.lines[slot].payload[1];
    }
    // Depth of the incoming predecessor_state
    static uint8_t depth_of_new(const predecessor_state& ps) {
        return ps.get_depth();
    }
    // Pack predecessor_state into payload slot, mark occupied
    static void write_slot(cluster& cl, int slot, const predecessor_state& ps) {
        std::memcpy(cl.lines[slot].payload, ps.data, 56);
        cl.lines[slot].payload[0] = 1;  // occupied
    }
    // Copy payload only (NOT other_hash) — intentional; matches the original
    // predecessor_flat_cache which memcpys the 56-byte payload array, not the
    // full 64-byte cache_line.
    static void copy_slot(cluster& cl, int dst, int src) {
        std::memcpy(cl.lines[dst].payload, cl.lines[src].payload, 56);
    }

    // ── Hash-guard accessors ──────────────────────────────────────────────────
    // lines[0].other_hash stores the hash of the entry in slot 1.
    // lines[1].other_hash stores the hash of the entry in slot 0.
    // Only lines[0].other_hash is read in the hot path (contains / insert).

    // Guard value for checking slot 1: cl.lines[0].other_hash
    static uint64_t get_guard_hash(const cluster& cl) {
        return cl.lines[0].other_hash;
    }
    // After writing slot 1: record its hash as the guard stored in slot 0
    static void set_slot1_guard(cluster& cl, uint64_t hash) {
        cl.lines[0].other_hash = hash;
    }
    // After cascade (slot 0 → slot 1, new entry to slot 0):
    // record the new slot 0's hash in slot 1's other_hash field
    static void set_cascade_guard(cluster& cl, uint64_t hash) {
        cl.lines[1].other_hash = hash;
    }
    // When slot 0 is first written and slot 1 is still empty:
    // zero the guard to indicate "no slot-1 entry yet"
    static void clear_slot1_guard(cluster& cl) {
        cl.lines[0].other_hash = 0;
    }
};


// ─── MultiplicityClusterPolicy ───────────────────────────────────────────────
//
// Payload: multiplicity_descriptor_store (64 B). Two entries per cluster → 128 B,
// aligned to 128 B (two cache lines).
// No hash guard (HAS_HASH_GUARD = false). TwoBig1 depth-preferred replacement
// (same strategy as CompactStatePolicy).

struct MultiplicityClusterPolicy {
    typedef multiplicity_descriptor_store payload_type;
    typedef insert_depth_tag insert_strategy;
    static const bool HAS_HASH_GUARD = false;

    // Two 64-byte entries = 128 bytes, aligned to 128 B (two cache lines)
    struct alignas(128) cluster {
        multiplicity_descriptor_store entries[2];
    };

    template <typename GS>
    static uint64_t hash_of(const GS& gs) {
        return gs.get_zobrist_hash();
    }

    // payload_of dispatches on whether GS uses multiplicity_descriptor_store.
    // The false branch is only reachable from generic_flat_cache's virtual
    // insert/contains overrides (which pass the default game_state typedef) —
    // those overrides are never called in practice for MultiplicityPolicy.
    template <typename GS>
    static const multiplicity_descriptor_store& payload_of(const GS& gs) {
        return payload_of_impl(gs, typename std::is_same<
            typename GS::descriptor_store_type,
            multiplicity_descriptor_store>::type{});
    }

private:
    template <typename GS>
    static const multiplicity_descriptor_store& payload_of_impl(const GS& gs, std::true_type) {
        return gs.get_payload();
    }
    template <typename GS>
    static const multiplicity_descriptor_store& payload_of_impl(const GS&, std::false_type) {
        // Unreachable in practice: virtual cache_interface overrides with the default
        // game_state (FlatPolicy) are never called for MultiplicityPolicy.
        static multiplicity_descriptor_store dummy;
        return dummy;
    }

public:

    static bool is_occupied(const cluster& cl, int slot) {
        return cl.entries[slot].is_occupied();
    }
    // Compare bytes 3-54 (slot data only; excludes occupied flag and depth)
    static bool matches(const cluster& cl, int slot,
                        const multiplicity_descriptor_store& payload) {
        return cl.entries[slot].matches(payload);
    }
    // Depth of an existing slot entry (bytes 1-2 of multiplicity_descriptor_store)
    static uint16_t get_depth(const cluster& cl, int slot) {
        return cl.entries[slot].get_depth();
    }
    // Depth of the incoming payload
    static uint16_t depth_of_new(const multiplicity_descriptor_store& payload) {
        return payload.get_depth();
    }
    // Write payload into slot, marking it occupied
    static void write_slot(cluster& cl, int slot,
                           const multiplicity_descriptor_store& payload) {
        cl.entries[slot] = payload;
        cl.entries[slot].set_occupied(true);
    }
    // Copy one slot to another (full store copy, including occupied flag)
    static void copy_slot(cluster& cl, int dst, int src) {
        cl.entries[dst] = cl.entries[src];
    }
};

#endif // SOLVITAIRE_GENERIC_FLAT_CACHE_POLICIES_H
