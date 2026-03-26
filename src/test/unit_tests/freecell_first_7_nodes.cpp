#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <vector>
#include "../../main/game/zobrist.h"
#include "../../main/game/search-state/game_state.h"
#include "../../main/game/global_cache.h"
#include "../../main/game/flat_cache.h"
#include "../../main/input-output/input/json-parsing/rules_parser.h"

struct NodeRecord {
    int node_num;
    std::string board;
    uint64_t hash;
    bool lru_hit;
    bool flat_hit;
};

class CaptureFirst7Nodes : public cache_interface {
public:
    CaptureFirst7Nodes(const game_state& gs, uint64_t capacity)
        : lru(gs, capacity)
        , flat(capacity)
        , node_count(0)
    {}

    bool insert(const game_state& gs) override {
        node_count++;

        bool lru_result = lru.insert(gs);
        bool flat_result = flat.insert(gs);

        if (node_count <= 7) {
            NodeRecord rec;
            rec.node_num = node_count;
            rec.lru_hit = !lru_result;
            rec.flat_hit = !flat_result;
            rec.hash = gs.get_zobrist_hash();

            std::stringstream ss;
            ss << gs;
            rec.board = ss.str();

            nodes.push_back(rec);

            if (rec.lru_hit != rec.flat_hit) {
                mismatch_node = node_count;
            }
        }

        if (node_count > 7) {
            return flat_result;
        }

        return flat_result;
    }

    bool contains(const game_state& gs) const override {
        return flat.contains(gs);
    }

    void clear() override {
        lru.clear();
        flat.clear();
        node_count = 0;
    }

    uint64_t size() const override { return flat.size(); }
    uint64_t get_states_removed_from_cache() const override { return flat.get_states_removed_from_cache(); }
    uint64_t bucket_count() const override { return flat.bucket_count(); }

    const std::vector<NodeRecord>& get_nodes() const { return nodes; }
    int get_mismatch_node() const { return mismatch_node; }
    int get_node_count() const { return node_count; }

private:
    lru_cache lru;
    flat_cache flat;
    int node_count;
    int mismatch_node = 0;
    std::vector<NodeRecord> nodes;
};

class FreeCellFirst7Nodes : public ::testing::Test {
protected:
    void SetUp() override {
        zobrist_hash::init();
    }

    void capture_first_7() {
        sol_rules rules = rules_parser::from_preset("free-cell");
        game_state gs(rules, 1, game_state::streamliner_options::NONE);

        CaptureFirst7Nodes cache(gs, 10000000);

        // Manually insert initial state
        cache.insert(gs);

        // Run DFS but stop after we've captured 7 nodes or hit mismatch
        std::vector<move> moves;
        moves = gs.get_legal_moves();

        while (cache.get_node_count() < 7 && !moves.empty()) {
            move m = moves[0];
            gs.make_move(m);
            cache.insert(gs);

            if (cache.get_node_count() >= 7) break;

            moves = gs.get_legal_moves();
            if (moves.empty()) {
                gs.undo_move(m);
                moves = gs.get_legal_moves();
                if (!moves.empty()) {
                    moves.erase(moves.begin());
                }
            }
        }

        // Write output
        std::ofstream ofs("/tmp/freecell_first_7_nodes.txt");

        ofs << "=== FreeCell Seed 1: First 7 Cache Operations ===" << std::endl;
        ofs << std::endl;

        const auto& nodes = cache.get_nodes();
        for (const auto& node : nodes) {
            ofs << "--- Node " << node.node_num << " ---" << std::endl;
            ofs << "LRU.insert() returned: " << (node.lru_hit ? "false (HIT)" : "true (MISS)") << std::endl;
            ofs << "Flat.insert() returned: " << (node.flat_hit ? "false (HIT)" : "true (MISS)") << std::endl;
            if (node.lru_hit != node.flat_hit) {
                ofs << "*** MISMATCH ***" << std::endl;
            }
            ofs << "Hash: 0x" << std::hex << node.hash << std::dec << std::endl;
            ofs << std::endl;
            ofs << "Board state:" << std::endl;
            ofs << node.board << std::endl;
            ofs << std::endl << std::endl;
        }

        std::cout << "First 7 nodes saved to /tmp/freecell_first_7_nodes.txt" << std::endl;
    }
};

TEST_F(FreeCellFirst7Nodes, Capture) {
    capture_first_7();
}
