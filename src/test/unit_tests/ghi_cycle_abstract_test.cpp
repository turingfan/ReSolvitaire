// ghi_cycle_abstract_test.cpp
//
// Part B of the Stage-2 item-2d adversarial test suite (see plan §4.3).
//
// A SELF-CONTAINED, pure-logic demonstration — NO solver engine, NO game state,
// NO cache class — that the cross-pass-reuse soundness hazards described in
//     docs/depth-bounded-search/proposal.md  §3.3/§3.4 (the reuse inequality and
//     min-arrival-depth re-open)  and  §3.5  (cycles / Graph-History Interaction),
// are real, and that this suite has TEETH: a NAIVE cross-pass rule gives the WRONG
// winnability verdict while the DFSTT3 finite-ancestor-contribution rule gives the
// RIGHT one (== an engine-independent brute-force oracle).
//
// It is a handful of tiny abstract directed graphs plus a few search procedures,
// so it can be reasoned about by hand and cannot drift with the real
// implementation. It encodes the *correctness contract* that Stage-2 item 2b must
// satisfy, written independently of (and before) that implementation.
//
// ─── The contract under test (proposal §3.2–§3.5) ────────────────────────────
//
// A depth-bounded pass classifies each node as one of:
//     DEAD      — subtree fully exhausted, NO truncated leaf below it, no goal.
//                 Budget-independent; sound to reuse across all passes.
//     OPEN(b)   — searched to a finite verified remaining-budget b, no goal yet,
//                 >=1 truncated leaf below.  Reuse only if b >= the current budget
//                 AND the node is not re-reached via a strictly shorter path.
//     (absent)  — never visited (or evicted).
// plus a transient ON_PATH marker = "ancestor currently on the search stack".
// `unwinnable` is reported only when a pass exhausts WITHOUT any truncation.
//
// Two naive shortcuts, each provably WRONG here:
//
//   (1) "seen => prune" (a depth/status-blind transposition rule — the task's
//       first named hazard).  It caches a node as terminal the first time its
//       subtree is exhausted *within the bound*, even when a descendant was
//       TRUNCATED — i.e. it conflates OPEN(truncated) with DEAD and persists it.
//       A later, deeper pass then prunes at that false-terminal node and exhausts
//       with no truncation flag => FALSE `unwinnable` (the project's red line).
//       This is exactly the violation of the min-arrival-depth / reuse-inequality
//       discipline of proposal §3.3/§3.4.
//
//   (2) "taint-OPEN" (proposal §3.5 naive choice 1).  To stay sound on cycles it
//       refuses to EVER finalise a node DEAD once a back-edge was seen in the
//       pass.  Sound, but INCOMPLETE: a reachable cycle then blocks the
//       unwinnability proof FOREVER (UNKNOWN even as L -> infinity).
//
// And one subtlety the proposal flags as the place not to "optimise":
//
//   (3) the back-edge to an ON_PATH ancestor must contribute the ancestor's finite
//       estimate, NOT infinity / a fully-closed edge (proposal §3.5 naive choice
//       2).  We include this rule as a toggle on the faithful DFSTT3 pass and show
//       DFSTT3 stays correct.  (Finding: in this clean recursive reachability model
//       the closed-edge-infinity choice alone does not flip the final verdict,
//       because any region reachable through a cycle back to an ancestor is also
//       reachable from that ancestor directly; the engine-level hazard it warns of
//       is reproduced by hazard (1) above, which our discriminating graph uses.)

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

// ─── A tiny abstract search graph ────────────────────────────────────────────
// Unit edge cost, heuristic h == 0, satisficing (we only care WHETHER a goal is
// reachable) — the Solvitaire specialisation of DFSTT3 (proposal §3.5).  Nodes are
// ints; each node lists its successors IN THE ORDER a DFS tries them.

struct Graph {
    std::map<int, std::vector<int>> succ;   // node -> ordered successors
    std::set<int> goals;                    // goal nodes
    int root = 0;

