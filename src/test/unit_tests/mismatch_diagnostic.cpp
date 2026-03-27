#include <gtest/gtest.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "../../main/game/zobrist.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/game/global_cache.h"
#include "../../main/game/flat_cache.h"
#include "../../main/game/dual_cache.h"
#include "../../main/solver/solver.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"

static const char* desc_name(uint8_t d) {
    switch (d) {
        case 0: return "STARTING";
        case 1: return "START_FU";
        case 2: return "ROOT";
        case 3: return "IN_CELL";
        case 4: return "PARENT_0";
        case 5: return "PARENT_1";
        case 6: return "PARENT_2";
        case 7: return "PARENT_3";
        case 8: return "IN_HOLE";
        case 9: return "IN_SPACE";
        default: return "???";
    }
}

static std::string card_name(uint8_t cid) {
    static const char* suits[] = {"C", "H", "S", "D"};
    static const char* ranks[] = {"?","A","2","3","4","5","6","7","8","9","10","J","Q","K"};
    uint8_t suit = cid / 13;
    uint8_t rank = (cid % 13) + 1;
    return std::string(ranks[rank]) + suits[suit];
}

// Recompute hash from payload without modifying anything
static uint64_t compute_hash_from_payload(const compact_state& payload,
                                           const sol_rules& rules,
                                           bool has_hole, pile::ref /*hole_ref*/,
                                           bool has_stock_waste) {
    uint64_t hash = 0;
    for (uint8_t c = 0; c < 52; ++c) {
        hash ^= zobrist_hash::card_key(c, payload.get_descriptor(c));
    }
    if (rules.foundations_present) {
        for (uint8_t s = 0; s < 4; ++s) {
            hash ^= zobrist_hash::foundation_key(s, payload.get_foundation(s));
        }
    }
    if (has_hole) {
        hash ^= zobrist_hash::hole_top_key(payload.get_hole_top());
    }
    if (has_stock_waste) {
        hash ^= zobrist_hash::waste_key(payload.get_waste_ptr());
    }
    return hash;
}

// Records the payload at every insertion, dumps detailed report at first mismatch
class RecordingDiagnosticCache : public dual_cache {
public:
    RecordingDiagnosticCache(const game_state& gs, uint64_t capacity, std::ofstream& out,
                              const sol_rules& rules)
        : dual_cache(gs, capacity), out_stream(out),
          op_count(0),
          rules_ref(rules),
          has_hole(rules.hole),
          has_stock_waste(rules.stock_size > 0
                      && rules.stock_deal_t == sol_rules::stock_deal_type::WASTE)
    {}

    bool insert(const game_state& gs) override {
        op_count++;

        // Record payload BEFORE insertion
        uint64_t hash = gs.get_zobrist_hash();
        compact_state pay = gs.get_payload();
        uint64_t recomputed = compute_hash_from_payload(
            pay, rules_ref, has_hole, 0, has_stock_waste);

        // Use dual_cache's insert which calls both lru and flat internally
        // and logs mismatches via stderr
        bool result = dual_cache::insert(gs);

        // Determine what dual_cache saw:
        // dual_cache returns flat_result. We can check get_lru_only_hits()
        // to detect if LRU hit but flat missed.
        bool had_lru_only_hit_now = (get_lru_only_hits() > prev_lru_only_hits);
        bool lru_miss_val = !had_lru_only_hit_now ? (result) : false;  // approximate
        bool flat_miss_val = result;

        // Record every op
        OpRecord rec;
        rec.op_num = op_count;
        rec.hash = hash;
        rec.recomputed_hash = recomputed;
        rec.payload = pay;
        rec.lru_miss = lru_miss_val;
        rec.flat_miss = flat_miss_val;

        // Capture board state as string
        std::stringstream ss;
        ss << gs;
        rec.board = ss.str();

        records.push_back(rec);

        bool had_flat_only_hit_now = (get_flat_only_hits() > prev_flat_only_hits);

        if (had_lru_only_hit_now && !rec_mismatch_found) {
            rec_mismatch_found = true;
            mismatch_op = op_count;
            out_stream << std::endl;
            dump_full_mismatch_report(gs);
        }

        if (had_flat_only_hit_now && !flat_only_mismatch_found) {
            flat_only_mismatch_found = true;
            out_stream << std::endl;
            dump_flat_only_report(gs);
        }

        prev_lru_only_hits = get_lru_only_hits();
        prev_flat_only_hits = get_flat_only_hits();

        return result;
    }

