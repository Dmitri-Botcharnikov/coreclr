cmake_minimum_required(VERSION 2.8.12.2)

project(CoreProfiler)

# add_definitions(-DPAL_STDCPP_COMPAT)

set(PROFILER_SOURCES_COMMON
  avlnode.cpp
  basehlp.cpp
  classfactory.cpp
  dllmain.cpp
  log.cpp
  profilercallback.cpp
  ProfilerInfo.cpp
)

add_library_clr(coreprof
  SHARED
  ${PROFILER_SOURCES_COMMON}
)

set(PROFILER_LINK_LIBRARIES
  corguids
  utilcodestaticnohost
  # gcinfo
)

if(CLR_CMAKE_PLATFORM_UNIX)
  list(APPEND PROFILER_LINK_LIBRARIES
    mscorrc_debug
    coreclrpal
    palrt
  )
endif(CLR_CMAKE_PLATFORM_UNIX)

target_link_libraries(coreprof
  ${PROFILER_LINK_LIBRARIES}
)

install_clr(coreprof)
