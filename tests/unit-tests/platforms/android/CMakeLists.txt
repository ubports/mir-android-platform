add_definitions(-DANDROID)

include_directories(
  ${ANDROID_HEADERS_INCLUDE_DIRS}
  ${PROJECT_SOURCE_DIR}/src/platforms/android/include
)

set(
  ANDROID_UNIT_TEST_SOURCES
)

add_subdirectory(server)
add_subdirectory(client)

mir_add_wrapped_executable(mir_unit_tests_android
  ${ANDROID_UNIT_TEST_SOURCES}
  ##  $<TARGET_OBJECTS:mir-test-doubles-udev>
  $<TARGET_OBJECTS:mirgl>
  ${MIR_PLATFORM_OBJECTS}
  ${MIR_SERVER_OBJECTS}
)

add_dependencies(mir_unit_tests_android GMock)

target_link_libraries(
  mir_unit_tests_android

  mirsharedandroid-static
  ${Boost_PROGRAM_OPTIONS_LIBRARY}
  ${LIBHARDWARE_LDFLAGS} ${LIBHARDWARE_LIBRARIES}
  ${ANDROID_PROPERTIES_LDFLAGS} ${ANDROID_PROPERTIES_LIBRARIES}
  mir-test-doubles-platform-static

  ${MIRTEST_LIBRARIES}

  client_platform_common
  server_platform_common
  dl
)

if (MIR_RUN_UNIT_TESTS)
  mir_discover_tests_with_fd_leak_detection(mir_unit_tests_android)
endif (MIR_RUN_UNIT_TESTS)
