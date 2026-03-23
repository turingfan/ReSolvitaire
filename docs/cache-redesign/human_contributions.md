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
