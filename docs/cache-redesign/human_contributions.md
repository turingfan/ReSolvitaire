# Human Contributions Log

Observations, design decisions, and corrections made by the human researcher (Ian Gent) during the cache redesign project. These represent intellectual contributions beyond routine code review.

---

## 1. TwoBig1 Replacement Policy Correction (2026-03-22)

**Context:** Gemini's initial Milestone 3 implementation had an incorrect replacement policy — it overwrote slot 0 without cascading to slot 1, and didn't check if slot 1 was empty before doing depth comparison.

**Contribution:** Identified the bug and specified the correct algorithm:
1. If slot 0 is empty → insert into slot 0 (never use slot 1 unless slot 0 is occupied)
2. If slot 1 is empty → insert into slot 1
3. If both full and new depth ≤ slot 0 depth → cascade slot 0 → slot 1, insert new into slot 0
4. If both full and new depth > slot 0 depth → overwrite slot 1

Also corrected Claude's initial fix proposal which would have inserted into "either empty slot" rather than always preferring slot 0.

## 2. Metamorphic Cache Testing Insight (2026-03-23)

**Context:** Two independent cache implementations (lru_cache and flat_cache) coexist for the same game types during the refactoring.

**Contribution:** Recognised that this creates an opportunity for metamorphic testing: since the DFS solver is deterministic, both caches see the identical stream of operations. Up to the first eviction, every insert/contains call must return the same boolean result. With a sufficiently large cache (no evictions), this gives complete cross-validation of two independent state encodings, hash functions, and equality checks. Documented in `metamorphic_cache_testing.md`.

## 3. Spider-Type Dealing Exclusion (2026-03-23)

**Context:** The `use_new_cache()` function excluded two-deck, sequence, and accordion games, but not spider-type stock dealing.

**Contribution:** Identified that spider-type stock dealing (`stock_deal_type::TABLEAU_PILES`) — which distributes cards from the stock across all tableau piles simultaneously — breaks the per-card descriptor model's pile symmetry assumptions. Added `stock_deal_t != TABLEAU_PILES` to the exclusion criteria. This prevents incorrect cache behaviour for games like Spider variants that use this dealing mechanism even if they happen to be single-deck.

## 4. Milestone 5 Architectural Design (2026-03-23)

**Context:** Verification of the `flat_cache` implementation across subtle rule variations and state transitions.

**Contribution:** Designed the Milestone 5 verification framework, specifying the parallel `dual_cache` execution model for real-time metamorphic comparison and the `recompute_payload_from_scratch()` safety check. This architecture allowed for the systematic isolation of structural bugs from performance-related cache evictions.

## 5. Foundation Invariance Correction (2026-03-23)

**Context:** During debugging, the AI (Antigravity) proposed that foundation discrepancies might be "acceptable" based on misinterpreting suit symmetry.

**Contribution:** Firmly corrected the AI, asserting that foundation progress is an invariant that must match exactly across caches. This critical guardrail forced the investigation to look deeper into the Zobrist hash initialisation, ultimately leading to the discovery of the `STARTING` vs `ROOT` descriptor conflict in deal-originated states.

## 6. Waste-Symmetry Optimization Insight (2026-03-24)

**Context:** Identifying the source of "Proxy Misses" (LRU=HIT, Flat=MISS) in Klondike-type games.

**Contribution:** Identified that the legacy LRU cache implicitly optimized "infinite redeal" states by collapsing waste pointer positions when the waste pile size is a multiple of the deal size. Provided the theoretical basis for classifying these as acceptable "false negatives" for the current `flat_cache` baseline, as they represent a legacy shortcut rather than a correctness bug in the new system.

## 7. Regression Integrity and Determinism (2026-03-17 to 2026-03-20)

**Context:** Developing the Level 1-5 regression suite and ground truth.

**Contribution:** Identified the "Smart Streamliner" multi-run requirement (Run 1 for winnable, Run 2 for unsolvability) and the Hardware Variance rule for node count consistency. Also discovered the "JSON Round-Trip Bug" where pile-reordering on export caused internal arrangement mismatches on reload, leading to the shift from JSON-based to Seed-based regression testing.

## 8. Statistical Benchmarking Strategy (2026-03-21 to 2026-03-22)

**Context:** Implementing a hardware-agnostic benchmarking suite.

**Contribution:** Designed the "Standard Candle" approach for hardware normalization and implemented the shift from mean to median-based statistics to ensure resilience against OS-level timing noise.
