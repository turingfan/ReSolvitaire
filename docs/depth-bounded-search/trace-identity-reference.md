# Trace Identity Reference — how to build, recreate, and use it

**Owner:** depth-bounded-search branch (`claude/depth-bounded-search`)
**Created:** 2026-06-07 · **Status:** active, session-local reference in use

This document is the durable record of the **trace identity reference binary** used
as the `L=∞` identity gate for the depth-bounded-search work. It is written so any
future session (human or agent) can understand it, recreate it from scratch, and run
the gate — without relying on memory or chat history.

---

## 1. What this is and why it exists

Depth-bounding adds a depth cut to the DFS. The **non-negotiable correctness
requirement** is that, **with the bound disabled (no `--initial-depth-bound` flag),
the solver must behave byte-for-byte identically to the pre-feature code** — same
search order, same nodes, same verdicts, same counts. If that holds, the bound is a
pure opt-in and cannot have regressed the existing engine.

We prove that with the repo's **search-trace** infrastructure. A trace-instrumented
binary emits one event per search step (push/pop/dominance/cache/etc.). The
`trace_regression_level1` CTest target runs two binaries over all 150 Level-1
instances and asserts their event streams are **identical, event-for-event**:

```
compare_traces.py --regression --level 1 \
    --binary-a <REFERENCE>            # known-good, pre-change
    --binary-b <CANDIDATE>            # current build
    --tests-dir tests
```

So we need a **reference binary built from the pre-change source**. After each Stage 1
change, we rebuild the (bound-disabled) candidate and re-run the gate: **150/150 must
hold**, or the change altered existing behavior and is rejected.

This per-event comparison is strictly stronger than comparing result structs
(verdict / states_searched / backtracks): it catches any reordering or off-by-one in
the search itself, not just the summary numbers.

## 2. The arch problem that forced us to build our own

