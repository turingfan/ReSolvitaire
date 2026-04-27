# Pickup Document: Variant Build Fix & Hash-Only Descriptor Store

**Branch:** `fix/variant-build-hash-only` (from `dev`)
**Last updated:** 2026-04-27 (planning session)

> **Session rule:** At the end of any session in which a commit is completed, redraft
> this file and update the Session Log in `implementation_plan.md`.

---

## Current Status

**Commit 1 done** (0653486). Next: Commit 2 — extract `card_descriptor` enum to `descriptor.h`.

Plan document: `docs/fix-variant-build-hash-only/implementation_plan.md`

---

## What This Branch Does

Fixes two bugs identified in the 2026-04-27 diagnostic session:

1. **Build bug (known-issues #11):** `build.sh` never builds the three cache-variant
   binaries (`solvitaire-flat`, `solvitaire-hash-only`, `solvitaire-lru`). These are needed
   to run the variant regression tests (`regression_level*_flat` etc.). Fix: add `--variants`
   flag to `build.sh`, `scripts/container-build.sh`, and `Dockerfile`.

2. **Hash-only payload waste (known-issues #14):** The `hash_only_cache` binary carries a
   32-byte `compact_state payload` in every `game_state` on the DFS stack and maintains it
   incrementally on every move, even though the hash-only cache never uses it as a cache key.
   The coupling is real: the four incremental update helpers (`update_card_descriptor` etc.)
   read the old value from `compact_state` before XOR-ing. Fix: introduce a new
   `hash_descriptor_store` (plain byte arrays, no nibble packing) as the old-value store for
   the hash-only path, and exclude `compact_state.h` entirely from `SOLVITAIRE_HASH_ONLY`
   compilation.

---

## Commit Plan (5 commits)

| # | Commit message | Status |
|---|----------------|--------|
| 1 | `build: add --variants flag; build variant binaries in container` | **Done** (0653486) |
| 2 | `descriptor: extract card_descriptor enum to shared header` | Not started |
| 3 | `hash_descriptor_store: new lightweight descriptor store for hash-only path` | Not started |
| 4 | `game_state: use hash_descriptor_store in SOLVITAIRE_HASH_ONLY; exclude compact_state` | Not started |
| 5 | `docs: close known-issues #11 and #14; add #17` | Not started |

---

## Key Files

| File | Role |
|------|------|
| `build.sh` | Add `--variants` flag (Commit 1) |
| `scripts/container-build.sh` | Forward `--variants` (Commit 1) |
| `Dockerfile` | Add second variant build step (Commit 1) |
| `src/main/game/descriptor.h` | New — standalone `card_descriptor` enum (Commit 2) |
| `src/main/game/compact_state.h` | Add backward-compat aliases for `card_descriptor` values (Commit 2) |
| `src/main/game/search-state/game_state.cpp` | Replace ~30 `compact_state::STARTING` etc. with `card_descriptor::STARTING` (Commit 2) |
| `src/main/game/hash_descriptor_store.h` | New — lightweight descriptor store (Commit 3) |
| `src/main/game/search-state/game_state.h` | Conditionalize includes and `payload`/`hash_desc` member (Commit 4) |
| `src/main/game/search-state/game_state.cpp` | Dispatch update helpers to `hash_desc` vs `payload` (Commit 4) |
| `docs/known-issues.md` | Close #11, #14; add #17 (Commit 5) |

---

## What a New Session Should Do

1. Read `docs/fix-variant-build-hash-only/implementation_plan.md` for full detail on the
   next uncommitted commit.
2. Check the commit table above to see which commits are done.
3. Read the relevant source files before touching them (do not rely on plan alone).
4. Implement exactly one commit per session unless Ian explicitly says to continue.
5. Run the per-commit test protocol specified in the plan before marking complete.
6. **Update this PICKUP.md and the Session Log in `implementation_plan.md` before ending
   the session.**

---

## Context: Why Not Option A (pile-position Zobrist for hash-only)?

The descriptor-based Zobrist is the whole point of this development track. Switching to a
pile-position hash for hash-only would give a different (weaker, more collision-prone) hash
function for no benefit beyond removing the descriptor maintenance. `hash_descriptor_store`
preserves the same hash semantics with less overhead by decoupling the old-value tracking
from `compact_state`'s nibble-packed cache-key format.

## Context: What Comes After This Branch

Once merged to `dev`, the next phase is proper templating of `game_state` on a cache policy
struct (eliminating the remaining boolean runtime dispatch and the `computing_flat_hash` /
`computing_flat_payload` flags). That work goes on a fresh branch from the clean `dev`.
See `known-issues.md` #8 and `docs/proposals/PROPOSAL-templated-game-state-dispatch.md`.

---

## Session Log

| Date | What was done |
|------|--------------|
| 2026-04-27 | Planning session: plan document written, PICKUP written, no code changes |
| 2026-04-27 | Commit 1 (0653486): build.sh loop-based parser + --variants flag; container-build.sh --variants; Dockerfile already correct |
