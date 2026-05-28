# Domain Questions Log — Bitmap Cache

This file is for logging domain questions and bugs encountered during
implementation. Append entries as they arise; Opus reviews at each stage.

## Format

```
### [Stage N] Short description
- **Context:** What you were doing
- **Question/Issue:** What you don't understand or what seems wrong
- **Your best guess:** What you did to proceed
- **Code marker:** `// DOMAIN_QUESTION:` location (if applicable)
```

---

### [Stage 2] Spec says to `#include "bitmap_cache.h"` in cache_policy.h, but that creates a circular include

- **Context:** Adding `BitmapPolicy` to `cache_policy.h`. The spec says to add `#include "bitmap_cache.h"` at the top of `cache_policy.h`.
- **Question/Issue:** `game_state.h` already includes `cache_policy.h` (line 47). `bitmap_cache.h` includes `cache_interface.h`, which includes `game_state.h`. So the chain would be: `cache_policy.h` → `bitmap_cache.h` → `cache_interface.h` → `game_state.h` → `cache_policy.h` — a circular dependency.
- **Your best guess:** Used a forward declaration `class bitmap_cache;` in `cache_policy.h` instead (matching the existing pattern for `lru_cache` and `generic_flat_cache`). Added `#include "game/bitmap_cache.h"` to the three dispatch translation units (main.cpp, benchmark.cpp, solvability_calc.cpp) where the template is actually instantiated.
- **Code marker:** `cache_policy.h` — forward declaration comment above `BitmapPolicy`.
