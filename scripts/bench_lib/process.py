"""
bench_lib.process — shared process-group kill discipline for benchmark runners.

Usage::

    from bench_lib.process import run_with_deadline, RunResult

    result = run_with_deadline(
        ["my-solver", "--timeout", "60000"],
        solver_timeout_s=60.0,
        sigterm_grace_s=30.0,
    )
    print(result.disposition, result.wall_us, result.stdout[:200])

Disposition values
------------------
EXITED_OK           Process exited with returncode 0 within the deadline.
EXITED_ERR          Process exited with non-zero returncode within the deadline.
KILLED_AFTER_SIGTERM  Deadline expired; SIGTERM sent; process exited before SIGKILL.
KILLED_HARD         Deadline + SIGTERM grace expired; SIGKILL sent to group.

Design notes
------------
* `start_new_session=True` puts the child (and any grandchildren, e.g. a
  /usr/bin/time wrapper) into their own process group so that a single
  os.killpg() call reaches every member.
* The solver's own --timeout flag is authoritative.  The Python deadline is a
  safety net: it fires only at 1.5 × solver_timeout_s (D1 resolution: no floor,
  no cap — just 1.5×, whatever the solver timeout is).
* On overrun: SIGTERM the group → wait sigterm_grace_s → SIGKILL the group →
  reap.  We never SIGKILL before SIGTERM+grace has elapsed.
* Partial stdout is always captured and returned, regardless of how the process
  ended.  No path discards already-emitted output.
* After return, no process in the group survives (reap guarantees this via
  communicate()).
"""

import os
import signal
import subprocess
import time
from dataclasses import dataclass, field
from typing import List, Optional


# ---------------------------------------------------------------------------
# Public result type
# ---------------------------------------------------------------------------

EXITED_OK = "EXITED_OK"
EXITED_ERR = "EXITED_ERR"
KILLED_AFTER_SIGTERM = "KILLED_AFTER_SIGTERM"
KILLED_HARD = "KILLED_HARD"


@dataclass
class RunResult:
    """Outcome of run_with_deadline()."""
    returncode: Optional[int]    # None if process never exited cleanly
    stdout: str
    stderr: str
    wall_us: float               # wall-clock microseconds (Python-measured)
    disposition: str             # one of the four constants above


# ---------------------------------------------------------------------------
# Core function
# ---------------------------------------------------------------------------

def run_with_deadline(
    cmd: List[str],
    *,
    solver_timeout_s: float,
    sigterm_grace_s: float = 30.0,
    capture: bool = True,
) -> RunResult:
    """Spawn *cmd* in a new session and enforce a two-tier kill discipline.

    Parameters
    ----------
    cmd:
        Command and arguments to run.
    solver_timeout_s:
        The solver's own ``--timeout`` value in seconds.  The Python deadline
        is set to **1.5 × solver_timeout_s** (D1: no floor, no cap).
    sigterm_grace_s:
        Seconds to wait after SIGTERM before escalating to SIGKILL.
        Default 30 s is appropriate for most flush windows.
    capture:
        If True (default), capture stdout/stderr and return them.
        Set False only if you want to inherit the parent's streams; in that
        case stdout/stderr in the result will be empty strings.

    Returns
    -------
    RunResult
        Always populated; partial stdout is preserved on kill paths.
    """
    total_wait_s = 1.5 * solver_timeout_s

    stdout_pipe = subprocess.PIPE if capture else None
    stderr_pipe = subprocess.PIPE if capture else None

    t0 = time.perf_counter()

    proc = subprocess.Popen(
        cmd,
        stdout=stdout_pipe,
        stderr=stderr_pipe,
        text=True,
        start_new_session=True,   # child gets its own process group (POSIX setsid)
    )

    stdout = ""
    stderr = ""
    returncode: Optional[int] = None
    disposition: str

    try:
        # --- Happy path: process finishes within the deadline ---------------
        stdout, stderr = proc.communicate(timeout=total_wait_s)
        returncode = proc.returncode
        disposition = EXITED_OK if returncode == 0 else EXITED_ERR

    except subprocess.TimeoutExpired:
        # --- Deadline elapsed: escalation sequence --------------------------
        # 1. SIGTERM the whole process group.
        pgid = _pgid(proc)
        _killpg_safe(pgid, signal.SIGTERM)

        # 2. Wait sigterm_grace_s for voluntary exit.
        try:
            stdout, stderr = proc.communicate(timeout=sigterm_grace_s)
            returncode = proc.returncode
            disposition = KILLED_AFTER_SIGTERM

        except subprocess.TimeoutExpired:
            # 3. SIGKILL the group — no more waiting.
            _killpg_safe(pgid, signal.SIGKILL)

            # 4. Reap: communicate() with no timeout so we drain all pipes.
            stdout, stderr = proc.communicate()
            returncode = proc.returncode
            disposition = KILLED_HARD

    # Ensure empty strings when capture=False (pipes were None)
    if stdout is None:
        stdout = ""
    if stderr is None:
        stderr = ""

    wall_us = (time.perf_counter() - t0) * 1_000_000

    return RunResult(
        returncode=returncode,
        stdout=stdout,
        stderr=stderr,
        wall_us=wall_us,
        disposition=disposition,
    )


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _pgid(proc: subprocess.Popen) -> int:
    """Return the process group ID for *proc*, falling back to its PID."""
    try:
        return os.getpgid(proc.pid)
    except OSError:
        return proc.pid


def _killpg_safe(pgid: int, sig: signal.Signals) -> None:
    """Send *sig* to process group *pgid*, ignoring ESRCH (already gone)."""
    try:
        os.killpg(pgid, sig)
    except OSError:
        pass
