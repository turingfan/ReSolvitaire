# Commit 2 API Audit: Descriptor Store Interface Unification

**Date:** 2026-04-28  
**Branch:** `feature/templated-dispatch`  
**Scope:** `compact_state` vs `hash_descriptor_store` — are the APIs already unified?

---

## Purpose

Commit 2 of the templated-dispatch plan requires `compact_state` and
`hash_descriptor_store` to present a uniform descriptor-store interface so
that `game_state_impl<Policy>` can call `desc_store.X()` on
`Policy::descriptor_store_type` without any `#ifdef` guards.

This document records the audit findings.

---

## Files Examined

| File | Role |
|---|---|
| `src/main/game/compact_state.h` / `.cpp` | FlatPolicy / PredecessorPolicy descriptor store + cache key |
| `src/main/game/hash_descriptor_store.h` | HashOnlyPolicy old-value store |
| `src/main/game/search-state/game_state.h` | game_state member declarations |
| `src/main/game/search-state/game_state.cpp` | All descriptor-store call sites |
| `src/main/game/cache_policy.h` | Policy trait structs (from Commit 1) |
| `src/main/game/descriptor.h` | `card_descriptor` enum (shared) |

---

## compact_state API (full)

Declared in `compact_state.h`; defined in `compact_state.cpp` behind
`#if !defined(SOLVITAIRE_LRU_ONLY)`.

### Descriptor-store methods (needed by game_state_impl template)

| Method | Signature |
|---|---|
| `clear` | `void clear()` |
| `get_descriptor` | `uint8_t get_descriptor(uint8_t card_id) const` |
| `set_descriptor` | `void set_descriptor(uint8_t card_id, uint8_t value)` |
| `get_foundation` | `uint8_t get_foundation(uint8_t suit) const` |
| `set_foundation` | `void set_foundation(uint8_t suit, uint8_t rank)` |
| `get_waste_ptr` | `uint8_t get_waste_ptr() const` |
| `set_waste_ptr` | `void set_waste_ptr(uint8_t ptr)` |
| `get_hole_top` | `uint8_t get_hole_top() const` |
| `set_hole_top` | `void set_hole_top(uint8_t card_id)` |

### Cache-cluster-only methods (NOT part of descriptor-store interface)

| Method | Why not needed on hash_descriptor_store |
|---|---|
| `set_occupied(bool)` / `is_occupied()` | Cache cluster occupancy; never called on the `payload` / `hash_desc` game_state member |
| `set_depth(uint16_t)` / `get_depth()` | Cache cluster depth; only called via `set_payload_depth()`, which is `#ifndef SOLVITAIRE_HASH_ONLY` guarded and will become `if constexpr (Policy::computes_payload)` |
| `matches(const compact_state&)` | Cache cluster comparison; only used in `assert_payload_consistent()`, which is `computes_payload`-only |
| Descriptor constant aliases (`STARTING`, `ROOT`, etc.) | Backward-compat aliases for old call sites; `game_state.cpp` uses `card_descriptor::STARTING` from `descriptor.h` directly, not `compact_state::STARTING` |

---

## hash_descriptor_store API (full)

All methods are defined inline in `hash_descriptor_store.h`, with no
preprocessor guards (present in all build configurations).

| Method | Signature |
|---|---|
| `clear` | `void clear()` |
| `get_descriptor` | `uint8_t get_descriptor(uint8_t card_id) const` |
| `set_descriptor` | `void set_descriptor(uint8_t card_id, uint8_t value)` |
| `get_foundation` | `uint8_t get_foundation(uint8_t suit) const` |
| `set_foundation` | `void set_foundation(uint8_t suit, uint8_t rank)` |
| `get_waste_ptr` | `uint8_t get_waste_ptr() const` |
| `set_waste_ptr` | `void set_waste_ptr(uint8_t ptr)` |
| `get_hole_top` | `uint8_t get_hole_top() const` |
| `set_hole_top` | `void set_hole_top(uint8_t cid)` |

---

## Call-site audit (game_state.cpp)

Every `#ifdef SOLVITAIRE_HASH_ONLY` block in `game_state.cpp` that switches
between `hash_desc.X()` and `payload.X()` calls the **same method name** with
the **same argument types** on both sides. The full list of methods called on
the descriptor-store members:

| Method | Called on `hash_desc`? | Called on `payload`? |
|---|---|---|
| `clear()` | ✓ (line 1155) | ✓ (line 1157) |
| `get_descriptor(cid)` | ✓ (lines 1142, 1266) | ✓ (lines 1094, 1144, 1269, 1411) |
| `set_descriptor(cid, val)` | ✓ (line 1267) | ✓ (line 1270) |
| `get_foundation(suit)` | ✓ (line 1284) | ✓ (lines 1287, 1417) |
| `set_foundation(suit, rank)` | ✓ (lines 1175, 1285) | ✓ (lines 1177, 1288) |
| `get_waste_ptr()` | ✓ (line 1311) | ✓ (lines 1314, 1428) |
| `set_waste_ptr(ptr)` | ✓ (lines 1201, 1312) | ✓ (lines 1203, 1315) |
| `get_hole_top()` | ✓ (lines 489, 1329) | ✓ (lines 491, 1332, 1423) |
| `set_hole_top(cid)` | ✓ (lines 1188, 1330) | ✓ (lines 1190, 1333) |
| `set_depth(depth)` | **never** | ✓ (line 1403) — `#ifndef SOLVITAIRE_HASH_ONLY` |

The single asymmetry (`set_depth`) is already correctly guarded and will
remain guarded by `if constexpr (Policy::computes_payload)` in the template,
so `hash_descriptor_store` never needs to expose it.

---

## Finding

**The descriptor-store API is already fully unified.**

All five core method groups (`clear`, descriptor, foundation, waste_ptr,
hole_top) have identical signatures on both types. The `#ifdef
SOLVITAIRE_HASH_ONLY` blocks in `game_state.cpp` select a *member name*
(`hash_desc` vs `payload`) — not a *method name* — so they collapse cleanly
to a single `desc_store.X()` call when the two members become one
`Policy::descriptor_store_type` field.

No methods need to be added to either type for Commit 2.

---

## Why the APIs are already aligned

`hash_descriptor_store` was created in the `fix/variant-build-hash-only`
branch specifically to replace `compact_state` as the on-stack old-value store
for the hash-only path. It was designed from the outset to mirror the
descriptor-store interface of `compact_state`, excluding the cache-cluster
members (`set_occupied`, `set_depth`, `matches`) that `compact_state` carries
for a different purpose.

---

## Implication for the templated-dispatch plan

Commit 2 as specified (add missing methods, pure API addition) has no code
changes to make. The branch can proceed directly to Commit 3.

The verification checklist for this commit is:

- [x] Both types have identical signatures for all five descriptor-store method groups
- [x] No method called on `hash_desc` is absent from `hash_descriptor_store`
- [x] No method called on `payload` (as descriptor store, not cache cluster) is absent from `compact_state`
- [x] The single asymmetry (`set_depth`) is correctly gated and needs no counterpart on `hash_descriptor_store`
- [x] `card_descriptor` enum is shared via `descriptor.h`; neither type needs to expose the constant aliases for template code to compile