    const std::vector<int>& successors(int n) const {
        static const std::vector<int> empty;
        auto it = succ.find(n);
        return it == succ.end() ? empty : it->second;
    }
    bool is_goal(int n) const { return goals.count(n) != 0; }
};

constexpr int INF = std::numeric_limits<int>::max();

// ─── Ground truth: is a goal reachable from the root AT ALL? ──────────────────
// Plain reachability over the (possibly cyclic) graph with a visited set —
// cache-free, bound-free, trivially correct.  This is the engine-independent
// "unbounded verdict" that every cached procedure must match.
bool reachable_to_goal(const Graph& g) {
    std::set<int> seen;
    std::vector<int> stack{g.root};
    seen.insert(g.root);
    while (!stack.empty()) {
        int n = stack.back();
        stack.pop_back();
        if (g.is_goal(n)) return true;
        for (int c : g.successors(n)) {
            if (seen.insert(c).second) stack.push_back(c);
        }
    }
    return false;
}

enum class Verdict { WINNABLE, UNWINNABLE, UNKNOWN };
const char* to_str(Verdict v) {
    switch (v) {
        case Verdict::WINNABLE:   return "WINNABLE";
        case Verdict::UNWINNABLE: return "UNWINNABLE";
        default:                  return "UNKNOWN";
    }
}
Verdict oracle(const Graph& g) {
    return reachable_to_goal(g) ? Verdict::WINNABLE : Verdict::UNWINNABLE;
}

// ─────────────────────────────────────────────────────────────────────────────
//  (A) The FAITHFUL DFSTT3 bounded pass (the correct contract, proposal §4.1)
// ─────────────────────────────────────────────────────────────────────────────

enum class Status { ABSENT, OPEN, DEAD };
struct Entry {
    Status status = Status::ABSENT;
    int    b      = 0;      // OPEN: finite verified remaining budget.
    int    g_min  = INF;    // minimum arrival depth (for shorter-path re-open).
};

// The one knob the proposal says not to get wrong (§3.5).
enum class CycleRule {
    DFSTT3_FINITE,     // back-edge contributes the ancestor's finite OPEN estimate
    NAIVE_CLOSED_INF   // back-edge contributes infinity (a "fully closed" edge)
};

struct Val { bool win = false; int b = INF; };   // b == INF  <=>  DEAD

static int plus_one(int child_b) {               // saturating 1 + child budget
    if (child_b == INF) return INF;
    return (child_b + 1 > child_b) ? child_b + 1 : INF;
}

class DfsTt3Pass {
public:
    DfsTt3Pass(const Graph& g, int L, CycleRule rule, std::map<int, Entry>& cache)
        : g_(g), L_(L), rule_(rule), cache_(cache) {}

    bool any_truncation() const { return any_trunc_; }

    Val search(int n, int d) {
        if (g_.is_goal(n)) return Val{true, INF};            // FOUND_WIN

        Entry& e = cache_[n];
        const int B_now = L_ - d;

        // Cross-pass reuse decision (proposal §3.3 / §3.4).
        if (e.status == Status::DEAD) return Val{false, INF};   // budget-independent
        if (e.status == Status::OPEN) {
            const bool shorter_path = d < e.g_min;
            if (!shorter_path && e.b >= B_now) return Val{false, e.b};  // covered
            // else more budget now, or a strictly shorter path: re-open below.
        }

        // Cycle: back-edge to an ON_PATH ancestor (proposal §3.5).
        if (on_path_.count(n)) {
            if (rule_ == CycleRule::NAIVE_CLOSED_INF) return Val{false, INF};
            int anc_b = (e.status == Status::OPEN) ? e.b : 0;   // finite estimate
            return Val{false, anc_b};
        }

        // Truncated leaf at the bound.
        if (d == L_) {
            any_trunc_ = true;
            e.status = Status::OPEN;
            e.b      = 0;
            e.g_min  = std::min(e.g_min, d);
            return Val{false, 0};
        }

        // Expand.
        on_path_.insert(n);
        e.status = Status::OPEN;                  // provisional while expanding
        e.g_min  = std::min(e.g_min, d);
        e.b      = std::max(e.b, B_now);          // a finite value for back-edges

        const std::vector<int>& kids = g_.successors(n);
        if (kids.empty()) {                       // genuine dead end -> DEAD
            on_path_.erase(n);
            e.status = Status::DEAD;
            return Val{false, INF};
        }

        int verified = INF;                       // min over children of 1+child_b
        for (int c : kids) {
            Val r = search(c, d + 1);
            if (r.win) { on_path_.erase(n); return Val{true, INF}; }
            verified = std::min(verified, plus_one(r.b));
        }
        on_path_.erase(n);

        if (verified == INF) {                    // all children dead -> DEAD
            e.status = Status::DEAD;
            return Val{false, INF};
        }
        e.status = Status::OPEN;                   // truncation still below -> OPEN
        e.b      = verified;
        return Val{false, verified};
    }

private:
    const Graph& g_;
    int          L_;
    CycleRule    rule_;
    std::map<int, Entry>& cache_;
    std::set<int> on_path_;
    bool any_trunc_ = false;
};

