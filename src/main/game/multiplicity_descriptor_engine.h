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
    // ── Constants ───────────────────────────────────────────────────────────
    // N = deck size. Currently 52 (single-deck); will be 104 for two-deck.
    // Use a constant so two-deck generalisation is a single change.
    static constexpr uint8_t N = 52;  // TODO: derive from rules.two_decks

    // ── Per-card internal descriptors (rebuilt from scratch each move) ────────
    multiplicity_descriptor descriptors[104];  // sized for two-deck max

    // ── Static class structure (set once in init(), unchanged thereafter) ─────
    static_class_structure classes;

    // ── Computed hash and payload (always fresh after recompute_all) ─────────
    uint64_t                    hash_value;
    multiplicity_descriptor_store store;

    // ── Working arrays (scratch space during recompute_from_descriptors) ──────
    uint8_t canonical_pos[104];  // card → canonical payload position
    uint8_t slot[104];           // card → current slot byte

    // ── Auxiliary data for incremental updates ──────────────────────────────
    int8_t   children[104];      // children[q] = card sitting on q, or -1
                                 // Sized for two-deck (104); only [0..N-1] used
    uint64_t class_sum[52];      // per-static-class Zobrist sum (indexed by class_id)
                                 // only [0..n_classes-1] used

    // ── Scratch arrays (only meaningful during incremental_update) ──────────
    uint8_t  old_slot_save[104]; // saved pre-update slot bytes for changed cards
    uint64_t changed_mask_lo;    // bitmask of cards 0-63 whose slot bytes changed
    uint64_t changed_mask_hi;    // bitmask of cards 64-103 (two-deck only)

    multiplicity_descriptor_engine() : hash_value(0), changed_mask_lo(0), changed_mask_hi(0) {
        multiplicity_zobrist::init();
        for (uint8_t i = 0; i < N; i++)
            descriptors[i] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
        store.clear();
        classes.init(symmetry_mode::NONE);  // default until init() is called
        for (uint8_t i = 0; i < N; i++) canonical_pos[i] = i;
        std::memset(children, -1, sizeof(children));
        std::memset(class_sum, 0, sizeof(class_sum));
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

    // ── Mode queries ──────────────────────────────────────────────────────────

    bool is_none_mode() const { return classes.n_classes == N; }

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

    // ── Incremental update: NONE mode O(k) fast path ──────────────────────────
    // Updates descriptors, children[], slot bytes, class_sum[], hash, and payload
    // for the given changed cards. No cascade needed in NONE mode (canonical_pos
    // is identity-mapped and never changes).

    void incremental_update_none(
        const std::pair<uint8_t, multiplicity_descriptor>* changes,
        uint8_t n_changes)
    {
        assert(classes.n_classes == N);  // NONE mode only

        for (uint8_t i = 0; i < n_changes; i++) {
            uint8_t c = changes[i].first;
            const auto& new_d = changes[i].second;
            const auto& old_d = descriptors[c];

            // Update children[]
            if (old_d.is_predecessor)
                children[old_d.predecessor_card_id] = -1;
            if (new_d.is_predecessor)
                children[new_d.predecessor_card_id] = static_cast<int8_t>(c);

            // Update descriptor
            descriptors[c] = new_d;

            // Compute new slot byte (NONE: canonical_pos[c] = c always)
            uint8_t new_s = raw_slot(c);

            // Update hash via class_sum delta
            // In NONE mode: class_id = card_id, so class_sum[c] = zob_for_card(c, c)
            hash_value ^= class_sum[c];          // XOR out old
            slot[c] = new_s;
            class_sum[c] = zob_for_card(c, c);   // recompute with new slot
            hash_value ^= class_sum[c];          // XOR in new

            // Update payload (NONE: canonical_pos = card_id, so store position = c)
            store.set_slot(c, new_s);
        }
    }

    // ── Incremental update: full cascade (COLOUR / SUIT_IRRELEVANT modes) ────
    // Also works for NONE mode (cascade never fires), but with slightly more
    // overhead than incremental_update_none().

    void incremental_update(
        const std::pair<uint8_t, multiplicity_descriptor>* changes,
        uint8_t n_changes)
    {
        // ── Step 0: Descriptor update ──────────────────────────────────────────
        uint64_t dirty_classes = 0;
        changed_mask_lo = 0;
        changed_mask_hi = 0;

        for (uint8_t i = 0; i < n_changes; i++) {
            uint8_t c = changes[i].first;
            const auto& new_d = changes[i].second;
            const auto& old_d = descriptors[c];

            // Update children[]
            if (old_d.is_predecessor)
                children[old_d.predecessor_card_id] = -1;
            if (new_d.is_predecessor)
                children[new_d.predecessor_card_id] = static_cast<int8_t>(c);

            // Save old slot byte
            old_slot_save[c] = slot[c];

            // Update descriptor
            descriptors[c] = new_d;

            // Compute new slot byte.  For predecessor cards, use
            // collapsed_pos (Scheme A) instead of raw canonical_pos so
            // the slot matches what recompute_from_descriptors() produces.
            uint8_t new_s;
            if (new_d.is_predecessor) {
                uint8_t pos = collapsed_pos(new_d.predecessor_card_id);
                new_s = new_d.face_down
                    ? static_cast<uint8_t>(255 - pos) : pos;
            } else {
                new_s = raw_slot(c);
            }
            if (new_s != slot[c]) {
                slot[c] = new_s;
                dirty_classes |= (1ULL << classes.class_of[c]);
                set_changed(c);
            }
        }

        // ── Step 1: BFS cascade ────────────────────────────────────────────────
        for (int cascade_iter = 0; dirty_classes != 0; cascade_iter++) {
            assert(cascade_iter < 20 && "cascade did not converge");
            (void)cascade_iter;
            uint64_t next_dirty = 0;

            // Process each dirty class
            uint64_t tmp = dirty_classes;
            while (tmp != 0) {
                uint8_t cls = static_cast<uint8_t>(__builtin_ctzll(tmp));
                tmp &= tmp - 1;  // clear lowest set bit

                const uint8_t base = classes.class_start[cls];

                // Re-sort class members by (slot[member], member)
                sort_class(classes.class_members + base, classes.class_size);

                // Reassign canonical_pos
                for (uint8_t j = 0; j < classes.class_size; j++) {
                    canonical_pos[classes.class_members[base + j]] = base + j;
                }

                // Check children of ALL members
                for (uint8_t j = 0; j < classes.class_size; j++) {
                    uint8_t m = classes.class_members[base + j];
                    int8_t child = children[m];
                    if (child < 0) continue;
                    uint8_t uc = static_cast<uint8_t>(child);
                    if (!descriptors[uc].is_predecessor) continue;

                    uint8_t pos = collapsed_pos(m);
                    uint8_t new_s = descriptors[uc].face_down
                        ? static_cast<uint8_t>(255 - pos) : pos;

                    if (new_s != slot[uc]) {
                        if (!is_changed(uc)) {
                            old_slot_save[uc] = slot[uc];  // first change for this card
                        }
                        slot[uc] = new_s;
                        set_changed(uc);
                        next_dirty |= (1ULL << classes.class_of[uc]);
                    }
                }
            }

            dirty_classes = next_dirty;
        }

        // ── Step 2: Post-cascade update ────────────────────────────────────────
        // Collect affected classes from changed cards
        uint64_t affected_classes = 0;
        for (uint8_t c = 0; c < N; c++) {
            if (is_changed(c))
                affected_classes |= (1ULL << classes.class_of[c]);
        }

        // Rebuild payload and hash for affected classes
        uint64_t ac = affected_classes;
        while (ac != 0) {
            uint8_t cls = static_cast<uint8_t>(__builtin_ctzll(ac));
            ac &= ac - 1;

            const uint8_t base = classes.class_start[cls];

            // XOR out old class sum
            hash_value ^= class_sum[cls];

            // Recompute class sum and payload entries
            uint64_t sum = 0;
            for (uint8_t j = 0; j < classes.class_size; j++) {
                uint8_t m = classes.class_members[base + j];
                store.set_slot(base + j, slot[m]);
                sum += zob_for_card(cls, m);
            }
            class_sum[cls] = sum;

            // XOR in new class sum
            hash_value ^= class_sum[cls];
        }
    }

    // ── verify_against_scratch: debug-mode oracle ────────────────────────────
    // After every incremental update, recompute from scratch and assert match.
    // Saves and restores all engine state so the incremental result is preserved.

#ifndef NDEBUG
    void verify_against_scratch(const descriptor_context& ctx) {
        // Save incremental state
        uint64_t saved_hash = hash_value;
        multiplicity_descriptor_store saved_store = store;
        uint8_t saved_slot[104], saved_canonical[104];
        int8_t saved_children[104];
        uint64_t saved_class_sum[52];
        uint8_t saved_class_members[104];
        multiplicity_descriptor saved_descriptors[104];
        std::memcpy(saved_slot, slot, N);
        std::memcpy(saved_canonical, canonical_pos, N);
        std::memcpy(saved_children, children, N);
        std::memcpy(saved_class_sum, class_sum, sizeof(uint64_t) * classes.n_classes);
        std::memcpy(saved_class_members, classes.class_members, N);
        std::memcpy(saved_descriptors, descriptors, N * sizeof(multiplicity_descriptor));

        // Recompute from scratch
        recompute_all(ctx);

        // Compare core outputs
        assert(hash_value == saved_hash && "incremental hash mismatch");
        assert(store.matches(saved_store) && "incremental payload mismatch");

        // Restore ALL state
        hash_value = saved_hash;
        store = saved_store;
        std::memcpy(slot, saved_slot, N);
        std::memcpy(canonical_pos, saved_canonical, N);
        std::memcpy(children, saved_children, N);
        std::memcpy(class_sum, saved_class_sum, sizeof(uint64_t) * classes.n_classes);
        std::memcpy(classes.class_members, saved_class_members, N);
        std::memcpy(descriptors, saved_descriptors, N * sizeof(multiplicity_descriptor));
    }
#endif

    // ── recompute_all: full from-scratch update from live board state ─────────

    void recompute_all(const descriptor_context& ctx) {
        // Clear: all cards start as PERMANENT (overwritten below)
        for (uint8_t c = 0; c < N; c++) {
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
                ? static_cast<uint8_t>(MLD_IN_SPACE)
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
        if (classes.n_classes == N) {
            store.clear();
            hash_value = 0;
            for (uint8_t c = 0; c < N; c++) {
                canonical_pos[c] = c;
                uint8_t s = raw_slot(c);
                slot[c] = s;
                store.set_slot(c, s);
                hash_value ^= zob_for_card(c, c);
            }
            rebuild_auxiliary();
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
        for (uint8_t c = 0; c < N; c++) {
            slot[c] = raw_slot(c);
        }

        // ── Phase 2: fixpoint with integrated Scheme A collapsing ─────────────
        // Each iteration: sort classes by slot byte, assign canonical_pos,
        // then recompute predecessor slot bytes using the COLLAPSED position
        // of the predecessor's indistinguishable group (not the raw
        // canonical_pos).  This merges the old "recompute" and "Scheme A"
        // steps so they don't fight each other.
        //
        // Terminates because each iteration can only reduce the number of
        // distinct slot values (bounded by N).
        int iter;
        for (iter = 0; iter < 20; iter++) {
            // Sort each class and assign canonical_pos
            for (uint8_t cls = 0; cls < classes.n_classes; cls++) {
                const uint8_t base = classes.class_start[cls];
                sort_class(classes.class_members + base, classes.class_size);
                for (uint8_t i = 0; i < classes.class_size; i++) {
                    canonical_pos[classes.class_members[base + i]] = base + i;
                }
            }

            // Recompute predecessor slot bytes using collapsed positions.
            // For each predecessor card, find the indistinguishable group
            // containing its predecessor target and use the group's lowest
            // canonical_pos instead of the target's individual canonical_pos.
            bool changed = false;
            for (uint8_t c = 0; c < N; c++) {
                if (!descriptors[c].is_predecessor) continue;
                uint8_t q = descriptors[c].predecessor_card_id;
                uint8_t pos = collapsed_pos(q);
                uint8_t new_s = descriptors[c].face_down
                    ? static_cast<uint8_t>(255 - pos) : pos;
                if (new_s != slot[c]) { slot[c] = new_s; changed = true; }
            }

            if (!changed) break;
        }
        assert(iter < 20);  // must converge; fires in debug if not

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
            uint64_t sum = 0;
            for (uint8_t i = 0; i < classes.class_size; i++) {
                sum += zob_for_card(cls, classes.class_members[base + i]);
            }
            class_sum[cls] = sum;
            hash_value ^= sum;
        }

        rebuild_auxiliary();
    }

    // ── raw_slot: slot byte for card c using current canonical_pos ────────────
    // Reflected encoding (v4 §5.1):
    //   face-up predecessor to canonical position p:   slot = p        (0..51)
    //   locative kind k:                               slot = 52 + k   (52..78)
    //   face-down predecessor to canonical position p: slot = 255 - p  (203..255)
    // Used only in the NONE fast path and Phase 1 (initial slot computation).

    uint8_t raw_slot(uint8_t c) const {
        const auto& d = descriptors[c];
        if (d.is_predecessor) {
            uint8_t pos = canonical_pos[d.predecessor_card_id];
            return d.face_down ? static_cast<uint8_t>(255 - pos) : pos;
        } else {
            uint8_t base = static_cast<uint8_t>(N + d.locative_kind);
            return d.face_down ? static_cast<uint8_t>(255 - base) : base;
        }
    }

    // ── collapsed_pos: canonical position for card q, collapsed if q is in
    //    an indistinguishable group (Scheme A).
    // Finds the group of members in q's static class that share the same
    // slot byte as q.  Returns the lowest canonical_pos in that group.
    // If q is the only member with its slot byte, returns canonical_pos[q].

    uint8_t collapsed_pos(uint8_t q) const {
        uint8_t q_cls  = classes.class_of[q];
        uint8_t q_base = classes.class_start[q_cls];
        uint8_t q_slot = slot[q];
        // class_members are in sorted order; find the first member with
        // the same slot byte — its canonical_pos is the lowest in the group.
        for (uint8_t i = 0; i < classes.class_size; i++) {
            if (slot[classes.class_members[q_base + i]] == q_slot)
                return static_cast<uint8_t>(q_base + i);
        }
        return canonical_pos[q];  // unreachable: q is in its own class
    }

    // ── zob_for_card: Zobrist contribution for one card ───────────────────────
    // class_id: the static class ID for this card (= card_id in NONE mode,
    //           classes.class_of[card_id] in COLOUR/SUIT_IRRELEVANT mode).
    // Column is derived from slot[] (which includes Scheme A collapsing)
    // rather than recomputing from canonical_pos, so that indistinguishable
    // predecessor targets produce the same Zobrist lookup.

    uint64_t zob_for_card(uint8_t class_id, uint8_t card_idx) const {
        const auto& d = descriptors[card_idx];
        // Undo face-down reflection to recover the base column from slot[]
        uint8_t col = d.face_down
            ? static_cast<uint8_t>(255 - slot[card_idx])
            : slot[card_idx];
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

    // ── rebuild_auxiliary: rebuild children[] and class_sum[] from current state ─
    // Called at end of recompute_from_descriptors() to keep auxiliary data
    // consistent with the from-scratch computation.

    void rebuild_auxiliary() {
        // Rebuild children[] from descriptors[]
        std::memset(children, -1, sizeof(children));
        for (uint8_t c = 0; c < N; c++) {
            if (descriptors[c].is_predecessor) {
                children[descriptors[c].predecessor_card_id] = static_cast<int8_t>(c);
            }
        }

        // Rebuild class_sum[] from the just-computed hash
        if (classes.n_classes == N) {
            // NONE mode: class_sum[c] = zob_for_card(c, c)
            for (uint8_t c = 0; c < N; c++) {
                class_sum[c] = zob_for_card(c, c);
            }
        }
        // For symmetry modes, class_sum[] is already computed in Phase 5
        // (we store it during the hash computation loop above).
    }

    // ── Bitmask helpers for changed_mask ─────────────────────────────────────

    void set_changed(uint8_t c) {
        if (c < 64) changed_mask_lo |= (1ULL << c);
        else        changed_mask_hi |= (1ULL << (c - 64));
    }
    bool is_changed(uint8_t c) const {
        if (c < 64) return (changed_mask_lo & (1ULL << c)) != 0;
        else        return (changed_mask_hi & (1ULL << (c - 64))) != 0;
    }

    // ── card_cid: card → static card ID (suit*13 + rank-1) ───────────────────

    static uint8_t card_cid(card c) {
        return zobrist_hash::card_id(c.get_suit(), c.get_rank());
    }
};

#endif // SOLVITAIRE_MULTIPLICITY_DESCRIPTOR_ENGINE_H
