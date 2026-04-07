# Branch Workflow: refactor-caching

**Last revised:** 2026-04-03

---

## Overview

`refactor-caching` is a long-lived **design and planning branch**. All production
code lives in `dev`. This branch holds the flat cache extension roadmap, optimization
analysis, and design notes that inform the next phase of implementation.

```
master ←── dev ←── refactor-caching (design docs, ongoing)
                ↑
                └── implement-<feature>  (short-lived, cut from dev)
```

---

## Starting a New Implementation

When ready to implement a roadmap item:

```bash
# 1. Cut a short-lived branch from dev (not from refactor-caching)
git checkout dev
git checkout -b implement-tableau-dealing   # or implement-mmap-cache, etc.

# 2. Do the work, validate with regression suite
./build.sh --release --unit-tests
cd cmake-build-release && ctest -R regression_level1 --output-on-failure

# 3. Benchmark before/after if performance-sensitive
python3 scripts/run_benchmark.py \
    --solver cmake-build-release/bin/solvitaire \
    --type klondike --seeds 1-150 --timeout 60000 \
    --output results/after.csv

# 4. Merge back to dev when complete
git checkout dev
git merge implement-tableau-dealing --no-ff
git push origin dev

# 5. Delete the feature branch
git branch -d implement-tableau-dealing
git push origin --delete implement-tableau-dealing
```

---

## Keeping `refactor-caching` Current with `dev`

After merging implementation work into `dev`, pull those changes back:

```bash
git checkout refactor-caching
git merge dev
git push origin refactor-caching
```

This keeps the design branch from drifting and ensures planning docs reflect
the actual current state of the codebase.

---

## Getting Design Docs into `dev`

The planning docs in this branch (roadmap, optimization analysis) are useful
context for anyone working on the codebase. When a doc is mature, merge it:

```bash
git checkout dev
git merge refactor-caching --no-ff -m "docs: merge cache redesign planning docs from refactor-caching"
git push origin dev
```

---

## Taking Changes to a Divergent Branch

For branches that don't track `dev`, use cherry-pick rather than merging:

```bash
# See what refactor-caching has that the target branch doesn't
git log --oneline other-branch..refactor-caching

# Apply specific commits
git checkout other-branch
git cherry-pick <hash>
```

Only cherry-pick documentation commits — they have no code dependencies. If
the target branch has a very different codebase, a targeted patch may be cleaner:

```bash
git diff other-branch refactor-caching -- docs/cache-redesign/ > cache-docs.patch
git checkout other-branch
git apply --3way cache-docs.patch
```

---

## Naming Convention for Feature Branches

| Roadmap item | Suggested branch name |
|---|---|
| mmap lazy allocation | `implement-mmap-cache` |
| Power-of-2 bucket count | `implement-pow2-cache` |
| Tableau dealing games | `implement-tableau-dealing` |
| Suit-canonical hashing | `implement-suit-canonical` |
| Suit-irrelevant games | `implement-suit-irrelevant` |
| Gaps | `implement-gaps-cache` |
| Accordion | `implement-accordion-cache` |

---

## Summary

| Situation | Action |
|---|---|
| Start new implementation | Cut branch from `dev`, not from `refactor-caching` |
| Implementation complete | Merge feature branch → `dev`; delete feature branch |
| `dev` has new work | Merge `dev` → `refactor-caching` to stay current |
| Planning docs are mature | Merge `refactor-caching` → `dev` with `--no-ff` |
| Port docs to divergent branch | `git cherry-pick` or targeted diff patch |
