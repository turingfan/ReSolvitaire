#!/bin/bash

build="release"
target="solvitaire"
build_variants=false
error=false

for arg in "$@"; do
    case "$arg" in
        --debug)      build="debug" ;;
        --release)    build="release" ;;
        --unit-tests) target="unit_tests" ;;
        --solvitaire) target="solvitaire" ;;
        --variants)   build_variants=true ;;
        *)            error=true ;;
    esac
done

if [ "$error" = true ]; then
    echo "Usage: ./build.sh [--release|--debug] [--solvitaire|--unit-tests] [--variants]"
    echo "(default args = --release --solvitaire)"
    echo "--variants: also build solvitaire-flat, solvitaire-hash-only, solvitaire-lru"
    exit 1
fi

if [ "$build" == "debug" ]; then
    build_type="Debug"
else
    build_type="Release"
fi

cmake "-DCMAKE_BUILD_TYPE=$build_type" "-Bcmake-build-$build" -H.
cmake --build "cmake-build-$build" --target solvitaire

if [ "$target" != "solvitaire" ]; then
    cmake --build "cmake-build-$build" --target "$target"
fi

if [ "$build_variants" = true ]; then
    cmake --build "cmake-build-$build" --target solvitaire-flat
    cmake --build "cmake-build-$build" --target solvitaire-hash-only
    cmake --build "cmake-build-$build" --target solvitaire-lru
fi

exit 0
