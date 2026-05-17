# Stage 1 Evaluation — Request for Critical Review

This document is a prompt for an Opus model to critically evaluate the Stage 1
multiplicity-encoding implementation. The agent who wrote this code (Sonnet) has been
asked to describe honestly what it believes it has done, without overclaiming, so that
Opus can evaluate whether the implementation is correct, complete, and sound.

---

## What Stage 1 Was Supposed to Do

Stage 1 of the multiplicity encoding plan was:

> Add `MultiplicityPolicy` as a new cache policy (alongside existing ones, not replacing
> anything). Use a from-scratch recomputation strategy: after every `make_move` and
> `undo_move`, call `recompute_all()` to rebuild all 52 card descriptors from the live
> board state and recompute the Zobrist hash and 64-byte payload. Expose this as an
> opt-in via `--cache-type multiplicity`. Validate that solvability results match the
> existing `auto` cache type. Do not implement suit-symmetry yet (that is Stage 2).

---

## What I Believe Was Done

### New files created

- **`src/main/game/multiplicity_descriptor.h`** — defines the descriptor byte encoding.
  Each of the 52 cards gets one byte. Two variants:
  - *Locative*: card is in a named location (cell, permanent, stock, waste, reserve,
    hole-top, space). Encoded as `52 + kind` where kind is `MLD_IN_CELL=0` through
    `MLD_IN_SPACE=6`, giving bytes 52–58.
  - *Predecessor*: card has another card above it in a tableau pile (or equivalent).
    Face-up predecessor at canonical position `p` → byte `p` (0–51). Face-down
    predecessor → byte `255 - p` (204–255).

- **`src/main/game/multiplicity_descriptor_store.h`** — 64-byte payload struct.
  Byte 0 is an occupied/depth header (packed); bytes 1–2 are depth; bytes 3–54 hold the
  52 descriptor bytes. Equality for cache matching uses `memcmp` on bytes 3–54 only
  (ignoring the depth/occupied header). Has `get_depth()`, `set_occupied()`,
  `is_occupied()`, `matches()`. Also has stub methods added to satisfy the flat-engine
  interface that is compiled in for all `computes_hash = true` policies: `get_hole_top`,
  `set_hole_top`, `get_descriptor`, `set_descriptor`, `get_foundation`, `set_foundation`,
  `get_waste_ptr`, `set_waste_ptr`. These stubs return 0 / do nothing; the actual state
  is held in the descriptor bytes computed by `recompute_all`.

- **`src/main/game/multiplicity_zobrist.h` and `.cpp`** — declares and defines
  `MZobrist::Z[52][80]`, a table of 64-bit constants. Columns 0–51 are for predecessor
  descriptors (card at canonical position 0–51 is the predecessor); columns 52–79 are for
  locative descriptors (kinds 0–27, though only kinds 0–6 are currently used). The table
  is seeded from `0xDEADBEEF12345678` using a simple LCG. I believe the table values are
  deterministic and non-zero, but I have not independently verified the generator output.

- **`src/main/game/multiplicity_descriptor_engine.h`** — the engine class
  `multiplicity_descriptor_engine`. Has:
  - `init(ctx)` — called once at construction; calls `recompute_all(ctx)`
  - `recompute_all(ctx)` — walks the live board (foundations, tableau, cells, stock,
    waste, reserve, hole) and assigns a descriptor byte to each card. Then XORs together
    `Z[card_index][descriptor_byte]` for all 52 cards to produce the Zobrist hash, and
    writes the 52 descriptor bytes into a `multiplicity_descriptor_store`.
  - `get_zobrist_hash()`, `get_store()` — accessors
  - No-op stubs for all flat-engine incremental methods (`update_card`,
    `update_foundation`, `update_hole_top`, `update_waste_ptr`,
    `was_initially_face_up`, `determine_destination_descriptor`). These stubs exist
    because `game_state.cpp` has many `if constexpr (Policy::computes_hash)` blocks that
    call those methods on the engine, and `MultiplicityPolicy::computes_hash = true`.
    The actual hash update happens through `recompute_all`, not through those paths.

### Modified files

- **`src/main/game/cache_policy.h`** — added `MultiplicityPolicy` struct with
  `computes_hash = true`, `computes_payload = true`, `computes_multiplicity_descriptor = true`,
  `descriptor_engine = multiplicity_descriptor_engine`,
  `descriptor_store_type = multiplicity_descriptor_store`,
  `cluster_policy = MultiplicityClusterPolicy`.

- **`src/main/game/generic_flat_cache_policies.h`** — added `MultiplicityClusterPolicy`
  with a 128-byte cluster (two `multiplicity_descriptor_store` entries). The `payload_of`
  method dispatches on `std::is_same<GS::descriptor_store_type, multiplicity_descriptor_store>`
  to handle the fact that the virtual `insert/contains` overrides in `generic_flat_cache`
  are instantiated with the default `game_state_impl<FlatPolicy>`, not
  `game_state_impl<MultiplicityPolicy>`. When called with the wrong game state type a
  static dummy store is returned.

