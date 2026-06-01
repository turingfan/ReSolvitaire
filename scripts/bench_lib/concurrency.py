"""
bench_lib.concurrency — memory-aware worker-count computation.

Used by benchmark_orchestrator.py and bench_multiplicity.sh to derive a safe
number of parallel worker processes that will NOT trigger an out-of-memory kill.

Why this matters (confirmed 2026-05-31)
---------------------------------------
On a 1 TB host whose *user slice* cgroup was capped at 64 GiB, running 32
multiplicity workers OOM-killed `solvitaire` (dmesg: ``CONSTRAINT_MEMCG``,
``oom_memcg=/user.slice/user-25002.slice``). So the binding limit is the
**cgroup memory.max**, not host RAM — and the per-worker footprint depends on
the **cache type and capacity**, not a flat guess.

Per-worker memory
-----------------
The flat-family caches reserve an mmap of ``(capacity/2) × cluster_bytes``
(generic_flat_cache sets ``num_clusters = max_entries / 2``), demand-paged, whose
*resident* size climbs toward that as the cache fills. Cluster sizes
(generic_flat_cache.h static_asserts):

    flat / compact-state        64 B/cluster
    hash-only                   16 B/cluster
    predecessor                128 B/cluster
    multiplicity               128 B/cluster

So at the default 100 M capacity a multiplicity worker can reach
``50M × 128 B = 6.4 GB`` resident. LRU is heap-based (Boost MultiIndex); we
estimate it per stored entry (approximate — see LRU_BYTES_PER_ENTRY).

Budget formula
--------------
    limit       = min(host RAM, cgroup memory.max)        [the real ceiling]
    per_worker  = (capacity/2) × cluster_bytes[type] + PROCESS_OVERHEAD
    max_safe    = floor(limit × RAM_FRACTION / per_worker)
    jobs        = min(requested_or_default, HARD_CAP, max_safe)

RAM_FRACTION leaves headroom because the cgroup is shared with the user's other
processes. Requesting more than max_safe warns and clamps (it does not refuse,
but exceeding it risks the OOM killer).

Public API
----------
    cluster_bytes_for(cache_type) -> int | None
    per_worker_bytes(cache_type, capacity_entries) -> int
    planning_worker_bytes(cache_types, capacity_entries) -> int
    effective_memory_limit() -> (bytes, source_str)
    get_total_ram_bytes() -> int
    compute_jobs(requested, limit_bytes, worker_bytes) -> JobsResult
"""

import math
import os
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

# ---------------------------------------------------------------------------
# Tunable constants
# ---------------------------------------------------------------------------

# Fraction of the memory limit usable by workers (headroom for OS + the user's
# other processes sharing the same cgroup slice).
RAM_FRACTION: float = 0.80

# Fixed per-process overhead beyond the cache (binary, stacks, libc, the Python
# wrapper + /usr/bin/time, non-cache solver structures). Deliberately generous.
PROCESS_OVERHEAD: int = 512 * 1024 * 1024   # 512 MiB

# Absolute maximum workers regardless of memory. Safety net against absurd input.
HARD_CAP: int = 256

# Cluster byte sizes per cache type (generic_flat_cache.h static_asserts).
# Keyed by the --cache-type token AND by variant-binary nickname.
_CLUSTER_BYTES = {
    "flat": 64, "compact": 64, "default": 64,
    "hash-only": 16, "hash_only": 16,
    "predecessor": 128, "accordion": 128,
    "multiplicity": 128,
}

# LRU (Boost MultiIndex) is heap-based, not an mmap. Measured ~300 B per stored
# entry (free-cell: 1.17M entries → 452 MB RSS ⇒ ~260–390 B/entry incl. overhead).
# Rounded up for safety. NOTE: unlike the flat mmap (which reserves its full size
# up front), LRU only uses what the search actually fills, so capacity × this is
# a WORST CASE that real runs rarely reach — see per_worker_bytes() caveat.
LRU_BYTES_PER_ENTRY: int = 320