    bool had_recording_mismatch() const { return rec_mismatch_found; }
    bool had_flat_only_mismatch() const { return flat_only_mismatch_found; }

private:
    struct OpRecord {
        int op_num;
        uint64_t hash;
        uint64_t recomputed_hash;
        compact_state payload;
        bool lru_miss;
        bool flat_miss;
        std::string board;
    };

    void dump_full_mismatch_report(const game_state& gs) {
        (void)gs;
        const OpRecord& cur = records.back();

        out_stream << "========================================================" << std::endl;
        out_stream << "FIRST MISMATCH at op " << cur.op_num << std::endl;
        out_stream << "  LRU: " << (cur.lru_miss ? "MISS" : "HIT") << std::endl;
        out_stream << "  Flat: " << (cur.flat_miss ? "MISS" : "HIT") << std::endl;
        out_stream << "========================================================" << std::endl;
        out_stream << std::endl;

        // Hash consistency
        out_stream << "=== Hash Consistency ===" << std::endl;
        out_stream << "  Incremental hash:      0x" << std::hex << cur.hash << std::dec << std::endl;
        out_stream << "  Recomputed from payload: 0x" << std::hex << cur.recomputed_hash << std::dec << std::endl;
        out_stream << "  Match: " << (cur.hash == cur.recomputed_hash ? "YES" : "*** NO — HASH DRIFT ***") << std::endl;
        out_stream << std::endl;

        // Board state at mismatch
        out_stream << "=== Board at Mismatch ===" << std::endl;
        out_stream << cur.board << std::endl;
        out_stream << std::endl;

        // Non-STARTING descriptors at mismatch
        out_stream << "=== Non-STARTING Descriptors at Op " << cur.op_num << " ===" << std::endl;
        dump_descriptors(cur.payload);

        // Since LRU hit, find the earlier op with the same board state
        // (LRU encodes the game state from scratch, so a HIT means an earlier
        //  op produced the same cached_game_state)
        out_stream << "=== Searching for earlier op with same board ===" << std::endl;
        bool found_match = false;
        for (int i = 0; i < (int)records.size() - 1; ++i) {
            if (records[i].board == cur.board) {
                found_match = true;
                const OpRecord& earlier = records[i];
                out_stream << "FOUND: Op " << earlier.op_num
                           << " has identical board state!" << std::endl;
                out_stream << std::endl;

                // Compare hashes
                out_stream << "  Earlier hash:  0x" << std::hex << earlier.hash << std::dec << std::endl;
                out_stream << "  Current hash:  0x" << std::hex << cur.hash << std::dec << std::endl;
                out_stream << "  Hashes match: " << (earlier.hash == cur.hash ? "YES" : "*** NO ***") << std::endl;
                out_stream << std::endl;

                // Compare payloads byte by byte
                out_stream << "  Payload comparison (bytes 3-31):" << std::endl;
                bool payload_match = true;
                for (int b = 3; b < 32; ++b) {
                    if (earlier.payload.data[b] != cur.payload.data[b]) {
                        payload_match = false;
                        out_stream << "    Byte " << b << ": earlier=0x"
                                   << std::hex << std::setw(2) << std::setfill('0')
                                   << (int)earlier.payload.data[b]
                                   << " current=0x"
                                   << std::setw(2) << std::setfill('0')
                                   << (int)cur.payload.data[b]
                                   << std::dec << std::endl;
                    }
                }
                if (payload_match) {
                    out_stream << "    ALL BYTES MATCH (this should not happen if flat missed!)"
                               << std::endl;
                }
                out_stream << std::endl;

                // Compare descriptors card by card
                out_stream << "  Descriptor differences:" << std::endl;
                bool any_diff = false;
                for (uint8_t c = 0; c < 52; ++c) {
                    uint8_t d_earlier = earlier.payload.get_descriptor(c);
                    uint8_t d_current = cur.payload.get_descriptor(c);
                    if (d_earlier != d_current) {
                        any_diff = true;
                        out_stream << "    card " << std::setw(2) << (int)c
                                   << " (" << std::setw(3) << card_name(c) << "): "
                                   << "earlier=" << desc_name(d_earlier)
                                   << "(" << (int)d_earlier << ")"
                                   << " current=" << desc_name(d_current)
                                   << "(" << (int)d_current << ")"
                                   << std::endl;
                    }
                }
                if (!any_diff) {
                    out_stream << "    NO DESCRIPTOR DIFFERENCES" << std::endl;
                }
                out_stream << std::endl;

                // Compare recomputed hashes
                out_stream << "  Earlier recomputed hash: 0x" << std::hex << earlier.recomputed_hash << std::dec << std::endl;
                out_stream << "  Current recomputed hash: 0x" << std::hex << cur.recomputed_hash << std::dec << std::endl;
                out_stream << std::endl;

                // Check hash consistency for earlier op too
                out_stream << "  Earlier hash consistent with payload: "
                           << (earlier.hash == earlier.recomputed_hash ? "YES" : "*** NO ***")
                           << std::endl;
                out_stream << "  Current hash consistent with payload: "
                           << (cur.hash == cur.recomputed_hash ? "YES" : "*** NO ***")
                           << std::endl;

                break;  // Only show first match
            }
        }

        if (!found_match) {
            out_stream << "  NO earlier op with identical board found in " << records.size() - 1 << " ops." << std::endl;
            out_stream << "  NOTE: LRU uses gs.tableau_piles (pile-sorted) while board print" << std::endl;
            out_stream << "  uses original order. Two different pile orderings of the same" << std::endl;
            out_stream << "  state would match in LRU but have different board strings." << std::endl;
            out_stream << std::endl;

            // In this case, look for hash matches instead
            out_stream << "  Searching for hash matches instead:" << std::endl;
            for (int i = 0; i < (int)records.size() - 1; ++i) {
                if (records[i].hash == cur.hash) {
                    out_stream << "    Op " << records[i].op_num << " has same hash 0x"
                               << std::hex << cur.hash << std::dec << std::endl;
                }
            }
            out_stream << std::endl;

            // Show the descriptor differences between mismatch state and each earlier state
            // to look for patterns
            out_stream << "  Looking for payload matches (bytes 3-31):" << std::endl;
            for (int i = 0; i < (int)records.size() - 1; ++i) {
                if (records[i].payload.matches(cur.payload)) {
                    out_stream << "    Op " << records[i].op_num << " has matching payload!" << std::endl;
                }
            }
        }

        out_stream << std::endl;
        out_stream << "=== Cache Stats ===" << std::endl;
        out_stream << "  Size: " << size()
                   << ", evictions: " << get_states_removed_from_cache() << std::endl;
        out_stream << "  Ops: " << get_ops()
                   << ", LRU-only hits: " << get_lru_only_hits()
                   << ", flat-only hits: " << get_flat_only_hits() << std::endl;
        out_stream << std::endl;
    }

