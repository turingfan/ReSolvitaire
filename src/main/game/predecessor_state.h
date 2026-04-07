#ifndef SOLVITAIRE_PREDECESSOR_STATE_H
#define SOLVITAIRE_PREDECESSOR_STATE_H

#include <cstdint>
#include <cstring>

/**
 * predecessor_state: A 64-byte fixed-size state representation for accordion games.
 *
 * Each card's state is encoded as its "predecessor" — the card it sits on or
 * the zone it occupies. For accordion, the predecessor of each visible top card
 * is the previous pile's top card in the accordion sequence (linked list).
 * Buried cards have predecessor FINAL. The leftmost pile's top card has PILE_0.
 *
 * Layout (64 bytes total):
 *   Byte 0:     occupied flag (0 = empty, nonzero = occupied)
 *   Byte 1:     depth (uint8_t, max 255)
 *   Bytes 2-53: predecessor array (52 cards x 8 bits)
 *   Bytes 54-63: spare (zeroed)
 */
struct predecessor_state {
    uint8_t data[64];

    enum zone_marker : uint8_t {
        FINAL      = 52,  // buried — can never be played again
        IN_CELL    = 53,
        IN_STOCK   = 54,
        IN_WASTE   = 55,
        IN_RESERVE = 56,
        STARTING   = 57,  // hasn't moved from starting position
        PILE_0     = 58   // bottom of accordion sequence (leftmost)
        // PILE_N = 58 + N (for future tableau dealing games)
    };

    void clear();

    void set_occupied(bool occ);
    bool is_occupied() const;

    void set_depth(uint8_t d);
    uint8_t get_depth() const;

    void set_predecessor(uint8_t card_id, uint8_t pred_value);
    uint8_t get_predecessor(uint8_t card_id) const;

    // Compare bytes 2-53 (predecessor array only, excludes occupied flag and depth)
    bool matches(const predecessor_state& other) const;
};

#endif // SOLVITAIRE_PREDECESSOR_STATE_H
