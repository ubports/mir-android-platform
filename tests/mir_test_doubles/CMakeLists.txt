include_directories(
  ${CMAKE_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/src/include/platform

    ${Boost_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}
)

set(
  MIR_TEST_DOUBLES_PLATFORM_SRCS

  #${CMAKE_CURRENT_SOURCE_DIR}/mock_egl.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/mock_gl.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/mock_android_hw.cpp
)

add_library(mir-public-test-doubles-platform OBJECT
  ${MIR_TEST_DOUBLES_PLATFORM_SRCS}
)

add_library(
  mir-test-doubles-platform-static STATIC
  $<TARGET_OBJECTS:mir-public-test-doubles-platform>
)


