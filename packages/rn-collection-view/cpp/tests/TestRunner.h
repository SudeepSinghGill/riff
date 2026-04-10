/**
 * TestRunner.h — Minimal header-only test runner for RNCV C++ unit tests.
 *
 * Provides GTest-compatible macros (TEST, EXPECT_*, ASSERT_*) so test files
 * need no external dependencies. Tests run as a plain C++ executable.
 *
 * Build (from cpp/tests/):
 *   c++ -std=c++17 \
 *       -I.. -I./stubs \
 *       -DRNCV_ENABLE_NATIVE_LOGS=0 -DRNCV_ENABLE_MVC_TRACE=0 \
 *       TestMain.cpp ListLayoutTest.cpp MVCCorrectionTest.cpp \
 *       ../LayoutCache.cpp ../SpatialIndex.cpp ../layouts/ListLayout.cpp \
 *       -o rncv_tests && ./rncv_tests
 */
#pragma once

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <stdexcept>

namespace rncv_test {

// ── Test registry ─────────────────────────────────────────────────────────────

struct TestCase {
  std::string suite;
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}

inline void registerTest(const std::string& suite, const std::string& name,
                          std::function<void()> fn) {
  registry().push_back({suite, name, std::move(fn)});
}

// ── Assertion state (per-test) ────────────────────────────────────────────────

struct TestState {
  bool failed = false;
  std::string failures;
};

inline TestState* currentTest = nullptr;

inline void recordFailure(const std::string& msg) {
  if (currentTest) {
    currentTest->failed = true;
    currentTest->failures += "  FAIL: " + msg + "\n";
  }
}

// ── Assertion helpers ─────────────────────────────────────────────────────────

inline void _expect(bool cond, const char* expr, const char* file, int line,
                     const std::string& msg = "") {
  if (!cond) {
    std::ostringstream oss;
    oss << file << ":" << line << ": " << expr;
    if (!msg.empty()) oss << " — " << msg;
    recordFailure(oss.str());
  }
}

template<typename A, typename B>
inline void _expectNear(A a, B b, double tol, const char* exprA, const char* exprB,
                         const char* file, int line, const std::string& msg = "") {
  if (std::abs(static_cast<double>(a) - static_cast<double>(b)) > tol) {
    std::ostringstream oss;
    oss << file << ":" << line << ": " << exprA << " ≈ " << exprB
        << " (got " << a << " vs " << b << ", tol=" << tol << ")";
    if (!msg.empty()) oss << " — " << msg;
    recordFailure(oss.str());
  }
}

template<typename A, typename B>
inline void _expectEq(A a, B b, const char* exprA, const char* exprB,
                        const char* file, int line, const std::string& msg = "") {
  if (!(a == b)) {
    std::ostringstream oss;
    oss << file << ":" << line << ": " << exprA << " == " << exprB
        << " (got " << a << " vs " << b << ")";
    if (!msg.empty()) oss << " — " << msg;
    recordFailure(oss.str());
  }
}

template<typename A, typename B>
inline void _expectNe(A a, B b, const char* exprA, const char* exprB,
                        const char* file, int line, const std::string& msg = "") {
  if (a == b) {
    std::ostringstream oss;
    oss << file << ":" << line << ": " << exprA << " != " << exprB
        << " (both == " << a << ")";
    if (!msg.empty()) oss << " — " << msg;
    recordFailure(oss.str());
  }
}

template<typename A, typename B>
inline void _expectGe(A a, B b, const char* exprA, const char* exprB,
                        const char* file, int line, const std::string& msg = "") {
  if (!(a >= b)) {
    std::ostringstream oss;
    oss << file << ":" << line << ": " << exprA << " >= " << exprB
        << " (got " << a << " < " << b << ")";
    if (!msg.empty()) oss << " — " << msg;
    recordFailure(oss.str());
  }
}

template<typename A, typename B>
inline void _expectLe(A a, B b, const char* exprA, const char* exprB,
                        const char* file, int line, const std::string& msg = "") {
  if (!(a <= b)) {
    std::ostringstream oss;
    oss << file << ":" << line << ": " << exprA << " <= " << exprB
        << " (got " << a << " > " << b << ")";
    if (!msg.empty()) oss << " — " << msg;
    recordFailure(oss.str());
  }
}

inline void _assertTrue(bool cond, const char* expr, const char* file, int line,
                          const std::string& msg = "") {
  if (!cond) {
    std::ostringstream oss;
    oss << file << ":" << line << ": ASSERT " << expr << " is false";
    if (!msg.empty()) oss << " — " << msg;
    recordFailure(oss.str());
    throw std::runtime_error("assertion failed");
  }
}

// ── Runner ────────────────────────────────────────────────────────────────────

inline int runAll() {
  int passed = 0, failed = 0;
  std::string lastSuite;

  for (auto& tc : registry()) {
    if (tc.suite != lastSuite) {
      std::cout << "\n[" << tc.suite << "]\n";
      lastSuite = tc.suite;
    }
    TestState state;
    currentTest = &state;
    bool threw = false;
    try {
      tc.fn();
    } catch (const std::exception& ex) {
      if (!state.failed) {
        state.failed = true;
        state.failures += "  EXCEPTION: " + std::string(ex.what()) + "\n";
      }
      threw = true;
    } catch (...) {
      state.failed = true;
      state.failures += "  EXCEPTION: unknown\n";
      threw = true;
    }
    currentTest = nullptr;

    if (state.failed) {
      std::cout << "  FAIL " << tc.name << (threw ? " (aborted)" : "") << "\n"
                << state.failures;
      ++failed;
    } else {
      std::cout << "  PASS " << tc.name << "\n";
      ++passed;
    }
  }

  std::cout << "\n" << (failed == 0 ? "ALL PASSED" : "FAILURES")
            << " — " << passed << " passed, " << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}

// ── Registration helper ───────────────────────────────────────────────────────

struct Registrar {
  Registrar(const char* suite, const char* name, std::function<void()> fn) {
    registerTest(suite, name, std::move(fn));
  }
};

} // namespace rncv_test

