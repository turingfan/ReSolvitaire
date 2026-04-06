import csv
import math
import os

INPUT_CSV = "results/level4_benchmark_combined.csv"

def geo_mean(iterable):
    vals = [x for x in iterable if x > 0]
    if not vals:
        return 0
    return math.exp(sum(math.log(x) for x in vals) / len(vals))

def analyze():
    with open(INPUT_CSV, 'r') as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    buckets = {
        "<1s": [],
        "1-5s": [],
        "5-10s": [],
        ">10s": []
    }

    mismatches = []
    
    total_flat_nodes = 0
    total_hash_nodes = 0
    total_flat_time = 0
    total_hash_time = 0

    for r in rows:
        baseline = float(r['baseline_time_ms']) / 1000.0
        
        # Outcome check
        if r['flat_outcome'] != r['hash_outcome']:
            # Timeouts on either side are soft mismatches if the other is definitive
            mismatches.append(f"{r['instance']}: Flat={r['flat_outcome']}, Hash={r['hash_outcome']}")
            
        flat_time = float(r['flat_time_ms']) / 1000.0
        hash_time = float(r['hash_time_ms']) / 1000.0
        
        if flat_time > 0 and hash_time > 0:
            speedup = flat_time / hash_time
            if baseline < 1.0:
                buckets["<1s"].append(speedup)
            elif baseline < 5.0:
                buckets["1-5s"].append(speedup)
            elif baseline < 10.0:
                buckets["5-10s"].append(speedup)
            else:
                buckets[">10s"].append(speedup)
                
        # Throughput
        flat_nodes = int(r['flat_nodes'])
        hash_nodes = int(r['hash_nodes'])
        
        # Aggregate NPS
        if r['flat_outcome'] != 'timeout' and r['hash_outcome'] != 'timeout':
            total_flat_nodes += flat_nodes
            total_hash_nodes += hash_nodes
            total_flat_time += flat_time
            total_hash_time += hash_time

    print("# Level 4 Benchmark Analysis (Hash-Only vs Flat Cache)\n")
    
    print("## 1. Speedup by Difficulty Bucket (Geo-Mean)")
    print("| Bucket | Instances | Speedup |")
    print("| :--- | :--- | :--- |")
    for b in ["<1s", "1-5s", "5-10s", ">10s"]:
        speeds = buckets[b]
        gm = geo_mean(speeds)
        print(f"| {b} | {len(speeds)} | {gm:.3f}x |")
    
    print("\n## 2. Per-Node Throughput (Aggregate NPS)")
    # We use non-timeout instances for aggregate NPS to avoid skewing by early exits
    flat_nps = total_flat_nodes / total_flat_time if total_flat_time > 0 else 0
    hash_nps = total_hash_nodes / total_hash_time if total_hash_time > 0 else 0
    nps_gain = hash_nps / flat_nps if flat_nps > 0 else 0
    
    print(f"- **Flat Cache NPS**: {flat_nps:,.0f}")
    print(f"- **Hash-Only NPS**: {hash_nps:,.0f}")
    print(f"- **Throughput Gain**: {nps_gain:.3f}x")
    
    print("\n## 3. Outcome Differences")
    if not mismatches:
        print("- **No outcome differences found.** All instances matched Solved/Unwinnable/Timeout status.")
    else:
        print(f"- **{len(mismatches)} instances with differing outcomes found:**")
        for m in mismatches:
            print(f"  - {m}")

if __name__ == "__main__":
    analyze()
