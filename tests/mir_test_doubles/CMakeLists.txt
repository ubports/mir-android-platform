include_directories(
  ${CMAKE_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src/include/platform
  ${Boost_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}
)

add_library(
  mir-test-doubles-platform-static STATIC
  mock_egl.cpp
  mock_gl.cpp
  mock_android_hw.cpp
)