// Outer iterative-deepening loop WITH cross-pass cache reuse (the Stage-2 config,
// proposal §4.2 with the cache kept across passes).
Verdict id_dfstt3(const Graph& g, CycleRule rule, int L0, int grow, int Lmax) {
    std::map<int, Entry> cache;                   // PERSISTS across passes
    int L = L0;
    for (int guard = 0; guard < 1000; ++guard) {
        DfsTt3Pass pass(g, L, rule, cache);
        Val r = pass.search(g.root, 0);
        if (r.win)                  return Verdict::WINNABLE;
        if (!pass.any_truncation()) return Verdict::UNWINNABLE;   // root DEAD, no trunc
        if (L >= Lmax)              return Verdict::UNKNOWN;
        int nl = L * grow;
        if (nl <= L) nl = L + 1;                   // non-progress guard
        L = nl;
    }
    return Verdict::UNKNOWN;
}

// ─────────────────────────────────────────────────────────────────────────────
//  (B) NAIVE rule 1 — "seen => prune" (status/budget-blind transposition)
// ─────────────────────────────────────────────────────────────────────────────
// Caches a node as terminal the first time its subtree is exhausted WITHIN the
// bound — including when a descendant was truncated (it does NOT distinguish
// OPEN(truncated) from DEAD) — and persists that across passes. A later pass then
// prunes at the false-terminal node. This is the unsound rule the task names.
class SeenPrunePass {
public:
    SeenPrunePass(const Graph& g, int L, std::set<int>& seen)
        : g_(g), L_(L), seen_(seen) {}
    bool any_truncation() const { return any_trunc_; }

    // returns {win, terminal}
    std::pair<bool, bool> search(int n, int d) {
        if (g_.is_goal(n)) return {true, false};
        if (seen_.count(n)) return {false, true};      // <- the unsound prune
        if (on_path_.count(n)) return {false, true};   // cycle: treat as closed
        if (d == L_) { any_trunc_ = true; return {false, false}; }

        on_path_.insert(n);
        const std::vector<int>& kids = g_.successors(n);
        if (kids.empty()) { on_path_.erase(n); seen_.insert(n); return {false, true}; }

        bool all_terminal = true;
        for (int c : kids) {
            auto r = search(c, d + 1);
            if (r.first) { on_path_.erase(n); return {true, false}; }
            if (!r.second) all_terminal = false;
        }
        on_path_.erase(n);
        // BUG: cache as "seen" regardless of whether a descendant truncated. The
        // node may have been only partially explored, but it is now pruned forever.
        seen_.insert(n);
        return {false, all_terminal};
    }

private:
    const Graph& g_;
    int          L_;
    std::set<int>& seen_;
    std::set<int>  on_path_;
    bool any_trunc_ = false;
};

