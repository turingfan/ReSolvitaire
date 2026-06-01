"""Unit tests for bench_lib.concurrency (memory-aware worker sizing)."""

import unittest

from bench_lib import concurrency as c

GiB = 1024 ** 3


class TestPerWorkerBytes(unittest.TestCase):
    def test_flat_family_cluster_sizes(self):
        # (capacity/2) * cluster_bytes + overhead, default capacity 100M.
        cap = 100_000_000
        ov = c.PROCESS_OVERHEAD
        self.assertEqual(c.per_worker_bytes("multiplicity", cap), (cap // 2) * 128 + ov)
        self.assertEqual(c.per_worker_bytes("predecessor", cap), (cap // 2) * 128 + ov)
        self.assertEqual(c.per_worker_bytes("flat", cap), (cap // 2) * 64 + ov)
        self.assertEqual(c.per_worker_bytes("hash-only", cap), (cap // 2) * 16 + ov)

    def test_multiplicity_default_is_about_6_5gib(self):
        # 50M clusters * 128 B = 6.4 GB = 5.96 GiB, + 0.5 GiB overhead ≈ 6.46 GiB.
        b = c.per_worker_bytes("multiplicity")
        self.assertAlmostEqual(b / GiB, 6.46, delta=0.1)

    def test_lru_uses_per_entry(self):
        cap = 1_000_000
        self.assertEqual(c.per_worker_bytes("lru", cap), cap * c.LRU_BYTES_PER_ENTRY + c.PROCESS_OVERHEAD)

    def test_unknown_type_conservative(self):
        # Unknown falls back to the largest flat cluster (128 B).
        cap = 100_000_000
        self.assertEqual(c.per_worker_bytes("???", cap), (cap // 2) * 128 + c.PROCESS_OVERHEAD)

    def test_cluster_bytes_for(self):
        self.assertEqual(c.cluster_bytes_for("multiplicity"), 128)
        self.assertEqual(c.cluster_bytes_for("flat"), 64)
        self.assertIsNone(c.cluster_bytes_for("lru"))


class TestPlanningWorkerBytes(unittest.TestCase):
    def test_mixed_run_sized_by_flat_family_not_lru(self):
        # phase C = lru + multiplicity: should size by multiplicity (flat-family),
        # NOT the larger LRU worst case (LRU is workload-bounded).
        mixed = c.planning_worker_bytes(["lru", "multiplicity"])
        self.assertEqual(mixed, c.per_worker_bytes("multiplicity"))

    def test_pure_lru_sized_by_lru_worstcase(self):
        self.assertEqual(c.planning_worker_bytes(["lru"]), c.per_worker_bytes("lru"))

    def test_picks_largest_flat_family(self):
        self.assertEqual(c.planning_worker_bytes(["hash-only", "flat", "multiplicity"]),
                         c.per_worker_bytes("multiplicity"))


class TestComputeJobs(unittest.TestCase):
    def test_64gib_slice_multiplicity_gives_about_7(self):
        # The real case: 64 GiB user-slice cgroup, multiplicity workers.
        limit = 64 * GiB
        pw = c.per_worker_bytes("multiplicity")
        r = c.compute_jobs(32, limit, pw)
        self.assertEqual(r.jobs, 7)          # 64*0.8/6.46 ≈ 7
        self.assertTrue(r.warnings)          # 32 requested > 7 → warns + clamps
        self.assertIn("OOM", r.warnings[0])

    def test_request_below_safe_is_honoured(self):
        limit = 64 * GiB
        pw = c.per_worker_bytes("multiplicity")
        r = c.compute_jobs(4, limit, pw)
        self.assertEqual(r.jobs, 4)
        self.assertFalse(r.warnings)

    def test_ample_memory_honours_request(self):
        r = c.compute_jobs(8, 1024 * GiB, c.per_worker_bytes("flat"))
        self.assertEqual(r.jobs, 8)

    def test_hard_cap(self):
        r = c.compute_jobs(10_000, 100_000 * GiB, c.per_worker_bytes("hash-only"))
        self.assertEqual(r.jobs, c.HARD_CAP)

    def test_at_least_one(self):
        r = c.compute_jobs(8, 1 * GiB, c.per_worker_bytes("multiplicity"))  # can't even fit 1
        self.assertEqual(r.jobs, 1)


class TestMemoryLimit(unittest.TestCase):
    def test_get_total_ram_positive(self):
        self.assertGreater(c.get_total_ram_bytes(), 0)

    def test_effective_limit_returns_pair(self):
        limit, source = c.effective_memory_limit()
        self.assertIsInstance(limit, int)
        self.assertIsInstance(source, str)
        self.assertGreater(limit, 0)


if __name__ == "__main__":
    unittest.main()
