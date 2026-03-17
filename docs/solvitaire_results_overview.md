# Solvitaire Results Exploration Walkthrough

I have explored the Solvitaire results repository as requested. Here is a summary of the findings.

## Repository Location
The results are located in:
`/Users/ipg/Research/ReSolvitaire-project/Patience/solvitaire-paper-v10-WorkingDir/solvitaire-paper-v10-Feb2026/`

## Documentation Structure
The repository is well-documented with `000ReadMe` files at various levels:
- **Root**: Contains dataset description, citation info (figshare), and general overview.
- **ExperimentalResults/**: Explains the division by game and version.
- **AnalysisResults/**: Defines summary columns like solvability, CPU seconds, and node counts.
- **GameJSON/**: Describes the game rules and versioning.
- **Specific Games**: Some games like `British Canister` and `Fortune's Favor` have local `000ReadMe` files explaining specific data pruning or experimental conditions.

## Data Extraction Verification
I verified that experimental data can be extracted from the compressed CSV files.

### Sample Command (Mac)
```bash
gzcat ExperimentalResults/free-cell/v0.08.1/free-cell-smart-v0.08.1-10M-30m25M-A.csv.gz | head -n 5
```

### Observed CSV Header
The CSVs include detailed search metrics:
- `Attempted Seed`
- `Outcome` (e.g., `solved`, `unsolvable`, `timeout`)
- `Time Taken(ms)`
- `States Searched`
- `Unique States Searched`
- `Backtracks`
- `Maximum Search Depth`
- `Overall Result`

### Example Data Row
```csv
59, solved, 2, 217, 176, 59, 56, 0, 120, 193, 158, 158, , , , , , , , , , , , solved
```

## Summary of Findings
- The repository contains a massive amount of data (6GB+ compressed) covering many solitaire variants.
- Data is cleanly organized by game and solver version.
- Documentation is thorough and explains any anomalies or specific experimental setups.
- I am able to programmatically access and parse the results as needed.
