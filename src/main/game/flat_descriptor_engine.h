#ifndef SOLVITAIRE_FLAT_DESCRIPTOR_ENGINE_H
#define SOLVITAIRE_FLAT_DESCRIPTOR_ENGINE_H

// ─── flat_descriptor_engine.h ───────────────────────────────────────────────
//
// Encapsulates the descriptor-aligned Zobrist hash and payload logic for the
// flat cache (FlatPolicy, HashOnlyPolicy, PredecessorPolicy).
//
// Holds the descriptor store, hash value, and initial face-up table as state.
// Provides incremental update methods (self-contained) and context-dependent
// methods (init, determine_destination_descriptor) that take a descriptor_context.
//
// This is a pure extraction from game_state.cpp — no behaviour change.
// Stage 0.2 of the multiplicity encoding implementation plan.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <cstring>
#include <vector>
#include <list>

#include "card.h"
#include "pile.h"
#include "sol_rules.h"
#include "zobrist.h"
#include "descriptor.h"
#include "parent_table.h"

// ─── descriptor_context ─────────────────────────────────────────────────────
//
// Lightweight read-only view of game state passed to engine methods that need
// pile context. Constructed on the fly from game_state_impl members.
// Contains only references and small values — essentially free to construct.

struct descriptor_context {
    const std::vector<pile>& piles;
    const sol_rules& rules;
    const std::vector<pile::ref>& foundations;
    const std::vector<pile::ref>& original_tableau_piles;
    const std::vector<pile::ref>& original_cells;
    pile::ref hole;
    card::rank_t foundations_base;
};

// ─── flat_descriptor_engine ─────────────────────────────────────────────────
//
// Template parameter DescStore is either compact_state (FlatPolicy,
// PredecessorPolicy) or hash_descriptor_store (HashOnlyPolicy).
// Both provide: clear(), get_descriptor/set_descriptor, get_foundation/
// set_foundation, get_waste_ptr/set_waste_ptr, get_hole_top/set_hole_top.

template <typename DescStore>
class flat_descriptor_engine {
public:
    // ── State ────────────────────────────────────────────────────────────────
    DescStore store;
    uint64_t hash_value = 0;
    bool face_up_at_init[52] = {};

    // ── Accessors ────────────────────────────────────────────────────────────
    uint64_t get_hash() const { return hash_value; }
    const DescStore& get_store() const { return store; }
    DescStore& get_store() { return store; }
    bool was_initially_face_up(uint8_t cid) const { return face_up_at_init[cid]; }

    // ── Self-contained incremental updates ───────────────────────────────────

    void update_card(uint8_t cid, uint8_t new_desc) {
        uint8_t old_desc = store.get_descriptor(cid);
        store.set_descriptor(cid, new_desc);
        hash_value ^= zobrist_hash::card_key(cid, old_desc)
                     ^ zobrist_hash::card_key(cid, new_desc);
    }

    void update_foundation(uint8_t suit, uint8_t new_rank) {
        uint8_t old_rank = store.get_foundation(suit);
        store.set_foundation(suit, new_rank);
        hash_value ^= zobrist_hash::foundation_key(suit, old_rank)
                     ^ zobrist_hash::foundation_key(suit, new_rank);
    }

    void update_waste_ptr(uint8_t new_ptr) {
        uint8_t old_ptr = store.get_waste_ptr();
        store.set_waste_ptr(new_ptr);
        hash_value ^= zobrist_hash::waste_key(old_ptr)
                     ^ zobrist_hash::waste_key(new_ptr);
    }

    void update_hole_top(uint8_t new_cid) {
        uint8_t old_cid = store.get_hole_top();
        store.set_hole_top(new_cid);
        hash_value ^= zobrist_hash::hole_top_key(old_cid)
                     ^ zobrist_hash::hole_top_key(new_cid);
    }

    // ── Context-dependent: descriptor determination ──────────────────────────
    //
    // Determines what descriptor a card should get when moved to dest pile.
    // Extracted verbatim from game_state_impl::determine_destination_descriptor.

