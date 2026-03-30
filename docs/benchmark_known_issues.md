# Benchmark Implementation: Known Issues & Modularity Concerns

## Known Issue: C++ Benchmark Code Duplication

### Problem
Benchmark logic is split between two places:
- **C++**: `src/main/evaluation/benchmark.cpp` - Runs benchmarks, collects raw metrics
- **Python**: `scripts/compare_benchmarks.py` - Orchestrates comparisons, calculates statistics

This creates maintenance issues:
- **Branch drift**: Features added to one location don't automatically exist elsewhere
- **Duplication**: Complex metrics (PAR2, geometric mean, detailed solver metrics) implemented in both
- **Inconsistency**: Different branches can diverge in capability (e.g., mac-dev-benchmark-enhancements initially lacked detailed metrics)
- **Slower iteration**: Any new metric requires updates in two separate languages/locations

### Current State
Features are currently duplicated:
- Geometric means, PAR2, median calculations exist in both C++ and Python
- Detailed solver metrics (unique_nodes, backtracks, etc.) in both
- Virtual memory tracking in both
- Solution type reporting in both

### Long-Term Solution
**Refactor: Move all benchmark logic to Python orchestrator**

1. **C++ responsibilities** (minimize to essential only):
   - Initialize game state from seed or JSON deal
   - Execute solver with timeout
   - Output minimal JSON: `{time_us, nodes, solution_type, unique_nodes, backtracks, dominance_moves, ...}`
   - No statistics or comparisons

2. **Python responsibilities** (all logic):
   - Load/run C++ benchmarks via subprocess
   - Parse JSON output
   - Calculate all statistics (geometric mean, PAR2, median, NPS metrics, standard deviation)
   - Perform comparisons and generate reports
   - Memory measurement and reporting
   - Hardware normalization

3. **Benefits**:
   - Single source of truth for metrics
   - Faster to add/change metrics (Python is quicker to iterate)
   - Consistency across all branches automatically
   - Easier to maintain
   - Reduces C++ complexity

### Why This Matters
Without consolidation, each new feature or change requires:
1. Implement in Python
2. Implement in C++
3. Test both
4. Remember to port between branches

This is error-prone and slows development. The Python-only approach eliminates this burden.

---

## CRITICAL Issue: mac-dev-benchmark-enhancements Branch Contamination

### Problem
**mac-dev-benchmark-enhancements MUST remain a clean benchmarking-only branch.** It has repeatedly been contaminated by merges from `refactor-caching` branch (specifically commits that pull in flat_cache implementations and other M0-M8 milestone work).

This contamination:
- Violates the architectural separation of concerns
- Introduces flat_cache dependencies that mac-dev-benchmark-enhancements should NOT have
- Makes it impossible to use this branch for focused benchmarking work without the full refactor-caching infrastructure
- Requires manual cleanup and forced pushes

### Why It Matters
- **mac-dev**: Production branch that can include refactor-caching and other major features
- **mac-dev-benchmark-enhancements**: Research/benchmarking-only branch that should be INDEPENDENT of refactor-caching
- **implement-benchmark-features**: Feature development branch, safe to merge from

Merging refactor-caching into mac-dev-benchmark-enhancements is **architectural contamination** and must never happen.

### Prevention (Mandatory)
When working with these branches:
1. **NEVER** merge refactor-caching into mac-dev-benchmark-enhancements
2. **ONLY** merge from mac-dev or implement-benchmark-features if absolutely needed
3. If contamination occurs (detected by presence of flat_cache includes or M0-M8 milestone commits), reset immediately and document

### How to Detect
Check for these contamination markers:
```bash
git log --oneline | grep "Merge refactor-caching"
git log --oneline | grep "flat cache"
git log --oneline | grep "Milestone"
grep -r "flat_cache.h" src/
```

If any of these appear in mac-dev-benchmark-enhancements history, the branch is contaminated and must be cleaned.

### Current Status (2026-03-30)
mac-dev-benchmark-enhancements was cleaned of refactor-caching contamination and force-pushed to remote with clean state (commit 8d1cb06).
