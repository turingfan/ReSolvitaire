# KI-23 Short Prompt (for Claude on the Web)

Copy the text below into a new conversation on claude.ai with the project loaded.

---

Fix KI-23: centralise suit-symmetry detection for hole games. Read the detailed spec at `docs/multiplicity-encoding/ki23-suit-symmetry-prompt.md` and implement all 5 steps. Read CLAUDE.md for build/test commands.

Summary: Add `sol_rules::inherent_suit_symmetry()` returning `rules.hole`. Add `|| rules.inherent_suit_symmetry()` to all 4 dispatch `suit_sym` computations (main.cpp, benchmark.cpp x2, solvability_calc.cpp) and to `make_desc_ctx()` in game_state.cpp. Replace `gs.rules.hole` with `gs.rules.inherent_suit_symmetry()` in global_cache.h hasher and global_cache.cpp add_card(). Add unit tests. Run all 3 test gates. Regenerate oracles if black-hole node counts change (outcomes must not change). Create a PR to `dev` when done.

If anything in the spec is unclear or you think a step is wrong, say so rather than guessing.
