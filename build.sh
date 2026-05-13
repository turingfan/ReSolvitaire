#!/bin/bash
set -e

build="release"
target="solvitaire"
build_variants=false
build_trace_variants=false
solvitaire_trace=OFF
error=false

for arg in "$@"; do
    case "$arg" in
        --debug)      build="debug" ;;
        --release)    build="release" ;;
        --trace)      build="trace"; solvitaire_trace=ON; build_trace_variants=true; target="unit_tests" ;;
        --unit-tests) target="unit_tests"; build_variants=true ;;
        --solvitaire) target="solvitaire" ;;
        --variants)   build_variants=true ;;
        *)            error=true ;;
    esac
done

if [ "$error" = true ]; then
    echo "Usage: ./build.sh [--release|--debug|--trace] [--solvitaire|--unit-tests] [--variants]"
    echo "(default args = --release --solvitaire)"
    echo "--trace:      build trace-variant binaries into cmake-build-trace"
    echo "              (Release + SOLVITAIRE_TRACE=ON; also builds unit_tests)"
    echo "--unit-tests: build unit_tests binary (also builds regular variants)"
    echo "--variants:   build solvitaire-flat, solvitaire-hash-only, solvitaire-lru"
    exit 1
fi

if [ "$build" == "debug" ]; then
    build_type="Debug"
else
    build_type="Release"
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

exit 0
