add_library(server_platform_common STATIC
  anonymous_shm_file.cpp
  shm_buffer.cpp
  shm_file.h
)

target_include_directories(server_platform_common PUBLIC
  ${server_common_include_dirs}
  SYSTEM PUBLIC ${GL_INCLUDE_DIRS}
)

target_link_libraries(
  server_platform_common

  ${Boost_SYSTEM_LIBRARY}
  ${GL_LDFLAGS} ${GL_LIBRARIES}
)
