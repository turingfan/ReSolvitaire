#ifndef SOLVITAIRE_CACHE_POLICY_H
#define SOLVITAIRE_CACHE_POLICY_H

// ─── cache_policy.h ──────────────────────────────────────────────────────────
//
// Dispatch policy tag types for game_state_impl<Policy>.  Each policy encodes
// at compile time which hash / payload work the game_state must perform:
//
//   Policy             computes_hash  computes_payload  descriptor_store_type  cache_type
//   ─────────────────  ─────────────  ────────────────  ─────────────────────  ──────────
//   FlatPolicy         true           true              compact_state          generic_flat_cache<CompactStatePolicy>
//   HashOnlyPolicy     true           false             hash_descriptor_store  generic_flat_cache<HashOnlyClusterPolicy>
//   PredecessorPolicy  true           true              compact_state          generic_flat_cache<PredecessorClusterPolicy>
//   LRUPolicy          false          false             (empty sentinel)       lru_cache
//
// These are purely compile-time trait structs — no data members, no virtual
// functions, no runtime cost.  The descriptor_store_type typedef uses forward
// declarations so this header has no heavy dependencies.
//
// Note: the cache *cluster* storage policies for generic_flat_cache<P> live in
// generic_flat_cache_policies.h under different names (CompactStatePolicy,
// HashOnlyClusterPolicy, PredecessorClusterPolicy).
// ─────────────────────────────────────────────────────────────────────────────

// Forward declarations — full types needed only when descriptor_store_type
// members are accessed (i.e., in game_state_impl, not here).
struct compact_state;
struct hash_descriptor_store;

// Forward declarations for cache_type typedefs — full types are defined in
// generic_flat_cache.h and global_cache.h respectively.
template <typename P> class generic_flat_cache;
struct CompactStatePolicy;
struct HashOnlyClusterPolicy;
struct PredecessorClusterPolicy;
class lru_cache;

// ─── FlatPolicy ──────────────────────────────────────────────────────────────
// Used by generic_flat_cache<CompactStatePolicy>.  Maintains both the Zobrist
// hash and the full compact_state payload descriptor.

struct FlatPolicy {
    static constexpr bool computes_hash        = true;
    static constexpr bool computes_payload     = true;
    static constexpr bool skip_pile_ordering   = true;
    typedef compact_state descriptor_store_type;
    typedef generic_flat_cache<CompactStatePolicy> cache_type;
};

// ─── HashOnlyPolicy ──────────────────────────────────────────────────────────
// Used by generic_flat_cache<HashOnlyClusterPolicy>.  Maintains the Zobrist
// hash but NOT the compact_state payload; hash_descriptor_store is used
// instead as a lightweight old-value store for incremental XOR deltas.

struct HashOnlyPolicy {
    static constexpr bool computes_hash        = true;
    static constexpr bool computes_payload     = false;
    static constexpr bool skip_pile_ordering   = true;
    typedef hash_descriptor_store descriptor_store_type;
    typedef generic_flat_cache<HashOnlyClusterPolicy> cache_type;
};

// ─── PredecessorPolicy ───────────────────────────────────────────────────────
// Used by generic_flat_cache<PredecessorClusterPolicy> (accordion games).
// Maintains a predecessor-based Zobrist hash and compact_state descriptor.

struct PredecessorPolicy {
    static constexpr bool computes_hash        = true;
    static constexpr bool computes_payload     = true;
    static constexpr bool skip_pile_ordering   = false;
    typedef compact_state descriptor_store_type;
    typedef generic_flat_cache<PredecessorClusterPolicy> cache_type;
};

// ─── LRUPolicy ───────────────────────────────────────────────────────────────
// Used by lru_cache.  Neither hash nor payload is computed; the LRU cache
// canonicalises pile order internally and does not use Zobrist hashing.

struct LRUPolicy {
    static constexpr bool computes_hash        = false;
    static constexpr bool computes_payload     = false;
    static constexpr bool skip_pile_ordering   = false;
    struct empty_descriptor_store {};
    typedef empty_descriptor_store descriptor_store_type;
    typedef lru_cache cache_type;
};

#endif // SOLVITAIRE_CACHE_POLICY_H
