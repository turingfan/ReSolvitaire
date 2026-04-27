#include "compact_state.h"

// C++14 out-of-line definitions for static constexpr members (required when ODR-used,
// e.g. bound to a const reference in GTest EXPECT_EQ calls).
constexpr card_descriptor compact_state::STARTING;
constexpr card_descriptor compact_state::STARTING_FACE_UP;
constexpr card_descriptor compact_state::ROOT;
constexpr card_descriptor compact_state::IN_CELL;
constexpr card_descriptor compact_state::PARENT_0;
constexpr card_descriptor compact_state::PARENT_1;
constexpr card_descriptor compact_state::PARENT_2;
constexpr card_descriptor compact_state::PARENT_3;
constexpr card_descriptor compact_state::IN_HOLE;
constexpr card_descriptor compact_state::IN_SPACE;

#if !defined(SOLVITAIRE_LRU_ONLY)

void compact_state::clear() {
    std::memset(data, 0, 32);
}

void compact_state::set_occupied(bool occ) {
    data[0] = occ ? 1 : 0;
}

bool compact_state::is_occupied() const {
    return data[0] != 0;
}

void compact_state::set_depth(uint16_t d) {
    data[1] = d & 0xFF;
    data[2] = (d >> 8) & 0xFF;
}

uint16_t compact_state::get_depth() const {
    return data[1] | (data[2] << 8);
}

void compact_state::set_foundation(uint8_t suit, uint8_t rank) {
    if (suit >= 4 || rank > 13) return;

    if (suit == 0) {  // Clubs
        data[3] = (data[3] & 0xF0) | (rank & 0x0F);
    } else if (suit == 1) {  // Hearts
        data[3] = (data[3] & 0x0F) | ((rank & 0x0F) << 4);
    } else if (suit == 2) {  // Spades
        data[4] = (data[4] & 0xF0) | (rank & 0x0F);
    } else if (suit == 3) {  // Diamonds
        data[4] = (data[4] & 0x0F) | ((rank & 0x0F) << 4);
    }
}

uint8_t compact_state::get_foundation(uint8_t suit) const {
    if (suit >= 4) return 0;

    if (suit == 0) {  // Clubs
        return data[3] & 0x0F;
    } else if (suit == 1) {  // Hearts
        return (data[3] >> 4) & 0x0F;
    } else if (suit == 2) {  // Spades
        return data[4] & 0x0F;
    } else {  // Diamonds (suit == 3)
        return (data[4] >> 4) & 0x0F;
    }
}

void compact_state::set_hole_top(uint8_t card_id) {
    if (card_id >= 52) return;
    data[3] = (data[3] & 0xC0) | (card_id & 0x3F);
}

uint8_t compact_state::get_hole_top() const {
    return data[3] & 0x3F;
}

void compact_state::set_waste_ptr(uint8_t ptr) {
    if (ptr >= 64) return;
    data[5] = ptr;
}

uint8_t compact_state::get_waste_ptr() const {
    return data[5];
}

void compact_state::set_descriptor(uint8_t card_id, uint8_t value) {
    if (card_id >= 52 || value > 15) return;

    uint8_t byte_idx = 6 + (card_id / 2);
    if (card_id % 2 == 0) {
        // Even card: low nibble
        data[byte_idx] = (data[byte_idx] & 0xF0) | (value & 0x0F);
    } else {
        // Odd card: high nibble
        data[byte_idx] = (data[byte_idx] & 0x0F) | ((value & 0x0F) << 4);
    }
}

uint8_t compact_state::get_descriptor(uint8_t card_id) const {
    if (card_id >= 52) return 0;

    uint8_t byte_idx = 6 + (card_id / 2);
    if (card_id % 2 == 0) {
        // Even card: low nibble
        return data[byte_idx] & 0x0F;
    } else {
        // Odd card: high nibble
        return (data[byte_idx] >> 4) & 0x0F;
    }
}

bool compact_state::matches(const compact_state& other) const {
    return std::memcmp(data + 3, other.data + 3, 29) == 0;
}

#endif // !SOLVITAIRE_LRU_ONLY