Verdict id_seen_prune(const Graph& g, int L0, int grow, int Lmax) {
    std::set<int> seen;                            // PERSISTS across passes
    int L = L0;
    for (int guard = 0; guard < 1000; ++guard) {
        SeenPrunePass pass(g, L, seen);
        auto r = pass.search(g.root, 0);
        if (r.first)  return Verdict::WINNABLE;
        if (r.second && !pass.any_truncation()) return Verdict::UNWINNABLE;
        if (L >= Lmax) return Verdict::UNKNOWN;
        int nl = L * grow; if (nl <= L) nl = L + 1; L = nl;
    }
    return Verdict::UNKNOWN;
}

// ─────────────────────────────────────────────────────────────────────────────
//  (C) NAIVE rule 2 — "taint-OPEN" (never finalise DEAD once a cycle was seen)
// ─────────────────────────────────────────────────────────────────────────────
class TaintPass {
public:
    TaintPass(const Graph& g, int L) : g_(g), L_(L) {}
    bool any_truncation() const { return any_trunc_; }
    bool saw_cycle()      const { return saw_cycle_; }

    std::pair<bool, bool> search(int n, int d) {     // returns {win, dead}
        if (g_.is_goal(n)) return {true, false};
        if (on_path_.count(n)) { saw_cycle_ = true; return {false, false}; }
        if (d == L_) { any_trunc_ = true; return {false, false}; }
        on_path_.insert(n);
        const std::vector<int>& kids = g_.successors(n);
        if (kids.empty()) { on_path_.erase(n); return {false, true}; }
        bool all_dead = true;
        for (int c : kids) {
            auto r = search(c, d + 1);
            if (r.first) { on_path_.erase(n); return {true, false}; }
            if (!r.second) all_dead = false;
        }
        on_path_.erase(n);
        return {false, all_dead};
    }
private:
    const Graph& g_;
    int          L_;
    std::set<int> on_path_;
    bool any_trunc_ = false;
    bool saw_cycle_ = false;
};

Verdict id_taint(const Graph& g, int L0, int grow, int Lmax) {
    int L = L0;
    for (int guard = 0; guard < 1000; ++guard) {
        TaintPass pass(g, L);
        auto r = pass.search(g.root, 0);
        if (r.first) return Verdict::WINNABLE;
        // taint rule: a proof of unwinnability is accepted only if NO cycle was
        // seen (and no truncation). A reachable cycle blocks the proof forever.
        if (r.second && !pass.any_truncation() && !pass.saw_cycle())
            return Verdict::UNWINNABLE;
        if (L >= Lmax) return Verdict::UNKNOWN;
        int nl = L * grow; if (nl <= L) nl = L + 1; L = nl;
    }
    return Verdict::UNKNOWN;
}

// ─── The discriminating graphs ───────────────────────────────────────────────

// Graph 1 — WINNABLE, with a cycle, and the goal one ply beyond a small bound.
//
//     ROOT(0) -> D(1) -> C(2) -> { ROOT (back-edge / cycle),  G(3)=goal }
//
// The ONLY goal path is ROOT -> D -> C -> G (length 3). There is a cycle
// ROOT -> D -> C -> ROOT.  Oracle: WINNABLE.
//
// Why "seen => prune" gives a FALSE `unwinnable` (hand-traced):
//   Pass L=2:  ROOT(d0) -> D(d1) -> C(d2 == L) is TRUNCATED (G is at d3, unseen).
//              On backtrack, D and ROOT are cached "seen" even though a descendant
//              was truncated.  Pass result: truncation occurred -> deepen.
//              seen = {ROOT, D}  PERSISTS.
//   Pass L=4:  ROOT is "seen" -> pruned immediately; the pass exhausts with NO
//              truncation flag -> reports UNWINNABLE.  But G sits at depth 3 < 4
//              and was never looked at.  FALSE `unwinnable`.
//   DFSTT3 keeps ROOT/D as OPEN(truncated), re-opens them when L grows, and finds
//   G at depth 3 in the L=4 pass -> WINNABLE (== oracle).
Graph make_win_behind_cycle() {
    Graph g;
    g.root = 0;
    g.succ[0] = {1};        // ROOT -> D
    g.succ[1] = {2};        // D    -> C
    g.succ[2] = {0, 3};     // C    -> ROOT (cycle, tried first), then G
    g.succ[3] = {};         // G is the goal
    g.goals   = {3};
    return g;
}

