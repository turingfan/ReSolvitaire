#ifndef SOLVITAIRE_DUAL_CACHE_H
#define SOLVITAIRE_DUAL_CACHE_H

#include "cache_interface.h"
#include "global_cache.h"
#include "flat_cache.h"
#include "../input-output/output/state_printer.h"
#include <cassert>
#include <iostream>

// Wraps both lru_cache and flat_cache, asserting agreement on every operation.
// Pre-eviction: insert() and contains() must return identical results.
// Used for metamorphic testing only — not for production.
class dual_cache : public cache_interface {
public:
    dual_cache(const game_state& gs, uint64_t capacity)
        : lru(gs, capacity)
        , flat(capacity)
        , ops(0)
        , first_eviction_op(0)
        , eviction_occurred(false)
        , lru_only_hits(0)
        , flat_only_hits(0)
        , first_mismatch_op(0)
        , mismatch_zobrist_hash(0)
        , mismatch_lru_hit(false)
        , mismatch_flat_hit(false)
    {}

    static std::string& context() {
        static std::string s_context = "unknown";
        return s_context;
    }

    uint64_t get_first_mismatch_op() const { return first_mismatch_op; }
    uint64_t get_mismatch_zobrist_hash() const { return mismatch_zobrist_hash; }
    bool get_mismatch_lru_hit() const { return mismatch_lru_hit; }
    bool get_mismatch_flat_hit() const { return mismatch_flat_hit; }

    bool insert(const game_state& gs) override {
        ops++;
        bool lru_result = lru.insert(gs);
        bool flat_result = flat.insert(gs);

        if (!eviction_occurred) {
            if (lru.get_states_removed_from_cache() > 0 ||
                flat.get_states_removed_from_cache() > 0) {
                eviction_occurred = true;
                first_eviction_op = ops;
            }
        }

        if (!eviction_occurred && lru_result != flat_result) {
            bool lru_hit = !lru_result;
            bool flat_hit = !flat_result;

            if (lru_hit && !flat_hit) {
                lru_only_hits++;
            } else if (!lru_hit && flat_hit) {
                flat_only_hits++;
            }

            std::cerr << "MISMATCH [" << context() << "] at op " << ops
                      << ": insert() " << (lru_hit ? "LRU=HIT" : "LRU=MISS")
                      << ", " << (flat_hit ? "flat=HIT" : "flat=MISS") << std::endl;

            if (first_mismatch_op == 0) {
                first_mismatch_op = ops;
                mismatch_zobrist_hash = gs.get_zobrist_hash();
                mismatch_lru_hit = lru_hit;
                mismatch_flat_hit = flat_hit;
            }
        }

        return flat_result; // Return flat result to drive testing
    }

    bool contains(const game_state& gs) const override {
        bool lru_hit = lru.contains(gs);
        bool flat_hit = flat.contains(gs);

        if (!eviction_occurred && lru_hit != flat_hit) {
            std::cerr << "MISMATCH [" << context() << "] at op " << ops
                      << ": contains() " << (lru_hit ? "LRU=HIT" : "LRU=MISS")
                      << ", " << (flat_hit ? "flat=HIT" : "flat=MISS") << std::endl;
        }

        return flat_hit;
    }

    void clear() override {
        lru.clear();
        flat.clear();
        ops = 0;
        eviction_occurred = false;
        lru_only_hits = 0;
        flat_only_hits = 0;
    }

    uint64_t size() const override {
        return flat.size();
    }

    uint64_t get_states_removed_from_cache() const override {
        return flat.get_states_removed_from_cache();
    }

    uint64_t bucket_count() const override {
        return flat.bucket_count();
    }

    // Diagnostic accessors
    uint64_t get_ops() const { return ops; }
    uint64_t get_first_eviction_op() const { return first_eviction_op; }
    bool had_eviction() const { return eviction_occurred; }
    uint64_t get_lru_only_hits() const { return lru_only_hits; }
    uint64_t get_flat_only_hits() const { return flat_only_hits; }

private:
    lru_cache lru;
    flat_cache flat;
    uint64_t ops;
    uint64_t first_eviction_op;
    bool eviction_occurred;
    uint64_t lru_only_hits;
    uint64_t flat_only_hits;
    uint64_t first_mismatch_op;
    uint64_t mismatch_zobrist_hash;
    bool mismatch_lru_hit;
    bool mismatch_flat_hit;
};

#endif // SOLVITAIRE_DUAL_CACHE_H
