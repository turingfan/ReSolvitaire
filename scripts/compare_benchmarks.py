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
            
            # Robust JSON extraction: keep the longest valid JSON block found
            best_json = None
            longest_len = -1
            for i in range(len(output)):
                char = output[i]
                if char in '{[':
                    stack = 0
                    for j in range(i, len(output)):
                        if output[j] == char:
                           stack += 1
                        elif (char == '{' and output[j] == '}') or (char == '[' and output[j] == ']'):
                           stack -= 1
                        
                        if stack == 0:
                            candidate = output[i:j+1]
                            try:
                                parsed = json.loads(candidate)
                                if len(candidate) > longest_len:
                                    best_json = parsed
                                    longest_len = len(candidate)
                            except json.JSONDecodeError:
                                pass
            
            if best_json is not None:
                return best_json
                
            print(f"Failed to extract benchmark JSON from {exe_path}. Raw output snippet:\n{output[:500]}...")
            sys.exit(1)
            
        except Exception as e:
            print(f"Exception during benchmark execution: {str(e)}")
            sys.exit(1)

def measure_standard_candle(reference_exe, calibration_workload):
    """Measures the Hardware Normalization Factor (HNF) by summing the median times of all instances in a fixed workload."""
    if not reference_exe:
        print("\033[93mWARNING: No --reference-exe provided. Hardware Normalization Scores will not be representative.\033[0m")
        return 1.0
    
    if not os.path.exists(calibration_workload):
        print(f"\033[91mError: Calibration workload not found: {calibration_workload}\033[0m")
        sys.exit(1)

    # We run the calibration workload with multiple iterations for stability 
    # Use 3 iterations for the reference solver to minimize noise in the HNF baseline.
    args = ["--benchmark-json", calibration_workload, "--benchmark-iterations", "3"]
    payload = get_json_payload(reference_exe, args)
    
    # The HNF should represent the 'Total Calibration Time' across all instances in the set.
    if isinstance(payload, list):
        # Sum the median times of each instance. Summing provides a much larger, more stable scalar baseline.
        total_us = sum(inst["median_time_us"] for inst in payload)
        return total_us
    else:
        # Fallback for old single-object aggregate format
        return payload["aggregate_stats"]["median_time_us"]

def get_git_hash():
    try:
        result = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True)
        return result.stdout.strip() if result.returncode == 0 else "unknown"
    except Exception:
        return "unknown"