    void dump_flat_only_report(const game_state& gs) {
        (void)gs;
        const OpRecord& cur = records.back();

        out_stream << "========================================================" << std::endl;
        out_stream << "FLAT-ONLY HIT at op " << cur.op_num << std::endl;
        out_stream << "  LRU: MISS  (LRU did NOT find this state)" << std::endl;
        out_stream << "  Flat: HIT  (flat DID find this state)" << std::endl;
        out_stream << "========================================================" << std::endl;
        out_stream << std::endl;

        // Board at mismatch
        out_stream << "=== Board at Flat-Only Hit ===" << std::endl;
        out_stream << cur.board << std::endl;
        out_stream << std::endl;

        // Search backward for an earlier op with the SAME board string.
        // If found, the states are genuinely identical and LRU has a false negative.
        // If not found, flat may have a false positive (hash collision).
        out_stream << "=== Verifying: does an earlier op have the same board? ===" << std::endl;
        bool found_match = false;
        for (int i = 0; i < (int)records.size() - 1; ++i) {
            if (records[i].board == cur.board) {
                found_match = true;
                const OpRecord& earlier = records[i];
                out_stream << "YES — Op " << earlier.op_num
                           << " has IDENTICAL board state." << std::endl;
                out_stream << "  => LRU FALSE NEGATIVE CONFIRMED." << std::endl;
                out_stream << "  => The flat cache correctly identified a duplicate"
                           << " that LRU missed." << std::endl;
                out_stream << std::endl;

                // Compare hashes and payloads for completeness
                out_stream << "  Earlier hash: 0x" << std::hex << earlier.hash << std::dec << std::endl;
                out_stream << "  Current hash: 0x" << std::hex << cur.hash << std::dec << std::endl;
                out_stream << "  Hashes match: "
                           << (earlier.hash == cur.hash ? "YES" : "NO") << std::endl;
                out_stream << std::endl;

                // Compare payloads
                out_stream << "  Payload match (bytes 3-31): ";
                bool payload_match = true;
                for (int b = 3; b < 32; ++b) {
                    if (earlier.payload.data[b] != cur.payload.data[b]) {
                        payload_match = false;
                        break;
                    }
                }
                out_stream << (payload_match ? "YES" : "NO") << std::endl;
                out_stream << std::endl;

                break;
            }
        }

        if (!found_match) {
            out_stream << "NO — no earlier op has the same board." << std::endl;
            out_stream << std::endl;

            // Search for payload match (bytes 3-31) to understand what flat matched
            out_stream << "  Searching for earlier op with matching payload..." << std::endl;
            bool found_payload = false;
            for (int i = 0; i < (int)records.size() - 1; ++i) {
                if (records[i].payload.matches(cur.payload)) {
                    found_payload = true;
                    out_stream << "  Op " << records[i].op_num
                               << " has matching payload (bytes 3-31)!" << std::endl;
                    out_stream << "  But DIFFERENT board state:" << std::endl;
                    out_stream << records[i].board << std::endl;
                }
            }
            if (!found_payload) {
                out_stream << "  No earlier op has matching payload either." << std::endl;
                out_stream << "  => Flat HIT must be from a hash collision in the 2-slot cluster" << std::endl;
                out_stream << "     (different payload mapped to same cluster, matched by memcmp)." << std::endl;
            }
            out_stream << std::endl;

            // Search for hash match
            out_stream << "  Earlier ops with same hash:" << std::endl;
            for (int i = 0; i < (int)records.size() - 1; ++i) {
                if (records[i].hash == cur.hash) {
                    out_stream << "    Op " << records[i].op_num << std::endl;
                }
            }
            out_stream << std::endl;
        }
    }

