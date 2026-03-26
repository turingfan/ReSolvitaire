---
name: unit_test_mismatches_unknown_cause
description: 5 dual_cache tests fail with LRU=HIT, flat=MISS mismatches; root cause unknown
type: project
---

5 dual_cache unit tests show `LRU=HIT, flat=MISS` patterns at various operations:
- FreeCellAgreement (seed 1, op 7 onwards)
- BakersGameAgreement
- SomersetAgreement
- FlowerGardenAgreement
- SeahavenTowersAgreement

The mismatches occur pre-eviction. All outcome tests pass, so flat_cache reaches correct conclusions despite the mismatches.

**Status**: Root cause unknown. Not yet investigated. These are real discrepancies between how lru_cache and flat_cache handle state deduplication, but the reason is unclear.

**How to apply**: Flag these as known issues for future investigation. Do not assume they are pile-ordering or deduplication-related without proof.
