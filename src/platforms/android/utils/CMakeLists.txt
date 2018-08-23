list(
  APPEND ANDROID_DIAGNOSTICS_SRCS
  ${CMAKE_CURRENT_SOURCE_DIR}/test_android_hardware_sanity.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/patterns.cpp
)

add_definitions(-DMIR_SERVER_GRAPHICS_PLATFORM_VERSION="${MIR_SERVER_GRAPHICS_PLATFORM_VERSION}")

include_directories(
  ${PROJECT_SOURCE_DIR}/src/include/common
  ${PROJECT_SOURCE_DIR}/tests/include
  ${PROJECT_SOURCE_DIR}/src/include/server
  ${PROJECT_SOURCE_DIR}/src/platforms/android/include
)

add_executable(
  mir_android_diagnostics
  ${ANDROID_DIAGNOSTICS_SRCS})

target_link_libraries(
  mir_android_diagnostics
  ${MIRSERVER_LDFLAGS}
  mirdraw
  mir-test-static
  mir-test-framework-static
  ${LIBHARDWARE_LIBRARIES}
)

target_include_directories(mir_android_diagnostics SYSTEM PRIVATE
  ${ANDROID_INCLUDE_DIRS})

mir_add_wrapped_executable(mir_demo_standalone_render_overlays
  render_overlays.cpp
)

target_include_directories(mir_demo_standalone_render_overlays SYSTEM PRIVATE
  ${ANDROID_INCLUDE_DIRS})

target_link_libraries(mir_demo_standalone_render_overlays
  ${MIRCLIENT_LDFLAGS}
  ${MIRCOMMON_LDFLAGS}
  ${MIRPLATFORM_LDFLAGS}
)

install(PROGRAMS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/mir_android_diagnostics
  DESTINATION ${CMAKE_INSTALL_BINDIR}
)
