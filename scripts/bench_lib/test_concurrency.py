"""
Unit tests for bench_lib.concurrency.compute_jobs().

Tests verify:
  - Normal case: RAM is ample, requested <= max_safe → jobs = requested, no warnings.
  - Clamped to max_safe: requested > max_safe → jobs = max_safe, warning fires.
  - Clamped to HARD_CAP: requested > HARD_CAP → jobs = HARD_CAP, warning fires.
  - Ginormous cache: large cache_capacity_bytes → extra warning even if jobs OK.
  - Minimum: requested=1 always returns jobs>=1 even with tiny RAM.
  - Edge: total_ram_bytes = 0 → jobs = 1 (floor of 0 → max_safe=1).
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from bench_lib.concurrency import (
    BYTES_PER_WORKER,
    GINORMOUS_THRESHOLD,
    HARD_CAP,
    RAM_FRACTION,
    JobsResult,
    compute_jobs,
)

GB = 1024 ** 3


class TestComputeJobsNormal(unittest.TestCase):
    """RAM is ample; requested fits within budget."""

    def setUp(self):
        # 32 GB RAM → max_safe = floor(32 * 0.8 / 3) = floor(8.53) = 8
        self.total_ram = 32 * GB
        self.result = compute_jobs(requested=4, total_ram_bytes=self.total_ram)

    def test_jobs_equals_requested(self):
        self.assertEqual(self.result.jobs, 4)

    def test_no_warnings(self):
        self.assertEqual(self.result.warnings, [])

    def test_max_safe_value(self):
        import math
        expected = max(1, math.floor(self.total_ram * RAM_FRACTION / BYTES_PER_WORKER))
        self.assertEqual(self.result.max_safe, expected)

    def test_limiting_factor_is_request(self):
        self.assertIn("request", self.result.limiting_factor)


class TestComputeJobsClampedToMaxSafe(unittest.TestCase):
    """Requested workers exceeds memory budget."""

    def setUp(self):
        # 6 GB RAM → max_safe = floor(6 * 0.8 / 3) = floor(1.6) = 1
        self.total_ram = 6 * GB
        self.result = compute_jobs(requested=8, total_ram_bytes=self.total_ram)

    def test_jobs_clamped_to_max_safe(self):
        self.assertEqual(self.result.jobs, self.result.max_safe)
        self.assertLessEqual(self.result.jobs, 8)

    def test_warning_fires(self):
        self.assertTrue(len(self.result.warnings) >= 1)
        # Warning must mention the clamping
        combined = " ".join(self.result.warnings)
        self.assertIn("memory budget", combined)

    def test_warning_mentions_clamped_value(self):
        combined = " ".join(self.result.warnings)
        self.assertIn(str(self.result.jobs), combined)


class TestComputeJobsClampedToHardCap(unittest.TestCase):
    """Requested workers exceeds HARD_CAP."""

    def setUp(self):
        # Very large RAM so max_safe > HARD_CAP; requested > HARD_CAP too.
        self.total_ram = 1000 * GB
        self.result = compute_jobs(requested=HARD_CAP + 10, total_ram_bytes=self.total_ram)

    def test_jobs_clamped_to_hard_cap(self):
        self.assertEqual(self.result.jobs, HARD_CAP)

    def test_warning_fires(self):
        self.assertTrue(len(self.result.warnings) >= 1)
        combined = " ".join(self.result.warnings)
        self.assertIn("hard cap", combined)


class TestComputeJobsGinormousCache(unittest.TestCase):
    """Large --cache-capacity triggers louder warning even if job count fits."""

    def setUp(self):
        self.total_ram = 32 * GB
        huge_cache = GINORMOUS_THRESHOLD + GB   # just over the threshold
        self.result = compute_jobs(
            requested=2, total_ram_bytes=self.total_ram,
            cache_capacity_bytes=huge_cache,
        )

    def test_jobs_may_still_be_requested(self):
        # Jobs is not forced to 0 or 1; user is just warned.
        self.assertGreaterEqual(self.result.jobs, 1)

    def test_ginormous_warning_fires(self):
        combined = " ".join(self.result.warnings)
        self.assertIn("cache-capacity", combined.lower())

    def test_no_other_warnings_if_in_budget(self):
        # Only the ginormous-cache warning should fire; no memory-budget clamp.
        budget_warnings = [w for w in self.result.warnings if "Clamping" in w]
        self.assertEqual(budget_warnings, [])


class TestComputeJobsEdgeCases(unittest.TestCase):
    """Edge cases: zero RAM, requested=1, very small RAM."""

    def test_zero_ram_returns_one(self):
        result = compute_jobs(requested=4, total_ram_bytes=0)
        self.assertEqual(result.jobs, 1)

    def test_requested_one_always_succeeds(self):
        result = compute_jobs(requested=1, total_ram_bytes=2 * GB)
        self.assertEqual(result.jobs, 1)

    def test_requested_less_than_one_treated_as_one(self):
        result = compute_jobs(requested=0, total_ram_bytes=32 * GB)
        self.assertEqual(result.jobs, 1)

    def test_exact_budget_fit(self):
        # RAM exactly enough for 2 workers (no fractional slack)
        # max_safe = floor(total * 0.8 / 3GB)
        # For max_safe = 2: total >= 2 * 3GB / 0.8 = 7.5 GB
        import math
        needed_for_two = math.ceil(2 * BYTES_PER_WORKER / RAM_FRACTION)
        result = compute_jobs(requested=2, total_ram_bytes=needed_for_two)
        self.assertGreaterEqual(result.jobs, 2)
        self.assertEqual(result.warnings, [])


class TestComputeJobsLimitingFactor(unittest.TestCase):
    """limiting_factor string describes the binding constraint."""

    def test_limited_by_memory(self):
        # 4 GB RAM → max_safe = floor(4*0.8/3) = 1; request 4
        result = compute_jobs(requested=4, total_ram_bytes=4 * GB)
        self.assertIn("memory", result.limiting_factor.lower())

    def test_limited_by_request(self):
        # 32 GB RAM, request 2 (well within budget)
        result = compute_jobs(requested=2, total_ram_bytes=32 * GB)
        self.assertIn("request", result.limiting_factor.lower())

    def test_limited_by_hard_cap(self):
        # Vast RAM, request > HARD_CAP
        result = compute_jobs(requested=HARD_CAP + 5, total_ram_bytes=500 * GB)
        self.assertIn("hard cap", result.limiting_factor.lower())


if __name__ == "__main__":
    unittest.main(verbosity=2)
