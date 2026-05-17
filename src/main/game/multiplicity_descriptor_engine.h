#ifndef SOLVITAIRE_MULTIPLICITY_DESCRIPTOR_ENGINE_H
#define SOLVITAIRE_MULTIPLICITY_DESCRIPTOR_ENGINE_H

// ─── multiplicity_descriptor_engine.h ───────────────────────────────────────
//
// Descriptor engine for MultiplicityPolicy.
//
// Stage 1 strategy: from-scratch recomputation.
//   - After every make_move / undo_move, recompute_all(ctx) re-walks all piles
//     to rebuild the 52 internal descriptors, then recomputes hash and payload.
//   - get_hash() / get_store() return the pre-computed values (always fresh).
//   - No incremental updates. Performance is acceptable for Stage 1 validation.
//
// Interface mirrors flat_descriptor_engine for methods called from game_state.cpp:
//   init(ctx, waste_ptr)         — initial setup (calls recompute_all)
//   init_face_up_table(ctx)      — no-op (face-down encoded in predecessor.face_down)
//   get_hash()                   — returns current Zobrist hash
//   get_store()                  — returns current payload store
//   recompute_hash(rules)        — recomputes hash+payload from descriptors[] (testing)
//   recompute_all(ctx)           — full from-scratch update from live board state
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <cstring>
#include <vector>

#include "card.h"
#include "pile.h"
#include "sol_rules.h"
#include "zobrist.h"
#include "multiplicity_descriptor.h"
#include "multiplicity_descriptor_store.h"
#include "multiplicity_zobrist.h"
#include "flat_descriptor_engine.h"   // for descriptor_context

class multiplicity_descriptor_engine {
public:
    // ── Per-card internal state ───────────────────────────────────────────────
    //
    // Rebuilt from scratch after every make/undo. Held here so recompute_hash()
    // (used in debug/test paths) can reconstruct hash+payload without board access.
    multiplicity_descriptor descriptors[52];

    // ── Computed hash and payload (always fresh after recompute_all) ─────────
    uint64_t                    hash_value;
    multiplicity_descriptor_store store;

