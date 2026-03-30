#!/usr/bin/env python3

import argparse
import subprocess
import json
import datetime
import platform
import os
import sys
import re
import time
import resource

import tempfile

# Global to store detailed timing from legacy solver
legacy_timing_details = {}

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
                # Try to measure system memory from a single representative seed
                # This avoids inflating memory usage from running many instances
                single_seed_cmd = None

                if "--benchmark-seeds" in cmd:
                    # For seed-based runs, measure just the first seed
                    idx = cmd.index("--benchmark-seeds")
                    first_seed = cmd[idx + 1]
                    single_seed_cmd = [exe_path, "--type", "klondike", "--random", first_seed]
                else:
                    # For JSON-based runs, measure a single seed as representative
                    # Extract game type if available
                    game_type = "klondike"
                    if "--type" in cmd:
                        idx = cmd.index("--type")
                        if idx + 1 < len(cmd):
                            game_type = cmd[idx + 1]
                    single_seed_cmd = [exe_path, "--type", game_type, "--random", "1"]

                system_memory = None
                if single_seed_cmd:
                    _, _, system_memory = get_detailed_timing_and_memory(single_seed_cmd)

                if system_memory:
                    if isinstance(best_json, list):
                        for item in best_json:
                            item["system_memory_bytes"] = system_memory
                    elif isinstance(best_json, dict) and "aggregate_stats" in best_json:
                        best_json["aggregate_stats"]["system_memory_bytes"] = system_memory

                return best_json

            print(f"Failed to extract benchmark JSON from {exe_path}. Raw output snippet:\n{output[:500]}...")
            sys.exit(1)

        except Exception as e:
            print(f"Exception during benchmark execution: {str(e)}")
            sys.exit(1)

def run_with_timing_and_memory(cmd, timeout=120):
    """Runs a command and returns (internal_time_ms, wall_time_ms, user_time_ms, sys_time_ms, peak_memory_bytes)."""
    start_time = time.time()
    try:
        process = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        end_time = time.time()

        output = process.stdout + process.stderr
        wall_time_ms = (end_time - start_time) * 1000  # Wall-clock time

        # Parse internal timing if available: "Time Taken (milliseconds): <N>"
        internal_time_ms = None
        match = re.search(r'Time Taken \(milliseconds\):\s*([\d.]+)', output)
        if match:
            internal_time_ms = float(match.group(1))

        # Get detailed timing and memory from /usr/bin/time
        user_time_ms, sys_time_ms, peak_memory_bytes = get_detailed_timing_and_memory(cmd, timeout)

        return internal_time_ms, wall_time_ms, user_time_ms, sys_time_ms, peak_memory_bytes
    except subprocess.TimeoutExpired:
        return None, None, None, None, None
    except Exception as e:
        print(f"Warning: Error running command: {str(e)}")
        return None, None, None, None, None

def get_detailed_timing_and_memory(cmd, timeout=120):
    """Uses /usr/bin/time to get user time, system time, and peak memory."""
    try:
        if sys.platform == "darwin":
            # macOS: use 'time -l' to get detailed stats
            time_cmd = ["/usr/bin/time", "-l"] + cmd
            result = subprocess.run(time_cmd, capture_output=True, text=True, timeout=timeout)
            stderr = result.stderr

            # Parse: "X.XX real   Y.YY user   Z.ZZ sys"
            time_match = re.search(r'([\d.]+)\s+real\s+([\d.]+)\s+user\s+([\d.]+)\s+sys', stderr)
            user_time_ms = None
            sys_time_ms = None
            if time_match:
                user_time_ms = float(time_match.group(2)) * 1000  # Convert to ms
                sys_time_ms = float(time_match.group(3)) * 1000

            # Parse memory: number followed by "maximum resident set size"
            mem_match = re.search(r'(\d+)\s*maximum resident set size', stderr, re.MULTILINE)
            peak_memory_bytes = int(mem_match.group(1)) if mem_match else None

            return user_time_ms, sys_time_ms, peak_memory_bytes
        else:
            # Linux: use 'time -v' for verbose output
            time_cmd = ["/usr/bin/time", "-v"] + cmd
            result = subprocess.run(time_cmd, capture_output=True, text=True, timeout=timeout)
            stderr = result.stderr

            # Parse "User time (seconds): X.XX" and "System time (seconds): Y.YY"
            user_match = re.search(r'User time \(seconds\): ([\d.]+)', stderr)
            sys_match = re.search(r'System time \(seconds\): ([\d.]+)', stderr)

            user_time_ms = float(user_match.group(1)) * 1000 if user_match else None
            sys_time_ms = float(sys_match.group(1)) * 1000 if sys_match else None

            # Parse "Maximum resident set size (kbytes): <KB>"
            mem_match = re.search(r'Maximum resident set size \(kbytes\): (\d+)', stderr)
            peak_memory_bytes = int(mem_match.group(1)) * 1024 if mem_match else None

            return user_time_ms, sys_time_ms, peak_memory_bytes
    except Exception as e:
        pass  # Silently fail; detailed timing is optional

    return None, None, None