- **`src/main/game/search-state/game_state.h`** — added
  `typedef typename Policy::descriptor_store_type descriptor_store_type;` and updated
  `get_payload()` return type to `const typename Policy::descriptor_store_type&`.

- **`src/main/game/search-state/game_state.cpp`** — after the move switch in both
  `make_move()` and `undo_move()`, added:
  ```cpp
  if constexpr (Policy::computes_multiplicity_descriptor) {
      desc_engine.recompute_all(make_desc_ctx());
  }
  ```
  Also updated `make_desc_ctx()` to always pass `stock`, `waste`, and `original_reserve`
  so the multiplicity engine can walk those piles.

- All explicit instantiation blocks in `game_state.legal_moves.cpp`,
  `game_state.dominance_moves.cpp`, `game_state.pile_order.cpp`, `solver.cpp`,
  `deal_parser.cpp`, `state_printer.cpp` — added `MultiplicityPolicy`.

- **`src/main/main.cpp`**, **`benchmark.cpp`**, **`solvability_calc.cpp`** — added
  `multiplicity` dispatch.

- **`src/main/input-output/input/command_line_helper.cpp`** — added `multiplicity` as
  valid `--cache-type` value.

---

## What Was Tested

1. All three build gates pass:
   - Release build: unit tests + Level 1 regression
   - Trace build: unit tests + trace tests
   - Debug build: unit tests

2. An 8-seed Klondike spot-check (seeds 1–8) comparing `--cache-type auto` vs
   `--cache-type multiplicity` showed matching solvability results (no mismatches).

---

## What Was Not Tested or May Be Wrong

I want to be explicit about the limits of my confidence:

1. **`states_searched` counts were not compared** between `auto` and `multiplicity`.
   Only solvability was checked. If the multiplicity cache is producing false-positive
   matches (treating two different states as equal), that would reduce `states_searched`
   without affecting solvability — but it would be a correctness bug for Stage 2 when
   suit-symmetry is added.

2. **The descriptor encoding for stock/waste/reserve cards** — `recompute_all` walks
   these piles and assigns locative descriptors. I believe this is correct but the logic
   is new and has not been independently reviewed.

3. **The Zobrist table** — I believe the LCG-generated values are correct and
   well-distributed, but I have not verified this independently or checked for
   collisions in practice.

4. **The `payload_of` dummy-store dispatch** — when `generic_flat_cache`'s virtual
   overrides call `MultiplicityClusterPolicy::payload_of` with a `FlatPolicy` game state,
   a static dummy store is returned. I believe this path is never reached in practice
   during a multiplicity-policy solve, but the reasoning relies on the virtual dispatch
   chain never being invoked for the multiplicity cache. This should be scrutinised.

5. **No game types other than Klondike were tested.** Games with stock, waste, reserve,
   hole, or cells may have untested descriptor assignment paths.

6. **The no-op stub approach** — rather than auditing all 15+ `if constexpr
   (Policy::computes_hash)` blocks and adding `&& !Policy::computes_multiplicity_descriptor`
   guards, I added no-op stubs to the engine. This avoids touching many call sites but
   means the incremental flat-engine code path compiles and executes for
   `MultiplicityPolicy`, doing wasted work before `recompute_all` overwrites the result.
   It also means any future bug in the incremental path (e.g. updating the hash
   incorrectly before `recompute_all` resets it) would be silent. Whether this is
   acceptable for Stage 1 (where performance does not matter) or whether the guards
   should be added properly is a design question.

---

## Key Files for Review

```
src/main/game/multiplicity_descriptor.h
src/main/game/multiplicity_descriptor_store.h
src/main/game/multiplicity_zobrist.h
src/main/game/multiplicity_zobrist.cpp
src/main/game/multiplicity_descriptor_engine.h
src/main/game/cache_policy.h                        (MultiplicityPolicy struct)
src/main/game/generic_flat_cache_policies.h         (MultiplicityClusterPolicy)
src/main/game/search-state/game_state.h             (descriptor_store_type typedef)
src/main/game/search-state/game_state.cpp           (recompute_all call sites, make_desc_ctx)
src/main/main.cpp                                   (multiplicity dispatch)
```

---

## Questions for the Reviewer

1. Is the descriptor byte encoding in `multiplicity_descriptor.h` correct and
   unambiguous? In particular, are the locative/predecessor ranges non-overlapping and
   do they cover all cases?

2. Does `recompute_all` correctly walk all pile types and assign the right descriptor
   to each card? Are there any pile types or edge cases it might miss?

3. Is the `payload_of` dummy-store dispatch in `MultiplicityClusterPolicy` sound? Could
   it be reached in practice and cause a silent cache corruption?

4. Is the no-op stub approach acceptable for Stage 1, or does it introduce risks that
   should be addressed before Stage 2?

5. Are there any other correctness concerns that should be resolved before proceeding
   to Stage 2?
