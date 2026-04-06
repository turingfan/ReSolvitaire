#ifndef SOLVITAIRE_PREDECESSOR_DUAL_CACHE_H
#define SOLVITAIRE_PREDECESSOR_DUAL_CACHE_H

#include "cache_interface.h"
#include "global_cache.h"
#include "predecessor_flat_cache.h"
#include <iostream>

// Diagnostic wrapper that compares predecessor_flat_cache vs lru_cache.
// Similar to dual_cache but for accordion-style predecessor encoding.
class predecessor_dual_cache : public cache_interface {
public:
    predecessor_dual_cache(const game_state& gs, uint64_t capacity)
        : lru(gs, capacity)
        , pred_flat(capacity)
        , ops(0)
        , lru_only_hits(0)
        , pred_flat_only_hits(0)
        , eviction_occurred(false)
    {}

    static std::string& context() {
        static std::string s_context = "unknown";
        return s_context;
    }

    bool insert(const game_state& gs) override {
        ops++;
        bool lru_result = lru.insert(gs);
        bool pred_result = pred_flat.insert(gs);

        if (!eviction_occurred) {
            if (lru.get_states_removed_from_cache() > 0 || pred_flat.get_states_removed_from_cache() > 0) {
                eviction_occurred = true;
            }
        }

        if (!eviction_occurred && lru_result != pred_result) {
            bool lru_hit = !lru_result;
            bool pred_hit = !pred_result;

            if (lru_hit && !pred_hit) lru_only_hits++;
            else if (!lru_hit && pred_hit) pred_flat_only_hits++;
            
            std::cerr << "MISMATCH [" << context() << "] at op " << ops 
                      << ": insert() " << (lru_hit ? "LRU=HIT" : "LRU=MISS")
                      << ", Pred=" << (pred_hit ? "HIT" : "MISS") << std::endl;
        }
        return pred_result;
    }

    bool contains(const game_state& gs) const override {
        bool lru_hit = lru.contains(gs);
        bool pred_hit = pred_flat.contains(gs);

        if (!eviction_occurred && lru_hit != pred_hit) {
            std::cerr << "MISMATCH [" << context() << "] at op " << ops 
                      << ": contains() " << (lru_hit ? "LRU=HIT" : "LRU=MISS")
                      << ", Pred=" << (pred_hit ? "HIT" : "MISS") << std::endl;
        }
        return pred_hit;
    }

    void clear() override {
        lru.clear();
        pred_flat.clear();
        ops = 0;
        eviction_occurred = false;
        lru_only_hits = 0;
        pred_flat_only_hits = 0;
    }

    uint64_t size() const override { return pred_flat.size(); }
    uint64_t bucket_count() const override { return pred_flat.bucket_count(); }
    uint64_t get_states_removed_from_cache() const override { return pred_flat.get_states_removed_from_cache(); }

    uint64_t get_lru_only_hits() const { return lru_only_hits; }
    uint64_t get_pred_flat_only_hits() const { return pred_flat_only_hits; }
    bool had_eviction() const { return eviction_occurred; }

private:
    lru_cache lru;
    predecessor_flat_cache pred_flat;
    uint64_t ops;
    uint64_t lru_only_hits;
    uint64_t pred_flat_only_hits;
    bool eviction_occurred;
};

#endif // SOLVITAIRE_PREDECESSOR_DUAL_CACHE_H
