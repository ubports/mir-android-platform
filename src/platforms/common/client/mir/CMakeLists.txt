add_library(client_platform_common STATIC
  aging_buffer.cpp
  weak_egl.cpp
)
target_include_directories(client_platform_common 
  PUBLIC ${PROJECT_SOURCE_DIR}/src/include/client 
  PUBLIC ${PROJECT_SOURCE_DIR}/include/client)
