"""
bench_lib.concurrency — memory-aware worker-count computation.

Used by benchmark_orchestrator.py and bench_multiplicity.sh (via the
orchestrator) to derive a safe number of parallel worker processes.

Design (D2/D3 resolutions, 2026-05-29)
---------------------------------------
* Each concurrent worker spawns a nesting stack:
    GNU parallel → run_benchmark.py → /usr/bin/time → solver
  The solver (flat-cache) can hold several GB of mmap'd virtual address
  space, but the *resident* footprint we plan against is ~3 GB / worker.

* Memory budget formula::

    max_safe = floor(total_ram_bytes × RAM_FRACTION / BYTES_PER_WORKER)

  where RAM_FRACTION = 0.80 (leave 20 % for OS + misc) and
  BYTES_PER_WORKER = 3 × 1024³ = 3_221_225_472.

* Final jobs = min(requested_or_default, HARD_CAP, max_safe).

* Be lenient:
    - For ordinary requests, WARN (do not refuse) if requested > max_safe,
      then clamp to max_safe.
    - For "ginormous" cache requests (cache_capacity > GINORMOUS_THRESHOLD),
      escalate to a louder warning.

* When ``--cache-capacity`` is very large (> GINORMOUS_THRESHOLD = 8 GB),
  each worker may hold substantially more than 3 GB resident, so the warning
  is stronger (but still not a hard refusal — the user is responsible).

Public API
----------
    compute_jobs(requested, total_ram_bytes, cache_capacity_bytes=0)
        -> JobsResult(jobs, max_safe, limiting_factor, warnings)
"""

import math
from dataclasses import dataclass, field
from typing import List, Optional

# ---------------------------------------------------------------------------
# Tunable constants (documented here for clarity)
# ---------------------------------------------------------------------------

# Fraction of total RAM available for worker processes (leave headroom for OS).
RAM_FRACTION: float = 0.80

# Assumed resident memory per worker (bytes).  Covers solver + Python + time.
# Flat-cache mmap VA reservation can be several GB but resident footprint in
# practice is ~1–3 GB for large games; we plan for the worst case.
BYTES_PER_WORKER: int = 3 * 1024 * 1024 * 1024   # 3 GiB

# Absolute maximum workers regardless of RAM.  Safety net against absurd inputs.
HARD_CAP: int = 64

# A "ginormous" cache: when --cache-capacity exceeds this, the per-worker
# resident footprint assumption of 3 GB is unreliable → louder warning.
GINORMOUS_THRESHOLD: int = 8 * 1024 * 1024 * 1024  # 8 GiB


# ---------------------------------------------------------------------------
# Result type
# ---------------------------------------------------------------------------

@dataclass
class JobsResult:
    """Outcome of compute_jobs()."""
    jobs: int                      # Final recommended worker count
    max_safe: int                  # Memory-derived ceiling
    limiting_factor: str           # Human description of what limited jobs
    warnings: List[str] = field(default_factory=list)  # Non-empty if caution needed


# ---------------------------------------------------------------------------
# Core function
# ---------------------------------------------------------------------------

def compute_jobs(
    requested: int,
    total_ram_bytes: int,
    cache_capacity_bytes: int = 0,
) -> JobsResult:
    """Compute a safe number of parallel workers.

    Parameters
    ----------
    requested:
        The number of workers the caller requested (from CLI or default).
        Must be >= 1.
    total_ram_bytes:
        Total physical RAM on the machine (bytes).  Use get_total_ram_bytes()
        to obtain this portably.
    cache_capacity_bytes:
        The ``--cache-capacity`` value passed to the solver (bytes), or 0 if
        using the solver default.  A very large value triggers a louder warning.

    Returns
    -------
    JobsResult
        ``.jobs`` is the recommended (safe) worker count.
        ``.warnings`` is a list of strings to print; empty if all is fine.
    """
    if requested < 1:
        requested = 1

    # --- Memory ceiling ---------------------------------------------------
    budget_bytes = total_ram_bytes * RAM_FRACTION
    max_safe = max(1, math.floor(budget_bytes / BYTES_PER_WORKER))

    # --- Apply caps in priority order -------------------------------------
    #   1. HARD_CAP (absolute safety net)
    #   2. max_safe (memory budget)
    #   3. requested (what the user asked for)
    jobs = min(requested, HARD_CAP, max_safe)

    warnings: List[str] = []

    # Determine what limited us
    if jobs == HARD_CAP and HARD_CAP < requested:
        limiting_factor = f"hard cap ({HARD_CAP})"
    elif jobs == max_safe and max_safe < requested:
        limiting_factor = f"memory budget (~{total_ram_bytes // (1024**3)} GB RAM → {max_safe} workers at {BYTES_PER_WORKER // (1024**3)} GB each)"
    elif jobs == requested:
        if requested == max_safe:
            limiting_factor = "request (equals memory-safe ceiling)"
        else:
            limiting_factor = f"request ({requested}; memory budget allows up to {max_safe})"
    else:
        limiting_factor = "memory budget"

    # --- Warn if clamped --------------------------------------------------
    if requested > max_safe:
        msg = (
            f"WARNING: requested {requested} workers but memory budget allows {max_safe} "
            f"(~{total_ram_bytes // (1024**3)} GB RAM × {RAM_FRACTION:.0%} / "
            f"{BYTES_PER_WORKER // (1024**3)} GB per worker). "
            f"Clamping to {jobs}."
        )
        warnings.append(msg)

    if requested > HARD_CAP:
        warnings.append(
            f"WARNING: requested {requested} workers exceeds hard cap {HARD_CAP}. "
            f"Clamped to {HARD_CAP}."
        )

    # --- Ginormous cache warning ------------------------------------------
    if cache_capacity_bytes > GINORMOUS_THRESHOLD:
        gb = cache_capacity_bytes / (1024**3)
        warnings.append(
            f"WARNING: --cache-capacity is very large ({gb:.1f} GB). "
            f"Each worker may hold significantly more than the assumed "
            f"{BYTES_PER_WORKER // (1024**3)} GB resident. "
            f"Consider reducing --workers further (currently {jobs})."
        )

    return JobsResult(
        jobs=jobs,
        max_safe=max_safe,
        limiting_factor=limiting_factor,
        warnings=warnings,
    )


# ---------------------------------------------------------------------------
# Platform RAM helper
# ---------------------------------------------------------------------------

def get_total_ram_bytes() -> int:
    """Return total physical RAM in bytes (portable macOS/Linux).

    Returns 0 if unable to determine (caller should treat 0 as 'unknown'
    and fall back to a conservative default).
    """
    import platform
    import subprocess

    system = platform.system()
    try:
        if system == "Darwin":
            r = subprocess.run(
                ["sysctl", "-n", "hw.memsize"],
                capture_output=True, text=True, timeout=5,
            )
            if r.returncode == 0:
                return int(r.stdout.strip())
        else:
            # Linux — prefer MemAvailable for a tighter bound; fall back to MemTotal
            with open("/proc/meminfo") as f:
                for line in f:
                    if line.startswith("MemTotal:"):
                        kb = int(line.split()[1])
                        return kb * 1024
    except Exception:
        pass
    return 0
