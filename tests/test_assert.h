// Copyright (c) 2026 solar@heliacal.net
// SPDX-License-Identifier: MIT

#pragma once

// The tests use assert() for setup as well as verification. Keep those calls
// active when the library is tested in an optimized build.
#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