    multiplicity_descriptor_engine() : hash_value(0) {
        multiplicity_zobrist::init();
        for (auto& d : descriptors) d = multiplicity_descriptor::make_locative(MLD_PERMANENT);
        store.clear();
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    uint64_t get_hash() const { return hash_value; }

    const multiplicity_descriptor_store& get_store() const { return store; }
    multiplicity_descriptor_store&       get_store()       { return store; }

    // ── init: called from game_state_impl::init_payload_and_hash() ───────────

    void init(const descriptor_context& ctx, uint8_t /*waste_ptr*/) {
        // waste_ptr is not used: stock/waste cards get IN_STOCK/IN_WASTE descriptors.
        recompute_all(ctx);
    }

    // ── init_face_up_table: no-op (face-down state is in predecessor.face_down) ─

    void init_face_up_table(const descriptor_context& /*ctx*/) {}

    // ── Stub incremental methods ─────────────────────────────────────────────
    // Called from if constexpr (Policy::computes_hash) blocks shared with flat
    // policies. These are no-ops; recompute_all() in make_move/undo_move handles
    // all state updates for this engine.
    void    update_card(uint8_t, uint8_t)      {}
    void    update_foundation(uint8_t, uint8_t) {}
    void    update_hole_top(uint8_t)            {}
    void    update_waste_ptr(uint8_t)           {}
    bool    was_initially_face_up(uint8_t) const { return false; }
    uint8_t determine_destination_descriptor(pile::ref, card,
                                              const descriptor_context&) const { return 0; }

    // ── recompute_all: full from-scratch update from live board state ─────────
    //
    // Walks all piles to assign descriptors[52], then calls
    // recompute_from_descriptors() to build hash_value and store.

    void recompute_all(const descriptor_context& ctx) {
        // Clear: all cards start as PERMANENT (overwritten below for every card)
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

        // ── Hole → HOLE_TOP (top), PERMANENT (rest) ──────────────────────────
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
        // waste.size() % stock_deal_count == 0, the stock/waste partition is
        // irrelevant — you can always re-deal to reach the same accessible
        // cards. In this case, all stock+waste cards get the same descriptor
        // (MLD_IN_STOCK) so the cache correctly deduplicates equivalent states.
        // When the symmetry does NOT hold, IN_STOCK vs IN_WASTE distinguishes
        // the current deal position.
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

        // ── Tableau → IN_SPACE (bottom) or predecessor(parent, face_down) ────
        // pile[0] is TOP, pile[size-1] is BOTTOM.
        // pile[i] sits on pile[i+1] (i+1 is deeper in the pile).
        for (pile::ref tab_ref : ctx.original_tableau_piles) {
            const pile& p = ctx.piles[tab_ref];
            for (pile::size_type i = 0; i < p.size(); i++) {
                card c = p[i];
                uint8_t cid = card_cid(c);

                if (i + 1 == p.size()) {
                    // Bottom of pile: sits on empty space
                    descriptors[cid] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
                } else {
                    // Sits on p[i+1] (the card one position deeper)
                    card parent = p[i + 1];
                    uint8_t parent_cid = card_cid(parent);
                    descriptors[cid] =
                        multiplicity_descriptor::make_predecessor(parent_cid, c.is_face_down());
                }
            }
        }

        recompute_from_descriptors();
    }

    // ── recompute_hash: rebuild hash+payload from descriptors[] ──────────────
    // Called by game_state_impl::compute_hash_from_scratch() (debug/test).

    void recompute_hash(const sol_rules& /*rules*/) {
        recompute_from_descriptors();
    }

private:
    // ── recompute_from_descriptors: hash+payload from current descriptors[] ──

    void recompute_from_descriptors() {
        store.clear();
        hash_value = 0;
        for (uint8_t c = 0; c < 52; c++) {
            uint8_t slot = descriptor_to_slot_byte(descriptors[c]);
            store.set_slot(c, slot);
            hash_value ^= zob_lookup(c, descriptors[c]);
        }
    }

    // ── Descriptor → payload slot byte ───────────────────────────────────────
    // Reflected encoding (v4 spec §5.1):
    //   face-up predecessor to canonical position p:   slot = p          (0..N-1)
    //   locative kind k:                               slot = N + k      (N..N+L-1)
    //   face-down predecessor to canonical position p: slot = 255 - p    (256-N..255)

    uint8_t descriptor_to_slot_byte(const multiplicity_descriptor& d) const {
        if (d.is_predecessor) {
            uint8_t pos = canonical_position(d.predecessor_card_id);
            return d.face_down ? static_cast<uint8_t>(255 - pos) : pos;
        } else {
            // N=52 for single-deck
            return static_cast<uint8_t>(52 + d.locative_kind);
        }
    }

    // ── Descriptor → Zobrist column index ────────────────────────────────────
    // For predecessors: column = canonical_position(predecessor_card)
    // For locatives:    column = N + locative_kind

    uint8_t descriptor_to_zob_index(const multiplicity_descriptor& d) const {
        if (d.is_predecessor) {
            return canonical_position(d.predecessor_card_id);
        } else {
            return static_cast<uint8_t>(52 + d.locative_kind);
        }
    }

    // ── Zobrist lookup with NOT trick for face-down predecessors ─────────────

    uint64_t zob_lookup(uint8_t class_id, const multiplicity_descriptor& d) const {
        uint8_t col = descriptor_to_zob_index(d);
        uint64_t z = multiplicity_zobrist::Z[class_id][col];
        return (d.is_predecessor && d.face_down) ? ~z : z;
    }

    // ── Canonical position (no-symmetry Stage 1: position = card ID) ─────────

    static uint8_t canonical_position(uint8_t card_id) { return card_id; }

    // ── Helper: card → card ID ────────────────────────────────────────────────

    static uint8_t card_cid(card c) {
        return zobrist_hash::card_id(c.get_suit(), c.get_rank());
    }
};

#endif // SOLVITAIRE_MULTIPLICITY_DESCRIPTOR_ENGINE_H
