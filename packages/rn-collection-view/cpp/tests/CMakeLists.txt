cmake_minimum_required(VERSION 3.16)
project(RNCVTests CXX)

set(CMAKE_CXX_STANDARD 17)

# ── Source files under test ───────────────────────────────────────────────────
set(RNCV_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/../LayoutCache.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../SpatialIndex.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../layouts/ListLayout.cpp
)

# ── Test executable ───────────────────────────────────────────────────────────
# Uses the self-contained TestRunner.h — no external dependencies required.
add_executable(rncv_tests
  TestMain.cpp
  ListLayoutTest.cpp
  MVCCorrectionTest.cpp
  ${RNCV_SRC}
)

target_include_directories(rncv_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..           # cpp/ — LayoutCache.h, Geometry.h, etc.
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs        # provides jsi/jsi.h stub
  ${CMAKE_CURRENT_SOURCE_DIR}             # TestRunner.h
)

# Suppress RNCV debug logging in tests
target_compile_definitions(rncv_tests PRIVATE
  RNCV_ENABLE_NATIVE_LOGS=0
  RNCV_ENABLE_MVC_TRACE=0
)

# ── How to build and run ──────────────────────────────────────────────────────
# From this directory (cpp/tests/):
#
#   mkdir build && cd build && cmake .. && cmake --build . && ./rncv_tests
#
# Or single-command without CMake:
#
#   c++ -std=c++17 \
#       -I.. -I./stubs -I. \
#       -DRNCV_ENABLE_NATIVE_LOGS=0 -DRNCV_ENABLE_MVC_TRACE=0 \
#       TestMain.cpp ListLayoutTest.cpp MVCCorrectionTest.cpp \
#       ../LayoutCache.cpp ../SpatialIndex.cpp ../layouts/ListLayout.cpp \
#       -o rncv_tests && ./rncv_tests
#
# No 'pod install' needed — tests use only pure C++ code.
# TestRunner.h is self-contained (no GTest, no external deps).