// Graph 2 — genuinely UNWINNABLE, with a reachable cycle.
//
//     ROOT(0) -> A(1) -> { B(2) -> A (back-edge / cycle),  C(3) -> (dead end) }
//
// No goal anywhere.  Oracle: UNWINNABLE.  The taint-OPEN rule sees the reachable
// cycle and therefore NEVER accepts an unwinnability proof (UNKNOWN forever, even
// for L far beyond the longest acyclic path); DFSTT3 finalises the region DEAD and
// returns UNWINNABLE (== oracle).
Graph make_unwinnable_with_cycle() {
    Graph g;
    g.root = 0;
    g.succ[0] = {1};        // ROOT -> A
    g.succ[1] = {2, 3};     // A -> B, A -> C
    g.succ[2] = {1};        // B -> A   (back-edge / cycle)
    g.succ[3] = {};         // C is a dead end
    g.goals   = {};         // NO goal
    return g;
}

// Graph 3 — WINNABLE, goal hidden BEHIND a cycle (the proposal §3.5 "naive choice 2
// is unsound" scenario — here shown SOUND for satisficing reachability).
//
//     ROOT(0) -> A(1) -> { B(2) -> A (back-edge / cycle),  T(3) -> U(4) -> G(5)=goal }
//
// The back-edge B->A closes a cycle. Under the +inf (closed-edge) rule, B's only child
// is the cycle, so B finalises DEAD and is cached across passes. The ONLY goal path is
// ROOT->A->T->U->G (length 4), which sits BEHIND the cycle in the sense that B can reach
// it only via B->A->T->...->G. The hazard the proposal warned about: B is cached DEAD
// while a goal is reachable "through" the cycle. It is NOT unsound, because the cycle
// target A is an ANCESTOR of B — reachable from ROOT via the prefix ROOT->A that does
// not pass through B — so the goal is found by expanding A directly (A->T->U->G), and
// B's DEAD never blocks it. Oracle: WINNABLE. Both cycle rules must agree.
//
// Hand-trace (L0=3): pass L=3 expands B (d2<3), hits B->A on-path => B contributes the
// cycle value and finalises DEAD (cached); A->T->U(d3==L) truncates => deepen, B stays
// cached DEAD. Pass L=6: B is pruned (DEAD), but A is re-opened and A->T->U->G (d4<6) is
// found => WINNABLE. A DEAD-via-+inf B did not hide the goal.
Graph make_goal_behind_cycle() {
    Graph g;
    g.root = 0;
    g.succ[0] = {1};        // ROOT -> A
    g.succ[1] = {2, 3};     // A -> B (cycle, tried first), A -> T
    g.succ[2] = {1};        // B -> A   (back-edge / cycle)
    g.succ[3] = {4};        // T -> U
    g.succ[4] = {5};        // U -> G
    g.succ[5] = {};         // G is the goal
    g.goals   = {5};
    return g;
}

// ─── Tests ───────────────────────────────────────────────────────────────────

// Sanity: the brute-force oracle classifies the two graphs as intended.
TEST(GhiCycleAbstract, GroundTruthOracleIsAsConstructed) {
    EXPECT_EQ(oracle(make_win_behind_cycle()),      Verdict::WINNABLE);
    EXPECT_EQ(oracle(make_unwinnable_with_cycle()), Verdict::UNWINNABLE);
}

