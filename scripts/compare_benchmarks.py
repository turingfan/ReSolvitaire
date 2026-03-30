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

def measure_reference_solver(reference_exe, calibration_workload, legacy_reference=False):
    """Measures reference solver performance on this machine (for informational purposes only)."""
    if not reference_exe:
        return None

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
    args = ["--benchmark-json", calibration_workload, "--benchmark-iterations", "3"]
    payload = get_json_payload(reference_exe, args)

    # Calculate reference solver time on this machine
    if isinstance(payload, list):
        # Sum the median times of each instance for total time
        total_us = sum(inst["median_time_us"] for inst in payload)
    else:
        # Fallback for old single-object aggregate format
        total_us = payload["aggregate_stats"]["median_time_us"]

    # Capture all three memory metrics if available
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

def calculate_geometric_mean(data):
    if not data:
        return 0.0
    try:
        # Use log to prevent float overflow
        return math.exp(sum(math.log(max(1.0, x)) for x in data) / len(data))
    except (ValueError, ZeroDivisionError):
        return 0.0

def calculate_par2(times, timeout_ms):
    if not times:
        return 0.0
    timeout_us = timeout_ms * 1000
    sum_par2 = 0
    for t in times:
        if t >= timeout_us * 0.99:
            sum_par2 += timeout_us * 2
        else:
            sum_par2 += t
    return sum_par2 / len(times)

