#!/usr/bin/env python3

import argparse
import subprocess
import json
import datetime
import platform
import os
import sys

import tempfile

def get_json_payload(exe_path, extra_args):
    """Runs the benchmark and streams output directly to a temporary file, then parses it."""
    cmd = [exe_path] + extra_args
    
    with tempfile.NamedTemporaryFile(mode='w+', delete=True) as tmp:
        try:
            # Stream stdout to the temporary file
            process = subprocess.Popen(cmd, stdout=tmp, stderr=subprocess.PIPE, text=True)
            stdout, stderr = process.communicate()
            
            if process.returncode != 0:
                print(f"Error running benchmark: {' '.join(cmd)}")
                print(f"Stderr: {stderr}")
                sys.exit(1)
            
            # Seek to search for JSON in the file
            tmp.seek(0)
            output = tmp.read()
            
            # Robust JSON extraction: look for the last valid brace-enclosed block
            best_json = None
            stack = 0
            start_idx = -1
            
            for i, char in enumerate(output):
                if char == '{':
                    if stack == 0:
                        start_idx = i
                    stack += 1
                elif char == '}':
                    stack -= 1
                    if stack == 0 and start_idx != -1:
                        candidate = output[start_idx:i+1]
                        try:
                            parsed = json.loads(candidate)
                            if isinstance(parsed, dict) and "aggregate_stats" in parsed:
                                best_json = parsed
                        except json.JSONDecodeError:
                            pass
            
            if best_json:
                return best_json
                
            print(f"Failed to extract benchmark JSON from {exe_path}. Raw output snippet:\n{output[:500]}...")
            sys.exit(1)
            
        except Exception as e:
            print(f"Exception during benchmark execution: {str(e)}")
            sys.exit(1)

def measure_standard_candle(candle_exe):
    # Hardcoded simple workload for the standard candle: Klondike, seeds 1 to 5, 5 iterations
    args = ["--type", "klondike", "--benchmark-seeds", "1", "5", "--benchmark-iterations", "3", "--benchmark-warmup", "1"]    
    payload = get_json_payload(candle_exe, args)
    # Using median for more robust candle measurement
    return payload["aggregate_stats"]["median_time_us"]

def get_git_hash():
    try:
        result = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True)
        return result.stdout.strip() if result.returncode == 0 else "unknown"
    except Exception:
        return "unknown"

