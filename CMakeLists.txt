cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(msc_vector
        VERSION 1.0.0
        DESCRIPTION "A page-mapped vector with O(1) splice, built on mmap/mremap"
        LANGUAGES CXX)

# Everything that is only useful when hacking on the library itself is gated on
# this, so a project pulling us in with FetchContent gets the header and nothing
# else: no gtest download, no benchmark, no compiler flags forced on its build.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(MSC_VECTOR_IS_TOP_LEVEL ON)
else()
  set(MSC_VECTOR_IS_TOP_LEVEL OFF)
endif()

option(MSC_VECTOR_BUILD_TESTS   "Build the tests"      ${MSC_VECTOR_IS_TOP_LEVEL})
option(MSC_VECTOR_BUILD_BENCH   "Build the benchmarks" ${MSC_VECTOR_IS_TOP_LEVEL})
option(MSC_VECTOR_BUILD_EXAMPLE "Build main.cpp"       ${MSC_VECTOR_IS_TOP_LEVEL})
option(MSC_VECTOR_INSTALL       "Generate install rules" ${MSC_VECTOR_IS_TOP_LEVEL})

#-------------------------------------------------------------------------------
#   The library
#
#   Header only, so INTERFACE: there is nothing to compile and nothing to link.
#   target_include_directories rather than the old include_directories(), which
#   is directory scoped and would never reach a consumer.
#-------------------------------------------------------------------------------
add_library(msc_vector INTERFACE)
add_library(msc::vector ALIAS msc_vector)

target_include_directories(msc_vector INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

# propagates to consumers, so they cannot accidentally compile us as C++20
target_compile_features(msc_vector INTERFACE cxx_std_23)

# mremap and MAP_NORESERVE are Linux only;
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR
    "msc::vector requires Linux: it is built on mremap(2) and MAP_NORESERVE, "
    "which have no portable equivalent. Detected: ${CMAKE_SYSTEM_NAME}")
endif()

if(NOT MSC_VECTOR_IS_TOP_LEVEL)
  return()
endif()

#-------------------------------------------------------------------------------
#   Developer build below this line
#   These are per target rather than in CMAKE_CXX_FLAGS so they never reach a
#   project that pulls the library in, and so they never overwrite whatever the
#   caller passed on the command line.
#-------------------------------------------------------------------------------
add_library(msc_vector_dev_flags INTERFACE)
target_compile_options(msc_vector_dev_flags INTERFACE
  -Wall -Wextra -fno-omit-frame-pointer)
target_link_libraries(msc_vector_dev_flags INTERFACE msc::vector)

# off by default: a newer compiler inventing a new warning should not be able to
# stop someone from building the tests. CI is where this belongs.
option(MSC_VECTOR_WERROR "Treat warnings as errors in the developer targets" OFF)
if(MSC_VECTOR_WERROR)
  target_compile_options(msc_vector_dev_flags INTERFACE -Werror)
endif()

# appended, so -DCMAKE_CXX_FLAGS_RELEASE=... from the command line still wins
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG -O3 -ggdb")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

#----------------------------------------------
#   Tests
#----------------------------------------------
if(MSC_VECTOR_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
      gtest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.15.2
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(gtest)

  enable_testing()
  add_executable(vector_test ${CMAKE_CURRENT_SOURCE_DIR}/test/vector_test.cpp)
  target_link_libraries(vector_test PRIVATE msc_vector_dev_flags GTest::gtest_main)

  include(GoogleTest)
  gtest_discover_tests(vector_test)
endif()

#----------------------------------------------
#   Benchmarks
#----------------------------------------------
if(MSC_VECTOR_BUILD_BENCH)
  add_executable(bench ${CMAKE_CURRENT_SOURCE_DIR}/bench/bench.cpp)
  target_link_libraries(bench PRIVATE msc_vector_dev_flags)
  # bench.cpp includes "bench/PerfEvent.hpp", so it needs the project root
  target_include_directories(bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

#----------------------------------------------
#   Example
#----------------------------------------------
if(MSC_VECTOR_BUILD_EXAMPLE)
  add_executable(main ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
  target_link_libraries(main PRIVATE msc_vector_dev_flags)
endif()

#----------------------------------------------
#   Install rules, so find_package(msc_vector) works after an install
#----------------------------------------------
if(MSC_VECTOR_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(TARGETS msc_vector EXPORT msc_vectorTargets)

  install(EXPORT msc_vectorTargets
          FILE msc_vectorTargets.cmake
          NAMESPACE msc::
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/msc_vector)

  if(PROJECT_VERSION_MAJOR EQUAL 0)
    set(MSC_VECTOR_COMPAT SameMinorVersion)
  else()
    set(MSC_VECTOR_COMPAT SameMajorVersion)
  endif()

  write_basic_package_version_file(
      "${CMAKE_CURRENT_BINARY_DIR}/msc_vectorConfigVersion.cmake"
      VERSION ${PROJECT_VERSION}
      COMPATIBILITY ${MSC_VECTOR_COMPAT}
      ARCH_INDEPENDENT)

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/msc_vectorConfig.cmake"
"include(CMakeFindDependencyMacro)\n\
include(\"\${CMAKE_CURRENT_LIST_DIR}/msc_vectorTargets.cmake\")\n")

  install(FILES
      "${CMAKE_CURRENT_BINARY_DIR}/msc_vectorConfig.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/msc_vectorConfigVersion.cmake"
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/msc_vector)
endif()
