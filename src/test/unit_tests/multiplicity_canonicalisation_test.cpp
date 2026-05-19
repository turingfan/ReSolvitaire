// ─── multiplicity_canonicalisation_test.cpp ─────────────────────────────────
//
// Stage 2C unit tests: validate suit-symmetry canonicalisation in the
// multiplicity descriptor engine.
//
// The core metamorphic property tested:
//   For any descriptor configuration D and any within-class permutation π,
//   hash(D) == hash(π(D))  and  payload(D) == payload(π(D)).
//
// Tests generate random game-like descriptor configurations across multiple
// game profiles (klondike, freecell, black-hole, spiderette) and symmetry
// modes (COLOUR, SUIT_IRRELEVANT), then apply random suit permutations and
// verify the invariant.  Deterministic seeds ensure reproducibility.
//
// Card ID mapping (zobrist_hash::card_id): suit*13 + (rank-1)
//   Clubs = 0..12, Hearts = 13..25, Spades = 26..38, Diamonds = 39..51
// ─────────────────────────────────────────────────────────────────────────────

#include <gtest/gtest.h>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "../../main/game/multiplicity_descriptor_engine.h"
#include "../../main/game/multiplicity_zobrist.h"
#include "../../main/game/multiplicity_descriptor.h"
#include "../../main/game/multiplicity_static_class.h"
#include "../../main/game/sol_rules.h"

// ═════════════════════════════════════════════════════════════════════════════
// Helpers
// ═════════════════════════════════════════════════════════════════════════════

// ── Game profile: describes which locative kinds are available ────────────────

enum class game_profile {
    KLONDIKE,     // foundations, stock, waste, 7 tableau (pile-symmetric)
    FREECELL,     // foundations, cells, 8 tableau (pile-symmetric)
    BLACK_HOLE,   // hole, 17 tableau (pile-symmetric)
    SPIDERETTE,   // foundations, stock, 7 tableau (pile-indexed)
};

static const char* profile_name(game_profile p) {
    switch (p) {
        case game_profile::KLONDIKE:   return "klondike";
        case game_profile::FREECELL:   return "freecell";
        case game_profile::BLACK_HOLE: return "black-hole";
        case game_profile::SPIDERETTE: return "spiderette";
    }
    return "unknown";
}

// ── Generate random game-like descriptor configuration ───────────────────────
//
// Distributes 52 cards across locations appropriate to the game profile,
// building valid predecessor chains in tableau piles.

