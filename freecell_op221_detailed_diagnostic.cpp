#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>

#include "src/main/game/zobrist.h"
#include "src/main/game/search-state/game_state.h"
#include "src/main/game/flat_cache.h"
#include "src/main/game/global_cache.h"
#include "src/main/game/dual_cache.h"
#include "src/main/solver/solver.h"
#include "src/main/input-output/input/json-parsing/rules_parser.h"

// Capture detailed state at each operation
struct OpSnapshot {
    int op_num;
    uint64_t hash;
    compact_state payload;
    std::string board_string;

    bool payload_matches(const OpSnapshot& other) const {
        for (int b = 0; b < 32; ++b) {
            if (payload.data[b] != other.payload.data[b]) {
                return false;
            }
        }
        return true;
    }
};

// Cache that records all operations and reports detailed analysis
class DetailedDiagnosticCache : public dual_cache {
private:
    std::vector<OpSnapshot> history;
    int op_count = 0;
    int first_mismatch_at = -1;
    std::ofstream& out_stream;
    bool found_mismatch_at_221 = false;

public:
    DetailedDiagnosticCache(const game_state& gs, uint64_t capacity, std::ofstream& out)
        : dual_cache(
            std::make_unique<flat_cache>(capacity),
            std::make_unique<lru_cache>(gs, capacity),
            "flat", "lru"
          ), out_stream(out) {}

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
        bool lru_result = lru_cache_ref().insert(gs);
        bool flat_result = flat_cache_ref().insert(gs);

        // Check for mismatch at op 221 specifically
        if (op_count == 221) {
            out_stream << "=== DETAILED ANALYSIS AT OP 221 ===" << std::endl;
            out_stream << std::endl;

            // Check if there's a mismatch
            if ((lru_result && !flat_result) || (!lru_result && flat_result)) {
                out_stream << "MISMATCH DETECTED at op 221:" << std::endl;
                out_stream << "  LRU result: " << (lru_result ? "HIT (found)" : "MISS (not found)") << std::endl;
                out_stream << "  Flat result: " << (flat_result ? "HIT (found)" : "MISS (not found)") << std::endl;
                found_mismatch_at_221 = true;
            } else {
                out_stream << "NO MISMATCH at op 221:" << std::endl;
                out_stream << "  Both caches agree: " << (lru_result ? "HIT" : "MISS") << std::endl;
            }

            out_stream << std::endl;
            out_stream << "Hash: 0x" << std::hex << snap.hash << std::dec << std::endl;
            out_stream << std::endl;

            out_stream << "Board at op 221:" << std::endl;
            out_stream << snap.board_string << std::endl;
            out_stream << std::endl;

            // If mismatch is lru=MISS, flat=HIT, search for earlier matching op
            if (!lru_result && flat_result) {
                out_stream << "Searching for earlier operation with matching payload..." << std::endl;
                out_stream << std::endl;

                bool found = false;
                for (int i = 0; i < (int)history.size() - 1; ++i) {
                    if (history[i].payload_matches(snap)) {
                        found = true;
                        out_stream << "FOUND: Op " << history[i].op_num << " has MATCHING PAYLOAD" << std::endl;
                        out_stream << std::endl;
                        out_stream << "Hash at op " << history[i].op_num << ": 0x"
                                  << std::hex << history[i].hash << std::dec << std::endl;
                        out_stream << "Hash at op 221: 0x"
                                  << std::hex << snap.hash << std::dec << std::endl;
                        out_stream << std::endl;

                        out_stream << "Board at op " << history[i].op_num << ":" << std::endl;
                        out_stream << history[i].board_string << std::endl;
                        out_stream << std::endl;

                        out_stream << "=== COMPARISON ===" << std::endl;
                        if (history[i].board_string == snap.board_string) {
                            out_stream << "BOARDS ARE IDENTICAL" << std::endl;
                            out_stream << "This is legitimate deduplication." << std::endl;
                            out_stream << "But LRU missed it — why?" << std::endl;
                        } else {
                            out_stream << "BOARDS ARE DIFFERENT!" << std::endl;
                            out_stream << "This is a FALSE POSITIVE — flat cache matched wrong state." << std::endl;
                            out_stream << "POTENTIAL REGRESSION!" << std::endl;
                        }
                        out_stream << std::endl;

                        break;
                    }
                }

                if (!found) {
                    out_stream << "No earlier op has matching payload." << std::endl;
                    out_stream << "Searching for earlier ops with same hash..." << std::endl;
                    out_stream << std::endl;

                    for (int i = 0; i < (int)history.size() - 1; ++i) {
                        if (history[i].hash == snap.hash) {
                            out_stream << "Op " << history[i].op_num << " has same hash (0x"
                                      << std::hex << snap.hash << std::dec << ")" << std::endl;
                            out_stream << "But different payload (hash collision)" << std::endl;
                            out_stream << std::endl;
                        }
                    }
                }
            }
        }

        return lru_result && flat_result;  // Return only if both agree
    }

    bool had_mismatch_at_221() const { return found_mismatch_at_221; }
};

int main() {
    zobrist_hash::init();
    sol_rules rules = rules_parser::from_preset("free-cell");
    game_state gs(rules, 1, game_state::streamliner_options::NONE, true);

    std::ofstream out("/tmp/freecell_op221_detailed_analysis.txt");
    DetailedDiagnosticCache cache(gs, 10000000, out);
    solver sol(gs, cache);
    sol.run(std::chrono::milliseconds(30000));

    out << "=== END OF ANALYSIS ===" << std::endl;
    if (cache.had_mismatch_at_221()) {
        out << "Mismatch confirmed at op 221" << std::endl;
    } else {
        out << "No mismatch at op 221" << std::endl;
    }

    std::cout << "Detailed analysis written to /tmp/freecell_op221_detailed_analysis.txt" << std::endl;
    return 0;
}
