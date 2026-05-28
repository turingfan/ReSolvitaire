#include <gtest/gtest.h>
#include "../../main/game/sol_rules.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"

TEST(SuitSymmetryDispatch, BlackHoleHasInherentSuitSymmetry) {
    sol_rules rules = rules_parser::from_preset("black-hole");
    EXPECT_TRUE(rules.inherent_suit_symmetry());
    EXPECT_TRUE(rules.hole);
}

TEST(SuitSymmetryDispatch, FreeCellNoInherentSuitSymmetry) {
    sol_rules rules = rules_parser::from_preset("free-cell");
    EXPECT_FALSE(rules.inherent_suit_symmetry());
    EXPECT_FALSE(rules.hole);
}

TEST(SuitSymmetryDispatch, KlondikeNoInherentSuitSymmetry) {
    sol_rules rules = rules_parser::from_preset("klondike");
    EXPECT_FALSE(rules.inherent_suit_symmetry());
}
