#ifndef SOLVITAIRE_MULTIPLICITY_DESCRIPTOR_STORE_H
#define SOLVITAIRE_MULTIPLICITY_DESCRIPTOR_STORE_H

// ─── multiplicity_descriptor_store.h ────────────────────────────────────────
//
// 64-byte payload entry for the multiplicity cache.
//
// Layout (v4 spec §5.4):
//   Byte 0:      Occupied flag (0 = empty slot in cluster)
//   Bytes 1–2:   Depth (uint16_t big-endian, excluded from equality comparison)
//   Bytes 3–54:  Slot data — one byte per card (52 cards, single-deck)
//   Byte 55:     Reserved (0)
//   Bytes 56–63: Reserved / hash-guard (zeroed in Stage 1; 8-byte aligned)
//
// Equality comparison: memcmp over bytes 3–54 (slot data only, 52 bytes).
//
// Slot byte encoding (reflected encoding, N = 52 for single-deck):
//   b ∈ [0, N)        face-up predecessor to card at canonical position b
//   b ∈ [N, 256–N)    locative descriptor: locative_kind = b – N
//   b ∈ [256–N, 256)  face-down predecessor to card at canonical position 255 – b
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <cstring>

struct multiplicity_descriptor_store {
    uint8_t data[64];

    multiplicity_descriptor_store() { clear(); }

    void clear() { std::memset(data, 0, sizeof(data)); }

    // ── Occupied flag (byte 0) ───────────────────────────────────────────────

    bool is_occupied() const { return data[0] != 0; }
    void set_occupied(bool b) { data[0] = b ? 1 : 0; }

    // ── Depth (bytes 1–2, big-endian, excluded from comparison) ─────────────

    uint16_t get_depth() const {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(data[1]) << 8) | data[2]);
    }
    void set_depth(uint16_t d) {
        data[1] = static_cast<uint8_t>(d >> 8);
        data[2] = static_cast<uint8_t>(d & 0xFF);
    }

    // ── Slot data (bytes 3–54) ───────────────────────────────────────────────

    uint8_t get_slot(uint8_t card_id) const { return data[3 + card_id]; }
    void    set_slot(uint8_t card_id, uint8_t v) { data[3 + card_id] = v; }

    // ── Equality comparison (bytes 3–54 only) ────────────────────────────────

    bool matches(const multiplicity_descriptor_store& other) const {
        return std::memcmp(data + 3, other.data + 3, 52) == 0;
    }

    // ── Stub accessors: satisfy game_state.cpp flat-cache code paths ─────────
    // These are called inside if constexpr (Policy::computes_hash) blocks that
    // are shared with flat policies. The multiplicity engine ignores them;
    // recompute_all() in make_move/undo_move replaces incremental updates.
    uint8_t get_hole_top()           const { return 0; }
    void    set_hole_top(uint8_t)          {}
    uint8_t get_descriptor(uint8_t)  const { return 0; }
    void    set_descriptor(uint8_t, uint8_t) {}
    uint8_t get_foundation(uint8_t)  const { return 0; }
    void    set_foundation(uint8_t, uint8_t) {}
    uint8_t get_waste_ptr()          const { return 0; }
    void    set_waste_ptr(uint8_t)         {}
};

#endif // SOLVITAIRE_MULTIPLICITY_DESCRIPTOR_STORE_H
