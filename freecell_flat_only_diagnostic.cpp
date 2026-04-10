#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>

// Include Solvitaire headers - adjust paths as needed
#include "src/main/game/zobrist.h"
#include "src/main/game/search-state/game_state.h"
#include "src/main/game/flat_cache.h"
#include "src/main/game/global_cache.h"
#include "src/main/game/dual_cache.h"
#include "src/main/solver/solver.h"
#include "src/main/input-output/input/json-parsing/rules_parser.h"

// Record each operation's state
struct OpSnapshot {
    int op_num;
    uint64_t hash;
    compact_state payload;
    std::string board_string;
};

// Custom cache that records all operations
class HistoryCapturingCache : public dual_cache {
private:
    std::vector<OpSnapshot> history;
    int op_count = 0;
    int first_flat_only_at = -1;

public:
    HistoryCapturingCache(const game_state& gs, uint64_t capacity)
        : dual_cache(
            std::make_unique<flat_cache>(capacity),
            std::make_unique<lru_cache>(gs, capacity),
            "flat", "lru"
          ) {}

    bool insert(const game_state& gs) override {
        op_count++;

        // Record snapshot BEFORE insertion
        OpSnapshot snap;
        snap.op_num = op_count;
        snap.hash = gs.get_zobrist_hash();
        snap.payload = gs.get_payload();
        std::stringstream ss;
        ss << gs;
        snap.board_string = ss.str();
        history.push_back(snap);

        // Do the actual insertion
        bool result = dual_cache::insert(gs);

        // Check for first flat-only hit
        if (get_flat_only_hits() > 0 && first_flat_only_at == -1) {
            first_flat_only_at = op_count;
        }

        return result;
    }

    void print_analysis(std::ostream& out) {
        if (first_flat_only_at == -1) {
            out << "No flat-only hits detected\n";
            return;
        }

        out << "FIRST FLAT-ONLY HIT at op " << first_flat_only_at << "\n";
        out << "Hash: 0x" << std::hex << history[first_flat_only_at - 1].hash << std::dec << "\n";
        out << "Board:\n" << history[first_flat_only_at - 1].board_string << "\n\n";

        // Search backward for matching board
        out << "Searching for earlier operation with identical board...\n";
        for (int i = 0; i < first_flat_only_at - 1; i++) {
            if (history[i].board_string == history[first_flat_only_at - 1].board_string) {
                out << "FOUND: Operation " << history[i].op_num << " has identical board!\n";
                out << "  Earlier op hash:  0x" << std::hex << history[i].hash << std::dec << "\n";
                out << "  Current op hash:  0x" << std::hex << history[first_flat_only_at - 1].hash << std::dec << "\n";
                out << "  Hashes match: " << (history[i].hash == history[first_flat_only_at - 1].hash ? "YES" : "NO") << "\n";
                return;
            }
        }

        out << "NO earlier operation with identical board found\n";
        out << "(This suggests flat cache may have a false positive or boards differ only in pile ordering)\n";
    }
};

int main() {
    zobrist_hash::init();
    sol_rules rules = rules_parser::from_preset("free-cell");
    game_state gs(rules, 1, game_state::streamliner_options::NONE, true);

    HistoryCapturingCache cache(gs, 10000000);
    solver sol(gs, cache);
    sol.run(std::chrono::milliseconds(10000));

    std::ofstream out("/tmp/freecell_flat_only_analysis.txt");
    out << "FreeCell Seed 1 - Flat-Only Hit Analysis\n";
    out << "=========================================\n\n";
    out << "Flat-only hits: " << cache.get_flat_only_hits() << "\n";
    out << "LRU-only hits: " << cache.get_lru_only_hits() << "\n\n";

    cache.print_analysis(out);

    std::cout << "Analysis written to /tmp/freecell_flat_only_analysis.txt\n";
    return 0;
}
