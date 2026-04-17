// generic_flat_cache_compile_test.cpp
//
// Compile-only translation unit for P2-A.  Its sole purpose is to force
// instantiation of generic_flat_cache<Policy> for all three policies so that
// the mandatory cluster-size static_asserts in generic_flat_cache.h are
// evaluated at build time.  No test cases are defined here — those are in P2-B.

#include "../../main/game/generic_flat_cache.h"
