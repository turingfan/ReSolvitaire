#!/bin/bash
set -e

build="release"
target="solvitaire"
build_variants=false
build_trace_variants=false
solvitaire_trace=OFF
clean=false
error=false

for arg in "$@"; do
    case "$arg" in
        --debug)      build="debug" ;;
        --release)    build="release" ;;
        --trace)      build="trace"; solvitaire_trace=ON; build_trace_variants=true; target="unit_tests" ;;
        --unit-tests) target="unit_tests"; build_variants=true ;;
        --solvitaire) target="solvitaire" ;;
        --variants)   build_variants=true ;;
        --clean)      clean=true ;;
        *)            error=true ;;
    esac
done

if [ "$error" = true ]; then
    echo "Usage: ./build.sh [--release|--debug|--trace] [--solvitaire|--unit-tests] [--variants] [--clean]"
    echo "(default args = --release --solvitaire)"
    echo "--trace:      build trace-variant binaries into cmake-build-trace"
    echo "              (Release + SOLVITAIRE_TRACE=ON; also builds unit_tests)"
    echo "--unit-tests: build unit_tests binary (also builds regular variants)"
    echo "--variants:   build solvitaire-flat, solvitaire-hash-only, solvitaire-lru"
    echo "--clean:      wipe cmake-build-<cfg>/ first for a guaranteed-fresh build"
    echo "              (use when incremental detection may be stale, e.g. after"
    echo "               copying sources between trees)"
    exit 1
fi

if [ "$build" == "debug" ]; then
    build_type="Debug"
else
    build_type="Release"
fi

if [ "$clean" = true ]; then
    echo "[build] --clean: removing cmake-build-$build/ for a fresh build"
    rm -rf "cmake-build-$build"
fi

cmake "-DCMAKE_BUILD_TYPE=$build_type" "-DSOLVITAIRE_TRACE=$solvitaire_trace" "-Bcmake-build-$build" -H.
cmake --build "cmake-build-$build" --target solvitaire

if [ "$target" != "solvitaire" ]; then
    cmake --build "cmake-build-$build" --target "$target"
fi

if [ "$build_variants" = true ]; then
    cmake --build "cmake-build-$build" --target solvitaire-flat
    cmake --build "cmake-build-$build" --target solvitaire-hash-only
    cmake --build "cmake-build-$build" --target solvitaire-lru
fi

if [ "$build_trace_variants" = true ]; then
    cmake --build "cmake-build-$build" --target solvitaire-trace
    cmake --build "cmake-build-$build" --target solvitaire-flat-trace
    cmake --build "cmake-build-$build" --target solvitaire-hash-only-trace
    cmake --build "cmake-build-$build" --target solvitaire-lru-trace
fi

# Summary: show the binaries that now exist and their timestamps, so a stale
# binary (e.g. one a target didn't rebuild) is visible at a glance.
echo ""
echo "[build] Binaries in cmake-build-$build/bin/ (name | size | mtime):"
if ls "cmake-build-$build/bin/" >/dev/null 2>&1; then
    for f in "cmake-build-$build/bin/"*; do
        [ -f "$f" ] || continue
        # Portable mtime: GNU stat (-c) if it works, else BSD stat (-f).
        if mtime=$(stat -c '%y' "$f" 2>/dev/null); then
            mtime=${mtime%.*}
        else
            mtime=$(stat -f '%Sm' "$f" 2>/dev/null)
        fi
        printf '          %-26s %8s  %s\n' "$(basename "$f")" "$(wc -c <"$f" | tr -d ' ')" "$mtime"
    done
else
    echo "          (none found)"
fi

exit 0
