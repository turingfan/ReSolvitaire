#ifndef SOLVITAIRE_MULTIPLICITY_ZOBRIST_H
#define SOLVITAIRE_MULTIPLICITY_ZOBRIST_H

// ─── multiplicity_zobrist.h ──────────────────────────────────────────────────
//
// Zobrist hash table for the multiplicity encoding.
//
// Table: Z[class_id][column_index]  — 52 × 80 entries for single-deck.
//   class_id ∈ [0, 52): static class of the card.
//     No-symmetry (Stage 1): class_id = card_id (each card is its own class).
//   column_index ∈ [0, 80):
//     [0, 52)  — predecessor columns: column = canonical position of predecessor card
//     [52, 80) — locative columns:    column = 52 + locative_kind (MLD_*)
//
// Hash combining (no-symmetry, Stage 1):
//   H = XOR over all cards c: Z_lookup(c, descriptor_of(c))
//
// Face-down predecessors use the NOT trick:
//   Z_lookup(c, pred(q, fd)) = fd ? ~Z[c][canonical_pos(q)] : Z[c][canonical_pos(q)]
//
// Seed: 0xDEADBEEF12345678, filling in class-then-column order (v4 spec §7).
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <random>

namespace multiplicity_zobrist {

static constexpr int N_CLASSES  = 52;  // single-deck, no-symmetry
static constexpr int N_COLUMNS  = 80;  // 52 predecessor + 28 locative
static constexpr int N_PRED_COL = 52;  // columns 0..51
static constexpr int N_LOC_BASE = 52;  // locative columns start at 52

extern uint64_t Z[N_CLASSES][N_COLUMNS];

inline void init() {
    static bool initialized = false;
    if (initialized) return;
    std::mt19937_64 rng(UINT64_C(0xDEADBEEF12345678));
    for (int c = 0; c < N_CLASSES; c++) {
        for (int col = 0; col < N_COLUMNS; col++) {
            Z[c][col] = rng();
        }
    }
    initialized = true;
}

} // namespace multiplicity_zobrist

#endif // SOLVITAIRE_MULTIPLICITY_ZOBRIST_H