// THE DISCRIMINATING TEST — false-`unwinnable` direction (the project's red line).
// On the winnable-behind-a-cycle graph, the naive "seen => prune" transposition
// rule reports the WRONG verdict (UNWINNABLE) while DFSTT3 reports the RIGHT one
// (WINNABLE == oracle).  A Stage-2 2b that prunes a partially-explored / cycle node
// without the budget/min-arrival-depth discipline would reproduce the naive failure
// and this test would catch it.
TEST(GhiCycleAbstract, NaiveSeenPruneFalselyReportsUnwinnable_DFSTT3Correct) {
    const Graph g = make_win_behind_cycle();
    const Verdict truth = oracle(g);
    ASSERT_EQ(truth, Verdict::WINNABLE);

    // Small L0 + geometric growth => the goal is below the horizon for the first
    // pass, so truncation+deepening and cross-pass reuse actually happen.
    const int L0 = 2, grow = 2, Lmax = 64;

    const Verdict dfstt3 = id_dfstt3(g, CycleRule::DFSTT3_FINITE, L0, grow, Lmax);
    const Verdict naive  = id_seen_prune(g, L0, grow, Lmax);

    EXPECT_EQ(dfstt3, truth)
        << "DFSTT3 must match the oracle (" << to_str(truth) << ") but gave "
        << to_str(dfstt3);

    EXPECT_EQ(naive, Verdict::UNWINNABLE)
        << "naive seen=>prune rule was expected to FALSELY report UNWINNABLE";
    EXPECT_NE(naive, truth)
        << "naive rule unexpectedly agreed with the oracle — the graph no longer "
           "discriminates; revisit the construction";
}

// THE DISCRIMINATING TEST — never-`unwinnable` / incompleteness direction.
// On a genuinely unwinnable graph that contains a reachable cycle, the taint-OPEN
// rule can NEVER prove unwinnability (UNKNOWN even with a huge bound), while DFSTT3
// correctly proves UNWINNABLE (== oracle).
TEST(GhiCycleAbstract, NaiveTaintOpenNeverProvesUnwinnable_DFSTT3Correct) {
    const Graph g = make_unwinnable_with_cycle();
    const Verdict truth = oracle(g);
    ASSERT_EQ(truth, Verdict::UNWINNABLE);

    // Lmax is FAR larger than the longest acyclic path (3): the incompleteness is
    // intrinsic to the taint rule, not a too-small-bound artefact.
    const int L0 = 2, grow = 2, Lmax = 4096;

    const Verdict dfstt3 = id_dfstt3(g, CycleRule::DFSTT3_FINITE, L0, grow, Lmax);
    const Verdict taint  = id_taint(g, L0, grow, Lmax);

    EXPECT_EQ(dfstt3, truth)
        << "DFSTT3 must prove UNWINNABLE on the unwinnable cyclic graph but gave "
        << to_str(dfstt3);

    EXPECT_EQ(taint, Verdict::UNKNOWN)
        << "taint-OPEN rule was expected to NEVER prove unwinnable (UNKNOWN)";
    EXPECT_NE(taint, truth)
        << "taint rule unexpectedly proved the verdict — the graph no longer "
           "discriminates the incompleteness failure";
}

// DFSTT3 stays correct on the discriminating graphs under BOTH cycle rules. This
// documents the §3.5 "naive choice 2" finding: in this clean recursive reachability
// model the closed-edge-infinity back-edge choice does NOT by itself flip the final
// verdict (the engine-level hazard it warns of is the seen=>prune / partial-node
// reuse exercised above). The real soundness guard for 2b is therefore the
// truncation-aware OPEN/DEAD discipline, which the engine-level tests (Part A,
// depth_bound_verdict_test.cpp) check directly on the live solver.
TEST(GhiCycleAbstract, DFSTT3CorrectUnderBothCycleRules) {
    const Graph win   = make_win_behind_cycle();
    const Graph unwin = make_unwinnable_with_cycle();

    EXPECT_EQ(id_dfstt3(win,   CycleRule::DFSTT3_FINITE,    2, 2, 64),   Verdict::WINNABLE);
    EXPECT_EQ(id_dfstt3(win,   CycleRule::NAIVE_CLOSED_INF, 2, 2, 64),   Verdict::WINNABLE);
    EXPECT_EQ(id_dfstt3(unwin, CycleRule::DFSTT3_FINITE,    2, 2, 4096), Verdict::UNWINNABLE);
    EXPECT_EQ(id_dfstt3(unwin, CycleRule::NAIVE_CLOSED_INF, 2, 2, 4096), Verdict::UNWINNABLE);
}

