#!/usr/bin/env python3
"""
run_tests.py — unified test driver for ReSolvitaire.

Runs the 3-gate build+test workflow (release, trace, debug).  Building
is integral to testing; each gate builds its own config before running
tests.  Use --skip-build to reuse existing binaries.

Usage:
  run_tests.py                    # all 3 gates, unit tests + level 1 regression
  run_tests.py --gate release     # gate 1 only
  run_tests.py --gate trace       # gate 2 only
  run_tests.py --gate debug       # gate 3 only
  run_tests.py --quick            # unit tests only, all 3 configs
  run_tests.py --level 2          # unit tests + regression through level N
  run_tests.py --skip-build       # skip build step (assume binaries exist)
  run_tests.py --dry-run          # print what would run, don't execute

Exit 0 if all selected gates pass, 1 if any gate fails.

Gate definitions:
  Gate 1 (release): ./build.sh --release --unit-tests
                    ctest -R ^unit_tests$
                    ctest -R regression_level1  [and levelN for --level N]

  Gate 2 (trace):   ./build.sh --trace
                    ctest -R ^unit_tests$
                    ctest -R trace_              [all trace targets]

  Gate 3 (debug):   ./build.sh --debug --unit-tests
                    ctest -R ^unit_tests$

With --quick, only the ctest -R ^unit_tests$ step runs in each gate.
"""

import argparse
import os
import subprocess
import sys

# Locate repo root relative to this script (scripts/run_tests.py → repo root)
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BUILD_SCRIPT = os.path.join(REPO_ROOT, 'build.sh')

# Gate order — used when no --gate is specified
ALL_GATES = ['release', 'trace', 'debug']

GATE_CONFIG = {
    'release': {
        'label':     'Gate 1 — Release',
        'build_args': ['--release', '--unit-tests'],
        'build_dir':  'cmake-build-release',
    },
    'trace': {
        'label':     'Gate 2 — Trace',
        'build_args': ['--trace'],
        'build_dir':  'cmake-build-trace',
    },
    'debug': {
        'label':     'Gate 3 — Debug',
        'build_args': ['--debug', '--unit-tests'],
        'build_dir':  'cmake-build-debug',
    },
}


def ctest_steps(gate, level, quick):
    """
    Return a list of (label, ctest_regex) pairs for a gate.

    Each pair drives one `ctest -R <regex> --output-on-failure` invocation.
    """
    steps = [('unit tests', '^unit_tests$')]

    if quick:
        return steps

    if gate == 'release':
        for lvl in range(1, level + 1):
            steps.append((f'regression level {lvl}', f'regression_level{lvl}'))

    elif gate == 'trace':
        # trace_ matches all trace targets (identity + regression L1 and L2)
        steps.append(('trace tests', 'trace_'))

    # debug: unit tests only (no regression targets in debug build)

    return steps


def run_cmd(cmd, dry_run, cwd=None):
    """Print and optionally run a command. Returns exit code (0 on dry-run)."""
    display = ' '.join(cmd)
    cwd_label = f'  (in {os.path.relpath(cwd, REPO_ROOT)})' if cwd else ''
    print(f'  $ {display}{cwd_label}', flush=True)
    if dry_run:
        return 0
    result = subprocess.run(cmd, cwd=cwd)
    return result.returncode


def run_gate(gate, level, quick, skip_build, dry_run):
    """
    Build and test one gate.  Returns True if all steps pass.
    """
    cfg = GATE_CONFIG[gate]
    build_dir = os.path.join(REPO_ROOT, cfg['build_dir'])
    label = cfg['label']

    print(f'\n{"=" * 60}', flush=True)
    print(f'{label}', flush=True)
    print(f'{"=" * 60}', flush=True)

    # Build step
    if skip_build:
        print('  [build skipped]', flush=True)
    else:
        build_cmd = [BUILD_SCRIPT] + cfg['build_args']
        rc = run_cmd(build_cmd, dry_run, cwd=REPO_ROOT)
        if rc != 0:
            print(f'\nBUILD FAILED (exit {rc})', flush=True)
            return False

    # Test steps
    for step_label, regex in ctest_steps(gate, level, quick):
        cmd = ['ctest', '-R', regex, '--output-on-failure']
        print(f'\n  [{step_label}]', flush=True)
        rc = run_cmd(cmd, dry_run, cwd=build_dir)
        if rc != 0:
            print(f'\n  FAILED: {step_label} (exit {rc})', flush=True)
            return False

    return True


def main():
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument('--gate', choices=ALL_GATES,
                   help='run a single gate only (default: all 3)')
    p.add_argument('--quick', action='store_true',
                   help='unit tests only, no regression (all 3 gates unless --gate)')
    p.add_argument('--level', type=int, default=1,
                   help='run regression through level N in release gate (default: 1)')
    p.add_argument('--skip-build', action='store_true',
                   help='skip build step; assume binaries already exist')
    p.add_argument('--dry-run', action='store_true',
                   help='print commands without executing them')
    args = p.parse_args()

    gates = [args.gate] if args.gate else ALL_GATES

    if args.dry_run:
        print('[dry-run: commands will be printed but not executed]', flush=True)

    n_pass = n_fail = 0
    for gate in gates:
        ok = run_gate(gate, args.level, args.quick, args.skip_build, args.dry_run)
        if ok:
            n_pass += 1
        else:
            n_fail += 1
            # Stop on first gate failure — subsequent gates are unreliable
            print(f'\nStopping after gate failure.', flush=True)
            break

    print(f'\n{"=" * 60}', flush=True)
    total = n_pass + n_fail
    if n_fail == 0:
        print(f'All {total} gate(s) passed.', flush=True)
    else:
        skipped = len(gates) - total
        msg = f'{n_pass}/{len(gates)} gate(s) passed, {n_fail} failed'
        if skipped:
            msg += f', {skipped} skipped'
        print(msg, flush=True)

    sys.exit(0 if n_fail == 0 else 1)


if __name__ == '__main__':
    main()
