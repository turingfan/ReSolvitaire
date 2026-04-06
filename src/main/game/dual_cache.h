#ifndef SOLVITAIRE_DUAL_CACHE_H
#define SOLVITAIRE_DUAL_CACHE_H

#include "../input-output/output/state_printer.h"
#include "cache_interface.h"
#include "flat_cache.h"
#include "global_cache.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>

// Wraps two cache_interface implementations, asserting agreement on every
// operation. Pre-eviction: insert() and contains() must return identical
// results. Used for metamorphic testing only — not for production.
class dual_cache : public cache_interface {
public:
  // Primary is the cache whose result drives the solver (returned from
  // insert/contains). Reference is compared against primary; mismatches are
  // logged.
  dual_cache(std::unique_ptr<cache_interface> primary,
             std::unique_ptr<cache_interface> reference,
             const std::string &primary_name = "primary",
             const std::string &reference_name = "reference")
      : primary_(std::move(primary)), reference_(std::move(reference)),
        primary_name_(primary_name), reference_name_(reference_name), ops(0),
        first_eviction_op(0), eviction_occurred(false), lru_only_hits(0),
        flat_only_hits(0), first_mismatch_op(0), mismatch_zobrist_hash(0),
        mismatch_lru_hit(false), mismatch_flat_hit(false) {}

  static std::string &context() {
    static std::string s_context = "unknown";
    return s_context;
  }

  uint64_t get_first_mismatch_op() const { return first_mismatch_op; }
  uint64_t get_mismatch_zobrist_hash() const { return mismatch_zobrist_hash; }
  bool get_mismatch_lru_hit() const { return mismatch_lru_hit; }
  bool get_mismatch_flat_hit() const { return mismatch_flat_hit; }

  bool insert(const game_state &gs) override {
    ops++;
    bool primary_result = primary_->insert(gs);
    bool reference_result = reference_->insert(gs);

    if (!eviction_occurred) {
      if (primary_->get_states_removed_from_cache() > 0 ||
          reference_->get_states_removed_from_cache() > 0) {
        eviction_occurred = true;
        first_eviction_op = ops;
      }
    }

    if (!eviction_occurred && primary_result != reference_result) {
      bool primary_hit = !primary_result;
      bool reference_hit = !reference_result;

      // lru_only_hits / flat_only_hits names kept for backward compatibility
      // with existing test accessors. "lru" = reference, "flat" = primary.
      if (reference_hit && !primary_hit) {
        lru_only_hits++;
      } else if (!reference_hit && primary_hit) {
        flat_only_hits++;
      }

      std::cerr << "MISMATCH [" << context() << "] at op " << ops
                << ": insert() "
                << (reference_hit ? (reference_name_ + "=HIT")
                                  : (reference_name_ + "=MISS"))
                << ", "
                << (primary_hit ? (primary_name_ + "=HIT")
                                : (primary_name_ + "=MISS"))
                << std::endl;

      if (first_mismatch_op == 0) {
        first_mismatch_op = ops;
        mismatch_zobrist_hash = gs.get_zobrist_hash();
        mismatch_lru_hit = reference_hit;
        mismatch_flat_hit = primary_hit;
      }
    }

    return primary_result;
  }

  bool contains(const game_state &gs) const override {
    bool primary_hit = primary_->contains(gs);
    bool reference_hit = reference_->contains(gs);

    if (!eviction_occurred && primary_hit != reference_hit) {
      std::cerr << "MISMATCH [" << context() << "] at op " << ops
                << ": contains() "
                << (reference_hit ? (reference_name_ + "=HIT")
                                  : (reference_name_ + "=MISS"))
                << ", "
                << (primary_hit ? (primary_name_ + "=HIT")
                                : (primary_name_ + "=MISS"))
                << std::endl;
    }

    return primary_hit;
  }

  void clear() override {
    primary_->clear();
    reference_->clear();
    ops = 0;
    eviction_occurred = false;
    lru_only_hits = 0;
    flat_only_hits = 0;
  }

  uint64_t size() const override { return primary_->size(); }

  uint64_t get_states_removed_from_cache() const override {
    return primary_->get_states_removed_from_cache();
  }

  uint64_t bucket_count() const override { return primary_->bucket_count(); }

  std::string get_diagnostic_info(const game_state &gs) const override {
    return primary_->get_diagnostic_info(gs);
  }

  // Diagnostic accessors
  uint64_t get_ops() const { return ops; }
  uint64_t get_first_eviction_op() const { return first_eviction_op; }
  bool had_eviction() const { return eviction_occurred; }
  uint64_t get_lru_only_hits() const { return lru_only_hits; }
  uint64_t get_flat_only_hits() const { return flat_only_hits; }

private:
  std::unique_ptr<cache_interface> primary_;
  std::unique_ptr<cache_interface> reference_;
  std::string primary_name_;
  std::string reference_name_;
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
