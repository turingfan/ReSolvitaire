# Benchmarking Branch Workflow

**Branch:** `benchmark-python`
**Last revised:** 2026-04-03

---

## Overview

`benchmark-python` is a long-lived feature branch for benchmarking development.
Stable work flows into `dev` via periodic no-ff merges; `dev` updates flow back
into `benchmark-python` to stay current.

```
master ←── dev ←── benchmark-python (ongoing)
```

---

## Getting Stable Work into `dev`

When a milestone is stable, merge into `dev` with `--no-ff` to preserve branch
topology in history:

```bash
git checkout dev
git merge benchmark-python --no-ff -m "feat: merge benchmarking work from benchmark-python"
git push origin dev
```

Tag the stable point on `benchmark-python` first so you have a named reference:

```bash
git tag benchmark-v1.1 benchmark-python
git push origin benchmark-v1.1
```

---

## Keeping `benchmark-python` Current with `dev`

When `dev` gets relevant updates (C++ changes that affect benchmarking, bug
fixes, etc.), pull them in:

```bash
git checkout benchmark-python
git merge dev
git push origin benchmark-python
```

Do this regularly rather than letting the branch drift. Large divergences are
much harder to resolve than small ones.

---

## Taking Changes to a Divergent Branch

For branches that do not track `dev` (e.g. `mac-dev-benchmark-enhancements`),
do **not** merge `benchmark-python` directly — it will pull in unrelated history.
Use cherry-pick or a targeted patch instead.

### Cherry-pick (preferred for small sets of commits)

```bash
# See what benchmark-python has that the target branch doesn't
git log --oneline other-branch..benchmark-python

# Apply specific commits to the target branch
git checkout other-branch
git cherry-pick <hash>           # single commit
git cherry-pick A^..B            # inclusive range
```

If a cherry-pick conflicts:

```bash
# Resolve conflicts, then:
git add <resolved-files>
git cherry-pick --continue

# Or, if you need to adjust before committing:
git cherry-pick --no-commit <hash>
# edit as needed
git commit -m "Port: <original message>"
```

Only cherry-pick commits that are pure benchmarking changes. Skip any commit
that touched C++ solver code if the target branch has a different version of
those files.

### Targeted patch (for large or conflicting sets of changes)

When cherry-picks produce too many conflicts, a diff-based patch is cleaner:

```bash
# Generate patch covering only benchmarking files
git diff other-branch benchmark-python \
    -- scripts/run_benchmark.py analysis/ docs/benchmarking/ \
    > benchmark.patch

git checkout other-branch
git apply --3way benchmark.patch
# resolve any conflicts, then commit
```

`--3way` uses merge-style conflict resolution rather than failing on fuzzy
matches.

---

## Finding the Right Commits to Port

```bash
# Commits on benchmark-python not yet in dev
git log --oneline benchmark-python ^dev

# Commits on benchmark-python not in another branch
git log --oneline benchmark-python ^other-branch

# Which files a commit touches (to decide whether it's safe to port)
git show --stat <hash>
```

---

## Summary

| Situation | Action |
|---|---|
| Stable milestone ready | Tag, then merge `benchmark-python` → `dev` with `--no-ff` |
| `dev` has relevant updates | Merge `dev` → `benchmark-python` |
| Port to branch diverged from `dev` | `git cherry-pick` selected commits |
| Cherry-picks too conflicted | `git diff` patch with `--3way` |