static void generate_descriptors(
    game_profile profile,
    uint32_t seed,
    multiplicity_descriptor descriptors[52]
) {
    std::mt19937 rng(seed);

    // Shuffled deck — determines which cards go where
    uint8_t deck[52];
    for (uint8_t i = 0; i < 52; i++) deck[i] = i;
    std::shuffle(deck, deck + 52, rng);

    // Start all as PERMANENT (foundation default)
    for (uint8_t i = 0; i < 52; i++)
        descriptors[i] = multiplicity_descriptor::make_locative(MLD_PERMANENT);

    int idx = 0;  // cursor into shuffled deck

    // Foundation cards stay PERMANENT (skip past them)
    int n_foundation = std::uniform_int_distribution<>(0, 16)(rng);
    idx += n_foundation;

    // Profile-specific non-tableau locations
    switch (profile) {
    case game_profile::KLONDIKE: {
        int n_stock = std::uniform_int_distribution<>(0, std::min(12, 52 - idx))(rng);
        for (int i = 0; i < n_stock; i++, idx++)
            descriptors[deck[idx]] = multiplicity_descriptor::make_locative(MLD_IN_STOCK);
        int n_waste = std::uniform_int_distribution<>(0, std::min(8, 52 - idx))(rng);
        for (int i = 0; i < n_waste; i++, idx++)
            descriptors[deck[idx]] = multiplicity_descriptor::make_locative(MLD_IN_WASTE);
        break;
    }
    case game_profile::FREECELL: {
        int n_cells = std::uniform_int_distribution<>(0, std::min(4, 52 - idx))(rng);
        for (int i = 0; i < n_cells; i++, idx++)
            descriptors[deck[idx]] = multiplicity_descriptor::make_locative(MLD_IN_CELL);
        break;
    }
    case game_profile::BLACK_HOLE: {
        // 1 card on hole top, a few more permanent in hole
        if (idx < 52) {
            descriptors[deck[idx]] = multiplicity_descriptor::make_locative(MLD_HOLE_TOP);
            idx++;
        }
        int n_hole = std::uniform_int_distribution<>(0, std::min(6, 52 - idx))(rng);
        for (int i = 0; i < n_hole; i++, idx++)
            descriptors[deck[idx]] = multiplicity_descriptor::make_locative(MLD_PERMANENT);
        break;
    }
    case game_profile::SPIDERETTE: {
        int n_stock = std::uniform_int_distribution<>(0, std::min(10, 52 - idx))(rng);
        for (int i = 0; i < n_stock; i++, idx++)
            descriptors[deck[idx]] = multiplicity_descriptor::make_locative(MLD_IN_STOCK);
        break;
    }
    }

    // Remaining cards go into tableau piles
    int n_remaining = 52 - idx;
    int n_piles;
    bool pile_indexed;
    switch (profile) {
        case game_profile::KLONDIKE:   n_piles = 7;  pile_indexed = false; break;
        case game_profile::FREECELL:   n_piles = 8;  pile_indexed = false; break;
        case game_profile::BLACK_HOLE: n_piles = 17; pile_indexed = false; break;
        case game_profile::SPIDERETTE: n_piles = 7;  pile_indexed = true;  break;
    }

    if (n_remaining > 0 && n_piles > 0) {
        // Distribute remaining cards into piles
        std::vector<std::vector<uint8_t>> piles(n_piles);
        for (int i = 0; i < n_remaining; i++)
            piles[std::uniform_int_distribution<>(0, n_piles - 1)(rng)].push_back(deck[idx + i]);

        for (int p = 0; p < n_piles; p++) {
            if (piles[p].empty()) continue;
            uint8_t space_kind = pile_indexed
                ? static_cast<uint8_t>(MLD_IN_SPACE + p)
                : MLD_IN_SPACE;

            // Bottom card: locative (IN_SPACE), occasionally face-down
            bool fd_bottom = std::bernoulli_distribution(0.2)(rng);
            descriptors[piles[p].back()] =
                multiplicity_descriptor::make_locative(space_kind, fd_bottom);

            // Rest: predecessor chain (each sits on the card below it)
            // Cards deeper in the pile are more likely face-down
            for (int i = static_cast<int>(piles[p].size()) - 2; i >= 0; i--) {
                bool fd = std::bernoulli_distribution(0.3)(rng);
                descriptors[piles[p][i]] =
                    multiplicity_descriptor::make_predecessor(piles[p][i + 1], fd);
            }
        }
    }
}

// ── Apply a random within-class permutation to a descriptor array ────────────
//
// For each static class, shuffles which member has which descriptor.
// Consistently updates all predecessor references so the permuted state
// represents the same game state under suit symmetry.
//
// Writes the permuted result into `dst` (must be a separate array from `src`).

// A valid suit permutation applies a GLOBAL suit→suit mapping, not independent
// per-class swaps.  For COLOUR mode: independently choose to swap or not swap
// each colour's suits (Clubs↔Spades, Hearts↔Diamonds).  For SUIT_IRRELEVANT
// mode: choose a random permutation of the 4 suits.
//
// The card_id mapping for a suit permutation π:
//   new_cid = π(suit) * 13 + rank_idx
// where suit = old_cid / 13, rank_idx = old_cid % 13.

