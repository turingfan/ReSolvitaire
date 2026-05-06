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

Comparison modes:
  --full            Exact diff of the full event streams.
  --until-evict     Truncate both streams at the first EVICT event in
                    either trace (exclusive), then compare.
  --until-timeout N Keep only events whose operation counter is < N,
                    then compare.  Use this when runs may end at different
                    operation counts (e.g. wall-clock-timed-out searches).

Exit codes:
  0  traces match (within the selected comparison window)
  1  traces diverge; the first differing operation number is printed
"""

import argparse
import os
import subprocess
import sys
import tempfile

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
# Truncation helpers
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
# Comparison
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
# Binary invocation
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

    # Binary mode
    p.add_argument('--binary', metavar='BIN',
                   help='run the same binary twice (one for each trace)')
    p.add_argument('--binary-a', metavar='BIN',
                   help='binary for trace A (use with --binary-b)')
    p.add_argument('--binary-b', metavar='BIN',
                   help='binary for trace B (use with --binary-a)')

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
