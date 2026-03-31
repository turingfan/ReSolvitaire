#!/usr/bin/env python3
"""
Extract benchmark NPS metrics and generate comparison reports.

This utility reads benchmark result JSON files (report-*.json) from a directory,
extracts baseline vs current nodes-per-second metrics, calculates speedup factors,
and generates a markdown summary table.

Usage:
    ./scripts/extract_benchmark_results.py [OPTIONS] [benchmark_dir]

Examples:
    # Default: analyze benchmarks/TuesdayNight/
    ./scripts/extract_benchmark_results.py

    # Analyze a specific directory
    ./scripts/extract_benchmark_results.py benchmarks/MyExperiment

    # Save to custom output file
    ./scripts/extract_benchmark_results.py --output results.md benchmarks/Data

    # Show detailed results per game
    ./scripts/extract_benchmark_results.py --verbose

Options:
    -o, --output FILE     Save markdown table to FILE (default: BENCHMARK_RESULTS.md in benchmark_dir)
    -v, --verbose         Print detailed results for each game
    -h, --help            Show this help message
"""

import json
import sys
import os
import argparse
from pathlib import Path
from typing import Dict, List, Tuple
from datetime import datetime


def extract_nps_from_report(report_path: str) -> Tuple[float, float, str]:
    """
    Extract baseline and current NPS from a benchmark report JSON.

    Returns: (baseline_nps, current_nps, game_name)
    Raises: KeyError if required fields missing
    """
    with open(report_path, 'r') as f:
        data = json.load(f)

    baseline_nps = data['results']['baseline']['stats']['aggregate_nps']
    current_nps = data['results']['current']['stats']['aggregate_nps']

    # Extract game name from filename (report-klondike.json -> klondike)
    game_name = Path(report_path).stem.replace('report-', '')

    return baseline_nps, current_nps, game_name


def calculate_speedup(baseline_nps: float, current_nps: float) -> float:
    """Calculate speedup factor (current / baseline)."""
    if baseline_nps == 0:
        return 0.0
    return current_nps / baseline_nps


def generate_markdown_table(results: List[Dict]) -> str:
    """Generate markdown table from extracted results."""
    if not results:
        return "No benchmark results found.\n"

    # Sort by game name for consistent output
    results = sorted(results, key=lambda x: x['game'])

    # Header
    lines = [
        "# Benchmark Results: Nodes Per Second Comparison\n",
        f"**Date:** {datetime.now().strftime('%Y-%m-%d')}  ",
        f"**Total Games:** {len(results)}\n",
        "| Game | Baseline NPS | Current NPS | Speedup Factor |",
        "|------|--------------|-------------|-----------------|"
    ]

    # Data rows
    for r in results:
        baseline = f"{r['baseline_nps']:,.0f}"
        current = f"{r['current_nps']:,.0f}"
        speedup = f"{r['speedup']:.4f}x"
        status = "✓" if r['speedup'] >= 1.0 else "✗"
        lines.append(f"| {r['game']} | {baseline} | {current} | {speedup} {status} |")

    # Summary section
    improved = sum(1 for r in results if r['speedup'] >= 1.0)
    avg_speedup = sum(r['speedup'] for r in results) / len(results)
    min_speedup = min(r['speedup'] for r in results)
    max_speedup = max(r['speedup'] for r in results)

    lines.extend([
        "",
        "## Summary",
        "",
        f"- **Average Speedup:** {avg_speedup:.4f}x",
        f"- **Min Speedup:** {min_speedup:.4f}x",
        f"- **Max Speedup:** {max_speedup:.4f}x",
        f"- **Games Improved:** {improved}/{len(results)}",
        ""
    ])

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Extract benchmark NPS metrics and generate comparison reports.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    parser.add_argument(
        'benchmark_dir',
        nargs='?',
        default='benchmarks/TuesdayNight',
        help='Directory containing report-*.json files (default: benchmarks/TuesdayNight)'
    )
    parser.add_argument(
        '-o', '--output',
        help='Output file for markdown table (default: BENCHMARK_RESULTS.md in benchmark_dir)'
    )
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help='Print detailed results for each game'
    )

    args = parser.parse_args()

    benchmark_dir = Path(args.benchmark_dir)
    if not benchmark_dir.exists():
        print(f"Error: benchmark directory not found: {benchmark_dir}", file=sys.stderr)
        sys.exit(1)

    # Find all report-*.json files
    report_files = sorted(benchmark_dir.glob('report-*.json'))
    if not report_files:
        print(f"No report-*.json files found in {benchmark_dir}", file=sys.stderr)
        sys.exit(1)

    # Extract results
    results = []
    for report_path in report_files:
        try:
            baseline_nps, current_nps, game_name = extract_nps_from_report(str(report_path))
            speedup = calculate_speedup(baseline_nps, current_nps)
            results.append({
                'game': game_name,
                'baseline_nps': baseline_nps,
                'current_nps': current_nps,
                'speedup': speedup
            })
            if args.verbose:
                status = "✓" if speedup >= 1.0 else "✗"
                print(f"{game_name:20s} {speedup:6.4f}x {status}")
        except (json.JSONDecodeError, KeyError) as e:
            print(f"Warning: Failed to parse {report_path}: {e}", file=sys.stderr)
            continue

    if not results:
        print("No valid benchmark results found.", file=sys.stderr)
        sys.exit(1)

    # Generate markdown table
    markdown = generate_markdown_table(results)

    # Determine output file
    if args.output:
        output_file = Path(args.output)
    else:
        output_file = benchmark_dir / 'BENCHMARK_RESULTS.md'

    # Ensure parent directory exists
    output_file.parent.mkdir(parents=True, exist_ok=True)

    # Write output
    with open(output_file, 'w') as f:
        f.write(markdown)

    print(f"✓ Generated {len(results)} benchmark results")
    print(f"✓ Saved to {output_file}")


if __name__ == '__main__':
    main()
