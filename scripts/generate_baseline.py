import os
import subprocess
import json
import time

BIN_PATH = "/Users/ipg/Research/ReSolvitaire-project/02-Code-Repositories/ReSolvitaire/cmake-build-release/bin/solvitaire"
INSTANCES_DIR = "/Users/ipg/Research/ReSolvitaire-project/02-Code-Repositories/ReSolvitaire/tests/level1/instances"
OUTPUT_FILE = "/Users/ipg/Research/ReSolvitaire-project/02-Code-Repositories/ReSolvitaire/tests/level1/baseline_oracle.json"

def generate_baseline():
    oracle = {}
    
    files = sorted([f for f in os.listdir(INSTANCES_DIR) if f.endswith(".json")])
    total = len(files)
    print(f"Generating baseline for {total} instances...")
    
    start_time = time.time()
    TESTS_ROOT = os.path.dirname(os.path.abspath(INSTANCES_DIR))
    if os.path.basename(TESTS_ROOT) == "level1":
         TESTS_ROOT = os.path.dirname(TESTS_ROOT) # Go up to 'tests'

    for i, filename in enumerate(files):
        instance_path = os.path.join(INSTANCES_DIR, filename)
        rel_path = os.path.relpath(instance_path, TESTS_ROOT)
        
        # Determine game type from filename: game_name_seed_#.json
        game_name = filename.split("_seed_")[0]
        
        # Run from project root to get consistent relative paths in output
        cmd = [BIN_PATH, instance_path, "--type", game_name, "--json"]
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            json_output = json.loads(result.stdout.strip())
            # Replace absolute path with relative path in the output object
            json_output["instance_name"] = rel_path
            oracle[filename] = json_output
            if (i + 1) % 10 == 0:
                elapsed = time.time() - start_time
                print(f"[{i+1}/{total}] Processed... (Avg: {elapsed/(i+1):.2f}s/instance)")
        except Exception as e:
            print(f"Error processing {filename}: {e}")
            if hasattr(e, 'stdout'): print(f"Stdout: {e.stdout}")
            if hasattr(e, 'stderr'): print(f"Stderr: {e.stderr}")
            
    with open(OUTPUT_FILE, 'w') as f:
        json.dump(oracle, f, indent=4)
        
    print(f"Baseline oracle generated at {OUTPUT_FILE}")
    print(f"Total time: {time.time() - start_time:.2f}s")

if __name__ == "__main__":
    generate_baseline()
