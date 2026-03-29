# Known Issues

This file tracks open issues in the `refactor-caching` branch. Resolved issues that
are interesting as development history are documented in `docs/resolved-bugs/`.

---

## Open Issues

### 1. JSON Deal Round-Trip Changes Node Counts (`json_helper.cpp`)

**Affected file:** `src/main/input-output/input/json-parsing/json_helper.cpp`
**Status:** Open in `refactor-caching`; fixed in `claude/quizzical-darwin`
**Impact:** Different `states_searched` counts when running from exported JSON vs seed

`json_helper::print_game_state_as_json` serialises tableau piles by iterating
`gs.tableau_piles` (the runtime-reordered list) rather than `gs.original_tableau_piles`
(the fixed construction order). When pile symmetry has reordered the piles, the
serialised JSON records them in a different order than the parser expects, producing
a logically identical but internally different game state.

**Workaround:** The Level 2–5 regression runner invokes the solver with `--random <seed>`
directly, bypassing JSON serialisation. Level 1 is unaffected in practice.

**Fix (one line, in `claude/quizzical-darwin`):**
```cpp
// Change in json_helper::print_game_state_as_json:
for (auto pr : gs.original_tableau_piles)  // was: gs.tableau_piles
```

### 2. Spanish Patience Traversal Regression

**Affected game type:** `spanish-patience` (13 tableau piles, any-suit build)
**Status:** Open; accepted for first delivery
**Impact:** Some solvable instances explore many more nodes without pile ordering

With pile ordering removed (M6), the DFS traversal order for Spanish Patience degrades
significantly for some seeds. The pile ordering previously served a dual purpose:
deduplication (now handled by the descriptor hash) and implicit move ordering (now lost).
For games with many tableau piles, the move ordering effect can be large.

The regression suite treats these as soft passes (TIMEOUT is acceptable). Correctness
is not affected — solvable games are still solved given sufficient time; unsolvable games
are still proven unsolvable.

**Possible future fix:** A lightweight move-ordering heuristic that does not require
full pile sorting. Deferred post-merge.

---

## Resolved Issues (for reference)

The following issues were open during development and are now fixed. Full details
are in `docs/resolved-bugs/`.

| Bug | Fix commit | Details |
|---|---|---|
| STARTING(0) not position-canonical (false negatives) | `52b8b63` | `bug_starting_descriptor_not_position_canonical.md` |
| ROOT descriptor overloaded (false positives) | `52b8b63` | `bug_root_descriptor_false_positives.md` |
| Waste pointer stale on regular moves (FortunesFavor) | `52b8b63` | `bug_waste_ptr_and_canfield_wrapping.md` |
| Canfield wrapping builds not recognised (CanfieldStrict) | `52b8b63` | `bug_waste_ptr_and_canfield_wrapping.md` |
| `sol_rules` uninitialized bools (UBSan) | `cb9d26d` | `implementation_plan_v4.md` §M5 |
| `recompute_payload_from_scratch()` four bugs | `3d5f66d` | `implementation_plan_v4.md` §M5 |
| `--force-lru` pile ordering not restored in M6 Phase 1 | `a7f3744` | `implementation_plan_v4.md` §M6 |
