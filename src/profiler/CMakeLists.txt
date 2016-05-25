cmake_minimum_required(VERSION 2.8.12.2)

project(CoreProfiler)

add_definitions(-DPAL_STDCPP_COMPAT)

include_directories(${COREPAL_SOURCE_DIR}/inc)
include_directories(${ARCH_SOURCES_DIR})

set(PROFILER_SOURCES_COMMON
  profiler.cpp
  log.cpp
  classfactory.cpp
  guids.cpp
  entrypoint.cpp
)

if(CLR_CMAKE_TARGET_ARCH_AMD64)
  set(PROFILER_SOURCES_ASM
    ${ARCH_SOURCES_DIR}/asmhelpers.S
  )
elseif(CLR_CMAKE_TARGET_ARCH_ARM)
  set(PROFILER_SOURCES_ASM
    ${ARCH_SOURCES_DIR}/asmhelpers.S
  )
endif()

add_library(coreprof
  SHARED
  ${PROFILER_SOURCES_COMMON}
  ${PROFILER_SOURCES_ASM}
)

install(TARGETS coreprof DESTINATION lib)
