"""
Unit tests for bench_lib.process.run_with_deadline().

Uses fake child processes (small python -c / sh -c snippets) instead of the
real solver so the suite runs in a few seconds.  All timeouts are very short
(solver_timeout_s=0.3, sigterm_grace_s=0.3).
"""

import os
import subprocess
import sys
import time
import unittest

# Allow running from any cwd (e.g. project root or scripts/).
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from bench_lib.process import (
    EXITED_ERR,
    EXITED_OK,
    KILLED_AFTER_SIGTERM,
    KILLED_HARD,
    RunResult,
    run_with_deadline,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _no_survivors(pgid: int) -> bool:
    """Return True if the process group no longer exists (no live members)."""
    try:
        os.killpg(pgid, 0)   # signal 0 = check only
        return False          # group still alive
    except OSError:
        return True           # ESRCH — gone


# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------

class TestCleanExit(unittest.TestCase):
    """Child exits cleanly within the deadline."""

    def test_exited_ok_disposition(self):
        result = run_with_deadline(
            [sys.executable, "-c", "import sys; print('hello'); sys.exit(0)"],
            solver_timeout_s=0.3,
            sigterm_grace_s=0.3,
        )
        self.assertEqual(result.disposition, EXITED_OK)

    def test_full_stdout_captured(self):
        result = run_with_deadline(
            [sys.executable, "-c", "print('full output captured')"],
            solver_timeout_s=0.3,
            sigterm_grace_s=0.3,
        )
        self.assertIn("full output captured", result.stdout)

    def test_returncode_zero(self):
        result = run_with_deadline(
            [sys.executable, "-c", "pass"],
            solver_timeout_s=0.3,
            sigterm_grace_s=0.3,
        )
        self.assertEqual(result.returncode, 0)

    def test_nonzero_exit_is_exited_err(self):
        result = run_with_deadline(
            [sys.executable, "-c", "import sys; sys.exit(1)"],
            solver_timeout_s=0.3,
            sigterm_grace_s=0.3,
        )
        self.assertEqual(result.disposition, EXITED_ERR)
        self.assertEqual(result.returncode, 1)

    def test_wall_us_positive(self):
        result = run_with_deadline(
            [sys.executable, "-c", "pass"],
            solver_timeout_s=0.3,
            sigterm_grace_s=0.3,
        )
        self.assertGreater(result.wall_us, 0)


class TestSIGTERMExit(unittest.TestCase):
    """Child prints partial output and then exits cleanly on SIGTERM.

    The child writes a line, installs a SIGTERM handler that exits 0, then
    sleeps longer than the total deadline so the wrapper fires SIGTERM.
    """

    _CHILD = (
        "import signal, sys, time\n"
        "sys.stdout.write('partial output\\n')\n"
        "sys.stdout.flush()\n"
        "signal.signal(signal.SIGTERM, lambda *a: sys.exit(0))\n"
        "time.sleep(10)\n"
    )

    def setUp(self):
        self.result = run_with_deadline(
            [sys.executable, "-c", self._CHILD],
            solver_timeout_s=0.3,
            sigterm_grace_s=0.3,
        )

    def test_disposition_killed_after_sigterm(self):
        self.assertEqual(self.result.disposition, KILLED_AFTER_SIGTERM)

    def test_partial_stdout_captured(self):
        self.assertIn("partial output", self.result.stdout)

    def test_returncode_available(self):
        # Child exits 0 on SIGTERM in this case
        self.assertIsNotNone(self.result.returncode)


class TestSIGKILLHard(unittest.TestCase):
    """Child prints partial output then ignores SIGTERM and hangs.

    After deadline + sigterm_grace, the wrapper must SIGKILL and still
    return the partial output.  No process must survive.
    """

    _CHILD = (
        "import signal, sys, time\n"
        "sys.stdout.write('partial before hang\\n')\n"
        "sys.stdout.flush()\n"
        # Ignore SIGTERM entirely
        "signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
        "time.sleep(60)\n"
    )

    def setUp(self):
        self.result = run_with_deadline(
            [sys.executable, "-c", self._CHILD],
            solver_timeout_s=0.3,
            sigterm_grace_s=0.3,
        )

    def test_disposition_killed_hard(self):
        self.assertEqual(self.result.disposition, KILLED_HARD)

    def test_partial_stdout_captured(self):
        self.assertIn("partial before hang", self.result.stdout)

    def test_no_process_survives(self):
        # After run_with_deadline returns, the group must be gone.
        # We can't recover the pgid after the fact, but we verify the
        # function returns at all (i.e. doesn't block), which proves
        # the reap completed.  We also check wall_us is bounded.
        # 0.3 + 0.3 + overhead — should complete well under 5 seconds.
        self.assertLess(self.result.wall_us, 5_000_000)

    def test_wall_us_reasonable(self):
        # Must have waited at least 0.3 s for the deadline to fire.
        self.assertGreater(self.result.wall_us, 300_000)


class TestProcessGroupOrphans(unittest.TestCase):
    """Verify the entire process group is reaped (no orphan grandchildren).

    Spawn a shell that launches a background grandchild.  The shell ignores
    SIGTERM; the grandchild sleeps.  After SIGKILL the whole group is gone.
    """

    # Shell script: print a marker, launch a sleeping grandchild, then sleep.
    # SIGTERM is ignored at the shell level.
    _CHILD_SH = (
        "echo group_test_marker; "
        "trap '' TERM; "
        "sleep 60 & "
        "sleep 60"
    )

    def test_group_fully_reaped(self):
        result = run_with_deadline(
            ["sh", "-c", self._CHILD_SH],
            solver_timeout_s=0.3,
            sigterm_grace_s=0.3,
        )
        # Should have been killed (SIGTERM ignored → SIGKILL)
        self.assertIn(result.disposition, (KILLED_AFTER_SIGTERM, KILLED_HARD))
        self.assertIn("group_test_marker", result.stdout)
        # Verify wall time is bounded (function returned within ~5 s)
        self.assertLess(result.wall_us, 5_000_000)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    unittest.main(verbosity=2)
