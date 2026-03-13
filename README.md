# Solvitaire: A general solver for perfect-information solitaire games
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html) [![GitHub release](https://d25lcipzij17d.cloudfront.net/badge.svg?id=gh&type=6&v=0.10.2)](https://github.com/thecharlesblake/Solvitaire/releases/tag/v0.10/2)
[![DOI](https://zenodo.org/badge/103662666.svg)](https://zenodo.org/badge/latestdoi/103662666)<!-- ALL-CONTRIBUTORS-BADGE:START - Do not remove or modify this section -->
[![All Contributors](https://img.shields.io/badge/all_contributors-4-orange.svg?style=flat-square)](#contributors-)
<!-- ALL-CONTRIBUTORS-BADGE:END -->

## Building and Running

Solvitaire can be built natively using CMake and a C++ compiler that supports C++14. It also requires the Boost libraries.

### Prerequisites

- A C++ compiler (GCC 7+, Clang 5+, or MSVC 2017+)
- CMake 3.10 or higher
- Boost libraries (system, filesystem, program_options, unit_test_framework)

On macOS (using Homebrew):
```
$ brew install cmake boost
```

On Ubuntu/Debian:
```
$ sudo apt-get install cmake libboost-all-dev
```

### Build

To build Solvitaire, you can use the provided `build.sh` script or run CMake commands manually.

Using `build.sh`:
```
$ ./build.sh [--release|--debug] [--solvitaire|--unit-tests]
(default args = "--release" "--solvitaire")
```

Manually with CMake:
```
$ cmake -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build
```

### Run

The built binary will be located in the root directory (if using `build.sh`) or in the `build` directory (if manual).

To run Solvitaire:
```
$ ./solvitaire --help
```

For unit tests:
```
$ cd src/test
$ ../../unit-tests
```

to see what solvitaire can do, use the `--help` command:

```
$ ./enter-container.sh "./solvitaire --help"
Usage: solvitaire [options] input-file1 input-file2 ...
options:
  --help                    produce help message
  --type arg                specify the type of the solitaire game to be solved
                            from the list of preset games. Must supply either 
                            this 'type' option, or the 'custom-rules' option
  --available-game-types    outputs a list of the different preset game types 
                            that can be solved
  --describe-game-rules arg outputs the JSON that describes the rules of the 
                            supplied preset game type
  --custom-rules arg        the path to a JSON file describing the rules of the
                            solitaire to be solved. Must supply either 'type' 
                            or 'custom-rules' option
  --random arg              create and solve a random solitaire deal based on a
                            seed. Must supply either 'random','solvability', 
                            'benchmark' or list of deals to be solved.
  ... etc ...
```

...
for example, if you wish to generate a random deal (with seed 1) of the game Klondike
and attempt to solve it, run:

```
$ ./solvitaire --type klondike --random 1
```
  [info] Attempting to solve with seed: 1...
  Deal:
  --- Foundations ---------
  []	[]	[]	[]	
  --- Tableau Piles -------
  9D	##	##	##	##	##	##	
  	AS	##	##	##	##	##	
  		JC	##	##	##	##	
  			6D	##	##	##	
  				JH	##	##	
  					AH	##	
  						JD	
  --- Stock | Waste -------
  6H	[]	
  KS		
  10C		
  QH		
  ...	
  ===================================
  Solution Type: unsolvable
  States Searched: 417235
  Unique States Searched: 164359
  Backtracks: 417234
  Dominance Moves: 63707
  States Removed From Cache: 0
  Final States In Cache: 100652
  Final Buckets In Cache: 196613
  Maximum Search Depth: 46
  Final Search Depth: 0
  Time Taken (milliseconds): 903
```

...
full documentation of solvitaire is not yet available, but information can be provided
to those who reach out over email (see below).

## Help

If you have any problems getting these steps to work, don't hesitate to get in
touch via <thecharlieblake@gmail.com>

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
