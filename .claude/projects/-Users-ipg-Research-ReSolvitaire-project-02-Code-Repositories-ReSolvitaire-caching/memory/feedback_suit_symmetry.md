---
name: suit_symmetry_divergence
description: Games with suit symmetry are expected to explore more nodes with flat_cache vs lru_cache — not a bug
type: feedback
---

Games with suit symmetry (e.g., black-hole, golf, spanish-patience) will naturally show higher states_searched with flat_cache than lru_cache. The old cached_game_state encoding benefits from suit symmetry reduction that the per-card descriptor model doesn't have. Count differences in these games are expected and not bugs.

**Why:** The flat_cache descriptor model is card-ID-sensitive — it doesn't collapse suit-swapped equivalent states. The lru_cache's pile-based encoding happens to benefit from suit interchangeability in some games.

**How to apply:** When evaluating regression failures, distinguish between (a) count-only differences in suit-symmetric games (expected) and (b) outcome flips or count differences in non-symmetric games (bugs). The 5 outcome flips (british-canister ×4, delta-star ×1) remain genuine concerns.