The committed reference binaries live in the **external `05-Executables` dataset**
(see the CMake default below), which is **not cloned in web-execution containers**.
The only committed Linux reference is **`solvitaire-trace-reference-linux-arm64-…`** —
built for **ARM64**. This branch's web container is **x86_64**, so that binary cannot
run here. (Confirmed with Ian, 2026-06-07: "the existing reference is for linux but on
ARM64 so no good to you… ok to build your own copy for reference from the current
state before making the changes.")

CMake's reference-path default (`CMakeLists.txt`, the `SOLVITAIRE_SEARCH_TRACE` block):

```cmake
if(APPLE)
    set(_default_ref_bin ".../05-Executables/reference/solvitaire-trace-reference-mac-arm64-20260529-9673fd3")
else()  # Linux — hardcoded ARM64; wrong on any amd64 Linux box
    set(_default_ref_bin ".../05-Executables/reference/solvitaire-trace-reference-linux-arm64-20260529-ff68bde")
endif()
set(TRACE_REF_BIN "${_default_ref_bin}" CACHE PATH "...")   # override with -DTRACE_REF_BIN=...
```

`../../05-Executables/` resolves *outside* the repo (sibling of the working dir), so
it is absent in web containers regardless of arch.

## 3. The reference we built (this session)

- **Source:** branch HEAD **`45ccd43`** — the last commit before *any* solver change
  (Stage 0 was docs/measurement only). Verified pristine before building:
  `git diff --stat HEAD -- src/ CMakeLists.txt` was empty.
- **Build:** `./build.sh --trace` (Release + `SOLVITAIRE_SEARCH_TRACE=ON`), producing
  `cmake-build-trace/bin/solvitaire-trace` (780,640 bytes).
- **Snapshot location (session-local, NOT committed):**
  `/home/user/reference-bin/solvitaire-trace-ref-45ccd43`
- **SHA-1:** `894bcbb67b770d7d9302d40db38427bbd5ceaa96`

### Why session-local and not committed

Decision by Ian (2026-06-07): **"Keep it local yes but document what you've done and
how to recreate it."** Rationale:

- `AGENTS.md` / `01-Knowledge-Base/` (the authoritative "where things go" rules) are
  **not present in this checkout**, so the binary-placement rule can't be confirmed.
- The visible convention says binaries live **outside** the solver repo: the CMake
  default points at the external `05-Executables/` dataset, and `.gitignore` excludes
  all `cmake-build-*/`.
- The reference is **fully reproducible from a pinned commit** (§4), so a committed
  binary buys only convenience, not capability.

Trade-off accepted: each new container must rebuild the reference once (a few
minutes). The reproducer below makes that a copy-paste step.

## 4. How to recreate the reference (copy-paste)

> The reference must always be built from the **pre-feature baseline commit
> `45ccd43`** — the identity claim is "bound-disabled depth-bounded-search ==
> pre-depth-bounding code." Do **not** regenerate it from a later Stage 1/2 commit;
> that would defeat the gate.

```bash
cd /home/user/ReSolvitaire

# 0. (web container, ephemeral) ensure Boost dev headers
sudo apt-get install -y libboost-program-options-dev   # only if find_package(Boost) fails

# 1. build the trace config from the pinned pre-feature baseline.
#    If your working tree is already at/after 45ccd43 with NO solver changes, you can
#    skip the checkout. Otherwise build from a clean checkout of 45ccd43 (e.g. a
#    worktree) so the reference is truly pre-change:
git worktree add /tmp/resolv-ref 45ccd43
cd /tmp/resolv-ref && ./build.sh --trace

# 2. snapshot the reference binary to the stable session-local path
mkdir -p /home/user/reference-bin
cp /tmp/resolv-ref/cmake-build-trace/bin/solvitaire-trace \
   /home/user/reference-bin/solvitaire-trace-ref-45ccd43

# 3. verify it matches the recorded fingerprint (traces, not bytes, are what matter —
#    see note below — but a matching SHA-1 confirms an identical rebuild)
sha1sum /home/user/reference-bin/solvitaire-trace-ref-45ccd43
#   expect: 894bcbb67b770d7d9302d40db38427bbd5ceaa96
git worktree remove /tmp/resolv-ref     # optional cleanup
```

**Binary bytes vs trace output:** the gate compares **trace output**, which is a
deterministic function of the *source* (search order, hashing, move generation — no
threads, no RNG beyond the seeded deal). A rebuild of `45ccd43` reproduces matching
traces even if the binary bytes differ across toolchains. The SHA-1 above is for the
exact build on this container's toolchain (gcc 13 / Boost 1.83); treat a SHA-1
mismatch as "different toolchain," not "wrong reference" — the authoritative check is
the gate in §5 passing 150/150 when candidate is also built from `45ccd43`.

## 5. How to run the identity gate

```bash
cd /home/user/ReSolvitaire
REF=/home/user/reference-bin/solvitaire-trace-ref-45ccd43

# build the current (candidate) trace binary and point the gate at the reference
./build.sh --trace
cmake -DTRACE_REF_BIN="$REF" cmake-build-trace     # re-runs cmake configure only

cd cmake-build-trace
ctest -R '^trace_regression_level1$' --output-on-failure          # MUST be 150/150
ctest -R 'trace_identity_flat|trace_identity_lru|trace_until_timeout'   # determinism

# or run the comparison directly to see per-instance detail:
cd /home/user/ReSolvitaire
python3 scripts/compare_traces.py --regression --level 1 \
    --binary-a "$REF" \
    --binary-b cmake-build-trace/bin/solvitaire-trace \
    --tests-dir tests
```

**Interpreting it:** with the depth-bound flags **absent**, `trace_regression_level1`
must report **150/150, 0 failed**. Any failure means the candidate diverged from the
pre-feature baseline → the change is not behavior-preserving and must be fixed before
proceeding. (A divergence is expected and fine *only* when the bound is actually
engaged; the gate is always run bound-disabled.)

## 6. Validation evidence (this session, candidate == reference)

Run on the pristine build (candidate identical to reference) to prove the harness
works on x86_64:

| Check | Result |
|---|---|
| `trace_identity_flat` / `trace_identity_lru` / `trace_until_timeout` | 3/3 PASS (≤2 s) |
| `SearchTraceTest.*` + `SearchTraceAgreementTest.*` (unit) | 5/5 PASS (incl. 107 s HashOnlyVsFlat 50-seed) |
| `trace_regression_level1` (CTest) | PASS, 49.6 s |
| `compare_traces.py --regression --level 1` (direct) | **150/150 passed, 0 failed**, 41.3 s |

Sample matched event counts (direct run): `free-cell_seed_334270` 142,803 events;
`golf_seed_1000041` 212,591 events; `trigon_seed_8` 261 events.

## 7. How the gate is used across Stage 1+

- After **every** depth-bounded-search change (each PR), rebuild the trace candidate
  and run §5. **150/150 with flags absent is a merge gate.**
- The reference stays pinned to `45ccd43` for the **whole feature branch**: the
  invariant "bound disabled ⇒ pre-feature behavior" must hold from PR1 through merge.
- The finite-`L` *verdict* check (Stage 1 item 1f, PR2) is separate: it engages the
  bound and confirms verdicts still match the unbounded oracle (counts legitimately
  differ under bounding). That uses `regression_runner.py`, not this trace gate.

## 8. Open item for Ian

Reference is **session-local** per Ian's instruction. If future-session rebuild cost
becomes annoying, the alternative is committing an amd64 reference in-repo
(e.g. `tests/reference/solvitaire-trace-reference-linux-amd64-20260607-45ccd43`) and
teaching the CMake default to pick it by arch (`CMAKE_SYSTEM_PROCESSOR`). Not done now
(conservative: external-dataset convention + unverifiable `AGENTS.md` rule).
