
cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

set(PROJECT_NAME vector)

project(${PROJECT_NAME} C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror -Wextra -fPIC -fno-omit-frame-pointer -march=x86-64")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fPIC -fno-omit-frame-pointer -march=native")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror -Wextra -fPIC -fno-omit-frame-pointer -march=native")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g -D_GLIBC_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG -O3 -ggdb")

# set(CMAKE_C_COMPILER "/usr/bin/gcc")
# set(CMAKE_CXX_COMPILER "/usr/bin/g++")

option(TEST "Build the test for the vector" ON)
option(BENCHMARK "Build the benchmark for the vector" ON)


include(FetchContent)

if(TEST)
FetchContent_Declare(
    gtest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(gtest)
endif()

if(BENCHMARK)
FetchContent_Declare(
    perfevent
    GIT_REPOSITORY https://github.com/viktorleis/perfevent.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(perfevent)

# single header without a CMakeLists.txt, so give it a target of its own
add_library(perfevent INTERFACE)
target_include_directories(perfevent SYSTEM INTERFACE ${perfevent_SOURCE_DIR})
endif()

#-------------------------------------------
# Includes
#-------------------------------------------

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/bench)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})



add_library(vector STATIC ${CMAKE_SOURCE_DIR}/vector.hpp)
set_target_properties(vector PROPERTIES LINKER_LANGUAGE CXX)


#----------------------------------------------
#   build vector main executable
#----------------------------------------------
add_executable(main ${CMAKE_SOURCE_DIR}/main.cpp)
target_link_libraries(main vector gtest)

#----------------------------------------------
#   build vector test executable
#----------------------------------------------
if(TEST)
add_executable(vector_test ${CMAKE_SOURCE_DIR}/test/vector_test.cpp)
target_link_libraries(vector_test vector gtest GTest::gtest_main)
endif()


#----------------------------------------------
#   build vector test executable
#----------------------------------------------
if(BENCHMARK)
add_executable(bench ${CMAKE_SOURCE_DIR}/bench/bench.cpp)
target_link_libraries(bench vector perfevent)
endif()