static void apply_random_suit_permutation(
    const multiplicity_descriptor src[52],
    multiplicity_descriptor dst[52],
    symmetry_mode mode,
    std::mt19937& rng
) {
    // Build suit mapping: suit_map[old_suit] = new_suit
    uint8_t suit_map[4] = {0, 1, 2, 3};

    switch (mode) {
    case symmetry_mode::COLOUR: {
        // Black suits {0=Clubs, 2=Spades}: swap or not
        if (std::bernoulli_distribution(0.5)(rng))
            std::swap(suit_map[0], suit_map[2]);
        // Red suits {1=Hearts, 3=Diamonds}: swap or not
        if (std::bernoulli_distribution(0.5)(rng))
            std::swap(suit_map[1], suit_map[3]);
        break;
    }
    case symmetry_mode::SUIT_IRRELEVANT:
        // Random permutation of all 4 suits
        std::shuffle(suit_map, suit_map + 4, rng);
        break;
    case symmetry_mode::NONE:
        break;  // identity — should never reach here
    }

    // Build card mapping: perm[old_cid] = new_cid
    uint8_t perm[52];
    for (uint8_t c = 0; c < 52; c++) {
        uint8_t suit = c / 13;
        uint8_t rank_idx = c % 13;
        perm[c] = suit_map[suit] * 13 + rank_idx;
    }

    // Apply: descriptor at position c moves to position perm[c],
    // with predecessor references also mapped through perm.
    for (uint8_t c = 0; c < 52; c++) {
        dst[perm[c]] = src[c];
        if (dst[perm[c]].is_predecessor)
            dst[perm[c]].predecessor_card_id = perm[src[c].predecessor_card_id];
    }
}

// ── Run the metamorphic check for a single (profile, mode, seed) triple ──────

static void run_metamorphic_check(
    game_profile profile,
    symmetry_mode mode,
    uint32_t seed
) {
    SCOPED_TRACE(std::string(profile_name(profile))
        + " mode=" + std::to_string(static_cast<int>(mode))
        + " seed=" + std::to_string(seed));

    sol_rules dummy;

    // Generate descriptors
    multiplicity_descriptor orig[52];
    generate_descriptors(profile, seed, orig);

    // Compute hash/payload for original
    multiplicity_descriptor_engine eng_orig;
    eng_orig.classes.init(mode);
    std::copy(orig, orig + 52, eng_orig.descriptors);
    eng_orig.recompute_hash(dummy);

    // Apply random suit permutation
    multiplicity_descriptor permuted[52];
    std::mt19937 perm_rng(seed * 7919u + 1u);  // distinct from generation seed
    apply_random_suit_permutation(orig, permuted, mode, perm_rng);

    // Compute hash/payload for permuted
    multiplicity_descriptor_engine eng_perm;
    eng_perm.classes.init(mode);
    std::copy(permuted, permuted + 52, eng_perm.descriptors);
    eng_perm.recompute_hash(dummy);

    EXPECT_EQ(eng_orig.hash_value, eng_perm.hash_value)
        << "Hash mismatch after suit permutation";
    EXPECT_TRUE(eng_orig.store.matches(eng_perm.store))
        << "Payload mismatch after suit permutation";
}

// ═════════════════════════════════════════════════════════════════════════════
// Metamorphic tests — randomised suit-permutation invariance
// ═════════════════════════════════════════════════════════════════════════════

// ── Klondike (COLOUR mode): foundations + stock + waste + tableau ─────────────

TEST(MultiplicityCanonicalisationTest, MetamorphicKlondikeColour) {
    for (uint32_t seed = 1; seed <= 20; seed++)
        run_metamorphic_check(game_profile::KLONDIKE, symmetry_mode::COLOUR, seed);
}

// ── FreeCell (SUIT_IRRELEVANT mode): foundations + cells + tableau ────────────

TEST(MultiplicityCanonicalisationTest, MetamorphicFreeCellSuitIrrelevant) {
    for (uint32_t seed = 1; seed <= 20; seed++)
        run_metamorphic_check(game_profile::FREECELL, symmetry_mode::SUIT_IRRELEVANT, seed);
}

// ── Black Hole (SUIT_IRRELEVANT mode): hole + tableau ────────────────────────

TEST(MultiplicityCanonicalisationTest, MetamorphicBlackHoleSuitIrrelevant) {
    for (uint32_t seed = 1; seed <= 20; seed++)
        run_metamorphic_check(game_profile::BLACK_HOLE, symmetry_mode::SUIT_IRRELEVANT, seed);
}

// ── Spiderette (COLOUR mode): stock + pile-indexed tableau ───────────────────

