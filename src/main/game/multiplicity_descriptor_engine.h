#ifndef SOLVITAIRE_MULTIPLICITY_DESCRIPTOR_ENGINE_H
#define SOLVITAIRE_MULTIPLICITY_DESCRIPTOR_ENGINE_H

// ─── multiplicity_descriptor_engine.h ───────────────────────────────────────
//
// Descriptor engine for MultiplicityPolicy (v4 spec).
//
// Stage 1 (no symmetry):
//   From-scratch recomputation after every make_move / undo_move.
//   canonical_pos[c] = c for all c; hash uses XOR.
//
// Stage 2 (suit-symmetry):
//   Same from-scratch strategy.  On init(), the static class structure is
//   determined from ctx.rules + ctx.suit_sym and stored in `classes`.
//   recompute_from_descriptors() runs the 5-phase fixpoint algorithm.
//
// Interface mirrors flat_descriptor_engine for the methods called from
// game_state.cpp under if constexpr (Policy::computes_hash) guards.
// Incremental methods (update_card, update_foundation, etc.) are no-ops;
// recompute_all() at make/undo level overwrites any incremental state.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>

#include "card.h"
#include "pile.h"
#include "sol_rules.h"
#include "zobrist.h"
#include "multiplicity_descriptor.h"
#include "multiplicity_descriptor_store.h"
#include "multiplicity_zobrist.h"
#include "multiplicity_static_class.h"
#include "flat_descriptor_engine.h"   // for descriptor_context

class multiplicity_descriptor_engine {
public:
    // ── Per-card internal descriptors (rebuilt from scratch each move) ────────
    multiplicity_descriptor descriptors[52];

    // ── Static class structure (set once in init(), unchanged thereafter) ─────
    static_class_structure classes;

    // ── Computed hash and payload (always fresh after recompute_all) ─────────
    uint64_t                    hash_value;
    multiplicity_descriptor_store store;

    // ── Working arrays (scratch space during recompute_from_descriptors) ──────
    uint8_t canonical_pos[52];  // card → canonical payload position
    uint8_t slot[52];           // card → current slot byte