def estimate_process_memory(cmd, timeout=120):
    """Runs command with /usr/bin/time to estimate peak memory. Platform-specific."""
    try:
        # Detect platform and use appropriate time command
        if sys.platform == "darwin":
            # macOS: use 'time -l' to get max resident set size (in bytes)
            time_cmd = ["/usr/bin/time", "-l"] + cmd
            result = subprocess.run(time_cmd, capture_output=True, text=True, timeout=timeout)
            # macOS time outputs: number followed by "maximum resident set size"
            match = re.search(r'(\d+)\s*maximum resident set size', result.stderr, re.MULTILINE)
            if match:
                return int(match.group(1))  # Already in bytes on macOS
        else:
            # Linux: use 'time -v' for verbose output
            time_cmd = ["/usr/bin/time", "-v"] + cmd
            result = subprocess.run(time_cmd, capture_output=True, text=True, timeout=timeout)
            # Linux time outputs "Maximum resident set size (kbytes): <KB>"
            match = re.search(r'Maximum resident set size \(kbytes\): (\d+)', result.stderr)
            if match:
                return int(match.group(1)) * 1024  # Convert KB to bytes
    except Exception as e:
        pass  # Silently fail; memory measurement is optional

    return None

def get_json_payload_legacy(exe_path, seed_start, seed_end, game_type="klondike"):
    """Runs legacy solver and extracts timing data with wall-clock, CPU, and memory metrics."""
    total_internal_time_us = 0
    total_wall_time_us = 0
    total_user_time_us = 0
    total_sys_time_us = 0
    max_memory_bytes = 0
    seed_count = 0

    for seed in range(seed_start, seed_end + 1):
        cmd = [exe_path, "--type", game_type, "--random", str(seed)]
        internal_ms, wall_ms, user_ms, sys_ms, memory_bytes = run_with_timing_and_memory(cmd, timeout=120)

        if internal_ms is not None:
            total_internal_time_us += internal_ms * 1000
            total_wall_time_us += wall_ms * 1000
            if user_ms is not None:
                total_user_time_us += user_ms * 1000
            if sys_ms is not None:
                total_sys_time_us += sys_ms * 1000
            if memory_bytes:
                max_memory_bytes = max(max_memory_bytes, memory_bytes)
            seed_count += 1
        else:
            print(f"Warning: Legacy solver failed on seed {seed}")

    if seed_count == 0:
        print(f"Error: Could not extract timing data from legacy solver {exe_path}")
        sys.exit(1)

    # Store timing details for reporting
    global legacy_timing_details
    legacy_timing_details = {
        "internal": total_internal_time_us,
        "wall": total_wall_time_us,
        "user": total_user_time_us,
        "sys": total_sys_time_us,
        "cpu": total_user_time_us + total_sys_time_us,  # CPU time = user + sys
        "memory": max_memory_bytes
    }

    # Return internal time as primary (for HNF)
    return total_internal_time_us

