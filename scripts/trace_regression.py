#!/usr/bin/env python3
"""
trace_regression.py — old-vs-new trace regression for Level 1 and Level 2 instances.

For each instance in the oracle:
  1. Run the reference binary with --trace  → tmp_ref.trace
  2. Run the current   binary with --trace  → tmp_cur.trace
  3. Stream-compare both files (skip 6-line header; stop at first TIMEOUT in either).
  4. Clean up temp files; report PASS / FAIL.

Comparison is byte-identical: same search logic must produce the same event sequence.
The TIMEOUT boundary is excluded from comparison because wall-clock speed may differ
between a reference binary and a recompiled current binary.

Usage:
  trace_regression.py --level {1|2}
                      --ref-binary PATH
                      --cur-binary PATH
                      --tests-dir  PATH      # repo tests/ root
                      [--timeout-ms N]       # solver timeout ms (default: 0=none for L1,
                                             #   5000 for L2 to keep traces manageable)
                      [--instances N]        # run only first N instances (smoke mode)
                      [--verbose]

Exit 0 if all pass, 1 if any fail.

Oracle formats:
  Level 1: JSON dict  keyed by filename; values have game_type, streamliner.
           Instance files live at tests/resources/level1/<filename>.
  Level 2: JSON list; entries have instance, streamliner, optional custom_rules.
           No instance files on disk — seed is extracted from filename and passed
           as --random <seed> (same approach as regression_runner.py).
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time

HEADER_LINES = 6  # TRACE, DATE, CMD, GAME, POLICY, <blank>


# ---------------------------------------------------------------------------
# Streaming comparison — only two lines ever in RAM
# ---------------------------------------------------------------------------

def stream_compare(path_ref, path_cur):
    """
    Open both trace files, skip the 6-line header, then read one line at a
    time from each simultaneously.  Stops (success) at the first TIMEOUT
    event in either trace — wall-clock speed may differ between binaries.

    Returns (passed: bool, message: str).
    """
    try:
        fref = open(path_ref, 'r')
    except OSError as exc:
        return False, f"cannot open ref trace: {exc}"
    try:
        fcur = open(path_cur, 'r')
    except OSError as exc:
        fref.close()
        return False, f"cannot open cur trace: {exc}"

    try:
        for _ in range(HEADER_LINES):
            lr = fref.readline()
            lc = fcur.readline()
            if not lr or not lc:
                return False, "trace file truncated before end of header"

        event_idx = 0
        got_event = False

        while True:
            line_ref = fref.readline()
            line_cur = fcur.readline()

            ref_eof = (line_ref == '')
            cur_eof = (line_cur == '')

            if ref_eof and cur_eof:
                break  # both ended cleanly — full agreement

            line_ref = line_ref.rstrip('\n')
            line_cur = line_cur.rstrip('\n')

            # Stop cleanly at the timeout boundary in either trace
            if ' TIMEOUT' in line_ref or ' TIMEOUT' in line_cur:
                break

            if ref_eof != cur_eof:
                longer = 'ref' if cur_eof else 'cur'
                return False, (
                    f"length mismatch at event {event_idx}: "
                    f"{longer} trace is longer"
                )

            got_event = True
            if line_ref != line_cur:
                return False, (
                    f"diverges at event {event_idx}\n"
                    f"  ref: {line_ref}\n"
                    f"  cur: {line_cur}"
                )
            event_idx += 1

        if not got_event:
            return False, "no events in either trace (empty search?)"

        return True, f"{event_idx} events match"

    finally:
        fref.close()
        fcur.close()


# ---------------------------------------------------------------------------
# Solver invocation builder
# ---------------------------------------------------------------------------

def build_cmd(binary, oracle_entry, instances_dir, timeout_ms, trace_path):
    """
    Build the solver command for one instance.

    Returns (cmd: list[str], error: str|None).
    """
    inst = oracle_entry.get('instance') or oracle_entry.get('instance_name', '')
    filename = os.path.basename(inst)

    # Level 2 entries have baseline_time_ms; use --random <seed> to avoid
    # needing instance files on disk (same policy as regression_runner.py).
    use_seed = 'baseline_time_ms' in oracle_entry

    if use_seed:
        m = re.search(r'_(-?\d+)_', filename)
        if not m:
            return None, f"cannot extract seed from filename '{filename}'"
        seed = m.group(1)
        cmd = [binary, '--random', seed, '--json']
    else:
        if not instances_dir:
            return None, "instances_dir required for Level 1"
        instance_path = os.path.join(instances_dir, filename)
        if not os.path.isfile(instance_path):
            return None, f"instance file not found: {instance_path}"
        cmd = [binary, instance_path, '--json']

    # Game type / custom rules
    if 'custom_rules' in oracle_entry:
        cmd.extend(['--custom-rules', oracle_entry['custom_rules']])
    elif 'game_type' in oracle_entry:
        cmd.extend(['--type', oracle_entry['game_type']])
    else:
        # Infer from filename
        if '_seed_' in filename:
            game_type = filename.split('_seed_')[0]
        else:
            game_type = re.split(r'_\d', filename)[0]
        cmd.extend(['--type', game_type])

    streamliner = oracle_entry.get('streamliner', 'none')
    cmd.extend(['--streamliners', streamliner])

    if timeout_ms:
        cmd.extend(['--timeout', str(timeout_ms)])

    cmd.extend(['--trace', trace_path])

    return cmd, None


# ---------------------------------------------------------------------------
# Single-instance driver
# ---------------------------------------------------------------------------

def run_instance(ref_binary, cur_binary, oracle_entry, instances_dir,
                 timeout_ms):
    """
    Run ref and cur binaries on one instance, compare traces.
    Returns (passed: bool, message: str).
    Temp files are always cleaned up.
    """
    tmp_ref = tmp_cur = None
    try:
        fd, tmp_ref = tempfile.mkstemp(suffix='.trace', prefix='trr_')
        os.close(fd)
        fd, tmp_cur = tempfile.mkstemp(suffix='.trace', prefix='trc_')
        os.close(fd)

        py_timeout = (timeout_ms / 1000.0 + 60.0) if timeout_ms else 120.0

        for label, binary, tmp in [('ref', ref_binary, tmp_ref),
                                    ('cur', cur_binary, tmp_cur)]:
            cmd, err = build_cmd(binary, oracle_entry, instances_dir,
                                 timeout_ms, tmp)
            if err:
                return False, f"invocation error ({label}): {err}"

            try:
                r = subprocess.run(cmd, capture_output=True, timeout=py_timeout)
            except subprocess.TimeoutExpired:
                return False, f"{label} binary exceeded Python timeout ({py_timeout:.0f}s)"

            # returncode != 0 is normal for TIMEOUT solutions (solver exits 0 in all
            # cases on this codebase), but guard against crashes.
            if r.returncode != 0:
                return False, (f"{label} binary exited {r.returncode}: "
                               f"{r.stderr.decode(errors='replace')[:200]}")

        return stream_compare(tmp_ref, tmp_cur)

    finally:
        for p in (tmp_ref, tmp_cur):
            if p and os.path.exists(p):
                try:
                    os.unlink(p)
                except OSError:
                    pass


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description='Trace regression: compare reference binary vs current binary')
    ap.add_argument('--level', type=int, choices=[1, 2], required=True,
                    help='Regression level (1 or 2)')
    ap.add_argument('--ref-binary', required=True,
                    help='Path to reference (known-good) trace binary')
    ap.add_argument('--cur-binary', required=True,
                    help='Path to current trace binary under test')
    ap.add_argument('--tests-dir', required=True,
                    help='Repo tests/ root (for oracle and instance files)')
    ap.add_argument('--timeout-ms', type=int, default=0,
                    help='Solver timeout in ms; 0 = none for L1, 5000 default for L2')
    ap.add_argument('--instances', type=int, default=0,
                    help='Limit to first N instances (0 = all)')
    ap.add_argument('--verbose', action='store_true',
                    help='Print PASS lines too, not just FAIL')
    args = ap.parse_args()

    # Validate binaries
    for label, path in [('--ref-binary', args.ref_binary),
                         ('--cur-binary', args.cur_binary)]:
        if not os.path.isfile(path):
            print(f"error: {label} not found: {path}", file=sys.stderr)
            sys.exit(1)
        if not os.access(path, os.X_OK):
            print(f"error: {label} not executable: {path}", file=sys.stderr)
            sys.exit(1)

    # Load oracle
    oracle_path = os.path.join(args.tests_dir, 'oracles', f'level{args.level}.json')
    if not os.path.isfile(oracle_path):
        print(f"error: oracle not found: {oracle_path}", file=sys.stderr)
        sys.exit(1)
    with open(oracle_path) as f:
        raw = json.load(f)

    # Normalize to list
    entries = list(raw.values()) if isinstance(raw, dict) else raw

    if args.instances:
        entries = entries[:args.instances]

    instances_dir = (os.path.join(args.tests_dir, 'resources', f'level{args.level}')
                     if args.level == 1 else None)

    # Default timeout
    timeout_ms = args.timeout_ms
    if timeout_ms == 0 and args.level == 2:
        timeout_ms = 5000  # keep traces manageable for L2

    total = len(entries)
    n_pass = n_fail = 0
    t0 = time.time()

    print(f"Trace regression level {args.level}: {total} instances", flush=True)
    print(f"  ref: {args.ref_binary}", flush=True)
    print(f"  cur: {args.cur_binary}", flush=True)
    print(f"  solver timeout: {timeout_ms}ms" if timeout_ms else
          "  solver timeout: none", flush=True)
    print('-' * 60, flush=True)

    for idx, entry in enumerate(entries, 1):
        inst = entry.get('instance') or entry.get('instance_name', f'unknown_{idx}')
        filename = os.path.basename(inst)

        ok, msg = run_instance(args.ref_binary, args.cur_binary, entry,
                               instances_dir, timeout_ms)

        if ok:
            n_pass += 1
            if args.verbose or idx % 25 == 0:
                print(f"  [{idx:3d}/{total}] PASS {filename} ({msg})", flush=True)
        else:
            n_fail += 1
            print(f"  [{idx:3d}/{total}] FAIL {filename}: {msg}", flush=True)

    elapsed = time.time() - t0
    print('-' * 60, flush=True)
    print(f"Level {args.level} trace regression: "
          f"{n_pass}/{total} passed, {n_fail} failed  ({elapsed:.1f}s)", flush=True)

    sys.exit(0 if n_fail == 0 else 1)


if __name__ == '__main__':
    main()