// ── Public macros — GTest-compatible interface ────────────────────────────────

#define TEST(Suite, Name) \
  static void _test_##Suite##_##Name(); \
  static ::rncv_test::Registrar _reg_##Suite##_##Name(#Suite, #Name, _test_##Suite##_##Name); \
  static void _test_##Suite##_##Name()

#define EXPECT_TRUE(expr) \
  ::rncv_test::_expect(!!(expr), #expr, __FILE__, __LINE__)

#define EXPECT_FALSE(expr) \
  ::rncv_test::_expect(!(expr), "!" #expr, __FILE__, __LINE__)

#define EXPECT_EQ(a, b) \
  ::rncv_test::_expectEq((a), (b), #a, #b, __FILE__, __LINE__)

#define EXPECT_NE(a, b) \
  ::rncv_test::_expectNe((a), (b), #a, #b, __FILE__, __LINE__)

#define EXPECT_GE(a, b) \
  ::rncv_test::_expectGe((a), (b), #a, #b, __FILE__, __LINE__)

#define EXPECT_LE(a, b) \
  ::rncv_test::_expectLe((a), (b), #a, #b, __FILE__, __LINE__)

#define EXPECT_NEAR(a, b, tol) \
  ::rncv_test::_expectNear((a), (b), (tol), #a, #b, __FILE__, __LINE__)

// Versions with streaming message support: EXPECT_EQ(a, b) << "msg"
// Simple approach: wrap in a no-op streaming object.
struct _MsgSink {
  template<typename T> _MsgSink& operator<<(const T&) { return *this; }
};

// Override macros that support << to use the streaming versions.
// For simplicity, the message is appended only if the check fails.
// We re-define EXPECT_NEAR/EXPECT_EQ etc. to return a _MsgSink.
// (The bare versions above already handle the common case; these overrides
//  capture the << message by passing it directly.)

#undef EXPECT_NEAR
#define EXPECT_NEAR(a, b, tol) \
  ([&]() -> ::_MsgSink { \
    ::rncv_test::_expectNear((a), (b), (tol), #a, #b, __FILE__, __LINE__); \
    return {}; \
  }())

#undef EXPECT_EQ
#define EXPECT_EQ(a, b) \
  ([&]() -> ::_MsgSink { \
    ::rncv_test::_expectEq((a), (b), #a, #b, __FILE__, __LINE__); \
    return {}; \
  }())

#undef EXPECT_NE
#define EXPECT_NE(a, b) \
  ([&]() -> ::_MsgSink { \
    ::rncv_test::_expectNe((a), (b), #a, #b, __FILE__, __LINE__); \
    return {}; \
  }())

#undef EXPECT_GE
#define EXPECT_GE(a, b) \
  ([&]() -> ::_MsgSink { \
    ::rncv_test::_expectGe((a), (b), #a, #b, __FILE__, __LINE__); \
    return {}; \
  }())

#undef EXPECT_LE
#define EXPECT_LE(a, b) \
  ([&]() -> ::_MsgSink { \
    ::rncv_test::_expectLe((a), (b), #a, #b, __FILE__, __LINE__); \
    return {}; \
  }())

#undef EXPECT_TRUE
#define EXPECT_TRUE(expr) \
  ([&]() -> ::_MsgSink { \
    ::rncv_test::_expect(!!(expr), #expr, __FILE__, __LINE__); \
    return {}; \
  }())

#undef EXPECT_FALSE
#define EXPECT_FALSE(expr) \
  ([&]() -> ::_MsgSink { \
    ::rncv_test::_expect(!(expr), "!" #expr, __FILE__, __LINE__); \
    return {}; \
  }())

#define ASSERT_TRUE(expr) \
  ::rncv_test::_assertTrue(!!(expr), #expr, __FILE__, __LINE__)

#define ASSERT_FALSE(expr) \
  ::rncv_test::_assertTrue(!(expr), "!" #expr, __FILE__, __LINE__)
