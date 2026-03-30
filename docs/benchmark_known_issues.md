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
