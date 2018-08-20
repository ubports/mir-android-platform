add_library(
  mir-test-doubles-platform-static STATIC
  mock_egl.cpp
  mock_gl.cpp
  mock_android_hw.cpp
)

target_include_directories(mir-test-doubles-platform-static
  PRIVATE ${CMAKE_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src/include/platform
  SYSTEM PRIVATE ${ANDROID_HEADERS_INCLUDE_DIRS}
  ${Boost_INCLUDE_DIRS}
)
