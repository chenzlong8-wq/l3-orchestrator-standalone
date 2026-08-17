# Independent H2D package. Does not link libhierarchical_orch or any simpler runtime.
# Host memcpy always. Real aclrtMemcpy only if libascendcl.so is loadable at runtime.

cmake_minimum_required(VERSION 3.15)
project(l3_h2d CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(L3_H2D_BUILD_TESTS "Build H2D gtests" ON)

find_package(Threads REQUIRED)

add_executable(h2d_bench h2d_bench.cpp)
target_include_directories(h2d_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(h2d_bench PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)

if(L3_H2D_BUILD_TESTS)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
            URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
        )
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    if(TARGET GTest::gtest_main)
        set(L3_GTEST_MAIN GTest::gtest_main)
    else()
        set(L3_GTEST_MAIN gtest_main)
    endif()

    enable_testing()
    add_executable(test_h2d test_h2d.cpp)
    target_include_directories(test_h2d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test_h2d PRIVATE ${L3_GTEST_MAIN} ${CMAKE_DL_LIBS} Threads::Threads)
    add_test(NAME test_h2d COMMAND test_h2d)
endif()
