add_definitions(-DANDROID)

find_package(Protobuf REQUIRED)

include_directories(
  ${ANDROID_HEADERS_INCLUDE_DIRS}
  ${PROJECT_SOURCE_DIR}/src/platforms/android/include
)
add_definitions(
    -DMIR_SERVER_GRAPHICS_PLATFORM_ABI_STRING="${MIR_SERVER_GRAPHICS_PLATFORM_ABI}"
    -DMIR_SERVER_PLATFORM_PATH="${MIR_SERVER_PLATFORM_PATH}"
    )

set(
  ANDROID_UNIT_TEST_SOURCES
)

# disable all implicit fallthrough warnings for GCC > 7.0
if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7.0)
	add_compile_options(-Wno-implicit-fallthrough)
endif()

add_subdirectory(server)
add_subdirectory(client)

mir_add_wrapped_executable(mir_unit_tests_android
  ${ANDROID_UNIT_TEST_SOURCES}
  ##  $<TARGET_OBJECTS:mir-test-doubles-udev>
  $<TARGET_OBJECTS:mirgl>
)

add_dependencies(mir_unit_tests_android GMock)

target_link_libraries(
  mir_unit_tests_android
  mirsharedandroid-static
  client_platform_common
  server_platform_common
  mir-test-doubles-platform-static

  ${MIRTEST_LDFLAGS}
  ${PROTOBUF_LITE_LIBRARIES}
  ${GTEST_BOTH_LIBRARIES}
  ${GMOCK_LIBRARY}
  ${GMOCK_MAIN_LIBRARY}
  ${Boost_PROGRAM_OPTIONS_LIBRARY}
  ${LIBHARDWARE_LDFLAGS} ${LIBHARDWARE_LIBRARIES} -lhwc2
  ${ANDROID_PROPERTIES_LDFLAGS} ${ANDROID_PROPERTIES_LIBRARIES}
  dl
)

if (MIR_RUN_UNIT_TESTS)
  mir_discover_tests_with_fd_leak_detection(mir_unit_tests_android)
endif (MIR_RUN_UNIT_TESTS)