    multiplicity_descriptor_engine() : hash_value(0) {
        multiplicity_zobrist::init();
        for (auto& d : descriptors) d = multiplicity_descriptor::make_locative(MLD_PERMANENT);
        store.clear();
        classes.init(symmetry_mode::NONE);  // default until init() is called
        for (uint8_t i = 0; i < 52; i++) canonical_pos[i] = i;
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    uint64_t get_hash() const { return hash_value; }

    const multiplicity_descriptor_store& get_store() const { return store; }
    multiplicity_descriptor_store&       get_store()       { return store; }

    // ── init: called once from game_state_impl::init_payload_and_hash() ──────

    void init(const descriptor_context& ctx, uint8_t /*waste_ptr*/) {
        classes.init(determine_symmetry_mode(ctx.rules, ctx.suit_sym));
        recompute_all(ctx);
    }

    // ── init_face_up_table: no-op ─────────────────────────────────────────────

    void init_face_up_table(const descriptor_context& /*ctx*/) {}

    // ── Stub incremental methods ─────────────────────────────────────────────
    // Called from if constexpr (Policy::computes_hash) blocks shared with flat
    // policies.  These are no-ops; recompute_all() handles all state updates.
    void    update_card(uint8_t, uint8_t)      {}
    void    update_foundation(uint8_t, uint8_t) {}
    void    update_hole_top(uint8_t)            {}
    void    update_waste_ptr(uint8_t)           {}
    bool    was_initially_face_up(uint8_t) const { return false; }
    uint8_t determine_destination_descriptor(pile::ref, card,
                                              const descriptor_context&) const { return 0; }

    // ── recompute_all: full from-scratch update from live board state ─────────

    void recompute_all(const descriptor_context& ctx) {
        // Clear: all cards start as PERMANENT (overwritten below)
        for (uint8_t c = 0; c < 52; c++) {
            descriptors[c] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
        }

        // ── Foundations → PERMANENT ──────────────────────────────────────────
        if (ctx.rules.foundations_present) {
            for (pile::ref f_ref : ctx.foundations) {
                for (pile::size_type i = 0; i < ctx.piles[f_ref].size(); i++) {
                    card c = ctx.piles[f_ref][i];
                    descriptors[card_cid(c)] =
                        multiplicity_descriptor::make_locative(MLD_PERMANENT);
                }
            }
        }

        // ── Hole → HOLE_TOP (top card), PERMANENT (rest) ──────────────────────
        if (ctx.rules.hole && ctx.hole != pile::ref(255)) {
            const pile& h = ctx.piles[ctx.hole];
            for (pile::size_type i = 0; i < h.size(); i++) {
                card c = h[i];
                uint8_t cid = card_cid(c);
                descriptors[cid] = multiplicity_descriptor::make_locative(
                    i == 0 ? MLD_HOLE_TOP : MLD_PERMANENT);
            }
        }

        // ── Cells → IN_CELL ──────────────────────────────────────────────────
        for (pile::ref c_ref : ctx.original_cells) {
            if (!ctx.piles[c_ref].empty()) {
                card c = ctx.piles[c_ref].top_card();
                descriptors[card_cid(c)] =
                    multiplicity_descriptor::make_locative(MLD_IN_CELL);
            }
        }

        // ── Stock/Waste ──────────────────────────────────────────────────────
        // Waste-deal symmetry: when stock_redeal is enabled and
        // waste.size() % stock_deal_count == 0, collapse stock and waste
        // to the same descriptor (MLD_IN_STOCK) so the cache correctly
        // deduplicates equivalent redeal positions.
        bool waste_deal_sym = ctx.rules.stock_redeal
            && ctx.waste != pile::ref(255)
            && ctx.piles[ctx.waste].size() % ctx.rules.stock_deal_count == 0;

        if (ctx.stock != pile::ref(255)) {
            const pile& sp = ctx.piles[ctx.stock];
            for (pile::size_type i = 0; i < sp.size(); i++) {
                card c = sp[i];
                descriptors[card_cid(c)] =
                    multiplicity_descriptor::make_locative(MLD_IN_STOCK);
            }
        }

        if (ctx.waste != pile::ref(255)) {
            const pile& wp = ctx.piles[ctx.waste];
            uint8_t waste_loc = waste_deal_sym ? MLD_IN_STOCK : MLD_IN_WASTE;
            for (pile::size_type i = 0; i < wp.size(); i++) {
                card c = wp[i];
                descriptors[card_cid(c)] =
                    multiplicity_descriptor::make_locative(waste_loc);
            }
        }

        // ── Reserve → IN_RESERVE ─────────────────────────────────────────────
        if (ctx.reserve_piles != nullptr) {
            for (pile::ref r_ref : *ctx.reserve_piles) {
                for (pile::size_type i = 0; i < ctx.piles[r_ref].size(); i++) {
                    card c = ctx.piles[r_ref][i];
                    descriptors[card_cid(c)] =
                        multiplicity_descriptor::make_locative(MLD_IN_RESERVE);
                }
            }
        }

        // ── Tableau → IN_SPACE/in_space(k) (bottom) or predecessor(parent, face_down) ──
        // pile[0] = top, pile[size-1] = bottom.  pile[i] sits on pile[i+1].
        // For TABLEAU_PILES games pile identity matters: use MLD_IN_SPACE + pile_idx.
        bool pile_sym = (ctx.rules.stock_size == 0
            || ctx.rules.stock_deal_t != sol_rules::stock_deal_type::TABLEAU_PILES);

        uint8_t pile_idx = 0;
        for (pile::ref tab_ref : ctx.original_tableau_piles) {
            const pile& p = ctx.piles[tab_ref];
            uint8_t space_kind = pile_sym
                ? MLD_IN_SPACE
                : static_cast<uint8_t>(MLD_IN_SPACE + pile_idx);
            for (pile::size_type i = 0; i < p.size(); i++) {
                card c = p[i];
                uint8_t cid = card_cid(c);

                if (i + 1 == p.size()) {
                    // Bottom of pile: sits on empty space
                    descriptors[cid] = multiplicity_descriptor::make_locative(space_kind, c.is_face_down());
                } else {
                    // Sits on p[i+1] (deeper card)
                    card parent = p[i + 1];
                    uint8_t parent_cid = card_cid(parent);
                    descriptors[cid] =
                        multiplicity_descriptor::make_predecessor(parent_cid, c.is_face_down());
                }
            }
            pile_idx++;
        }

        recompute_from_descriptors();
    }

    // ── recompute_hash: rebuild hash+payload from descriptors[] ──────────────
    // Called by game_state_impl::compute_hash_from_scratch() (debug/test).

    void recompute_hash(const sol_rules& /*rules*/) {
        recompute_from_descriptors();
    }

private:
    // ─────────────────────────────────────────────────────────────────────────
    // recompute_from_descriptors: 5-phase canonicalisation algorithm (v4 §5)
    // ─────────────────────────────────────────────────────────────────────────

    void recompute_from_descriptors() {

        // ── Fast path: NONE mode (Stage 1 behaviour, no symmetry) ─────────────
        if (classes.n_classes == 52) {
            store.clear();
            hash_value = 0;
            for (uint8_t c = 0; c < 52; c++) {
                canonical_pos[c] = c;
                uint8_t s = raw_slot(c);
                store.set_slot(c, s);
                hash_value ^= zob_for_card(c, descriptors[c]);
            }
            return;
        }

        // ── Phase 0: initialise canonical_pos from current class_members order ─
        for (uint8_t cls = 0; cls < classes.n_classes; cls++) {
            const uint8_t base = classes.class_start[cls];
            for (uint8_t i = 0; i < classes.class_size; i++) {
                canonical_pos[classes.class_members[base + i]] = base + i;
            }
        }

        // ── Phase 1: initial slot bytes ───────────────────────────────────────
        for (uint8_t c = 0; c < 52; c++) {
            slot[c] = raw_slot(c);
        }

        // ── Phase 2: fixpoint ─────────────────────────────────────────────────
        // Sort each class by (slot, card_id), assign canonical_pos, recompute
        // predecessor slot bytes.  Repeat until stable (max 12 iterations).
        // Downward-only propagation guarantees convergence (v4 §4.2).
        // Loop bound used directly so `iter` is read even in release builds.
        int iter;
        for (iter = 0; iter < 12; iter++) {
            for (uint8_t cls = 0; cls < classes.n_classes; cls++) {
                const uint8_t base = classes.class_start[cls];
                sort_class(classes.class_members + base, classes.class_size);
                for (uint8_t i = 0; i < classes.class_size; i++) {
                    canonical_pos[classes.class_members[base + i]] = base + i;
                }
            }

            bool changed = false;
            for (uint8_t c = 0; c < 52; c++) {
                if (!descriptors[c].is_predecessor) continue;
                uint8_t new_s = raw_slot(c);
                if (new_s != slot[c]) { slot[c] = new_s; changed = true; }
            }

            if (!changed) break;
        }
        assert(iter < 12);  // should have converged; fires in debug if not

        // ── Phase 3: Scheme A predecessor collapsing ─────────────────────────
        // Within each static class, members with equal slot bytes form a dynamic
        // class.  All cards whose predecessor is ANY member of that dynamic class
        // receive the lowest canonical position in that class.
        //
        // A single pass is sufficient: the sorted multiset of slot bytes within
        // each class is the invariant that makes symmetric states compare equal,
        // and that invariant holds after one collapse pass + one re-sort.
        for (uint8_t cls = 0; cls < classes.n_classes; cls++) {
            const uint8_t base = classes.class_start[cls];
            const uint8_t n    = classes.class_size;
            // class_members[base..base+n-1] are in slot-sorted order from Phase 2
            for (uint8_t i = 0; i < n; ) {
                // Find the extent of this dynamic class (equal slot bytes)
                uint8_t j = i + 1;
                while (j < n && slot[classes.class_members[base + j]]
                              == slot[classes.class_members[base + i]]) {
                    j++;
                }
                if (j > i + 1) {
                    // Multi-member dynamic class: collapse all predecessor refs
                    uint8_t lowest_pos = base + i;  // canonical_pos of first member
                    for (uint8_t c = 0; c < 52; c++) {
                        if (!descriptors[c].is_predecessor) continue;
                        uint8_t q = descriptors[c].predecessor_card_id;
                        // Is q one of this dynamic class's members?
                        for (uint8_t k = i; k < j; k++) {
                            if (classes.class_members[base + k] == q) {
                                slot[c] = descriptors[c].face_down
                                    ? static_cast<uint8_t>(255 - lowest_pos)
                                    : lowest_pos;
                                break;
                            }
                        }
                    }
                }
                i = j;
            }
        }

        // Final sort+assign (one pass, after Phase 3 slot modifications)
        for (uint8_t cls = 0; cls < classes.n_classes; cls++) {
            const uint8_t base = classes.class_start[cls];
            sort_class(classes.class_members + base, classes.class_size);
            for (uint8_t i = 0; i < classes.class_size; i++) {
                canonical_pos[classes.class_members[base + i]] = base + i;
            }
        }

        // ── Phase 4: write payload ────────────────────────────────────────────
        store.clear();
        for (uint8_t cls = 0; cls < classes.n_classes; cls++) {
            const uint8_t base = classes.class_start[cls];
            for (uint8_t i = 0; i < classes.class_size; i++) {
                // set_slot(position, slot_byte)
                store.set_slot(base + i, slot[classes.class_members[base + i]]);
            }
        }

        // ── Phase 5: additive hash combining ─────────────────────────────────
        // XOR of per-class sums: sum(Z[class_id][col]) within each class.
        // Additive combining (not XOR) within each class so that two members
        // with equal slot bytes don't cancel each other out.
        hash_value = 0;
        for (uint8_t cls = 0; cls < classes.n_classes; cls++) {
            const uint8_t base = classes.class_start[cls];
            uint64_t class_sum = 0;
            for (uint8_t i = 0; i < classes.class_size; i++) {
                class_sum += zob_for_card(cls, descriptors[classes.class_members[base + i]]);
            }
            hash_value ^= class_sum;
        }
    }

    // ── raw_slot: slot byte for card c using current canonical_pos ────────────
    // Reflected encoding (v4 §5.1):
    //   face-up predecessor to canonical position p:   slot = p        (0..51)
    //   locative kind k:                               slot = 52 + k   (52..78)
    //   face-down predecessor to canonical position p: slot = 255 - p  (203..255)

    uint8_t raw_slot(uint8_t c) const {
        const auto& d = descriptors[c];
        if (d.is_predecessor) {
            uint8_t pos = canonical_pos[d.predecessor_card_id];
            return d.face_down ? static_cast<uint8_t>(255 - pos) : pos;
        } else {
            uint8_t base = static_cast<uint8_t>(52 + d.locative_kind);
            return d.face_down ? static_cast<uint8_t>(255 - base) : base;
        }
    }

    // ── zob_for_card: Zobrist contribution for one card ───────────────────────
    // class_id: the static class ID for this card (= card_id in NONE mode,
    //           classes.class_of[card_id] in COLOUR/SUIT_IRRELEVANT mode).
    // Column index uses canonical_pos of the predecessor card, not the card itself.

    uint64_t zob_for_card(uint8_t class_id, const multiplicity_descriptor& d) const {
        uint8_t col;
        if (d.is_predecessor) {
            col = canonical_pos[d.predecessor_card_id];
        } else {
            col = static_cast<uint8_t>(52 + d.locative_kind);
        }
        uint64_t z = multiplicity_zobrist::Z[class_id][col];
        return d.face_down ? ~z : z;
    }

    // ── sort_class: insertion sort of up to 4 elements ───────────────────────
    // Sorts arr[0..n-1] ascending by (slot[arr[i]], arr[i]).
    // slot[] is the current working slot-byte array.

    void sort_class(uint8_t* arr, uint8_t n) const {
        for (uint8_t i = 1; i < n; i++) {
            uint8_t key = arr[i];
            int     j   = static_cast<int>(i) - 1;
            while (j >= 0 && (slot[arr[j]] > slot[key]
                           || (slot[arr[j]] == slot[key] && arr[j] > key))) {
                arr[j + 1] = arr[j];
                j--;
            }
            arr[j + 1] = key;
        }
    }

    // ── card_cid: card → static card ID (suit*13 + rank-1) ───────────────────

    static uint8_t card_cid(card c) {
        return zobrist_hash::card_id(c.get_suit(), c.get_rank());
    }
};

#endif // SOLVITAIRE_MULTIPLICITY_DESCRIPTOR_ENGINE_H