def measure_standard_candle(reference_exe, calibration_workload, legacy_reference=False):
    """Measures the Hardware Normalization Factor (HNF) and captures reference solver metrics."""
    if not reference_exe:
        print("\033[93mWARNING: No --reference-exe provided. Hardware Normalization Scores will not be representative.\033[0m")
        return {"hnf": 1.0, "internal_time": None, "external_time": None, "memory": None}

    if legacy_reference:
        # For legacy solvers, parse a simple seed range (e.g., "1,10" means seeds 1-10)
        if "," in calibration_workload:
            parts = calibration_workload.split(",")
            if len(parts) == 2:
                try:
                    seed_start = int(parts[0].strip())
                    seed_end = int(parts[1].strip())

                    # Call the legacy function which populates legacy_timing_details
                    total_internal_us = get_json_payload_legacy(reference_exe, seed_start, seed_end)

                    return {
                        "hnf": total_internal_us,
                        "internal_time": legacy_timing_details.get("internal"),
                        "wall_time": legacy_timing_details.get("wall"),
                        "user_time": legacy_timing_details.get("user"),
                        "sys_time": legacy_timing_details.get("sys"),
                        "cpu_time": legacy_timing_details.get("cpu"),
                        "memory": legacy_timing_details.get("memory")
                    }
                except ValueError:
                    print(f"Error: Invalid seed range format. Use 'START,END' (e.g., '1,50')")
                    sys.exit(1)
        else:
            print(f"Error: For legacy reference, calibration-workload must be seed range (e.g., '1,50')")
            sys.exit(1)

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
    else:
        # Fallback for old single-object aggregate format
        total_us = payload["aggregate_stats"]["median_time_us"]

    # For modern solvers, capture all three memory metrics if available
    virtual_memory = None      # From getrusage virtual memory
    resident_memory = None     # From getrusage resident (median)
    system_memory = None       # From /usr/bin/time (actual resident)

    if isinstance(payload, list) and len(payload) > 0:
        virtual_memory = payload[0].get("median_virtual_memory_bytes")
        resident_memory = payload[0].get("median_resident_memory_bytes")
        system_memory = payload[0].get("system_memory_bytes")
    elif not isinstance(payload, list):
        agg = payload.get("aggregate_stats", {})
        virtual_memory = agg.get("median_virtual_memory_bytes")
        resident_memory = agg.get("median_resident_memory_bytes")
        system_memory = agg.get("system_memory_bytes")

    return {
        "hnf": total_us,
        "internal_time": total_us,
        "virtual_memory": virtual_memory,      # From getrusage
        "resident_memory": resident_memory,    # From getrusage
        "system_memory": system_memory         # From /usr/bin/time
    }

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
    parser.add_argument("--baseline-exe", default=None, help="Path to the baseline/master executable (optional; if omitted, only current-exe is benchmarked)")
    parser.add_argument("--current-exe", required=True, help="Path to the current working executable to test")
    parser.add_argument("--reference-exe", default=None, help="Path to a stable, older reference solver for hardware normalization")
    parser.add_argument("--legacy-reference", action="store_true", help="Flag indicating the reference solver is a legacy binary without --benchmark-json support")
    parser.add_argument("--calibration-workload", default="tests/oracles/level1.json", help="Path to the fixed regression JSON used for system calibration (or seed range 'START,END' for legacy solvers)")
    parser.add_argument("--out-report", default="benchmark_report.json", help="Path to save the JSON diagnostic report")
    parser.add_argument("--baseline-args", default=None, help="Benchmark arguments for baseline solver only (space-separated, overrides general args)")
    parser.add_argument("--current-args", default=None, help="Benchmark arguments for current solver only (space-separated, overrides general args)")
    parser.add_argument("benchmark_args", nargs=argparse.REMAINDER, help="Arguments to pass through to the solvitaire benchmark engine (used if --baseline-args and --current-args not provided)")

    args = parser.parse_args()

    if args.baseline_exe and not os.path.isfile(args.baseline_exe):
        print(f"Baseline executable not found: {args.baseline_exe}")
        sys.exit(1)

    if not os.path.isfile(args.current_exe):
        print(f"Current executable not found: {args.current_exe}")
        sys.exit(1)
        
    print(f"--- ReSolvitaire Orchestrator (Hardware Normalization) ---")
    print(f"Establishing hardware normalization factor using {args.reference_exe or 'NONE'} ...")
    hnf_data = measure_standard_candle(args.reference_exe, args.calibration_workload, args.legacy_reference)
    hnf = hnf_data["hnf"]

    if args.reference_exe:
        print(f"Hardware Normalization Factor (HNF) established: {hnf:.2f} us")
        if hnf_data.get("cpu_time"):
            # Legacy solver: show CPU time (user+sys) as headline, then details
            cpu_time = hnf_data['cpu_time']
            print(f"  CPU time (user+sys): {cpu_time:.2f} us")
            if hnf_data.get("user_time"):
                print(f"    User time: {hnf_data['user_time']:.2f} us")
            if hnf_data.get("sys_time"):
                print(f"    Sys time:  {hnf_data['sys_time']:.2f} us")
        if hnf_data.get("wall_time"):
            print(f"  Wall time: {hnf_data['wall_time']:.2f} us")
        if hnf_data.get("internal_time"):
            print(f"  Internal time: {hnf_data['internal_time']:.2f} us")

        # Report all memory metrics, with system-resident as primary
        if hnf_data.get("memory"):
            # Legacy solver: only system-measured memory
            mem_mb = hnf_data['memory'] / (1024 * 1024)
            print(f"  Peak memory (system resident): {mem_mb:.1f} MB")
        else:
            # Modern solver: system-resident is primary, others for reference
            if hnf_data.get("system_memory"):
                system_mb = hnf_data['system_memory'] / (1024 * 1024)
                print(f"  Peak memory (system resident): {system_mb:.1f} MB")
            if hnf_data.get("resident_memory"):
                resident_mb = hnf_data['resident_memory'] / (1024 * 1024)
                print(f"    (getrusage resident):       {resident_mb:.1f} MB")
            if hnf_data.get("virtual_memory"):
                virtual_mb = hnf_data['virtual_memory'] / (1024 * 1024)
                print(f"    (getrusage virtual):        {virtual_mb:.1f} MB")

        print()
    else:
        print(f"Normalization Factor (HNF) defaulted to 1.0 (No normalization active)\n")

    # Parse arguments: support both unified args and separate baseline/current args
    forward_args = args.benchmark_args
    if forward_args and forward_args[0] == "--":
        forward_args = forward_args[1:]

    if not forward_args:
        forward_args = ["--type", "klondike", "--benchmark-seeds", "1", "50", "--benchmark-iterations", "1", "--benchmark-warmup", "1"]

    # Handle separate baseline and current arguments
    # Strategy: if --baseline-args or --current-args provided, prepend them to general args
    # This allows mixing specific flags with common benchmark parameters
    if args.baseline_args:
        baseline_args = args.baseline_args.split() + forward_args
    else:
        baseline_args = forward_args

    if args.current_args:
        current_args = args.current_args.split() + forward_args
    else:
        current_args = forward_args

    # Single-solver mode: skip baseline if not provided
    baseline_payload = None
    if args.baseline_exe:
        print(f"Running baseline benchmark: {args.baseline_exe} {' '.join(baseline_args)}")
        baseline_payload = get_json_payload(args.baseline_exe, baseline_args)
    else:
        print(f"Baseline-exe not provided. Running in single-solver mode.\n")

    print(f"Running current benchmark: {args.current_exe} {' '.join(current_args)}")
    current_payload = get_json_payload(args.current_exe, current_args)

    # Single-solver mode: report only current performance
    if baseline_payload is None:
        print("\n================ SINGLE SOLVER BENCHMARK ================\n")
        print(f"Workload: {' '.join(current_args)}\n")

        # Handle both formats
        if isinstance(current_payload, list):
            print(f"Total instances: {len(current_payload)}\n")
            print(f"--- Sample Results ---")
            for inst in current_payload[:5]:
                # Use system memory if available, else fall back to median resident
                mem_bytes = inst.get("system_memory_bytes") or inst.get("median_resident_memory_bytes", 0)
                time_mb = mem_bytes / (1024 * 1024)
                print(f"{inst['instance']:<30}: {inst['median_time_us']:>12.0f} us, {inst['median_nodes']:>12.0f} nodes, {time_mb:>8.1f} MB")
        else:
            stats = current_payload["aggregate_stats"]
            # System memory as primary metric, with max resident
            mem_bytes = stats.get("system_memory_bytes") or stats.get("median_resident_memory_bytes", 0)
            mem_mb = mem_bytes / (1024 * 1024)
            print(f"Median Time:       {stats['median_time_us']:.2f} us")
            print(f"Median Nodes:      {stats['median_nodes']:.0f}")
            print(f"Peak Memory (sys): {mem_mb:.1f} MB")
            if stats.get("max_resident_memory_bytes"):
                max_mb = stats["max_resident_memory_bytes"] / (1024 * 1024)
                print(f"Max Resident:      {max_mb:.1f} MB")
            print(f"Nodes/Second:      {stats['nodes_per_second']:.0f}")

        if args.reference_exe:
            print(f"\n--- Hardware Normalized ---")
            if isinstance(current_payload, list):
                median_times = [inst["median_time_us"] for inst in current_payload]
                total_time = sum(median_times)
            else:
                total_time = current_payload["aggregate_stats"]["median_time_us"]
            normalized_score = total_time / hnf
            print(f"Hardware Normalization Factor: {hnf:.2f} us")
            print(f"Normalized Score:             {normalized_score:.4f}")

        report = {
            "metadata": {
                "date": datetime.datetime.now().isoformat(),
                "machine_id": platform.node(),
                "git_hash": get_git_hash(),
                "hardware_normalization_factor_us": hnf,
                "mode": "single-solver"
            },
            "benchmark_workload": " ".join(current_args),
            "results": current_payload
        }

        with open(args.out_report, 'w') as f:
            json.dump(report, f, indent=4)

        print(f"\nReport written to {args.out_report}")
        return

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
        
        # Build workload description
        workload_desc = " ".join(current_args)
        if baseline_args != current_args:
            workload_desc = f"baseline: {' '.join(baseline_args)} | current: {' '.join(current_args)}"

        report = {
            "metadata": {
                "date": datetime.datetime.now().isoformat(),
                "machine_id": platform.node(),
                "git_hash": get_git_hash(),
                "hardware_normalization_factor_us": hnf,
                "mode": "paired-instances"
            },
            "benchmark_workload": workload_desc,
            "results": combined_results,
            "overall": {
                "median_speedup_ratio": median_speedup,
                "median_node_ratio": median_node_ratio
            }
        }

        print("\n================ PAIRED INSTANCE BENCHMARK ================\n")
        print(f"Workload: {workload_desc}")
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

        # Build workload description
        workload_desc = " ".join(current_args)
        if baseline_args != current_args:
            workload_desc = f"baseline: {' '.join(baseline_args)} | current: {' '.join(current_args)}"

        report = {
            "metadata": {
                "date": datetime.datetime.now().isoformat(),
                "machine_id": platform.node(),
                "git_hash": get_git_hash(),
                "hardware_normalization_factor_us": hnf,
                "mode": "aggregate-median"
            },
            "benchmark_workload": workload_desc,
            "results": {
                "baseline": {
                    "executable": args.baseline_exe,
                    "stats": baseline_stats,
                    "hardware_normalized_score": baseline_normalized_score,
                    "system_memory_bytes": baseline_stats.get("system_memory_bytes"),
                    "max_resident_memory_bytes": baseline_stats.get("max_resident_memory_bytes")
                },
                "current": {
                    "executable": args.current_exe,
                    "stats": current_stats,
                    "hardware_normalized_score": normalized_sys_score,
                    "system_memory_bytes": current_stats.get("system_memory_bytes"),
                    "max_resident_memory_bytes": current_stats.get("max_resident_memory_bytes")
                },
                "comparison": {
                    "median_speedup_ratio": speedup_ratio,
                    "median_node_ratio": current_stats["median_nodes"] / baseline_stats["median_nodes"] if baseline_stats["median_nodes"] > 0 else 1.0
                }
            }
        }
        
        print("\n================ AGGREGATE STATS BENCHMARK ================\n")
        print(f"Workload: {workload_desc}\n")
        
        print(f"--- Timing (Median us) ---")
        print(f"Baseline: {baseline_stats['median_time_us']:.2f} (Mean: {baseline_stats['mean_time_us']:.2f}, SD: {baseline_stats['sd_time_us']:.2f})")
        print(f"Current:  {current_stats['median_time_us']:.2f} (Mean: {current_stats['mean_time_us']:.2f}, SD: {current_stats['sd_time_us']:.2f})\n")

        print(f"--- Nodes (Median) ---")
        print(f"Baseline: {baseline_stats['median_nodes']:.2f} (Mean: {baseline_stats['mean_nodes']:.2f})")
        print(f"Current:  {current_stats['median_nodes']:.2f} (Mean: {current_stats['mean_nodes']:.2f})")
        print(f"Baseline NPS: {baseline_stats['nodes_per_second']:.2f} nodes/sec")
        print(f"Current NPS:  {current_stats['nodes_per_second']:.2f} nodes/sec\n")
        
        print(f"--- Memory Usage ---")
        # System-resident memory as primary (from /usr/bin/time)
        baseline_sys_mem = baseline_stats.get("system_memory_bytes")
        current_sys_mem = current_stats.get("system_memory_bytes")
        if baseline_sys_mem:
            baseline_mem_mb = baseline_sys_mem / (1024 * 1024)
            print(f"Baseline: {baseline_mem_mb:.1f} MB (system resident)")
        baseline_max_mem = baseline_stats.get("max_resident_memory_bytes")
        if baseline_max_mem:
            baseline_max_mb = baseline_max_mem / (1024 * 1024)
            print(f"          {baseline_max_mb:.1f} MB (max resident)")

        if current_sys_mem:
            current_mem_mb = current_sys_mem / (1024 * 1024)
            print(f"Current:  {current_mem_mb:.1f} MB (system resident)")
        current_max_mem = current_stats.get("max_resident_memory_bytes")
        if current_max_mem:
            current_max_mb = current_max_mem / (1024 * 1024)
            print(f"          {current_max_mb:.1f} MB (max resident)")
        print()

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
