# Solvitaire: A general solver for perfect-information solitaire games
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html) [![GitHub release](https://d25lcipzij17d.cloudfront.net/badge.svg?id=gh&type=6&v=0.10.2)](https://github.com/thecharlesblake/Solvitaire/releases/tag/v0.10/2)
[![DOI](https://zenodo.org/badge/103662666.svg)](https://zenodo.org/badge/latestdoi/103662666)<!-- ALL-CONTRIBUTORS-BADGE:START - Do not remove or modify this section -->
[![All Contributors](https://img.shields.io/badge/all_contributors-4-orange.svg?style=flat-square)](#contributors-)
<!-- ALL-CONTRIBUTORS-BADGE:END -->

## Building and Running

Solvitaire requires a C++14 compiler, CMake 3.14+, and the Boost libraries.
The compiled Boost component needed is `program_options`; several header-only components
(`multi_index`, `optional`, `pool`, `property_tree`, `random`) are also used and are
included automatically with any standard Boost installation.
The primary development branch is `dev`, which is tested on macOS (Apple Silicon) and Linux (x86_64 / arm64).

---

### (a) macOS

**Prerequisites** (Homebrew):
```bash
brew install cmake boost
```

**Build:**
```bash
./build.sh                          # release build (default)
./build.sh --release --unit-tests   # also build test binary
```

**Test:**
```bash
cd cmake-build-release
ctest -R '^unit_tests$' --output-on-failure
```

**Run:**
```bash
./cmake-build-release/bin/solvitaire --type klondike --random 42
```

---

### (b) Linux

**Prerequisites** (Ubuntu 22.04 / Debian):
```bash
sudo apt-get install build-essential cmake libboost-program-options-dev git python3
```

**Build:**
```bash
./build.sh                          # release build
./build.sh --release --unit-tests   # also build test binary
```

**Test:**
```bash
cd cmake-build-release
ctest -R '^unit_tests$' --output-on-failure
```

**Run:**
```bash
./cmake-build-release/bin/solvitaire --type klondike --random 42
```

---

### (c) Container (Linux build on macOS or any host)

Requires the [Apple container CLI](https://developer.apple.com/documentation/virtualization), Docker, or Podman.

**Build image and run unit tests:**
```bash
./scripts/container-build.sh --test
```

**Build image and run Level 1 regression suite:**
```bash
./scripts/container-build.sh --regression
```

**Build image only:**
```bash
./scripts/container-build.sh
```

**Interactive shell inside the container:**
```bash
container run --rm -it solvitaire-dev bash
```

By default the script forces a clean build on every run (to avoid stale-cache issues with container CLI v0.9). Pass `--use-cache` to reuse the apt install layer when on a slow network.

---

### Usage

List supported game types:
```bash
./cmake-build-release/bin/solvitaire --available-game-types
```

Solve a random Klondike deal (seed 42):
```bash
./cmake-build-release/bin/solvitaire --type klondike --random 42
```

Solve from a JSON deal file:
```bash
./cmake-build-release/bin/solvitaire --type free-cell path/to/deal.json
```

Key options: `--type`, `--random <seed>`, `--json`, `--timeout <ms>`,
`--cache-capacity <n>`, `--streamliners {none|auto-foundations|suit-symmetry|both|smart}`,
`--solvability <N>`.

---

## Help

If you have any problems, please open an issue on GitHub.

## Contributors ✨

Thanks goes to these wonderful people ([emoji key](https://allcontributors.org/docs/en/emoji-key)):

<!-- ALL-CONTRIBUTORS-LIST:START - Do not remove or modify this section -->
<!-- prettier-ignore-start -->
<!-- markdownlint-disable -->
<table>
  <tr>
    <td align="center"><a href="http://thecharlesblake.co.uk/"><img src="https://avatars1.githubusercontent.com/u/17354715?v=4" width="100px;" alt=""/><br /><sub><b>Charlie Blake</b></sub></a><br /><a href="https://github.com/thecharlieblake/Solvitaire/commits?author=thecharlesblake" title="Code">💻</a> <a href="#design-thecharlesblake" title="Design">🎨</a> <a href="#ideas-thecharlesblake" title="Ideas, Planning, & Feedback">🤔</a></td>
    <td align="center"><a href="http://ian.gent"><img src="https://avatars0.githubusercontent.com/u/2893913?v=4" width="100px;" alt=""/><br /><sub><b>Ian Gent</b></sub></a><br /><a href="https://github.com/thecharlieblake/Solvitaire/commits?author=turingfan" title="Code">💻</a> <a href="#design-turingfan" title="Design">🎨</a> <a href="#ideas-turingfan" title="Ideas, Planning, & Feedback">🤔</a></td>
    <td align="center"><a href="http://www.shlomifish.org/"><img src="https://avatars1.githubusercontent.com/u/3150?v=4" width="100px;" alt=""/><br /><sub><b>Shlomi Fish</b></sub></a><br /><a href="https://github.com/thecharlieblake/Solvitaire/commits?author=shlomif" title="Code">💻</a></td>
    <td align="center"><a href="https://github.com/galcohensius"><img src="https://avatars1.githubusercontent.com/u/25342140?v=4" width="100px;" alt=""/><br /><sub><b>Gal Cohensius</b></sub></a><br /><a href="https://github.com/thecharlieblake/Solvitaire/commits?author=galcohensius" title="Documentation">📖</a></td>
  </tr>
</table>

<!-- markdownlint-enable -->
<!-- prettier-ignore-end -->
<!-- ALL-CONTRIBUTORS-LIST:END -->

This project follows the [all-contributors](https://github.com/all-contributors/all-contributors) specification. Contributions of any kind welcome!
