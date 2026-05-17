#ifndef SOLVITAIRE_MULTIPLICITY_DESCRIPTOR_H
#define SOLVITAIRE_MULTIPLICITY_DESCRIPTOR_H

// ─── multiplicity_descriptor.h ──────────────────────────────────────────────
//
// Internal descriptor type for the multiplicity encoding (v4 spec).
//
// Each card has exactly one descriptor:
//   - predecessor(q, face_down): card sits on card q; face_down = whether this card is hidden
//   - locative(kind): card is in a named location (cell, foundation, stock, etc.)
//
// There is NO "STARTING" descriptor. Every card's descriptor is fully determined
// by its current board position, making incremental updates trivially reversible
// by re-walking the board.
//
// Naming convention (from implementation plan):
//   MLD_ = Multiplicity Locative Descriptor
//   MPD  = Multiplicity Predecessor Descriptor (predecessor(q, fd))
//
// Stage 1 of the multiplicity encoding implementation plan.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>

// ─── Locative kinds ───────────────────────────────────────────────────────────

enum multiplicity_locative : uint8_t {
    MLD_IN_CELL    = 0,   // Free cell
    MLD_PERMANENT  = 1,   // Foundation, non-top hole card, accordion FINAL
    MLD_IN_STOCK   = 2,   // Stock pile
    MLD_IN_WASTE   = 3,   // Waste pile
    MLD_IN_RESERVE = 4,   // Reserve pile
    MLD_HOLE_TOP   = 5,   // Top card of hole pile (hole games only)
    MLD_IN_SPACE   = 6,   // Bottom of tableau pile (pile-symmetric: k omitted)
    // Values 7..27 are reserved for future use (L=28 total locative columns)
};

static constexpr uint8_t MLD_COUNT = 7;   // number of locative kinds in use

// ─── Per-card descriptor ──────────────────────────────────────────────────────

struct multiplicity_descriptor {
    bool    is_predecessor;       // true  → predecessor encoding (MPD)
    uint8_t predecessor_card_id;  // card ID (0..51) — valid if is_predecessor
    bool    face_down;            // whether THIS card is face-down — valid if is_predecessor
    uint8_t locative_kind;        // MLD_* enum — valid if !is_predecessor

    // ── Factory functions ────────────────────────────────────────────────────

    static multiplicity_descriptor make_locative(uint8_t kind) {
        multiplicity_descriptor d{};
        d.is_predecessor = false;
        d.locative_kind  = kind;
        return d;
    }

    static multiplicity_descriptor make_predecessor(uint8_t pred_cid, bool fd) {
        multiplicity_descriptor d{};
        d.is_predecessor       = true;
        d.predecessor_card_id  = pred_cid;
        d.face_down            = fd;
        return d;
    }
};

#endif // SOLVITAIRE_MULTIPLICITY_DESCRIPTOR_H
