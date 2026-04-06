#!/usr/bin/env python3
import json
import subprocess
import os
import time
import csv
import sys

SOLVER_PATH = "cmake-build-release/bin/solvitaire"
ORACLE_PATH = "tests/oracles/level4.json"
TIMEOUT_MS = 600000
OUTPUT_CSV = "results/level4_benchmark_combined.csv"

# Games to EXCLUDE (from user instructions)
EXCLUDED_GAMES = {
    "accordion", "late-binding-solitaire", "simple-simon", "spiderette", 
    "will-o-the-wisp", "gaps-basic-variant", "gaps-one-deal"
}

def get_seed(instance_field):
    # Format: resources/level4/black-hole_9421045_unwinnable.json
    filename = os.path.basename(instance_field)
    # The seeds in level4 are either seed_X or game-type_seed_outcome.json
    parts = filename.replace('.json', '').rsplit('_', 2)
    if len(parts) == 3:
        # Example: black-hole_9421045_unwinnable -> parts = ['black-hole', '9421045', 'unwinnable']
        return parts[1]
    return None

def run_solver(instance, cache_type="auto"):
    seed = get_seed(instance['instance'])
    if not seed:
        print(f"Error: Could not extract seed from {instance['instance']}")
        return None
    
    cmd = [
        SOLVER_PATH, 
        "--random", seed, 
        "--json", 
        "--timeout", str(TIMEOUT_MS),
        "--streamliners", "none"
    ]
    
    if cache_type == "hash-only":
        cmd.extend(["--cache-type", "hash-only"])
    else:
        cmd.extend(["--cache-type", "auto"])
    
    if "custom_rules" in instance:
        cmd.extend(["--custom-rules", instance["custom_rules"]])
    
    # Run solver
    t0 = time.time()
    try:
        # Increase process timeout slightly over solver timeout to allow it to exit gracefully
        proc_timeout = (TIMEOUT_MS / 1000.0) + 30.0
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=proc_timeout)
        t1 = time.time()
        
        if result.returncode != 0:
            print(f"Error running solver for {instance['instance']}: return code {result.returncode}, stderr: {result.stderr}")
            return {"outcome": f"error({result.returncode})", "time_ms": (t1-t0)*1000, "nodes": 0, "resident_bytes": 0}
        
        output_text = result.stdout.strip()
        if not output_text:
             return {"outcome": "empty_output", "time_ms": (t1-t0)*1000, "nodes": 0, "resident_bytes": 0}
             
        try:
            output = json.loads(output_text)
        except json.JSONDecodeError as e:
            print(f"JSON error for {instance['instance']}: {e}. Raw: {output_text[:100]}...")
            return {"outcome": "json_error", "time_ms": (t1-t0)*1000, "nodes": 0, "resident_bytes": 0}

        return {
            "outcome": output.get("solution_type"),
            "time_ms": (t1-t0)*1000,
            "nodes": output.get("states_searched", 0),
            "resident_bytes": output.get("solver_resident_bytes", 0)
        }
    except subprocess.TimeoutExpired:
        print(f"Subprocess timeout for {instance['instance']}")
        return {"outcome": "subprocess_timeout", "time_ms": (TIMEOUT_MS + 30000), "nodes": 0, "resident_bytes": 0}

def main():
    if not os.path.exists("results"):
        os.makedirs("results")

    if not os.path.exists(ORACLE_PATH):
        print(f"Error: Oracle file not found at {ORACLE_PATH}")
        sys.exit(1)

    with open(ORACLE_PATH, 'r') as f:
        level4_data = json.load(f)

    # Filter instances
    eligible = []
    for inst in level4_data:
        # 1. streamliner == none
        if inst.get("streamliner") != "none":
            continue
            
        # 2. game type NOT excluded
        filename = os.path.basename(inst['instance'])
        # Game type is usually before the first underscore in level4
        game_type = filename.split('_')[0]
        if game_type in EXCLUDED_GAMES:
            continue
            
        eligible.append(inst)

    print(f"Total Level 4 instances in oracle: {len(level4_data)}")
    print(f"Eligible hard instances (flat-cache eligible): {len(eligible)}")

    with open(OUTPUT_CSV, 'w', newline='') as csvfile:
        fieldnames = [
            'instance', 'seed', 'game_type', 'baseline_time_ms',
            'flat_outcome', 'flat_time_ms', 'flat_nodes', 'flat_memory_bytes',
            'hash_outcome', 'hash_time_ms', 'hash_nodes', 'hash_memory_bytes'
        ]
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()

        for idx, inst in enumerate(eligible):
            print(f"[{idx+1}/{len(eligible)}] Processing {inst['instance']} (baseline {inst.get('baseline_time_ms')}ms)...", flush=True)
            seed = get_seed(inst['instance'])
            game_type = os.path.basename(inst['instance']).split('_')[0]
            
            # Flat (auto selects flat cache)
            flat_res = run_solver(inst, cache_type="auto")
            
            # Hash-only
            hash_res = run_solver(inst, cache_type="hash-only")
            
            writer.writerow({
                'instance': inst['instance'],
                'seed': seed,
                'game_type': game_type,
                'baseline_time_ms': inst.get('baseline_time_ms'),
                'flat_outcome': flat_res['outcome'] if flat_res else 'failed',
                'flat_time_ms': flat_res['time_ms'] if flat_res else 0,
                'flat_nodes': flat_res['nodes'] if flat_res else 0,
                'flat_memory_bytes': flat_res['resident_bytes'] if flat_res else 0,
                'hash_outcome': hash_res['outcome'] if hash_res else 'failed',
                'hash_time_ms': hash_res['time_ms'] if hash_res else 0,
                'hash_nodes': hash_res['nodes'] if hash_res else 0,
                'hash_memory_bytes': hash_res['resident_bytes'] if hash_res else 0,
            })
            csvfile.flush()

if __name__ == "__main__":
    main()
