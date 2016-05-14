cmake_minimum_required(VERSION 2.8.12.2)

project(CoreProfiler)

add_definitions(-DPAL_STDCPP_COMPAT)

include_directories(${COREPAL_SOURCE_DIR}/inc)

add_library(coreprof
  SHARED
  profiler.cpp
  log.cpp
  classfactory.cpp
  guids.cpp
  entrypoint.cpp
)

install(TARGETS coreprof DESTINATION lib)