# Default solver cache capacity in ENTRIES (command_line_helper.cpp: 100,000,000).
DEFAULT_CAPACITY_ENTRIES: int = 100_000_000


# ---------------------------------------------------------------------------
# Per-worker memory estimate
# ---------------------------------------------------------------------------

def cluster_bytes_for(cache_type: str) -> Optional[int]:
    """Bytes per flat-cache cluster for *cache_type*, or None for LRU/unknown."""
    return _CLUSTER_BYTES.get((cache_type or "").strip().lower())


def per_worker_bytes(cache_type: str, capacity_entries: int = DEFAULT_CAPACITY_ENTRIES) -> int:
    """Estimate worst-case resident bytes for one worker of *cache_type*.

    Flat family: (capacity/2) × cluster_bytes + overhead (the full mmap, which
    a long/hard search can fill). LRU: capacity × per-entry + overhead.
    """
    cap = max(1, int(capacity_entries))
    cb = cluster_bytes_for(cache_type)
    if cb is not None:
        cache = (cap // 2) * cb
    elif (cache_type or "").strip().lower() in ("lru", "force-lru"):
        cache = cap * LRU_BYTES_PER_ENTRY
    else:
        # Unknown: assume the largest flat cluster (128 B) — conservative.
        cache = (cap // 2) * 128
    return cache + PROCESS_OVERHEAD


def planning_worker_bytes(cache_types, capacity_entries: int = DEFAULT_CAPACITY_ENTRIES) -> int:
    """Per-worker estimate to size the worker cap for a run using *cache_types*.

    Policy (grounded in the 2026-05-31 OOM analysis):
      * If the run includes any FLAT-FAMILY cache (flat/hash-only/predecessor/
        multiplicity), size by the LARGEST such reservation. This is exact and is
        the dominant, *always-resident* consumer (the mmap fills toward its full
        size on hard searches — this is what got OOM-killed).
      * LRU's footprint is workload-bounded (it only grows to the states actually
        searched, usually far below capacity), so in a MIXED run it typically
        stays under the flat reservation and is not added on top.
      * A PURE-LRU run has no fixed reservation to anchor on, so it is sized by
        the LRU worst case (capacity × per-entry) — conservative; raise --workers
        if your searches don't fill the cache, or lower LRU --cache-capacity.
    """
    types = list(cache_types or ["multiplicity"])
    flat = [per_worker_bytes(t, capacity_entries) for t in types if cluster_bytes_for(t) is not None]
    if flat:
        return max(flat)
    return max(per_worker_bytes(t, capacity_entries) for t in types)


# ---------------------------------------------------------------------------
# Result type
# ---------------------------------------------------------------------------

@dataclass
class JobsResult:
    """Outcome of compute_jobs()."""
    jobs: int                      # Final recommended worker count
    max_safe: int                  # Memory-derived ceiling
    limiting_factor: str           # Human description of what limited jobs
    warnings: List[str] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Core function
# ---------------------------------------------------------------------------

def compute_jobs(requested: int, limit_bytes: int, worker_bytes: int) -> JobsResult:
    """Compute a safe worker count.

    requested:    workers asked for (CLI or default), >= 1.
    limit_bytes:  the binding memory limit (use effective_memory_limit()).
    worker_bytes: estimated per-worker resident (use per_worker_bytes(...)).
    """
    if requested < 1:
        requested = 1
    worker_bytes = max(1, worker_bytes)

    budget = limit_bytes * RAM_FRACTION
    max_safe = max(1, math.floor(budget / worker_bytes))
    jobs = min(requested, HARD_CAP, max_safe)

    gib = lambda b: b / (1024 ** 3)
    warnings: List[str] = []

    if jobs == max_safe and max_safe < requested:
        limiting_factor = (f"memory ({gib(limit_bytes):.0f} GB limit × {RAM_FRACTION:.0%} / "
                           f"{gib(worker_bytes):.1f} GB/worker → {max_safe})")
    elif jobs == HARD_CAP and HARD_CAP < requested:
        limiting_factor = f"hard cap ({HARD_CAP})"
    else:
        limiting_factor = (f"request ({requested}; memory allows up to {max_safe} "
                           f"at {gib(worker_bytes):.1f} GB/worker)")

    if requested > max_safe:
        warnings.append(
            f"WARNING: requested {requested} workers but the {gib(limit_bytes):.0f} GB memory "
            f"limit only safely allows {max_safe} at ~{gib(worker_bytes):.1f} GB/worker. "
            f"Clamping to {jobs} to avoid the OOM killer. Override with --workers to force "
            f"more (risks OOM-killed runs)."
        )
    if requested > HARD_CAP:
        warnings.append(f"WARNING: requested {requested} exceeds hard cap {HARD_CAP}; clamped.")

    return JobsResult(jobs=jobs, max_safe=max_safe, limiting_factor=limiting_factor, warnings=warnings)


# ---------------------------------------------------------------------------
# Memory limit detection (cgroup-aware)
# ---------------------------------------------------------------------------

# Linux sentinels meaning "no limit".
_CGROUP_UNLIMITED = {"max", "-1"}


def _cgroup_memory_limit() -> Optional[int]:
    """Smallest effective cgroup memory limit for this process (bytes), or None.

    Walks the cgroup v2 hierarchy from /proc/self/cgroup upward, taking the
    minimum numeric memory.max (this finds limits set on parent slices, e.g.
    /user.slice/user-N.slice). Falls back to cgroup v1 memory.limit_in_bytes.
    """
    best: Optional[int] = None
    try:
        # cgroup v2: a single "0::<path>" line.
        rel = None
        with open("/proc/self/cgroup") as f:
            for line in f:
                parts = line.strip().split(":", 2)
                if len(parts) == 3 and parts[0] == "0":
                    rel = parts[2]
                    break
        if rel is not None:
            base = "/sys/fs/cgroup"
            # Walk from the leaf up to the root, reading memory.max at each level.
            segs = [s for s in rel.split("/") if s]
            for i in range(len(segs), -1, -1):
                d = os.path.join(base, *segs[:i]) if i else base
                p = os.path.join(d, "memory.max")
                try:
                    with open(p) as mf:
                        v = mf.read().strip()
                except OSError:
                    continue
                if v in _CGROUP_UNLIMITED:
                    continue
                try:
                    n = int(v)
                except ValueError:
                    continue
                best = n if best is None else min(best, n)
        # cgroup v1 fallback.
        if best is None:
            for p in ("/sys/fs/cgroup/memory/memory.limit_in_bytes",):
                try:
                    with open(p) as mf:
                        n = int(mf.read().strip())
                    # v1 "unlimited" is a huge near-INT64 value; ignore it.
                    if n < (1 << 62):
                        best = n if best is None else min(best, n)
                except OSError:
                    pass
    except Exception:
        return None
    return best


def get_total_ram_bytes() -> int:
    """Total physical RAM in bytes (macOS/Linux). 0 if undetermined."""
    import platform
    import subprocess
    system = platform.system()
    try:
        if system == "Darwin":
            r = subprocess.run(["sysctl", "-n", "hw.memsize"],
                               capture_output=True, text=True, timeout=5)
            if r.returncode == 0:
                return int(r.stdout.strip())
        else:
            with open("/proc/meminfo") as f:
                for line in f:
                    if line.startswith("MemTotal:"):
                        return int(line.split()[1]) * 1024
    except Exception:
        pass
    return 0


def effective_memory_limit() -> Tuple[int, str]:
    """Return (limit_bytes, source) — the binding memory ceiling.

    The minimum of host RAM and any cgroup memory.max. `source` describes which
    bound won, for transparent logging.
    """
    host = get_total_ram_bytes()
    cg = _cgroup_memory_limit()
    if cg is not None and (host == 0 or cg < host):
        return cg, f"cgroup memory.max ({cg / (1024**3):.0f} GB; host {host / (1024**3):.0f} GB)"
    if host:
        return host, f"host RAM ({host / (1024**3):.0f} GB)"
    return 0, "unknown"
