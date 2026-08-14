# Copyright (c) PyPTO Contributors.
# Standalone L3 hierarchical Orchestrator package (no simpler repo required).
# Linux or macOS. Needs g++ (C++17), cmake >= 3.15, pthread. gtest is optional.

cmake_minimum_required(VERSION 3.15)
project(l3_orchestrator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    message(FATAL_ERROR "This package requires POSIX (fork / shm_open). Build on Linux.")
endif()

option(L3_ORCH_BUILD_TESTS "Build hierarchical gtests" ON)
option(L3_ORCH_BUILD_MAIN "Build orch_main smoke driver" ON)
option(L3_ORCH_BUILD_H2D "Build H2D memcpy/ACL bench and tests" ON)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(H ${ROOT}/src/common/hierarchical)
set(W ${ROOT}/src/common/worker)

add_library(hierarchical_orch STATIC
    ${H}/types.cpp
    ${H}/tensormap.cpp
    ${H}/ring.cpp
    ${H}/scope.cpp
    ${H}/remote_wire.cpp
    ${H}/remote_endpoint.cpp
    ${H}/orchestrator.cpp
    ${H}/worker_manager.cpp
    ${H}/scheduler.cpp
    ${H}/worker.cpp
    ${W}/chip_run_lane.cpp
    ${W}/chip_worker.cpp
    ${ROOT}/src/common/task_interface/assert_compat.cpp
)
target_include_directories(hierarchical_orch PUBLIC
    ${H}
    ${ROOT}/src/common/log/include
    ${ROOT}/src/common/platform/include
    ${ROOT}/src/common/task_interface
    ${W}
)
target_link_libraries(hierarchical_orch PUBLIC pthread)
if(UNIX AND NOT APPLE)
    target_link_libraries(hierarchical_orch PUBLIC rt)
endif()

if(L3_ORCH_BUILD_TESTS)
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
    function(add_l3_orch_test name src)
        add_executable(${name} ${src})
        target_link_libraries(${name} PRIVATE hierarchical_orch ${L3_GTEST_MAIN})
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    add_l3_orch_test(test_orchestrator tests/test_orchestrator.cpp)
    add_l3_orch_test(test_scheduler    tests/test_scheduler.cpp)
    add_l3_orch_test(test_ring         tests/test_ring.cpp)
    add_l3_orch_test(test_scope        tests/test_scope.cpp)
    add_l3_orch_test(test_tensormap    tests/test_tensormap.cpp)
    add_l3_orch_test(test_remote_wire  tests/test_remote_wire.cpp)
    if(L3_ORCH_BUILD_H2D)
        add_executable(test_h2d tests/test_h2d.cpp)
        target_include_directories(test_h2d PRIVATE ${ROOT}/apps)
        target_link_libraries(test_h2d PRIVATE ${L3_GTEST_MAIN} ${CMAKE_DL_LIBS})
        add_test(NAME test_h2d COMMAND test_h2d)
    endif()
endif()

if(L3_ORCH_BUILD_MAIN)
    add_executable(orch_main apps/orch_main.cpp)
    target_include_directories(orch_main PRIVATE ${ROOT}/apps)
    target_link_libraries(orch_main PRIVATE hierarchical_orch)
endif()

if(L3_ORCH_BUILD_H2D)
    add_executable(h2d_bench apps/h2d_bench.cpp)
    target_include_directories(h2d_bench PRIVATE ${ROOT}/apps)
    target_link_libraries(h2d_bench PRIVATE ${CMAKE_DL_LIBS})
endif()