TEST(MultiplicityCanonicalisationTest, MetamorphicSpideretteColour) {
    for (uint32_t seed = 1; seed <= 20; seed++)
        run_metamorphic_check(game_profile::SPIDERETTE, symmetry_mode::COLOUR, seed);
}

// ── Cross-profile with COLOUR mode (klondike already above, add freecell) ────

TEST(MultiplicityCanonicalisationTest, MetamorphicFreeCellColour) {
    for (uint32_t seed = 1; seed <= 10; seed++)
        run_metamorphic_check(game_profile::FREECELL, symmetry_mode::COLOUR, seed);
}

// ── Cross-profile with SI mode (klondike gets SI too) ────────────────────────

TEST(MultiplicityCanonicalisationTest, MetamorphicKlondikeSuitIrrelevant) {
    for (uint32_t seed = 1; seed <= 10; seed++)
        run_metamorphic_check(game_profile::KLONDIKE, symmetry_mode::SUIT_IRRELEVANT, seed);
}

// ── Multiple permutations of the same state all match ────────────────────────

TEST(MultiplicityCanonicalisationTest, MultiplePermutationsAllMatch) {
    sol_rules dummy;

    for (auto profile : {game_profile::KLONDIKE, game_profile::FREECELL,
                         game_profile::BLACK_HOLE, game_profile::SPIDERETTE}) {
        for (auto mode : {symmetry_mode::COLOUR, symmetry_mode::SUIT_IRRELEVANT}) {
            SCOPED_TRACE(std::string(profile_name(profile))
                + " mode=" + std::to_string(static_cast<int>(mode)));

            multiplicity_descriptor orig[52];
            generate_descriptors(profile, 42, orig);

            multiplicity_descriptor_engine eng_orig;
            eng_orig.classes.init(mode);
            std::copy(orig, orig + 52, eng_orig.descriptors);
            eng_orig.recompute_hash(dummy);

            // Apply 5 different random permutations, all must match
            for (uint32_t p = 0; p < 5; p++) {
                SCOPED_TRACE("permutation " + std::to_string(p));
                multiplicity_descriptor permuted[52];
                std::mt19937 perm_rng(p * 13u + 7u);
                apply_random_suit_permutation(orig, permuted, mode, perm_rng);

                multiplicity_descriptor_engine eng_perm;
                eng_perm.classes.init(mode);
                std::copy(permuted, permuted + 52, eng_perm.descriptors);
                eng_perm.recompute_hash(dummy);

                EXPECT_EQ(eng_orig.hash_value, eng_perm.hash_value);
                EXPECT_TRUE(eng_orig.store.matches(eng_perm.store));
            }
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Non-equivalence — structurally different states must differ
// ═════════════════════════════════════════════════════════════════════════════

TEST(MultiplicityCanonicalisationTest, DifferentStatesAreDifferent) {
    sol_rules dummy;

    // Generate two configurations from different seeds — should differ with
    // overwhelming probability (birthday on 64-bit hash).
    for (auto mode : {symmetry_mode::COLOUR, symmetry_mode::SUIT_IRRELEVANT}) {
        SCOPED_TRACE("mode=" + std::to_string(static_cast<int>(mode)));

        int n_distinct = 0;
        for (uint32_t seed = 1; seed <= 20; seed++) {
            multiplicity_descriptor desc_a[52], desc_b[52];
            generate_descriptors(game_profile::KLONDIKE, seed, desc_a);
            generate_descriptors(game_profile::KLONDIKE, seed + 1000, desc_b);

            multiplicity_descriptor_engine eng_a, eng_b;
            eng_a.classes.init(mode);
            eng_b.classes.init(mode);
            std::copy(desc_a, desc_a + 52, eng_a.descriptors);
            std::copy(desc_b, desc_b + 52, eng_b.descriptors);
            eng_a.recompute_hash(dummy);
            eng_b.recompute_hash(dummy);

            if (eng_a.hash_value != eng_b.hash_value
                || !eng_a.store.matches(eng_b.store))
                n_distinct++;
        }
        // At least 18 out of 20 pairs should differ (allow for unlikely collision)
        EXPECT_GE(n_distinct, 18);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Algebraic tests — specific engine properties
// ═════════════════════════════════════════════════════════════════════════════

// ── NONE mode: canonical_pos is identity, hash is manual XOR ─────────────────

TEST(MultiplicityCanonicalisationTest, NoneModePreservesStage1) {
    sol_rules dummy;

    // Test across several random configurations
    for (uint32_t seed = 1; seed <= 10; seed++) {
        SCOPED_TRACE("seed=" + std::to_string(seed));

        multiplicity_descriptor desc[52];
        generate_descriptors(game_profile::KLONDIKE, seed, desc);

        multiplicity_descriptor_engine eng;
        eng.classes.init(symmetry_mode::NONE);
        std::copy(desc, desc + 52, eng.descriptors);
        eng.recompute_hash(dummy);

        // canonical_pos must be identity
        for (uint8_t c = 0; c < 52; c++)
            EXPECT_EQ(eng.canonical_pos[c], c) << "card " << (int)c;

        // Hash must equal manual XOR of Z[c][column]
        uint64_t expected = 0;
        for (uint8_t c = 0; c < 52; c++) {
            const auto& d = eng.descriptors[c];
            uint8_t col = d.is_predecessor
                ? eng.canonical_pos[d.predecessor_card_id]
                : static_cast<uint8_t>(52 + d.locative_kind);
            uint64_t z = multiplicity_zobrist::Z[c][col];
            expected ^= (d.face_down ? ~z : z);
        }
        EXPECT_EQ(eng.hash_value, expected);
    }
}

// ── Fixpoint convergence: cross-class predecessor chains ─────────────────────

TEST(MultiplicityCanonicalisationTest, FixpointConvergence) {
    sol_rules dummy;

    // Cross-class predecessor chain requiring >1 fixpoint iteration:
    //   AC(0) sits on 2C(1)   — class 0 → class 2 (both black)
    //   AS(26) sits on 2S(27) — class 0 → class 2
    //   2C(1) at IN_SPACE, 2S(27) at IN_CELL
    //
    // Sorting class 2 swaps 2C↔2S canonical positions, which changes
    // predecessor slots in class 0, triggering a re-sort and second iteration.

    multiplicity_descriptor_engine eng_a;
    eng_a.classes.init(symmetry_mode::COLOUR);
    for (auto& d : eng_a.descriptors)
        d = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    eng_a.descriptors[0]  = multiplicity_descriptor::make_predecessor(1, false);
    eng_a.descriptors[26] = multiplicity_descriptor::make_predecessor(27, false);
    eng_a.descriptors[1]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    eng_a.descriptors[27] = multiplicity_descriptor::make_locative(MLD_IN_CELL);
    eng_a.recompute_hash(dummy);  // must converge (debug assert fires otherwise)

    // Permuted: swap within both classes simultaneously
    multiplicity_descriptor_engine eng_b;
    eng_b.classes.init(symmetry_mode::COLOUR);
    for (auto& d : eng_b.descriptors)
        d = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    eng_b.descriptors[0]  = multiplicity_descriptor::make_predecessor(27, false);
    eng_b.descriptors[26] = multiplicity_descriptor::make_predecessor(1, false);
    eng_b.descriptors[1]  = multiplicity_descriptor::make_locative(MLD_IN_CELL);
    eng_b.descriptors[27] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    eng_b.recompute_hash(dummy);

    EXPECT_EQ(eng_a.hash_value, eng_b.hash_value);
    EXPECT_TRUE(eng_a.store.matches(eng_b.store));
}

// ── Scheme A predecessor collapsing ──────────────────────────────────────────

TEST(MultiplicityCanonicalisationTest, SchemeAPredecessorCollapsing) {
    sol_rules dummy;

    // AC(0) and AS(26) both at IN_SPACE → same dynamic class.
    // Test: 2H(14) sits on AC vs sits on AS → payload must match (Scheme A
    // collapses to lowest canonical position).

    multiplicity_descriptor_engine eng_a;
    eng_a.classes.init(symmetry_mode::COLOUR);
    for (auto& d : eng_a.descriptors)
        d = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    eng_a.descriptors[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    eng_a.descriptors[26] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    eng_a.descriptors[14] = multiplicity_descriptor::make_predecessor(0, false);
    eng_a.recompute_hash(dummy);

    multiplicity_descriptor_engine eng_b;
    eng_b.classes.init(symmetry_mode::COLOUR);
    for (auto& d : eng_b.descriptors)
        d = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    eng_b.descriptors[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    eng_b.descriptors[26] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    eng_b.descriptors[14] = multiplicity_descriptor::make_predecessor(26, false);
    eng_b.recompute_hash(dummy);

    EXPECT_TRUE(eng_a.store.matches(eng_b.store))
        << "Scheme A should collapse pred(AC) and pred(AS) to same slot";

    // Verify 2H's slot byte specifically: find 2H's position in its class
    uint8_t cls = eng_a.classes.class_of[14];
    uint8_t base = eng_a.classes.class_start[cls];
    uint8_t pos_a = 255, pos_b = 255;
    for (uint8_t i = 0; i < eng_a.classes.class_size; i++) {
        if (eng_a.classes.class_members[base + i] == 14) pos_a = base + i;
        if (eng_b.classes.class_members[base + i] == 14) pos_b = base + i;
    }
    ASSERT_NE(pos_a, 255u);
    ASSERT_NE(pos_b, 255u);
    EXPECT_EQ(eng_a.store.get_slot(pos_a), eng_b.store.get_slot(pos_b));
}

// ── in_space(k): pile-indexed locatives produce distinct results ─────────────

TEST(MultiplicityCanonicalisationTest, InSpaceKPileIndexedDifferent) {
    sol_rules dummy;

    multiplicity_descriptor_engine eng;
    eng.classes.init(symmetry_mode::NONE);
    for (auto& d : eng.descriptors)
        d = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    eng.descriptors[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE + 0);
    eng.descriptors[13] = multiplicity_descriptor::make_locative(MLD_IN_SPACE + 1);
    eng.recompute_hash(dummy);

    EXPECT_NE(eng.store.get_slot(0), eng.store.get_slot(13));
    EXPECT_EQ(eng.store.get_slot(0), 52u + MLD_IN_SPACE + 0);
    EXPECT_EQ(eng.store.get_slot(13), 52u + MLD_IN_SPACE + 1);

    // Swapping pile indices must change the hash
    multiplicity_descriptor_engine eng2;
    eng2.classes.init(symmetry_mode::NONE);
    for (auto& d : eng2.descriptors)
        d = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    eng2.descriptors[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE + 1);
    eng2.descriptors[13] = multiplicity_descriptor::make_locative(MLD_IN_SPACE + 0);
    eng2.recompute_hash(dummy);

    EXPECT_NE(eng.hash_value, eng2.hash_value);
}

// ── Bare in_space: pile-symmetric cards get identical slot bytes ──────────────

TEST(MultiplicityCanonicalisationTest, InSpaceBareIdentical) {
    sol_rules dummy;

    multiplicity_descriptor_engine eng;
    eng.classes.init(symmetry_mode::NONE);
    for (auto& d : eng.descriptors)
        d = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    eng.descriptors[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    eng.descriptors[13] = multiplicity_descriptor::make_locative(MLD_IN_SPACE);
    eng.recompute_hash(dummy);

    EXPECT_EQ(eng.store.get_slot(0), eng.store.get_slot(13));
    EXPECT_EQ(eng.store.get_slot(0), static_cast<uint8_t>(52 + MLD_IN_SPACE));

    // Contrast: pile-indexed version must differ
    multiplicity_descriptor_engine eng_idx;
    eng_idx.classes.init(symmetry_mode::NONE);
    for (auto& d : eng_idx.descriptors)
        d = multiplicity_descriptor::make_locative(MLD_PERMANENT);
    eng_idx.descriptors[0]  = multiplicity_descriptor::make_locative(MLD_IN_SPACE + 0);
    eng_idx.descriptors[13] = multiplicity_descriptor::make_locative(MLD_IN_SPACE + 1);
    eng_idx.recompute_hash(dummy);

    EXPECT_NE(eng.hash_value, eng_idx.hash_value);
    EXPECT_FALSE(eng.store.matches(eng_idx.store));
}