    void dump_descriptors(const compact_state& p) {
        for (uint8_t c = 0; c < 52; ++c) {
            uint8_t d = p.get_descriptor(c);
            if (d != 0) {
                out_stream << "  card " << std::setw(2) << (int)c
                           << " (" << std::setw(3) << card_name(c) << "): "
                           << desc_name(d) << " (" << (int)d << ")" << std::endl;
            }
        }
        out_stream << std::endl;
    }

    std::ofstream& out_stream;
    int op_count;
    bool rec_mismatch_found = false;
    bool flat_only_mismatch_found = false;
    int mismatch_op = 0;
    uint64_t prev_lru_only_hits = 0;
    uint64_t prev_flat_only_hits = 0;
    const sol_rules& rules_ref;
    bool has_hole;
    bool has_stock_waste;
    std::vector<OpRecord> records;
};

class MismatchDiagnostic : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }

    void run_diagnostic(const std::string& preset, int seed) {
        std::string filename = "/tmp/mismatch_diagnostic_" + preset + "_seed" + std::to_string(seed) + ".txt";
        std::ofstream outfile(filename);

        outfile << "=== Mismatch Diagnostic: " << preset << " seed " << seed << " ===" << std::endl;
        outfile << std::endl;

        sol_rules rules = rules_parser::from_preset(preset);
        game_state gs(rules, seed, game_state::streamliner_options::NONE);

        RecordingDiagnosticCache cache(gs, 10000000, outfile, rules);
        solver sol(gs, cache);
        sol.run(boost::optional<std::chrono::milliseconds>(30000));

        if (!cache.had_recording_mismatch()) {
            outfile << "NO MISMATCHES DETECTED" << std::endl;
        }

        outfile << "=== End ===" << std::endl;
        std::cout << "Diagnostic saved to " << filename << std::endl;
    }

    void run_diagnostic_from_file(const std::string& rules_file, const std::string& name, int seed, uint64_t timeout_ms = 60000) {
        std::string filename = "/tmp/mismatch_diagnostic_" + name + "_seed" + std::to_string(seed) + ".txt";
        std::ofstream outfile(filename);

        outfile << "=== Mismatch Diagnostic: " << name << " seed " << seed << " ===" << std::endl;
        outfile << std::endl;

        sol_rules rules = rules_parser::from_file(rules_file);
        game_state gs(rules, seed, game_state::streamliner_options::NONE);

        RecordingDiagnosticCache cache(gs, 10000000, outfile, rules);
        solver sol(gs, cache);
        sol.run(boost::optional<std::chrono::milliseconds>(timeout_ms));

        outfile << "Stats: ops=" << cache.get_ops()
                << " lru_only_hits=" << cache.get_lru_only_hits()
                << " flat_only_hits=" << cache.get_flat_only_hits()
                << " evictions=" << cache.get_states_removed_from_cache() << std::endl;

        if (!cache.had_recording_mismatch() && !cache.had_flat_only_mismatch()) {
            outfile << "NO MISMATCHES DETECTED" << std::endl;
        }

        EXPECT_EQ(cache.get_flat_only_hits(), 0)
            << "FLAT FALSE POSITIVE in " << name << " at seed " << seed
            << " — see " << filename;
        EXPECT_EQ(cache.get_lru_only_hits(), 0)
            << "FLAT FALSE NEGATIVE in " << name << " at seed " << seed
            << " — see " << filename;

        outfile << "=== End ===" << std::endl;
        std::cout << "Diagnostic saved to " << filename << std::endl;
    }
};

TEST_F(MismatchDiagnostic, FreeCellSeed1) {
    run_diagnostic("free-cell", 1);
}

TEST_F(MismatchDiagnostic, FlowerGardenSeed1) {
    run_diagnostic("flower-garden", 1);
}

TEST_F(MismatchDiagnostic, SpanishPatienceSeed1) {
    run_diagnostic("spanish-patience", 1);
}

TEST_F(MismatchDiagnostic, SeahavenTowersSeed1) {
    run_diagnostic("seahaven-towers", 1);
}

TEST_F(MismatchDiagnostic, FortunesFavorSeed31646033) {
    run_diagnostic_from_file("tests/rules/fortunes-favor.json",
                             "fortunes-favor", 31646033);
}

TEST_F(MismatchDiagnostic, CanfieldStrictSeed4000100) {
    run_diagnostic_from_file("tests/rules/canfield-strict.json",
                             "canfield-strict", 4000100);
}
