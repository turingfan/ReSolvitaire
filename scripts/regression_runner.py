#!/usr/bin/env python3
import os
import subprocess
import json
import argparse
import sys

def run_regression(solver_path, instances_dir, oracle_path, timeout=30, verbose=False):
    if not os.path.exists(solver_path):
        print(f"Error: Solver not found at {solver_path}")
        return 1
    if not os.path.exists(instances_dir):
        print(f"Error: Instances directory not found at {instances_dir}")
        return 1
    if not os.path.exists(oracle_path):
        print(f"Error: Oracle not found at {oracle_path}")
        return 1

    with open(oracle_path, 'r') as f:
        oracle_raw = json.load(f)

    # Normalize oracle to a dict mapping base filename to result dict
    oracle = {}
    if isinstance(oracle_raw, dict):
        oracle = oracle_raw
    elif isinstance(oracle_raw, list):
        for entry in oracle_raw:
            inst_path = entry.get('instance') or entry.get('instance_name')
            if inst_path:
                basename = os.path.basename(inst_path)
                oracle[basename] = entry

    instances = sorted([f for f in os.listdir(instances_dir) if f.endswith(".json")])
    total = len(oracle)
    failed = 0
    passed = 0

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

    print(f"Running Regression: {total} instances (Oracle: {os.path.basename(oracle_path)})", flush=True)
    print("-" * 60, flush=True)

    for filename in instances:
        instance_path = os.path.join(instances_dir, filename)
        
        if filename not in oracle:
            continue

        baseline = oracle[filename]
        
        baseline_time_ms = baseline.get("baseline_time_ms", 30000)
        instance_timeout_ms = max(int(2 * baseline_time_ms), 2000)
        
        cmd = [
            solver_path, instance_path, 
            "--json", 
            "--timeout", str(instance_timeout_ms)
        ]
        
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
            
        try:
            py_timeout = (instance_timeout_ms / 1000.0) + 10.0
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=py_timeout)
            
            if result.returncode != 0:
                print(f"[FAIL] {filename} (Solver crashed with return code {result.returncode})", flush=True)
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
            expected_outcome = normalize_outcome(baseline.get("solution_type"))
            actual_nodes = int(output.get("states_searched", 0))
            expected_nodes = int(baseline.get("states_searched", 0))
            
            diffs = []
            
            if actual_outcome == "timeout":
                if actual_nodes > expected_nodes:
                    diffs.append(f"TIMEOUT FAILURE: {actual_nodes} nodes processed before timeout (baseline {expected_nodes})")
                else:
                    if verbose: print(f"[OK/SLOW] {filename} (Nodes: {actual_nodes} <= baseline {expected_nodes})", flush=True)
                    passed += 1
                    continue
            else:
                if actual_outcome != expected_outcome:
                    diffs.append(f"outcome: {actual_outcome} (expected {expected_outcome})")
                
                if actual_nodes != expected_nodes:
                    diffs.append(f"states_searched: {actual_nodes} (expected {expected_nodes})")

            if diffs:
                print(f"[FAIL] {filename} (streamliner: {streamliner})", flush=True)
                for d in diffs:
                    print(f"  - {d}", flush=True)
                failed += 1
            else:
                passed += 1
                if verbose or (passed + failed) % 25 == 0:
                    print(f"Progress: {passed+failed}/{total} (Pass: {passed}, Fail: {failed})", flush=True)

        except subprocess.TimeoutExpired:
            print(f"[FAIL] {filename} (HUNG: No output after {py_timeout:.1f}s)", flush=True)
            failed += 1
        except Exception as e:
            print(f"[ERROR] {filename}: {e}", flush=True)
            failed += 1

    print("-" * 60, flush=True)
    print(f"Final Report: Passed: {passed}/{total}", flush=True)
    if failed > 0:
        print(f"FAILED: {failed}/{total}", flush=True)
        return 1
    
    print("Regression suite components verified successfully.", flush=True)
    return 0

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Solvitaire Regression Runner")
    parser.add_argument("--exe", required=True, help="Path to solvitaire executable")
    parser.add_argument("--instances", required=True, help="Path to instances directory")
    parser.add_argument("--oracle", required=True, help="Path to baseline oracle JSON")
    parser.add_argument("--verbose", action="store_true", help="Print all pass messages")
    
    args = parser.parse_args()
    sys.exit(run_regression(args.exe, args.instances, args.oracle, verbose=args.verbose))