    uint8_t determine_destination_descriptor(pile::ref dest, card moved_card,
                                              const descriptor_context& ctx) const {
        // Foundation
        if (ctx.rules.foundations_present
                && !ctx.foundations.empty()
                && dest >= ctx.foundations.front()
                && dest < ctx.foundations.front() + ctx.foundations.size()) {
            return card_descriptor::STARTING;
        }

        // Hole
        if (ctx.rules.hole && dest == ctx.hole) {
            return card_descriptor::IN_HOLE;
        }

        // Cell
        if (!ctx.original_cells.empty()
                && dest >= ctx.original_cells.front()
                && dest <= ctx.original_cells.back()) {
            return card_descriptor::IN_CELL;
        }

        // Tableau: IN_SPACE if placed on empty pile, PARENT_i if on a parent card
        if (!ctx.original_tableau_piles.empty()) {
            pile::ref first_tab = ctx.original_tableau_piles.front();
            pile::ref last_tab = ctx.original_tableau_piles.back();
            if (dest >= first_tab && dest <= last_tab) {
                // After place_card, size==1 means the pile was empty before
                if (ctx.piles[dest].size() == 1) {
                    return card_descriptor::IN_SPACE;
                }
                // Card below the moved card is the parent
                card parent_card = ctx.piles[dest][1];
                // Face-down parent: card is above an unrevealed card
                if (parent_card.is_face_down()) {
                    return card_descriptor::STARTING;
                }
                uint8_t moved_cid = zobrist_hash::card_id(
                    moved_card.get_suit(), moved_card.get_rank());
                uint8_t parent_cid = zobrist_hash::card_id(
                    parent_card.get_suit(), parent_card.get_rank());
                uint8_t desc = parent_table::get_descriptor_for_parent(
                    moved_cid, parent_cid, ctx.rules.build_pol,
                    ctx.foundations_base, ctx.rules.max_rank);
                if (desc != 0) return desc;
                // Non-legal-build parent below — ROOT
                return card_descriptor::ROOT;
            }
        }

        // Reserve, stock, waste: keep STARTING
        return card_descriptor::STARTING;
    }

    // ── Context-dependent: initialisation ────────────────────────────────────
    //
    // From-scratch initialisation of descriptors and hash.
    // Extracted verbatim from game_state_impl::init_payload_and_hash.
    // waste_ptr_val is the pre-computed effective waste pointer.

    void init(const descriptor_context& ctx, uint8_t waste_ptr_val) {
        store.clear();
        hash_value = 0;

        // All cards start with descriptor STARTING (0)
        // XOR in Z_card[c][0] for all 52 cards
        for (uint8_t c = 0; c < 52; ++c) {
            hash_value ^= zobrist_hash::card_key(c, card_descriptor::STARTING);
        }

        // Foundation tops
        if (ctx.rules.foundations_present) {
            for (uint8_t s = 0; s < 4; ++s) {
                uint8_t top_rank = 0;
                if (s < ctx.foundations.size()) {
                    top_rank = ctx.piles[ctx.foundations[s]].empty()
                        ? 0 : ctx.piles[ctx.foundations[s]].top_card().get_rank();
                }
                store.set_foundation(s, top_rank);
                hash_value ^= zobrist_hash::foundation_key(s, top_rank);
            }
        }

        // Hole top
        if (ctx.rules.hole && !ctx.piles[ctx.hole].empty()) {
            card top = ctx.piles[ctx.hole].top_card();
            uint8_t cid = zobrist_hash::card_id(top.get_suit(), top.get_rank());
            store.set_hole_top(cid);
            hash_value ^= zobrist_hash::hole_top_key(cid);
        }

        // Waste pointer
        if (ctx.rules.stock_size > 0
                && ctx.rules.stock_deal_t == sol_rules::stock_deal_type::WASTE) {
            store.set_waste_ptr(waste_ptr_val);
            hash_value ^= zobrist_hash::waste_key(waste_ptr_val);
        }

        // Set positional descriptors for face-up tableau cards
        for (auto tab_ref : ctx.original_tableau_piles) {
            const pile& p = ctx.piles[tab_ref];
            for (pile::size_type i = 0; i < p.size(); ++i) {
                card c = p[i];
                if (c.is_face_down()) continue;

                uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
                uint8_t new_desc;
                if (i == p.size() - 1) {
                    // Bottom of pile
                    new_desc = card_descriptor::IN_SPACE;
                } else {
                    // Card below this one is p[i+1]
                    card parent_card = p[i + 1];
                    if (parent_card.is_face_down()) continue;
                    uint8_t parent_cid = zobrist_hash::card_id(
                        parent_card.get_suit(), parent_card.get_rank());
                    uint8_t desc = parent_table::get_descriptor_for_parent(
                        cid, parent_cid, ctx.rules.build_pol,
                        ctx.foundations_base, ctx.rules.max_rank);
                    new_desc = (desc != 0) ? desc
                        : static_cast<uint8_t>(card_descriptor::ROOT);
                }
                update_card(cid, new_desc);
            }
        }

        // Pre-filled cells: IN_CELL
        for (auto c_ref : ctx.original_cells) {
            if (!ctx.piles[c_ref].empty()) {
                card c = ctx.piles[c_ref].top_card();
                uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
                update_card(cid, card_descriptor::IN_CELL);
            }
        }

        // Hole cards
        if (ctx.rules.hole && !ctx.piles[ctx.hole].empty()) {
            for (pile::size_type i = 0; i < ctx.piles[ctx.hole].size(); ++i) {
                card c = ctx.piles[ctx.hole][i];
                uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
                update_card(cid, card_descriptor::IN_HOLE);
            }
        }
    }