// TEETH for the +inf (closed-edge) cycle rule — the engine's DEFAULT since the 2b
// review (Ian, 2026-06-08: "mark a-s-a as dead ... but not a-s or a- on its own", to
// collapse cyclic-dead regions). On a graph with a goal hidden BEHIND a cycle, the
// +inf rule caches the cycle node DEAD yet MUST still report WINNABLE (the goal is
// reachable from the always-expanded ancestor). This directly tests the proposal's
// claimed-unsound scenario and confirms it is sound for satisficing reachability.
// Both cycle rules must equal the oracle.
TEST(GhiCycleAbstract, ClosedEdgeCycleDeadDoesNotHideGoalBehindCycle_BothRules) {
    const Graph g = make_goal_behind_cycle();
    const Verdict truth = oracle(g);
    ASSERT_EQ(truth, Verdict::WINNABLE);

    // L0=3 so B (depth 2) is EXPANDED (cycle detected, not truncated) while the goal
    // (depth 4) is beyond the first horizon — forcing the cache-DEAD-then-deepen path.
    const int L0 = 3, grow = 2, Lmax = 256;

    const Verdict closed = id_dfstt3(g, CycleRule::NAIVE_CLOSED_INF, L0, grow, Lmax);
    const Verdict finite = id_dfstt3(g, CycleRule::DFSTT3_FINITE,    L0, grow, Lmax);

    EXPECT_EQ(closed, truth)
        << "+inf (closed-edge) cycle rule must NOT turn a goal-behind-a-cycle into a "
           "false unwinnable; got " << to_str(closed);
    EXPECT_EQ(finite, truth)
        << "finite DFSTT3 rule must also match the oracle; got " << to_str(finite);
}

// Belt-and-braces: when the initial bound already covers the whole (small,
// acyclic) graph — so the FIRST pass has no truncation — ALL three procedures
// agree with the oracle. This confirms the procedures are not trivially biased and
// that the disagreements asserted above are caused specifically by the
// truncation / cycle interaction (the regime cross-pass reuse runs in), not by a
// blanket bug in one procedure.
//
// NB: L0 is chosen LARGER than the graph's max depth on purpose. The seen=>prune
// bug fires whenever a goal lies BEYOND L0 (with OR without a cycle) — that is the
// whole hazard — so a fair "agreement" check must start past the depth where
// truncation occurs.
TEST(GhiCycleAbstract, AllProceduresAgreeWhenNoTruncationOnAcyclicGraphs) {
    const int L0 = 8, grow = 2, Lmax = 64;       // L0 > every depth below

    Graph win;  // acyclic, winnable: goal at depth 2
    win.root = 0;
    win.succ[0] = {1};
    win.succ[1] = {2};
    win.succ[2] = {};
    win.goals = {2};
    ASSERT_EQ(oracle(win), Verdict::WINNABLE);
    EXPECT_EQ(id_dfstt3(win, CycleRule::DFSTT3_FINITE, L0, grow, Lmax), Verdict::WINNABLE);
    EXPECT_EQ(id_seen_prune(win, L0, grow, Lmax),                       Verdict::WINNABLE);
    EXPECT_EQ(id_taint(win, L0, grow, Lmax),                            Verdict::WINNABLE);

    Graph unwin;  // acyclic, goal-free: unwinnable
    unwin.root = 0;
    unwin.succ[0] = {1, 2};
    unwin.succ[1] = {};
    unwin.succ[2] = {};
    unwin.goals = {};
    ASSERT_EQ(oracle(unwin), Verdict::UNWINNABLE);
    EXPECT_EQ(id_dfstt3(unwin, CycleRule::DFSTT3_FINITE, L0, grow, Lmax), Verdict::UNWINNABLE);
    EXPECT_EQ(id_seen_prune(unwin, L0, grow, Lmax),                       Verdict::UNWINNABLE);
    EXPECT_EQ(id_taint(unwin, L0, grow, Lmax),                            Verdict::UNWINNABLE);
}

}  // namespace
