pkg_check_modules(MIRTEST REQUIRED mirtest)
pkg_check_modules(UMOCKDEV REQUIRED umockdev-1.0>=0.6)
if (NOT UMOCKDEV_FOUND)
  message(FATAL_ERROR "Umockdev not found, cannot build without disabling tests (via MIR_ENABLE_TESTS).")
endif()

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wnull-dereference HAS_W_NULL_DEREFERENCE)
check_cxx_compiler_flag(-Woverloaded-virtual HAS_W_OVERLOADED_VIRTUAL)
check_cxx_compiler_flag(-Winconsistent-missing-override HAS_W_INCONSISTENT_MISSING_OVERRIDE)
check_cxx_compiler_flag(-Wgnu-zero-variadic-macro-arguments HAS_W_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS)

if (HAS_W_NULL_DEREFERENCE)
  # Avoid clang complaints about poor quality gmock/gtest headers
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=null-dereference")
endif()
if (HAS_W_OVERLOADED_VIRTUAL)
  # Avoid clang complaints about poor quality gmock/gtest headers
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=overloaded-virtual")
endif()
if (HAS_W_INCONSISTENT_MISSING_OVERRIDE)
  # MOCK_METHOD()s cannot be marked as override; we cannot consistently mark overrides
  # in the tests.
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-inconsistent-missing-override")
endif()
if (HAS_W_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS)
  # INSTANTIATE_TEST_CASE_P hits this like a slice of lemon wrapped around a large gold brick.
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-gnu-zero-variadic-macro-arguments")
else()
  # GCC is a bit less flexible than clang and needs a bigger hammer...
  if (NOT MIR_USE_PRECOMPILED_HEADERS)
    # Work around the Google Test headers not being C++11 friendly (LP: #1658604)
    string(REPLACE "-pedantic" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
  endif()
endif()

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-lto")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-lto")

if (MIR_BUILD_PLATFORM_ANDROID)
  #avoid complaints about poor quality android headers
  include_directories(SYSTEM ${LIBHARDWARE_INCLUDE_DIRS})
  add_definitions(-DMIR_BUILD_PLATFORM_ANDROID)
endif()

# public headers (only public headers should be accessed by acceptance tests)
include_directories(
  ${PROJECT_SOURCE_DIR}/src/include/gl
  ${PROJECT_SOURCE_DIR}/src/platforms/common/client/
  ${MIRTEST_INCLUDE_DIRS}
)

option(MIR_BUILD_UNIT_TESTS "Build unit tests" ON)

# Private test headers used by integration and unit tests
include_directories(
  include
)

if (MIR_BUILD_UNIT_TESTS)
  add_subdirectory(unit-tests/)
endif (MIR_BUILD_UNIT_TESTS)

add_subdirectory(mir_test_doubles/)

mir_add_memcheck_test()
mir_add_detect_fd_leaks_test()
