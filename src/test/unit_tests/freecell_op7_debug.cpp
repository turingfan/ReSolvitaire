#include <gtest/gtest.h>
#include <iostream>
#include <vector>
#include "../../main/game/zobrist.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/game/global_cache.h"
#include "../../main/game/flat_cache.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"

class FreeCellOp7Debug : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }

    void test_freecell_op7() {
        sol_rules rules = rules_parser::from_preset("free-cell");
        game_state gs(rules, 1, game_state::streamliner_options::NONE);

        lru_cache lru(gs, 10000000);
        flat_cache flat(10000000);

        std::vector<game_state> states_seen;
        std::vector<std::pair<bool, bool>> insert_results;  // (lru_result, flat_result)

        int op = 0;
        std::vector<move> moves = gs.get_legal_moves();

        std::cout << "\n=== Initial state ===" << std::endl;
        std::cout << "Hash: 0x" << std::hex << gs.get_zobrist_hash() << std::dec << std::endl;
        std::cout << "Legal moves: " << moves.size() << std::endl;

        // Insert initial state
        op++;
        bool lru_res = lru.insert(gs);
        bool flat_res = flat.insert(gs);
        states_seen.push_back(gs);
        insert_results.push_back({lru_res, flat_res});

        std::cout << "Op 1: insert initial, LRU=" << (lru_res ? "MISS" : "HIT")
                  << ", Flat=" << (flat_res ? "MISS" : "HIT") << std::endl;

        // DFS to op 7
        while (op < 7) {
            if (moves.empty()) {
                std::cout << "ERROR: No moves at op " << op << std::endl;
                break;
            }

            // Take first move for determinism
            move m = moves[0];
            gs.make_move(m);
            op++;

            moves = gs.get_legal_moves();

            bool lru_res = lru.insert(gs);
            bool flat_res = flat.insert(gs);
            states_seen.push_back(gs);
            insert_results.push_back({lru_res, flat_res});

            std::cout << "Op " << op << ": move "
                      << " | LRU=" << (lru_res ? "MISS" : "HIT")
                      << ", Flat=" << (flat_res ? "MISS" : "HIT")
                      << " | Hash: 0x" << std::hex << gs.get_zobrist_hash() << std::dec;

            if (lru_res != flat_res) {
                std::cout << " <-- MISMATCH";
            }
            std::cout << std::endl;
        }

        std::cout << "\n=== At Operation 7 ===" << std::endl;
        if (op == 7) {
            const game_state& state_at_7 = states_seen[6];
            std::cout << "Board:\n" << state_at_7;
            std::cout << "\nZobrist hash: 0x" << std::hex << state_at_7.get_zobrist_hash() << std::dec << std::endl;

            // Check: is this state truly new?
            bool lru_contains = lru.contains(state_at_7);
            bool flat_contains = flat.contains(state_at_7);

            std::cout << "\nPost-insertion check:" << std::endl;
            std::cout << "LRU.contains(state_at_7): " << (lru_contains ? "YES (HIT)" : "NO (MISS)") << std::endl;
            std::cout << "Flat.contains(state_at_7): " << (flat_contains ? "YES (HIT)" : "NO (MISS)") << std::endl;

            // Check if this state appeared before op 7
            std::cout << "\n=== Checking for duplicates before op 7 ===" << std::endl;
            for (int i = 0; i < 6; i++) {
                if (states_seen[i].get_zobrist_hash() == state_at_7.get_zobrist_hash()) {
                    std::cout << "DUPLICATE HASH: State at op " << (i + 1) << " matches op 7!" << std::endl;
                    std::cout << "Op " << (i + 1) << " hash: 0x" << std::hex
                              << states_seen[i].get_zobrist_hash() << std::dec << std::endl;
                }
            }

            // Compare payloads
            std::cout << "\n=== Payload comparison for ops 1-7 ===" << std::endl;
            for (int i = 0; i < 7; i++) {
                std::cout << "Op " << (i + 1) << " hash: 0x" << std::hex
                          << states_seen[i].get_zobrist_hash() << std::dec << std::endl;
            }
        }
    }
};

TEST_F(FreeCellOp7Debug, AnalyzeFirstMismatch) {
    test_freecell_op7();
}