def main():
    # Parse arguments manually to handle -- properly with flags like --force-lru
    # Split sys.argv at -- to separate orchestrator args from benchmark args
    try:
        dash_dash_idx = sys.argv.index("--")
        orch_argv = sys.argv[1:dash_dash_idx]
        benchmark_argv = sys.argv[dash_dash_idx+1:]
    except ValueError:
        # No -- separator, treat all as orchestrator args
        orch_argv = sys.argv[1:]
        benchmark_argv = []

    parser = argparse.ArgumentParser(description="ReSolvitaire Python Orchestrator: Hardware Normalization & Median Statistics")
    parser.add_argument("--baseline-exe", default=None, help="Path to the baseline/master executable (optional; if omitted, only current-exe is benchmarked)")
    parser.add_argument("--current-exe", required=True, help="Path to the current working executable to test")
    parser.add_argument("--reference-exe", default=None, help="Path to a reference solver to measure on this machine (informational; does not set RHT)")
    parser.add_argument("--legacy-reference", action="store_true", help="Flag indicating the reference solver is a legacy binary without --benchmark-json support")
    parser.add_argument("--calibration-workload", default="tests/oracles/level1.json", help="Path to the fixed regression JSON used for calibration (or seed range 'START,END' for legacy solvers)")
    parser.add_argument("--rht", type=float, default=None, help="Reference Hardware Time (microseconds) from a canonical machine. If not provided, normalization is disabled (RHT=1.0)")
    parser.add_argument("--out-report", default="benchmark_report.json", help="Path to save the JSON diagnostic report")
    parser.add_argument("--save-details", default=None, help="Path to save full seed-level or instance-level details (JSON)")
    parser.add_argument("--baseline-args", nargs=argparse.REMAINDER, help="Benchmark arguments for baseline solver only (can include flags like --force-lru)")
    parser.add_argument("--current-args", nargs=argparse.REMAINDER, help="Benchmark arguments for current solver only (can include flags like --force-lru)")

    # Parse only the orchestrator arguments before --
    args = parser.parse_args(orch_argv)

    # Handle baseline-args and current-args manually
    # We need to extract them from orch_argv and consume everything until the next known orchestrator flag
    known_orchestrator_flags = {
        "--baseline-exe", "--current-exe", "--reference-exe", "--legacy-reference",
        "--calibration-workload", "--out-report", "--baseline-args", "--current-args"
    }
    baseline_args_raw = []
    current_args_raw = []
    i = 0
    while i < len(orch_argv):
        if orch_argv[i] == "--baseline-args":
            # Consume all following args until next known orchestrator flag
            i += 1
            while i < len(orch_argv) and orch_argv[i] not in known_orchestrator_flags:
                baseline_args_raw.append(orch_argv[i])
                i += 1
        elif orch_argv[i] == "--current-args":
            # Consume all following args until next known orchestrator flag
            i += 1
            while i < len(orch_argv) and orch_argv[i] not in known_orchestrator_flags:
                current_args_raw.append(orch_argv[i])
                i += 1
        else:
            i += 1

    if args.baseline_exe and not os.path.isfile(args.baseline_exe):
        print(f"Baseline executable not found: {args.baseline_exe}")
        sys.exit(1)

    if not os.path.isfile(args.current_exe):
        print(f"Current executable not found: {args.current_exe}")
        sys.exit(1)
        
    print(f"--- ReSolvitaire Orchestrator ---")

    # Measure reference solver if provided (for informational purposes)
    ref_solver_data = None
    if args.reference_exe:
        print(f"Measuring reference solver: {args.reference_exe} ...")
        ref_solver_data = measure_reference_solver(args.reference_exe, args.calibration_workload, args.legacy_reference)
        if ref_solver_data:
            print(f"Reference solver performance on this machine:")
            if ref_solver_data.get("cpu_time"):
                # Legacy solver: show CPU time (user+sys) as headline
                cpu_time = ref_solver_data['cpu_time']
                print(f"  CPU time (user+sys): {cpu_time:.2f} us")
                if ref_solver_data.get("user_time"):
                    print(f"    User time: {ref_solver_data['user_time']:.2f} us")
                if ref_solver_data.get("sys_time"):
                    print(f"    Sys time:  {ref_solver_data['sys_time']:.2f} us")
            if ref_solver_data.get("wall_time"):
                print(f"  Wall time: {ref_solver_data['wall_time']:.2f} us")
            if ref_solver_data.get("internal_time"):
                print(f"  Internal time: {ref_solver_data['internal_time']:.2f} us")

            # Report memory metrics
            if ref_solver_data.get("memory"):
                # Legacy solver: only system-measured memory
                mem_mb = ref_solver_data['memory'] / (1024 * 1024)
                print(f"  Peak memory (system resident): {mem_mb:.1f} MB")
            else:
                # Modern solver: system-resident is primary
                if ref_solver_data.get("system_memory"):
                    system_mb = ref_solver_data['system_memory'] / (1024 * 1024)
                    print(f"  Peak memory (system resident): {system_mb:.1f} MB")
                if ref_solver_data.get("resident_memory"):
                    resident_mb = ref_solver_data['resident_memory'] / (1024 * 1024)
                    print(f"    (getrusage resident):       {resident_mb:.1f} MB")
                if ref_solver_data.get("virtual_memory"):
                    virtual_mb = ref_solver_data['virtual_memory'] / (1024 * 1024)
                    print(f"    (getrusage virtual):        {virtual_mb:.1f} MB")
            print()

    # Reference Hardware Time
    rht = args.rht if args.rht else 1.0
    if args.rht:
        print(f"Reference Hardware Time (RHT): {rht:.2f} us (provided)")
        print(f"  (from canonical machine measurement)\n")
    else:
        print(f"Reference Hardware Time (RHT): {rht:.2f} (default, no normalization)\n")

    # Use benchmark_argv (args after --) as the general args
    forward_args = benchmark_argv if benchmark_argv else []
    if not forward_args:
        forward_args = ["--type", "klondike", "--benchmark-seeds", "1", "50", "--benchmark-iterations", "1", "--benchmark-warmup", "1"]

    # Extract timeout for PAR2 calculation
    timeout_ms_val = 60000 # default from C++
    if "--timeout" in forward_args:
        try:
            idx = forward_args.index("--timeout")
            timeout_ms_val = int(forward_args[idx + 1])
        except (ValueError, IndexError):
            pass
    
    # Handle separate baseline and current arguments
    # Use the manually parsed args from orchestrator argv
    baseline_args = baseline_args_raw + forward_args
    current_args = current_args_raw + forward_args

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
            normalized_score = total_time / rht
            print(f"Reference Hardware Time (RHT): {rht:.2f} us")
            print(f"Normalized Score:             {normalized_score:.4f}")

        report = {
            "metadata": {
                "date": datetime.datetime.now().isoformat(),
                "machine_id": platform.node(),
                "git_hash": get_git_hash(),
                "hardware_normalization_factor_us": rht,
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
        nps_ratios = []
        solution_type_mismatches = []
        combined_results = []

        for item in current_payload:
            name = item["instance"]
            if name in baseline_map:
                base = baseline_map[name]
                ratio = item["median_time_us"] / base["median_time_us"] if base["median_time_us"] > 0 else 1.0
                n_ratio = item["median_nodes"] / base["median_nodes"] if base["median_nodes"] > 0 else 1.0

                # Calculate nodes per second
                baseline_nps = (base["median_nodes"] * 1000000) / base["median_time_us"] if base["median_time_us"] > 0 else 0
                current_nps = (item["median_nodes"] * 1000000) / item["median_time_us"] if item["median_time_us"] > 0 else 0
                nps_ratio = current_nps / baseline_nps if baseline_nps > 0 else 1.0

                # Check for solution type mismatches
                base_sol_type = base.get("solution_type", "unknown")
                current_sol_type = item.get("solution_type", "unknown")
                if base_sol_type != current_sol_type:
                    solution_type_mismatches.append({
                        "instance": name,
                        "baseline": base_sol_type,
                        "current": current_sol_type
                    })

                speedup_ratios.append(ratio)
                node_ratios.append(n_ratio)
                nps_ratios.append(nps_ratio)
                combined_results.append({
                    "name": name,
                    "baseline": base,
                    "current": item,
                    "speedup_ratio": ratio,
                    "node_ratio": n_ratio,
                    "nps_ratio": nps_ratio,
                    "baseline_nps": baseline_nps,
                    "current_nps": current_nps
                })

        median_speedup = calculate_median(speedup_ratios)
        geomean_speedup = calculate_geometric_mean(speedup_ratios)
        median_node_ratio = calculate_median(node_ratios)
        median_nps_ratio = calculate_median(nps_ratios)
        
        # Calculate PAR2 speedup if many instances
        baseline_times = [r["baseline"]["median_time_us"] for r in combined_results]
        current_times = [r["current"]["median_time_us"] for r in combined_results]
        par2_baseline = calculate_par2(baseline_times, timeout_ms_val)
        par2_current = calculate_par2(current_times, timeout_ms_val)
        par2_ratio = par2_current / par2_baseline if par2_baseline > 0 else 1.0

        # Build workload description
        workload_desc = " ".join(current_args)
        if baseline_args != current_args:
            workload_desc = f"baseline: {' '.join(baseline_args)} | current: {' '.join(current_args)}"

        report = {
            "metadata": {
                "date": datetime.datetime.now().isoformat(),
                "machine_id": platform.node(),
                "git_hash": get_git_hash(),
                "hardware_normalization_factor_us": rht,
                "mode": "paired-instances",
                "timeout_ms": timeout_ms_val
            },
            "benchmark_workload": workload_desc,
            "results": combined_results,
            "solution_type_mismatches": solution_type_mismatches,
            "overall": {
                "median_speedup_ratio": median_speedup,
                "geomean_speedup_ratio": geomean_speedup,
                "par2_speedup_ratio": par2_ratio,
                "median_node_ratio": median_node_ratio,
                "median_nps_ratio": median_nps_ratio,
                "mismatch_count": len(solution_type_mismatches)
            }
        }

        print("\n================ PAIRED INSTANCE BENCHMARK ================\n")
        print(f"Workload: {workload_desc}")
        print(f"Total instances matched: {len(combined_results)}\n")
        
        print(f"--- Hardware Normalized ---")
        print(f"Reference Hardware Time (RHT): {rht:.2f} us")
        
        print(f"\n--- Statistical Ratios (Current / Baseline) ---")
        color = "\033[91m" if geomean_speedup > 1.05 else ("\033[92m" if geomean_speedup < 0.95 else "")
        reset = "\033[0m"
        print(f"Geo-Mean Time Ratio: {color}{geomean_speedup:.4f}x{reset} (Primary metric)")
        print(f"Median Time Ratio:   {median_speedup:.4f}x")
        
        p_color = "\033[91m" if par2_ratio > 1.05 else ("\033[92m" if par2_ratio < 0.95 else "")
        print(f"PAR2 Score Ratio:    {p_color}{par2_ratio:.4f}x{reset} (Failures penalized 2x timeout)")

        n_color = "\033[92m" if median_node_ratio < 0.99 else ("\033[91m" if median_node_ratio > 1.01 else "")
        print(f"Median Node Ratio:   {n_color}{median_node_ratio:.4f}x{reset}")

        nps_color = "\033[92m" if median_nps_ratio > 1.01 else ("\033[91m" if median_nps_ratio < 0.99 else "")
        print(f"Median Nodes/Sec:    {nps_color}{median_nps_ratio:.4f}x{reset}\n")

        # Set speedup for final verdict (using Geomean)
        median_speedup = geomean_speedup

        if solution_type_mismatches:
            print(f"--- Solution Type Mismatches ({len(solution_type_mismatches)} instances) ---")
            print(f"Note: May be expected if different streamliners are used\n")
            for mismatch in solution_type_mismatches[:10]:
                print(f"{mismatch['instance']:<30}: {mismatch['baseline']} → {mismatch['current']}")
            if len(solution_type_mismatches) > 10:
                print(f"... and {len(solution_type_mismatches) - 10} more mismatches\n")

        if combined_results:
            print(f"--- Sample Instances ---")
            for r in combined_results[:5]:
                print(f"{r['name']:<30}: {r['speedup_ratio']:.4f}x time, {r['node_ratio']:.4f}x nodes, {r['nps_ratio']:.4f}x nps")

    else:
        # Fallback to old aggregate_stats logic
        baseline_stats = baseline_payload["aggregate_stats"]
        current_stats = current_payload["aggregate_stats"]

        baseline_val = baseline_stats["geometric_mean_time_us"] if "geometric_mean_time_us" in baseline_stats else baseline_stats["median_time_us"]
        current_val = current_stats["geometric_mean_time_us"] if "geometric_mean_time_us" in current_stats else current_stats["median_time_us"]

        speedup_ratio = current_val / baseline_val if baseline_val > 0 else 1.0
        
        # PAR2 score
        par2_baseline = baseline_stats.get("par2_score_us", baseline_val)
        par2_current = current_stats.get("par2_score_us", current_val)
        par2_ratio = par2_current / par2_baseline if par2_baseline > 0 else 1.0

        normalized_sys_score = current_val / rht if rht > 0 else 0.0
        baseline_normalized_score = baseline_val / rht if rht > 0 else 0.0

        # Build workload description
        workload_desc = " ".join(current_args)
        if baseline_args != current_args:
            workload_desc = f"baseline: {' '.join(baseline_args)} | current: {' '.join(current_args)}"

        report = {
            "metadata": {
                "date": datetime.datetime.now().isoformat(),
                "machine_id": platform.node(),
                "git_hash": get_git_hash(),
                "hardware_normalization_factor_us": rht,
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
                    "median_node_ratio": current_stats["median_nodes"] / baseline_stats["median_nodes"] if baseline_stats["median_nodes"] > 0 else 1.0,
                    "nps_ratio": (current_stats["median_nodes"] / current_stats["median_time_us"]) / (baseline_stats["median_nodes"] / baseline_stats["median_time_us"]) if baseline_stats["median_time_us"] > 0 and current_stats["median_time_us"] > 0 else 1.0
                }
            }
        }
        print(f"--- Timing (us) ---")
        print(f"Baseline Median: {baseline_stats['median_time_us']:.2f}, Geo-Mean: {baseline_stats.get('geometric_mean_time_us', 0.0):.2f}")
        print(f"Current Median:  {current_stats['median_time_us']:.2f}, Geo-Mean: {current_stats.get('geometric_mean_time_us', 0.0):.2f}")
        print(f"Baseline PAR2:   {baseline_stats.get('par2_score_us', 0.0):.2f}")
        print(f"Current PAR2:    {current_stats.get('par2_score_us', 0.0):.2f}\n")

        print(f"--- Nodes ---")
        print(f"Baseline Median: {baseline_stats['median_nodes']:.2f}, Geo-Mean: {baseline_stats.get('geometric_mean_nodes', 0.0):.2f}")
        print(f"Current Median:  {current_stats['median_nodes']:.2f}, Geo-Mean: {current_stats.get('geometric_mean_nodes', 0.0):.2f}\n")
        
        print(f"--- Nodes/Second ---")
        print(f"Baseline: Per-Instance Mean: {baseline_stats.get('mean_nps', 0.0):.2f}, Aggregate: {baseline_stats.get('aggregate_nps', 0.0):.2f}")
        print(f"Current:  Per-Instance Mean: {current_stats.get('mean_nps', 0.0):.2f}, Aggregate: {current_stats.get('aggregate_nps', 0.0):.2f}\n")
        
        print(f"--- Memory Usage ---")
        # System-resident memory as primary (from /usr/bin/time)
        baseline_sys_mem = baseline_stats.get("system_memory_bytes")
        current_sys_mem = current_stats.get("system_memory_bytes")
        if baseline_sys_mem:
            baseline_mem_mb = baseline_sys_mem / (1024 * 1024)
            print(f"Baseline: {baseline_mem_mb:.1f} MB (system resident)")
        
        if current_sys_mem:
            current_mem_mb = current_sys_mem / (1024 * 1024)
            print(f"Current:  {current_mem_mb:.1f} MB (system resident)")
        print()

        print(f"--- Hardware Normalized ---")
        print(f"Reference Hardware Time (RHT): {rht:.2f} us")
        print(f"Baseline Normalized Score:   {baseline_normalized_score:.4f}")
        print(f"Current Normalized Score:    {normalized_sys_score:.4f}\n")

        print(f"--- Comparison (Current / Baseline) ---")
        color = "\033[91m" if speedup_ratio > 1.05 else ("\033[92m" if speedup_ratio < 0.95 else "")
        reset = "\033[0m"
        print(f"Time Ratio (Geo-Mean): {color}{speedup_ratio:.4f}x{reset} (Values > 1.0 indicate regression)")
        
        p_color = "\033[91m" if par2_ratio > 1.05 else ("\033[92m" if par2_ratio < 0.95 else "")
        print(f"PAR2 Score Ratio:      {p_color}{par2_ratio:.4f}x{reset}")

        node_ratio = current_stats["median_nodes"] / baseline_stats["median_nodes"] if baseline_stats["median_nodes"] > 0 else 1.0
        n_color = "\033[92m" if node_ratio < 0.99 else ("\033[91m" if node_ratio > 1.01 else "")
        print(f"Node Ratio (Median):   {n_color}{node_ratio:.4f}x{reset}")

        # NPS Ratio - using Per-Instance Mean as it's more representative of "average" speedup
        baseline_nps = baseline_stats.get("mean_nps") or ((baseline_stats["median_nodes"] * 1000000) / max(1.0, baseline_stats["median_time_us"]))
        current_nps = current_stats.get("mean_nps") or ((current_stats["median_nodes"] * 1000000) / max(1.0, current_stats["median_time_us"]))
        nps_ratio = current_nps / baseline_nps if baseline_nps > 0 else 1.0
        nps_color = "\033[92m" if nps_ratio > 1.01 else ("\033[91m" if nps_ratio < 0.99 else "")
        print(f"Nodes/Sec Ratio (Mean): {nps_color}{nps_ratio:.4f}x{reset}")
        
        median_speedup = speedup_ratio # For the final verdict

    if median_speedup < 0.98:
        speedup_multiplier = 1.0 / median_speedup
        print(f"\nVerdict: Current build is FASTER by {speedup_multiplier:.2f}x")
    elif median_speedup > 1.02:
        print(f"\nVerdict: Current build is SLOWER (Regression) by {median_speedup:.2f}x")
    else:
        print(f"\nVerdict: No significant performance change within 2% noise margin.")

    # Report solution type status
    if 'solution_type_mismatches' in locals() and solution_type_mismatches:
        print(f"\n⚠️  Solution type discrepancies found: {len(solution_type_mismatches)} instances")
    elif 'solution_type_mismatches' in locals():
        print(f"\n✓ Solution types match across all instances")

    # Save raw details if requested
    if args.save_details:
        print(f"Saving full benchmark details to {args.save_details}")
        details = {
            "baseline": baseline_payload,
            "current": current_payload
        }
        with open(args.save_details, 'w') as f:
            json.dump(details, f, indent=4)

    with open(args.out_report, 'w') as f:
        json.dump(report, f, indent=4)
        
    print(f"\nReport written to {args.out_report}")

if __name__ == "__main__":
    import math
    main()