def main():
    parser = argparse.ArgumentParser(description="ReSolvitaire Python Orchestrator: Streaming JSON & Median Statistics")
    parser.add_argument("--baseline-exe", required=True, help="Path to the baseline baseline/master executable")
    parser.add_argument("--current-exe", required=True, help="Path to the current working executable to test")
    parser.add_argument("--candle-exe", default=None, help="Path to the executable to measure the standard candle. Defaults to baseline-exe.")
    parser.add_argument("--out-report", default="benchmark_report.json", help="Path to save the JSON diagnostic report")
    parser.add_argument("benchmark_args", nargs=argparse.REMAINDER, help="Arguments to pass through to the solvitaire benchmark engine")
    
    args = parser.parse_args()
    
    candle_exe = args.candle_exe if args.candle_exe else args.baseline_exe
    
    if not os.path.isfile(args.baseline_exe):
        print(f"Baseline executable not found: {args.baseline_exe}")
        sys.exit(1)
        
    if not os.path.isfile(args.current_exe):
        print(f"Current executable not found: {args.current_exe}")
        sys.exit(1)
        
    print(f"--- ReSolvitaire Orchestrator (Streaming & Median-Based) ---")
    print(f"Measuring standard candle using {candle_exe} ...")
    candle_median_us = measure_standard_candle(candle_exe)
    print(f"Standard candle measured (median): {candle_median_us:.2f} us\n")
    
    forward_args = args.benchmark_args
    if forward_args and forward_args[0] == "--":
        forward_args = forward_args[1:]
        
    if not forward_args:
        forward_args = ["--type", "klondike", "--benchmark-seeds", "1", "50", "--benchmark-iterations", "1", "--benchmark-warmup", "1"]
        
    print(f"Running baseline benchmark: {args.baseline_exe} {' '.join(forward_args)}")
    baseline_payload = get_json_payload(args.baseline_exe, forward_args)
    
    print(f"Running current benchmark: {args.current_exe} {' '.join(forward_args)}")
    current_payload = get_json_payload(args.current_exe, forward_args)
    
    baseline_stats = baseline_payload["aggregate_stats"]
    current_stats = current_payload["aggregate_stats"]
    
    # Priority: Use medians for comparison
    baseline_val = baseline_stats["median_time_us"]
    current_val = current_stats["median_time_us"]
    
    speedup_ratio = current_val / baseline_val if baseline_val > 0 else 1.0
    
    normalized_sys_score = current_val / candle_median_us if candle_median_us > 0 else 0.0
    baseline_normalized_score = baseline_val / candle_median_us if candle_median_us > 0 else 0.0

    metadata = {
        "date": datetime.datetime.now().isoformat(),
        "machine_id": platform.node(),
        "git_hash": get_git_hash(),
        "standard_candle_median_us": candle_median_us,
        "mode": "median-based"
    }
    
    report = {
        "metadata": metadata,
        "benchmark_workload": " ".join(forward_args),
        "results": {
            "baseline": {
                "executable": args.baseline_exe,
                "stats": baseline_stats,
                "hardware_normalized_score": baseline_normalized_score
            },
            "current": {
                "executable": args.current_exe,
                "stats": current_stats,
                "hardware_normalized_score": normalized_sys_score
            },
            "comparison": {
                "median_speedup_ratio": speedup_ratio,
                "median_node_ratio": current_stats["median_nodes"] / baseline_stats["median_nodes"] if baseline_stats["median_nodes"] > 0 else 1.0
            }
        }
    }
    
    with open(args.out_report, 'w') as f:
        json.dump(report, f, indent=4)
        
    print("\n================ BENCHMARK REPORT ================\n")
    print(f"Workload: {' '.join(forward_args)}\n")
    
    print(f"--- Timing (Median us) ---")
    print(f"Baseline: {baseline_stats['median_time_us']:.2f} (Mean: {baseline_stats['mean_time_us']:.2f}, SD: {baseline_stats['sd_time_us']:.2f})")
    print(f"Current:  {current_stats['median_time_us']:.2f} (Mean: {current_stats['mean_time_us']:.2f}, SD: {current_stats['sd_time_us']:.2f})\n")

    print(f"--- Nodes (Median) ---")
    print(f"Baseline: {baseline_stats['median_nodes']:.2f} (Mean: {baseline_stats['mean_nodes']:.2f})")
    print(f"Current:  {current_stats['median_nodes']:.2f} (Mean: {current_stats['mean_nodes']:.2f})")
    print(f"Baseline NPS: {baseline_stats['nodes_per_second']:.2f} nodes/sec")
    print(f"Current NPS:  {current_stats['nodes_per_second']:.2f} nodes/sec\n")
    
    print(f"--- Hardware Normalized ---")
    print(f"Standard Candle (Median):    {candle_median_us:.2f} us")
    print(f"Baseline Normalized Score:   {baseline_normalized_score:.4f}")
    print(f"Current Normalized Score:    {normalized_sys_score:.4f}\n")
    
    print(f"--- Comparison (Median-Based) ---")
    color = "\033[91m" if speedup_ratio > 1.05 else ("\033[92m" if speedup_ratio < 0.95 else "")
    reset = "\033[0m"
    print(f"Time Ratio (Current/Baseline): {color}{speedup_ratio:.4f}x{reset} (Values > 1.0 indicate regression)")
    
    node_ratio = current_stats["median_nodes"] / baseline_stats["median_nodes"] if baseline_stats["median_nodes"] > 0 else 1.0
    n_color = "\033[92m" if node_ratio < 0.99 else ("\033[91m" if node_ratio > 1.01 else "")
    print(f"Node Ratio (Current/Baseline): {n_color}{node_ratio:.4f}x{reset}")

    if speedup_ratio < 0.98:
        print(f"Verdict: Current build is FASTER by {((1.0 - speedup_ratio) * 100):.2f}%")
    elif speedup_ratio > 1.02:
        print(f"Verdict: Current build is SLOWER (Regression) by {((speedup_ratio - 1.0) * 100):.2f}%")
    else:
        print(f"Verdict: No significant performance change within 2% noise margin.")
        
    print(f"\nReport written to {args.out_report}")

if __name__ == "__main__":
    main()

if __name__ == "__main__":
    main()
