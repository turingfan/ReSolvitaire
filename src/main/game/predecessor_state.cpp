#include "predecessor_state.h"

void predecessor_state::clear() {
    std::memset(data, 0, 64);
}

void predecessor_state::set_occupied(bool occ) {
    data[0] = occ ? 1 : 0;
}

bool predecessor_state::is_occupied() const {
    return data[0] != 0;
}

void predecessor_state::set_depth(uint8_t d) {
    data[1] = d;
}

uint8_t predecessor_state::get_depth() const {
    return data[1];
}

void predecessor_state::set_predecessor(uint8_t card_id, uint8_t pred_value) {
    data[2 + card_id] = pred_value;
}

uint8_t predecessor_state::get_predecessor(uint8_t card_id) const {
    return data[2 + card_id];
}

bool predecessor_state::matches(const predecessor_state& other) const {
    return std::memcmp(data + 2, other.data + 2, 52) == 0;
}
