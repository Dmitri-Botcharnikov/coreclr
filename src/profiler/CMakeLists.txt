cmake_minimum_required(VERSION 2.8.12.2)

project(CoreProfiler)

add_library(coreprof
  SHARED
  profiler.cpp
  log.cpp
  classfactory.cpp
  guids.cpp
  entrypoint.cpp
)

target_link_libraries(coreprof
  coreclr
)

install(TARGETS coreprof DESTINATION lib)
