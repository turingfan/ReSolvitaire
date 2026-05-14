#!/usr/bin/env python3
"""
compare_traces.py — compare two solvitaire search trace files.

Strips the 6-line header (TRACE, DATE, CMD, GAME, POLICY, blank line)
and then compares the event streams according to the chosen mode.

FILE MODE (two pre-existing trace files):
  compare_traces.py --full         <trace_a> <trace_b>
  compare_traces.py --until-evict  <trace_a> <trace_b>
  compare_traces.py --until-timeout <N> <trace_a> <trace_b>

BINARY MODE (runs solver binaries, writes traces to temp files).
  Solver args must follow -- to avoid ambiguity with script flags:
  compare_traces.py --full --binary <bin> -- [solver args...]
  compare_traces.py --full --binary-a <binA> --binary-b <binB> -- [solver args...]
  (--until-evict and --until-timeout work in binary mode too)

REGRESSION MODE (batch: one ref binary vs one cur binary across all oracle instances):
  compare_traces.py --regression
                    --level {1|2}
                    --binary-a <ref-binary>
                    --binary-b <cur-binary>
                    --tests-dir <repo-tests-root>
                    [--timeout-ms N]   # default: 0 (none) for L1, 5000 for L2
                    [--instances N]    # run only first N instances (smoke mode)
                    [--verbose]
  Exit 0 if all pass, 1 if any fail.

Comparison modes:
  --full            Exact diff of the full event streams.
  --until-evict     Truncate both streams at the first EVICT event in
                    either trace (exclusive), then compare.
  --until-timeout N Keep only events whose operation counter is < N,
                    then compare.  Use this when runs may end at different
                    operation counts (e.g. wall-clock-timed-out searches).
  --regression      Batch regression: run both binaries on each oracle
                    instance, stream-compare traces, stop at TIMEOUT boundary.

Exit codes:
  0  traces match (within the selected comparison window)
  1  traces diverge; the first differing operation number is printed
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
# Trace I/O
# ---------------------------------------------------------------------------

def read_events(path):
    """Read a trace file and return its event lines (after the header)."""
    try:
        with open(path, 'r') as fh:
            raw = fh.read().splitlines()
    except OSError as exc:
        print(f"error: cannot read trace file '{path}': {exc}", file=sys.stderr)
        sys.exit(1)
    if len(raw) < HEADER_LINES:
        print(
            f"error: trace file '{path}' has only {len(raw)} lines "
            f"(expected >= {HEADER_LINES} header lines)",
            file=sys.stderr,
        )
        sys.exit(1)
    return raw[HEADER_LINES:]


def event_op(line):
    """Return the operation counter from an event line (first 10 chars)."""
    try:
        return int(line[:10])
    except ValueError:
        return -1


# ---------------------------------------------------------------------------
# Truncation helpers (used by file/binary modes)
# ---------------------------------------------------------------------------

def first_evict_index(events):
    """Return the index of the first EVICT event, or len(events) if none."""
    for i, line in enumerate(events):
        parts = line.split()
        if len(parts) >= 2 and parts[1] == 'EVICT':
            return i
    return len(events)


def truncate_until_evict(events_a, events_b):
    """Truncate both event lists to the prefix before either has an EVICT."""
    ia = first_evict_index(events_a)
    ib = first_evict_index(events_b)
    cutoff = min(ia, ib)
    return events_a[:cutoff], events_b[:cutoff]


def truncate_until_timeout(events, n):
    """Keep only events whose operation counter is < n."""
    result = []
    for line in events:
        op = event_op(line)
        if op < 0:
            result.append(line)   # malformed line: include and keep going
        elif op >= n:
            break
        else:
            result.append(line)
    return result


# ---------------------------------------------------------------------------
# Comparison (used by file/binary modes)
# ---------------------------------------------------------------------------

def compare_events(events_a, events_b, mode, n=None):
    """
    Compare two event lists.

    Returns (match: bool, first_diff_op: int or None).
    On divergence, first_diff_op is the operation counter of the first
    differing line (or -1 if the counter cannot be parsed).
    """
    if mode == 'until_evict':
        events_a, events_b = truncate_until_evict(events_a, events_b)
    elif mode == 'until_timeout':
        events_a = truncate_until_timeout(events_a, n)
        events_b = truncate_until_timeout(events_b, n)
    # 'full': use as-is

    length = min(len(events_a), len(events_b))
    for i in range(length):
        if events_a[i] != events_b[i]:
            op = event_op(events_a[i])
            return False, op

    if len(events_a) != len(events_b):
        shorter = length
        longer = events_a if len(events_a) > shorter else events_b
        op = event_op(longer[shorter])
        return False, op

    return True, None


# ---------------------------------------------------------------------------
# Streaming comparison (used by regression mode — O(1) memory)
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
# Regression mode: solver invocation builder
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


def run_regression(args):
    """Main loop for --regression mode."""
    ref_binary = args.binary_a
    cur_binary = args.binary_b

    for label, path in [('--binary-a (ref)', ref_binary),
                         ('--binary-b (cur)', cur_binary)]:
        if not path:
            print(f"error: --regression requires {label}", file=sys.stderr)
            sys.exit(1)
        if not os.path.isfile(path):
            print(f"error: {label} not found: {path}", file=sys.stderr)
            sys.exit(1)
        if not os.access(path, os.X_OK):
            print(f"error: {label} not executable: {path}", file=sys.stderr)
            sys.exit(1)

    if not args.tests_dir:
        print("error: --regression requires --tests-dir", file=sys.stderr)
        sys.exit(1)
    if not args.level:
        print("error: --regression requires --level", file=sys.stderr)
        sys.exit(1)

    oracle_path = os.path.join(args.tests_dir, 'oracles',
                               f'level{args.level}.json')
    if not os.path.isfile(oracle_path):
        print(f"error: oracle not found: {oracle_path}", file=sys.stderr)
        sys.exit(1)
    with open(oracle_path) as f:
        raw = json.load(f)

    entries = list(raw.values()) if isinstance(raw, dict) else raw

    if args.instances:
        entries = entries[:args.instances]

    instances_dir = (os.path.join(args.tests_dir, 'resources',
                                  f'level{args.level}')
                     if args.level == 1 else None)

    timeout_ms = args.timeout_ms
    if timeout_ms == 0 and args.level == 2:
        timeout_ms = 5000  # keep traces manageable for L2

    total = len(entries)
    n_pass = n_fail = 0
    t0 = time.time()

    print(f"Trace regression level {args.level}: {total} instances", flush=True)
    print(f"  ref: {ref_binary}", flush=True)
    print(f"  cur: {cur_binary}", flush=True)
    print(f"  solver timeout: {timeout_ms}ms" if timeout_ms else
          "  solver timeout: none", flush=True)
    print('-' * 60, flush=True)

    for idx, entry in enumerate(entries, 1):
        inst = entry.get('instance') or entry.get('instance_name',
                                                   f'unknown_{idx}')
        filename = os.path.basename(inst)

        ok, msg = run_instance(ref_binary, cur_binary, entry,
                               instances_dir, timeout_ms)

        if ok:
            n_pass += 1
            if args.verbose or idx % 25 == 0:
                print(f"  [{idx:3d}/{total}] PASS {filename} ({msg})",
                      flush=True)
        else:
            n_fail += 1
            print(f"  [{idx:3d}/{total}] FAIL {filename}: {msg}", flush=True)

    elapsed = time.time() - t0
    print('-' * 60, flush=True)
    print(f"Level {args.level} trace regression: "
          f"{n_pass}/{total} passed, {n_fail} failed  ({elapsed:.1f}s)",
          flush=True)

    sys.exit(0 if n_fail == 0 else 1)


# ---------------------------------------------------------------------------
# Binary invocation (used by file/binary modes)
# ---------------------------------------------------------------------------

def run_binary(binary, solver_args, trace_path):
    """Invoke *binary* with *solver_args* and --trace *trace_path*."""
    cmd = [binary] + solver_args + ['--trace', trace_path]
    # We don't check the return code: the trace content is what matters.
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_parser():
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    # Comparison mode (exactly one required)
    mode_grp = p.add_mutually_exclusive_group(required=True)
    mode_grp.add_argument(
        '--full', action='store_true',
        help='exact diff of the full event streams',
    )
    mode_grp.add_argument(
        '--until-evict', action='store_true',
        help='compare until first EVICT in either trace (exclusive)',
    )
    mode_grp.add_argument(
        '--until-timeout', type=int, metavar='N',
        help='compare only operations with counter < N',
    )
    mode_grp.add_argument(
        '--regression', action='store_true',
        help='batch regression: stream-compare ref vs cur across all oracle instances',
    )

    # Binary / regression mode
    p.add_argument('--binary', metavar='BIN',
                   help='run the same binary twice (one for each trace; file/binary mode)')
    p.add_argument('--binary-a', metavar='BIN',
                   help='binary for trace A / ref binary (binary or regression mode)')
    p.add_argument('--binary-b', metavar='BIN',
                   help='binary for trace B / cur binary (binary or regression mode)')

    # Regression-specific flags
    p.add_argument('--level', type=int, choices=[1, 2],
                   help='regression level (regression mode only)')
    p.add_argument('--tests-dir', metavar='PATH',
                   help='repo tests/ root for oracle and instance files (regression mode only)')
    p.add_argument('--timeout-ms', type=int, default=0,
                   help='solver timeout in ms; 0 = none for L1, 5000 default for L2 (regression mode only)')
    p.add_argument('--instances', type=int, default=0,
                   help='limit to first N instances, 0 = all (regression mode only)')
    p.add_argument('--verbose', action='store_true',
                   help='print PASS lines too, not just FAIL (regression mode only)')

    # File mode: positional trace paths (only in file mode, before any --)
    p.add_argument('traces', nargs='*',
                   help='trace file paths (file mode only)')
    return p


def main():
    # Split argv at '--' to separate script flags from solver args.
    # Everything before '--' is parsed by argparse; everything after is
    # passed directly to the solver binary (binary mode only).
    raw = sys.argv[1:]
    if '--' in raw:
        sep = raw.index('--')
        script_argv = raw[:sep]
        solver_args = raw[sep + 1:]
    else:
        script_argv = raw
        solver_args = []

    p = build_parser()
    args = p.parse_args(script_argv)

    # Regression mode: delegate entirely to run_regression().
    if args.regression:
        run_regression(args)
        return  # run_regression calls sys.exit

    # Determine comparison mode string.
    if args.full:
        mode = 'full'
    elif args.until_evict:
        mode = 'until_evict'
    else:
        mode = 'until_timeout'

    # Determine trace sources.
    in_binary_mode = bool(args.binary or args.binary_a or args.binary_b)

    if in_binary_mode:
        binary_a = args.binary or args.binary_a
        binary_b = args.binary or args.binary_b
        if not binary_a:
            p.error('binary mode requires --binary or --binary-a')
        if not binary_b:
            p.error('binary mode requires --binary or --binary-b')
        if not solver_args:
            p.error('binary mode requires solver args after --')

        # Write traces to temp files, then compare.
        fd_a, path_a = tempfile.mkstemp(suffix='.trace')
        fd_b, path_b = tempfile.mkstemp(suffix='.trace')
        os.close(fd_a)
        os.close(fd_b)
        try:
            run_binary(binary_a, solver_args, path_a)
            run_binary(binary_b, solver_args, path_b)
            events_a = read_events(path_a)
            events_b = read_events(path_b)
        finally:
            os.unlink(path_a)
            os.unlink(path_b)

    else:
        if solver_args:
            p.error('solver args after -- are only valid in binary mode')
        if len(args.traces) != 2:
            p.error('file mode requires exactly two positional trace file arguments')
        events_a = read_events(args.traces[0])
        events_b = read_events(args.traces[1])

    match, first_diff_op = compare_events(
        events_a, events_b, mode, n=args.until_timeout
    )

    if match:
        print('Traces match.')
        sys.exit(0)
    else:
        print(f'Traces diverge at operation {first_diff_op}.')
        sys.exit(1)


if __name__ == '__main__':
    main()
