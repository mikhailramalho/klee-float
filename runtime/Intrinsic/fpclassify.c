/*===-- fpclassify.c ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===*/
#include "klee/klee.h"

// These are implementations of internal functions found in libm for classifying
// floating point numbers. They have different names to avoid name collisions
// during linking.

// __isnanf
int klee_internal_isnanf(float f) {
  return klee_is_nan_float(f);
}

// __isnan
int klee_internal_isnan(double d) {
  return klee_is_nan_double(d);
}

// __isinff
// returns 1 if +inf, 0 is not infinite, -1 if -inf
int klee_internal_isinff(float f) {
  _Bool isinf = klee_is_infinite_float(f);
  return isinf ? (f > 0 ? 1 : -1) : 0;
}

// __isinf
// returns 1 if +inf, 0 is not infinite, -1 if -inf
int klee_internal_isinf(double d) {
  _Bool isinf = klee_is_infinite_double(d);
  return isinf ? (d > 0 ? 1 : -1) : 0;
}

// HACK: Taken from ``math.h``. I don't want
// include all of ``math.h`` just for this enum
// so just make a copy here for now
enum {
  FP_NAN = 0,
  FP_INFINITE = 1,
  FP_ZERO = 2,
  FP_SUBNORMAL = 3,
  FP_NORMAL = 4
};

// __fpclassifyf
int klee_internal_fpclassifyf(float f) {
  // Do we want a version of this that doesn't fork?
  if (klee_is_nan_float(f)) {
    return FP_NAN;
  } else if (klee_is_infinite_float(f)) {
    return FP_INFINITE;
  } else if (f == 0.0f) {
    return FP_ZERO;
  } else if (klee_is_normal_float(f)) {
    return FP_NORMAL;
  }
  return FP_SUBNORMAL;
}

// __fpclassify
int klee_internal_fpclassify(double f) {
  // Do we want a version of this that doesn't fork?
  if (klee_is_nan_double(f)) {
    return FP_NAN;
  } else if (klee_is_infinite_double(f)) {
    return FP_INFINITE;
  } else if (f == 0.0) {
    return FP_ZERO;
  } else if (klee_is_normal_double(f)) {
    return FP_NORMAL;
  }
  return FP_SUBNORMAL;
}

// __finitef
int klee_internal_finitef(float f) {
  return (!klee_is_nan_float(f)) & (!klee_is_infinite_float(f));
}

// __finite
int klee_internal_finite(double f) {
  return (!klee_is_nan_double(f)) & (!klee_is_infinite_double(f));
}