    // ── Context-dependent: face-up table initialisation ──────────────────────
    //
    // Records which cards are face-up after the initial deal, and fixes up
    // IN_SPACE descriptors for single-card tableau piles.
    // Extracted from game_state_impl::init_initially_face_up.

    void init_face_up_table(const descriptor_context& ctx) {
        std::memset(face_up_at_init, 0, sizeof(face_up_at_init));
        for (const auto& p : ctx.piles) {
            for (pile::size_type i = 0; i < p.size(); ++i) {
                card c = p[i];
                if (!c.is_face_down()) {
                    uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
                    face_up_at_init[cid] = true;
                }
            }
        }

        // Fix up single-card tableau piles whose top card is face-up but still
        // has STARTING=0 (because init ran while it was face-down).
        // Guarded for predecessor-cache (accordion) games which manage
        // descriptors differently.
        if (ctx.rules.accordion_size > 0) return;

        for (auto tab_ref : ctx.original_tableau_piles) {
            if (ctx.piles[tab_ref].size() == 1
                    && !ctx.piles[tab_ref][0].is_face_down()) {
                card c = ctx.piles[tab_ref][0];
                uint8_t cid = zobrist_hash::card_id(c.get_suit(), c.get_rank());
                if (store.get_descriptor(cid) == card_descriptor::STARTING) {
                    update_card(cid, card_descriptor::IN_SPACE);
                }
            }
        }
    }

    // ── Hash recomputation (testing/verification) ────────────────────────────

    void recompute_hash(const sol_rules& rules) {
        hash_value = 0;
        for (uint8_t c = 0; c < 52; ++c) {
            hash_value ^= zobrist_hash::card_key(c, store.get_descriptor(c));
        }
        if (rules.foundations_present) {
            for (uint8_t s = 0; s < 4; ++s) {
                hash_value ^= zobrist_hash::foundation_key(s, store.get_foundation(s));
            }
        }
        if (rules.hole) {
            hash_value ^= zobrist_hash::hole_top_key(store.get_hole_top());
        }
        if (rules.stock_size > 0
                && rules.stock_deal_t == sol_rules::stock_deal_type::WASTE) {
            hash_value ^= zobrist_hash::waste_key(store.get_waste_ptr());
        }
    }
};

// ─── null_descriptor_engine ─────────────────────────────────────────────────
//
// No-op engine for LRUPolicy. All methods are trivial or absent.
// Zero size — does not inflate game_state_impl<LRUPolicy>.

struct null_descriptor_engine {
    uint64_t get_hash() const { return 0; }
};

#endif // SOLVITAIRE_FLAT_DESCRIPTOR_ENGINE_H
