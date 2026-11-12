#pragma once

// Tiny assertion helpers shared by the host-side tests.
// Deliberately dependency-free: no gtest, no Catch2 — the firmware project has
// no test framework vendored and pulling one in for a handful of pure functions
// is not worth the maintenance.

#include <cstdio>
#include <cstring>
#include <string>

namespace anxtest {

inline int& failures() {
  static int count = 0;
  return count;
}

inline int& checks() {
  static int count = 0;
  return count;
}

inline void reportFailure(const char* file, int line, const char* expr, const char* detail) {
  ++failures();
  std::fprintf(stderr, "FAIL %s:%d\n  expr: %s\n  detail: %s\n", file, line, expr, detail);
}

inline void beginSuite(const char* name) {
  std::printf("-- %s\n", name);
}

inline void endSuite(const char* name) {
  std::printf("   done: %s (%d checks, %d failures so far)\n", name, checks(), failures());
}

inline int summary() {
  std::printf("\n%d checks, %d failures\n", checks(), failures());
  if (failures() == 0) {
    std::printf("ALL TESTS PASSED\n");
    return 0;
  }
  std::printf("TESTS FAILED\n");
  return 1;
}

}  // namespace anxtest

#define ANX_CHECK(cond)                                                \
  do {                                                                 \
    ++anxtest::checks();                                               \
    if (!(cond)) {                                                     \
      anxtest::reportFailure(__FILE__, __LINE__, #cond, "condition false"); \
    }                                                                  \
  } while (0)

#define ANX_CHECK_MSG(cond, msg)                                       \
  do {                                                                 \
    ++anxtest::checks();                                               \
    if (!(cond)) {                                                     \
      anxtest::reportFailure(__FILE__, __LINE__, #cond, msg);           \
    }                                                                  \
  } while (0)

#define ANX_CHECK_EQ_INT(expected, actual)                                         \
  do {                                                                             \
    ++anxtest::checks();                                                           \
    const long long e_ = static_cast<long long>(expected);                         \
    const long long a_ = static_cast<long long>(actual);                           \
    if (e_ != a_) {                                                                \
      char buf_[160];                                                              \
      std::snprintf(buf_, sizeof(buf_), "expected %lld, got %lld", e_, a_);        \
      anxtest::reportFailure(__FILE__, __LINE__, #actual, buf_);                    \
    }                                                                              \
  } while (0)

#define ANX_CHECK_EQ_STR(expected, actual)                                         \
  do {                                                                             \
    ++anxtest::checks();                                                           \
    const std::string e_ = (expected);                                             \
    const std::string a_ = (actual);                                               \
    if (e_ != a_) {                                                                \
      char buf_[512];                                                              \
      std::snprintf(buf_, sizeof(buf_), "expected \"%s\", got \"%s\"", e_.c_str(), a_.c_str()); \
      anxtest::reportFailure(__FILE__, __LINE__, #actual, buf_);                    \
    }                                                                              \
  } while (0)
