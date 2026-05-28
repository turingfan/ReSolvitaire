// bitmap_cache_test.cpp — unit tests for the 1-bit-per-entry transposition table

#include <gtest/gtest.h>
#include "../../main/game/bitmap_cache.h"
#include "../../main/game/zobrist.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"

class BitmapCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }
};

TEST_F(BitmapCacheTest, Construction_PowerOf2Rounding) {
    // Argument is max_entries (= number of bits). Rounded down to power of 2.
    // 1000 entries → largest power-of-2 ≤ 1000 is 512
    EXPECT_EQ(bitmap_cache(1000).get_num_bits(), uint64_t(512));
    // 1024 entries = 2^10 → exactly 1024
    EXPECT_EQ(bitmap_cache(1024).get_num_bits(), uint64_t(1024));
    // 5000 entries → largest power-of-2 ≤ 5000 is 4096
    EXPECT_EQ(bitmap_cache(5000).get_num_bits(), uint64_t(4096));
    // 256 entries = 2^8 → exactly 256
    EXPECT_EQ(bitmap_cache(256).get_num_bits(),  uint64_t(256));
}

TEST_F(BitmapCacheTest, Construction_ZeroCapacity) {
    bitmap_cache c(0);
    EXPECT_EQ(c.get_num_bits(), uint64_t(0));
}

TEST_F(BitmapCacheTest, ProbeAndInsert_NewHash_ReturnsFalse) {
    bitmap_cache cache(1024);
    uint64_t hash = 0xDEADBEEF12345678ULL;

    EXPECT_FALSE(cache.probe_and_insert(hash));  // miss: bit was not set
    EXPECT_TRUE(cache.probe_and_insert(hash));   // hit: bit is now set
}

TEST_F(BitmapCacheTest, ProbeAndInsert_DifferentHashes) {
    bitmap_cache cache(1024);  // 1024 bits; mask = 1023
    // hash 0 → bit 0, hash 1 → bit 1: guaranteed distinct slots
    EXPECT_FALSE(cache.probe_and_insert(uint64_t(0)));
    EXPECT_FALSE(cache.probe_and_insert(uint64_t(1)));
}

TEST_F(BitmapCacheTest, Probe_ReadOnly) {
    bitmap_cache cache(1024);
    uint64_t hash  = 0xABCDEF1234567890ULL;
    uint64_t other = 0x1111111111111111ULL;

    EXPECT_FALSE(cache.probe(hash));
    cache.probe_and_insert(hash);
    EXPECT_TRUE(cache.probe(hash));
    EXPECT_FALSE(cache.probe(other));
}

TEST_F(BitmapCacheTest, Statistics) {
    bitmap_cache cache(1024);
    uint64_t h1 = 42, h2 = 100;

    cache.probe_and_insert(h1);  // probe 1, insert 1
    cache.probe_and_insert(h1);  // probe 2, hit 1
    cache.probe_and_insert(h2);  // probe 3, insert 2
    cache.probe(h1);             // probe 4, hit 2

    EXPECT_EQ(cache.get_total_probes(), uint64_t(4));
    EXPECT_EQ(cache.get_hit_count(),    uint64_t(2));
    EXPECT_EQ(cache.get_insert_count(), uint64_t(2));
    EXPECT_EQ(cache.size(),             uint64_t(2));
}

TEST_F(BitmapCacheTest, Clear_ResetsState) {
    bitmap_cache cache(1024);
    uint64_t hash = 0xCAFEBABEULL;

    cache.probe_and_insert(hash);
    EXPECT_TRUE(cache.probe(hash));

    cache.clear();

    EXPECT_EQ(cache.get_total_probes(), uint64_t(0));
    EXPECT_EQ(cache.get_hit_count(),    uint64_t(0));
    EXPECT_EQ(cache.get_insert_count(), uint64_t(0));
    EXPECT_EQ(cache.size(),             uint64_t(0));
    // Verify the bit was actually cleared
    EXPECT_FALSE(cache.probe_and_insert(hash));
}

TEST_F(BitmapCacheTest, CacheInterface_Insert) {
    bitmap_cache cache(1 << 20);  // 1M entries (bits)
    sol_rules rules = rules_parser::from_preset("free-cell");
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    EXPECT_TRUE(cache.insert(gs));   // newly inserted → true
    EXPECT_FALSE(cache.insert(gs));  // already present → false
    EXPECT_EQ(cache.size(), uint64_t(1));
}

TEST_F(BitmapCacheTest, CacheInterface_Contains) {
    bitmap_cache cache(1 << 20);  // 1 MB
    sol_rules rules = rules_parser::from_preset("free-cell");
    game_state gs(rules, 1, game_state::streamliner_options::NONE);

    cache.insert(gs);
    EXPECT_TRUE(cache.contains(gs));

    auto moves = gs.get_legal_moves();
    ASSERT_FALSE(moves.empty());
    gs.make_move(moves[0]);

    EXPECT_FALSE(cache.contains(gs));
}
