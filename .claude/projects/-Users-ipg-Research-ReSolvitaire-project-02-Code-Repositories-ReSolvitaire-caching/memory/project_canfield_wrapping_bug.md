---
name: canfield_wrapping_bug
description: Canfield false positives caused by parent_table not handling wrapping builds (foundation_base_convert)
type: project
---

parent_table uses raw ranks to compute PARENT_0–3 descriptors, but canfield games use foundation_base_convert() for wrapping builds. When foundation base is non-Ace, the legal parents change (e.g. King becomes a legal parent of Ace). parent_table doesn't know about this, so wrapping builds get ROOT(2) instead of PARENT_i. Two different cards sitting on the same non-legal-parent both get ROOT(2), causing payload collisions (false positives).

**Why:** parent_table::get_parents() uses raw rank+1 and returns empty for Kings. It doesn't accept foundation_base or wrapping info.

**How to apply:** Fix parent_table to handle wrapping, or pass foundation_base_convert info through to it. This affects all canfield variants with random base.
