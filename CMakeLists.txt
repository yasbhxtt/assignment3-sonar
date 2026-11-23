cmake_minimum_required(VERSION 3.20)
project(Assignment3 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# SonarQube Cloud reads this file to learn how each C++ file is compiled.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_COVERAGE "Build the tests with gcov coverage instrumentation" OFF)

find_package(Threads REQUIRED)

# ---- the application (original source, unchanged) ----
add_executable(assignment3 Assignment3/Assignment3.cpp)
target_link_libraries(assignment3 PRIVATE Threads::Threads)

# ---- unit tests (GoogleTest) ----
enable_testing()

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
  GIT_SHALLOW    TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(assignment3_tests tests/test_assignment3.cpp)
target_include_directories(assignment3_tests PRIVATE Assignment3)
target_link_libraries(assignment3_tests PRIVATE GTest::gtest_main Threads::Threads)

# The whole program, built with fewer measurement repeats (_DEBUG), run as a smoke test.
add_executable(assignment3_smoke Assignment3/Assignment3.cpp)
target_compile_definitions(assignment3_smoke PRIVATE _DEBUG)
target_link_libraries(assignment3_smoke PRIVATE Threads::Threads)

if(ENABLE_COVERAGE)
  foreach(target assignment3_tests assignment3_smoke)
    target_compile_options(${target} PRIVATE --coverage -fprofile-update=atomic -O0 -g)
    target_link_options(${target} PRIVATE --coverage)
  endforeach()
endif()

# Run from the folder that contains Hamlet.txt so the file-loading code can find it.
add_test(
  NAME assignment3_tests
  COMMAND assignment3_tests
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/Assignment3
)

add_test(
  NAME assignment3_smoke
  COMMAND assignment3_smoke
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/Assignment3
)
set_tests_properties(assignment3_smoke PROPERTIES PASS_REGULAR_EXPRESSION "Total word count = [0-9]+")