def calculate_median(data):
    if not data:
        return 0.0
    sorted_data = sorted(data)
    n = len(sorted_data)
    if n % 2 == 1:
        return sorted_data[n // 2]
    else:
        return (sorted_data[n // 2 - 1] + sorted_data[n // 2]) / 2.0

def main():
    parser = argparse.ArgumentParser(description="ReSolvitaire Python Orchestrator: Hardware Normalization & Median Statistics")
    parser.add_argument("--baseline-exe", required=True, help="Path to the baseline/master executable")
    parser.add_argument("--current-exe", required=True, help="Path to the current working executable to test")
    parser.add_argument("--reference-exe", default=None, help="Path to a stable, older reference solver for hardware normalization")
    parser.add_argument("--calibration-workload", default="tests/oracles/level1.json", help="Path to the fixed regression JSON used for system calibration")
    parser.add_argument("--out-report", default="benchmark_report.json", help="Path to save the JSON diagnostic report")
    parser.add_argument("benchmark_args", nargs=argparse.REMAINDER, help="Arguments to pass through to the solvitaire benchmark engine")
    
    args = parser.parse_args()
    
    if not os.path.isfile(args.baseline_exe):
        print(f"Baseline executable not found: {args.baseline_exe}")
        sys.exit(1)
        
    if not os.path.isfile(args.current_exe):
        print(f"Current executable not found: {args.current_exe}")
        sys.exit(1)
        
    print(f"--- ReSolvitaire Orchestrator (Hardware Normalization) ---")
    print(f"Establishing hardware normalization factor using {args.reference_exe or 'NONE'} ...")
    hnf = measure_standard_candle(args.reference_exe, args.calibration_workload)
    
    if args.reference_exe:
        print(f"Hardware Normalization Factor (HNF) established: {hnf:.2f} us\n")
    else:
        print(f"Normalization Factor (HNF) defaulted to 1.0 (No normalization active)\n")
    
    forward_args = args.benchmark_args
    if forward_args and forward_args[0] == "--":
        forward_args = forward_args[1:]
        
    if not forward_args:
        forward_args = ["--type", "klondike", "--benchmark-seeds", "1", "50", "--benchmark-iterations", "1", "--benchmark-warmup", "1"]
        
    print(f"Running baseline benchmark: {args.baseline_exe} {' '.join(forward_args)}")
    baseline_payload = get_json_payload(args.baseline_exe, forward_args)
    
    print(f"Running current benchmark: {args.current_exe} {' '.join(forward_args)}")
    current_payload = get_json_payload(args.current_exe, forward_args)
    
    # Check if we have the new array-based format (list of instances)
    if isinstance(baseline_payload, list) and isinstance(current_payload, list):
        # Result pairing logic
        baseline_map = {item["instance"]: item for item in baseline_payload}
        speedup_ratios = []
        node_ratios = []
        combined_results = []
        
        for item in current_payload:
            name = item["instance"]
            if name in baseline_map:
                base = baseline_map[name]
                ratio = item["median_time_us"] / base["median_time_us"] if base["median_time_us"] > 0 else 1.0
                n_ratio = item["median_nodes"] / base["median_nodes"] if base["median_nodes"] > 0 else 1.0
                
                speedup_ratios.append(ratio)
                node_ratios.append(n_ratio)
                combined_results.append({
                    "name": name,
                    "baseline": base,
                    "current": item,
                    "speedup_ratio": ratio,
                    "node_ratio": n_ratio
                })
        
        median_speedup = calculate_median(speedup_ratios)
        median_node_ratio = calculate_median(node_ratios)
        
        report = {
            "metadata": {
                "date": datetime.datetime.now().isoformat(),
                "machine_id": platform.node(),
                "git_hash": get_git_hash(),
                "hardware_normalization_factor_us": hnf,
                "mode": "paired-instances"
            },
            "benchmark_workload": " ".join(forward_args),
            "results": combined_results,
            "overall": {
                "median_speedup_ratio": median_speedup,
                "median_node_ratio": median_node_ratio
            }
        }
        
        print("\n================ PAIRED INSTANCE BENCHMARK ================\n")
        print(f"Workload: {' '.join(forward_args)}")
        print(f"Total instances matched: {len(combined_results)}\n")
        
        print(f"--- Hardware Normalized ---")
        print(f"Reference Solver HNF:        {hnf:.2f} us")
        print(f"Baseline Median Ratio:       {median_speedup:.4f}x (Relative to reference HNF if provided)")
        
        print(f"\n--- Median Ratios (Across all instances) ---")
        color = "\033[91m" if median_speedup > 1.05 else ("\033[92m" if median_speedup < 0.95 else "")
        reset = "\033[0m"
        print(f"Median Time Ratio: {color}{median_speedup:.4f}x{reset} (Values > 1.0 indicate regression)")
        
        n_color = "\033[92m" if median_node_ratio < 0.99 else ("\033[91m" if median_node_ratio > 1.01 else "")
        print(f"Median Node Ratio: {n_color}{median_node_ratio:.4f}x{reset}\n")

        if combined_results:
            print(f"--- Sample Instances ---")
            for r in combined_results[:5]:
                print(f"{r['name']:<30}: {r['speedup_ratio']:.4f}x time, {r['node_ratio']:.4f}x nodes")

    else:
        # Fallback to old aggregate_stats logic
        baseline_stats = baseline_payload["aggregate_stats"]
        current_stats = current_payload["aggregate_stats"]
        
        baseline_val = baseline_stats["median_time_us"]
        current_val = current_stats["median_time_us"]
        
        speedup_ratio = current_val / baseline_val if baseline_val > 0 else 1.0
        normalized_sys_score = current_val / hnf if hnf > 0 else 0.0
        baseline_normalized_score = baseline_val / hnf if hnf > 0 else 0.0

        report = {
            "metadata": {
                "date": datetime.datetime.now().isoformat(),
                "machine_id": platform.node(),
                "git_hash": get_git_hash(),
                "hardware_normalization_factor_us": hnf,
                "mode": "aggregate-median"
            },
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
        
        print("\n================ AGGREGATE STATS BENCHMARK ================\n")
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
        print(f"Reference Solver HNF:        {hnf:.2f} us")
        print(f"Baseline Normalized Score:   {baseline_normalized_score:.4f}")
        print(f"Current Normalized Score:    {normalized_sys_score:.4f}\n")
        
        print(f"--- Comparison (Median-Based) ---")
        color = "\033[91m" if speedup_ratio > 1.05 else ("\033[92m" if speedup_ratio < 0.95 else "")
        reset = "\033[0m"
        print(f"Time Ratio (Current/Baseline): {color}{speedup_ratio:.4f}x{reset} (Values > 1.0 indicate regression)")
        
        node_ratio = current_stats["median_nodes"] / baseline_stats["median_nodes"] if baseline_stats["median_nodes"] > 0 else 1.0
        n_color = "\033[92m" if node_ratio < 0.99 else ("\033[91m" if node_ratio > 1.01 else "")
        print(f"Node Ratio (Current/Baseline): {n_color}{node_ratio:.4f}x{reset}")
        median_speedup = speedup_ratio # For the final verdict

    with open(args.out_report, 'w') as f:
        json.dump(report, f, indent=4)
        
    if median_speedup < 0.98:
        print(f"Verdict: Current build is FASTER by {((1.0 - median_speedup) * 100):.2f}%")
    elif median_speedup > 1.02:
        print(f"Verdict: Current build is SLOWER (Regression) by {((median_speedup - 1.0) * 100):.2f}%")
    else:
        print(f"Verdict: No significant performance change within 2% noise margin.")
        
    print(f"\nReport written to {args.out_report}")

if __name__ == "__main__":
    main()
