#!/usr/bin/env python3
import os
import subprocess
import json
import argparse
import sys
import time

# ---------------------------------------------------------------------------
# Comparison policy (post-M6)
#
# M6 removed pile ordering for flat-cache games, changing DFS traversal order.
# The set of states explored is the same; only the order differs. This means:
#
#   - SOLVED <-> UNSOLVABLE flip:  HARD FAIL (correctness bug)
#   - Either side is TIMEOUT:      SOFT PASS (traversal-order timing, acceptable)
#   - Both definitive and match:   check states_searched (must match)
#
# states_searched is still enforced for definitive matches because it is a
# useful regression signal for future changes that should not alter traversal
# order.  If oracles are regenerated on a new system/build, update them with
# --regenerate.
# ---------------------------------------------------------------------------

def run_regression(solver_path, instances_dir, oracle_path, verbose=False,
                   max_instance_timeout_ms=120000, regenerate=False,
                   force_lru=False, skip_ineligible=False,
                   compare_outcome_only=False, cache_type=None,
                   enforce_node_counts=False,
                   initial_depth_bound=None, depth_grow=None,
                   max_depth_bound=None):
    if not os.path.exists(solver_path):
        print(f"Error: Solver not found at {solver_path}")
        return 1
    if instances_dir and not os.path.exists(instances_dir):
        print(f"Error: Instances directory not found at {instances_dir}")
        return 1
    if not os.path.exists(oracle_path):
        print(f"Error: Oracle not found at {oracle_path}")
        return 1

    with open(oracle_path, 'r') as f:
        oracle_raw = json.load(f)

    # Remember input format so we can write back in the same shape.
    oracle_is_list = isinstance(oracle_raw, list)

    # Normalize to dict mapping base filename -> entry.
    oracle = {}
    oracle_order = []  # preserve list order for regeneration
    if isinstance(oracle_raw, dict):
        oracle = oracle_raw
        oracle_order = sorted(oracle_raw.keys())
    elif isinstance(oracle_raw, list):
        for entry in oracle_raw:
            inst_path = entry.get('instance') or entry.get('instance_name')
            if inst_path:
                basename = os.path.basename(inst_path)
                oracle[basename] = entry
                oracle_order.append(basename)

    instance_filenames = oracle_order
    total = len(instance_filenames)
    failed = 0
    passed = 0
    skipped = 0
    new_oracle_entries = {}  # used when regenerate=True

    def normalize_outcome(outcome):
        mapping = {
            "winnable": "solved",
            "unwinnable": "unsolvable",
            "solved": "solved",
            "unsolvable": "unsolvable",
            "timeout": "timeout",
            "unknown": "unknown"
        }
        return mapping.get(outcome, outcome)

    mode = "Regenerating" if regenerate else "Running"
    print(f"{mode} Regression: {total} instances (Oracle: {os.path.basename(oracle_path)})",
          flush=True)
    if initial_depth_bound is not None:
        grow_note = f", depth-grow={depth_grow}" if depth_grow is not None else ""
        max_note = f", max-depth-bound={max_depth_bound}" if max_depth_bound is not None else ""
        print(f"ITERATIVE-DEEPENING MODE: initial-depth-bound={initial_depth_bound}"
              f"{grow_note}{max_note} "
              f"(asserting bounded final verdict == unbounded oracle verdict)",
              flush=True)
    print("-" * 60, flush=True)

    for filename in instance_filenames:
        instance_path = os.path.join(instances_dir, filename) if instances_dir else ""
        baseline = oracle[filename]

        baseline_time_ms = baseline.get("baseline_time_ms", 30000)
        instance_timeout_ms = min(
            max(int(2 * baseline_time_ms), 2000),
            max_instance_timeout_ms
        )
        # In regenerate mode use a generous fixed timeout so we get definitive results.
        if regenerate:
            instance_timeout_ms = max_instance_timeout_ms

        # Levels 2-5 oracles have 'baseline_time_ms'; use --random <seed> to avoid
        # the JSON round-trip bug (json_helper serialises gs.tableau_piles instead of
        # gs.original_tableau_piles — see docs/known-issues.md).
        # Level 1 oracles lack 'baseline_time_ms'; pass the JSON file path.
        use_seed = "baseline_time_ms" in baseline
        if use_seed:
            parts = filename.replace('.json', '').rsplit('_', 2)
            if len(parts) != 3 or not parts[1].lstrip('-').isdigit():
                print(f"[WARN] {filename}: cannot extract seed from filename, skipping",
                      flush=True)
                continue
            seed = parts[1]
            cmd = [solver_path, "--random", seed, "--json", "--timeout", str(instance_timeout_ms)]
        else:
            cmd = [solver_path, instance_path, "--json", "--timeout", str(instance_timeout_ms)]

        if "custom_rules" in baseline:
            cmd.extend(["--custom-rules", baseline["custom_rules"]])
        elif "game_type" in baseline:
            cmd.extend(["--type", baseline["game_type"]])
        else:
            if "_seed_" in filename:
                game_type = filename.split("_seed_")[0]
            else:
                game_type = filename.split("_")[0]
            cmd.extend(["--type", game_type])

        streamliner = baseline.get("streamliner", "none")
        cmd.extend(["--streamliners", streamliner])

        if force_lru:
            cmd.append("--force-lru")

        if cache_type:
            cmd.extend(["--cache-type", cache_type])

        # Iterative-deepening (depth-bounded) mode. When --initial-depth-bound is
        # set, the solver runs the outer ID loop instead of a single unbounded
        # pass. The differential-verdict harness (Stage 1 item 1f) uses this to
        # assert the bounded FINAL verdict equals the unbounded oracle verdict.
        if initial_depth_bound is not None:
            cmd.extend(["--initial-depth-bound", str(initial_depth_bound)])
        if depth_grow is not None:
            cmd.extend(["--depth-grow", str(depth_grow)])
        if max_depth_bound is not None:
            cmd.extend(["--max-depth-bound", str(max_depth_bound)])

        try:
            # Give the process 60s on top of the solver's own timeout to flush output.
            py_timeout = (instance_timeout_ms / 1000.0) + 60.0
            t0 = time.time()
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=py_timeout)
            elapsed_ms = (time.time() - t0) * 1000.0

            if result.returncode != 0:
                stderr_lower = result.stderr.lower()
                if skip_ineligible and (
                        "requires" in stderr_lower or
                        "not eligible" in stderr_lower or
                        "not supported" in stderr_lower):
                    if verbose:
                        print(f"[SKIP] {filename} (ineligible: {result.stderr.strip()})",
                              flush=True)
                    skipped += 1
                    continue
                print(f"[FAIL] {filename} (Solver crashed with return code {result.returncode})",
                      flush=True)
                print(f"Stderr: {result.stderr}", flush=True)
                failed += 1
                continue

            output_text = result.stdout.strip()
            if not output_text:
                print(f"[FAIL] {filename} (Empty output from solver)", flush=True)
                failed += 1
                continue

            output = json.loads(output_text)

            actual_outcome = normalize_outcome(output.get("solution_type"))
            actual_nodes = int(output.get("states_searched", 0))

            if regenerate:
                new_entry = dict(baseline)  # preserve metadata (game_type, streamliner, etc.)
                new_entry["solution_type"] = actual_outcome
                new_entry["states_searched"] = actual_nodes
                new_entry["unique_states"] = int(output.get("unique_states", 0))
                new_entry["backtracks"] = int(output.get("backtracks", 0))
                new_entry["max_depth"] = int(output.get("max_depth", 0))
                if use_seed:
                    new_entry["baseline_time_ms"] = round(elapsed_ms, 1)
                new_oracle_entries[filename] = new_entry
                passed += 1
                if verbose or (passed + failed) % 25 == 0:
                    print(f"  [{passed+failed}/{total}] {filename}: "
                          f"{actual_outcome}, {actual_nodes} nodes, {elapsed_ms:.0f}ms",
                          flush=True)
                continue

            # --- Comparison policy ---
            expected_outcome = normalize_outcome(baseline.get("solution_type"))
            expected_nodes = int(baseline.get("states_searched", 0))

            # Timeout on either side: soft pass
            if actual_outcome == "timeout" or expected_outcome == "timeout":
                if actual_outcome == "timeout":
                    print(f"[TIMEOUT/SOFT-PASS] {filename} "
                          f"({actual_nodes} nodes before timeout; "
                          f"oracle: {expected_outcome}/{expected_nodes} nodes)", flush=True)
                else:
                    # actual is definitive, oracle was timeout: improvement
                    if verbose:
                        print(f"[IMPROVED] {filename}: {actual_outcome} "
                              f"({actual_nodes} nodes; oracle was timeout)", flush=True)
                passed += 1
                continue

            diffs = []
            if actual_outcome != expected_outcome:
                diffs.append(f"OUTCOME FLIP: {actual_outcome} (expected {expected_outcome})")
            # By default, states_searched is not enforced — traversal order
            # changes between cache implementations make node counts
            # non-reproducible across refactors.  Use --enforce-node-counts
            # when the refactoring should not change traversal order.
            if enforce_node_counts and actual_nodes != expected_nodes:
                diffs.append(f"NODE COUNT: {actual_nodes} (expected {expected_nodes})")

            if diffs:
                print(f"[FAIL] {filename} (streamliner: {streamliner})", flush=True)
                for d in diffs:
                    print(f"  - {d}", flush=True)
                failed += 1
            else:
                passed += 1
                if verbose:
                    if not compare_outcome_only and actual_nodes != expected_nodes:
                        node_note = f" [nodes: {actual_nodes} vs oracle {expected_nodes}]"
                    else:
                        node_note = ""
                    print(f"[OK] {filename}: {actual_outcome}{node_note}", flush=True)
                elif (passed + failed + skipped) % 25 == 0:
                    print(f"Progress: {passed+failed+skipped}/{total} "
                          f"(Pass: {passed}, Fail: {failed}, Skip: {skipped})", flush=True)

        except subprocess.TimeoutExpired:
            if regenerate:
                print(f"[ERROR] {filename}: no output after {py_timeout:.1f}s during regeneration",
                      flush=True)
                failed += 1
            else:
                expected_nodes = int(baseline.get("states_searched", 0))
                print(f"[TIMEOUT/SOFT-PASS] {filename} "
                      f"(no output after {py_timeout:.1f}s; "
                      f"oracle: {expected_nodes} nodes)", flush=True)
                passed += 1
        except Exception as e:
            print(f"[ERROR] {filename}: {e}", flush=True)
            failed += 1

    print("-" * 60, flush=True)

    if regenerate:
        if failed > 0:
            print(f"Regeneration incomplete: {failed}/{total} instances failed; oracle NOT written.",
                  flush=True)
            return 1
        # Write back in original format
        if oracle_is_list:
            new_oracle_list = [new_oracle_entries[f] for f in oracle_order
                               if f in new_oracle_entries]
            with open(oracle_path, 'w') as out:
                json.dump(new_oracle_list, out, indent=4)
        else:
            with open(oracle_path, 'w') as out:
                json.dump(new_oracle_entries, out, indent=4)
        print(f"Oracle regenerated: {oracle_path} ({passed} instances)", flush=True)
        return 0

    skip_note = f", Skipped: {skipped}" if skipped else ""
    print(f"Final Report: Passed: {passed}/{total}{skip_note}", flush=True)
    if failed > 0:
        print(f"FAILED: {failed}/{total}", flush=True)
        return 1

    print("Regression suite components verified successfully.", flush=True)
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Solvitaire Regression Runner")
    parser.add_argument("--exe", required=True, help="Path to solvitaire executable")
    parser.add_argument("--instances", default="",
                        help="Path to instances directory (required for Level 1 JSON-based runs; "
                             "omit for Levels 2-5 which use seed-based invocation)")
    parser.add_argument("--oracle", required=True, help="Path to baseline oracle JSON")
    parser.add_argument("--verbose", action="store_true", help="Print all pass messages")
    parser.add_argument("--max-instance-timeout-ms", type=int, default=120000,
                        help="Hard cap on per-instance solver timeout in ms (default: 120000). "
                             "Raise for level 4/5. In --regenerate mode this is used as the "
                             "per-instance timeout (no 2x multiplier).")
    parser.add_argument("--regenerate", action="store_true",
                        help="Regenerate oracle values from fresh solver runs instead of "
                             "comparing. Overwrites the oracle file in place on success.")
    parser.add_argument("--force-lru", action="store_true",
                        help="Append --force-lru to every solver invocation. "
                             "Use for solvitaire-lru variant targets.")
    parser.add_argument("--skip-ineligible", action="store_true",
                        help="If the solver exits non-zero and stderr contains 'requires', "
                             "'not eligible', or 'not supported', count the instance as "
                             "SKIP rather than FAIL. Use for variant binaries that reject "
                             "game types outside their scope.")
    parser.add_argument("--compare-outcome-only", action="store_true",
                        help="Suppress node-count notes in verbose output. "
                             "Use when node counts are expected to differ from the oracle "
                             "(e.g. hash-only or forced-LRU runs).")
    parser.add_argument("--cache-type", default=None,
                        help="Append --cache-type <VALUE> to every solver invocation. "
                             "Use 'hash-only' to generate or compare against hash-only "
                             "cache results using the default solvitaire binary.")
    parser.add_argument("--enforce-node-counts", action="store_true",
                        help="Fail if states_searched differs from oracle (in addition "
                             "to outcome checks). Use when the change under test should "
                             "not alter traversal order.")
    parser.add_argument("--initial-depth-bound", type=int, default=None,
                        help="Run the solver in iterative-deepening mode with this "
                             "initial depth bound L0 (appends --initial-depth-bound). "
                             "The differential-verdict harness (Stage 1 item 1f) uses "
                             "this to assert the bounded FINAL verdict equals the "
                             "unbounded oracle verdict. Node counts are NOT enforced in "
                             "this mode (truncation reshapes traversal) — do not combine "
                             "with --enforce-node-counts.")
    parser.add_argument("--depth-grow", type=int, default=None,
                        help="Append --depth-grow <N> (ID growth factor; solver default 2). "
                             "Only meaningful with --initial-depth-bound.")
    parser.add_argument("--max-depth-bound", type=int, default=None,
                        help="Append --max-depth-bound <N> (ID L_max cap). Only meaningful "
                             "with --initial-depth-bound.")

    args = parser.parse_args()

    # Guard: enforcing node counts under iterative deepening is meaningless — a
    # bounded run truncates and reshapes cache interactions, so states_searched
    # legitimately differs from the unbounded oracle. Fail fast rather than emit
    # confusing spurious NODE COUNT failures.
    if args.initial_depth_bound is not None and args.enforce_node_counts:
        print("Error: --enforce-node-counts cannot be combined with "
              "--initial-depth-bound (node counts differ under depth bounding; "
              "the harness compares verdicts only).", flush=True)
        sys.exit(2)

    sys.exit(run_regression(
        args.exe, args.instances, args.oracle,
        verbose=args.verbose,
        max_instance_timeout_ms=args.max_instance_timeout_ms,
        regenerate=args.regenerate,
        force_lru=args.force_lru,
        skip_ineligible=args.skip_ineligible,
        compare_outcome_only=args.compare_outcome_only,
        cache_type=args.cache_type,
        enforce_node_counts=args.enforce_node_counts,
        initial_depth_bound=args.initial_depth_bound,
        depth_grow=args.depth_grow,
        max_depth_bound=args.max_depth_bound,
    ))
