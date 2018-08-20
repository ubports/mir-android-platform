add_library(
  mirsharedandroid-static STATIC

  mir_native_window.cpp
  refcounted_buffer.cpp
  android_native_buffer.cpp
  syncfence.cpp
  egl_sync_fence.cpp
  egl_sync_extensions.cpp
  native_window_report.cpp
)
target_compile_definitions(mirsharedandroid-static PUBLIC ANDROID)
target_include_directories(mirsharedandroid-static PUBLIC
  PUBLIC ${server_common_include_dirs}
         include/
         SYSTEM PRIVATE ${ANDROID_HEADERS_INCLUDE_DIRS} # for linux/sync.h
  )
