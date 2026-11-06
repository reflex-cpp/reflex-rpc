# Part of the Reflex project.
# Copyright © Reflex Project Contributors.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.25)

project(reflex-rpc VERSION 2.0.0 LANGUAGES CXX)

option(REFLEX_RPC_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(REFLEX_RPC_INSTALL_CMAKE_CONFIG "Install CMake Config files" ${PROJECT_IS_TOP_LEVEL})
option(REFLEX_RPC_USE_FETCH_CONTENT "Use FetchContent for internal dependencies" OFF)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_EXTENSIONS OFF)
message(STATUS "reflex-rpc: C++ standard: ${CMAKE_CXX_STANDARD}")

if(REFLEX_RPC_USE_FETCH_CONTENT)
    include("${CMAKE_CURRENT_LIST_DIR}/tools/cmake/fetch_dependencies.cmake")
endif()

if(NOT TARGET reflex::reflex)
    find_package(reflex 1.0.1 REQUIRED)
endif()

if(NOT TARGET reflex::config)
    find_package(reflex-config 2.0.0 REQUIRED)
endif()

if(NOT TARGET reflex::tool)
    find_package(reflex-tool 1.0.2 REQUIRED)
endif()

if(NOT TARGET message_queue::message_queue)
    find_package(message-queue 1.1.2 REQUIRED)
endif()

if(NOT TARGET Boost::headers OR NOT TARGET Boost::json)
    find_package(Boost REQUIRED COMPONENTS headers json CONFIG)
endif()

if(NOT TARGET spdlog::spdlog)
    find_package(spdlog REQUIRED)
endif()

if(NOT TARGET unifex::unifex)
    find_package(unifex REQUIRED)
endif()

include(GNUInstallDirs)
include("${CMAKE_CURRENT_LIST_DIR}/tools/cmake/diagnostics.cmake")

add_library(rpc STATIC)
add_library(reflex::rpc ALIAS rpc)
set_target_properties(rpc PROPERTIES OUTPUT_NAME reflex_rpc)
reflex_set_diagnostic_options(rpc)
target_sources(
    rpc
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS include
            FILES
                include/reflex/rpc/binary_blob.hpp
                include/reflex/rpc/common.hpp
                include/reflex/rpc/handle.hpp
                include/reflex/rpc/message.hpp
                include/reflex/rpc/reflex_properties.hpp
                include/reflex/rpc/rpc_client.hpp
                include/reflex/rpc/rpc_server.hpp
    PRIVATE
        src/binary_blob.cpp
        src/common.cpp
        src/handle.cpp
        src/reflex_properties.cpp
        src/rpc_client.cpp
        src/rpc_server.cpp
)
target_link_libraries(
    rpc
    PUBLIC unifex::unifex spdlog::spdlog Boost::json reflex::config reflex::reflex message_queue::message_queue
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_library(RT_LIBRARY rt)
    if(RT_LIBRARY)
        target_link_libraries(rpc PRIVATE ${RT_LIBRARY})
    endif()
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set_target_properties(
        rpc
        PROPERTIES COMPILE_PDB_NAME reflex_rpc COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}"
    )
endif()

if(NOT COMMAND target_reflex_headers)
    include(ReflexCppMetadata)
endif()

target_reflex_headers(
    rpc
    include/reflex/rpc/binary_blob.hpp
    include/reflex/rpc/handle.hpp
    include/reflex/rpc/message.hpp
)

get_target_property(RPC_REFLEX_HEADERS rpc REFLEX_HEADERS)
reflex_generate_cpp_metadata(
    TARGET rpc
    OUTPUT_VARIABLE METADATA_HEADER_FILES
    METADATA_FILE_LIST_VARIABLE METADATA_FILES_ABS
    HEADERS ${RPC_REFLEX_HEADERS}
)
add_custom_target(rpc_metadata DEPENDS ${METADATA_HEADER_FILES})
add_dependencies(rpc rpc_metadata)
target_sources(
    rpc
    PUBLIC FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/include" FILES ${METADATA_HEADER_FILES}
)

if(REFLEX_RPC_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 REQUIRED)

    add_executable(reflex_rpc_tests)
    reflex_set_diagnostic_options(reflex_rpc_tests)
    target_link_libraries(
        reflex_rpc_tests
        PRIVATE Catch2::Catch2WithMain reflex::rpc message_queue::message_queue spdlog::spdlog
    )

    target_sources(
        reflex_rpc_tests
        PRIVATE tests/test_context.hpp tests/test_context.cpp tests/tests.hpp tests/tests.cpp
    )

    reflex_generate_cpp_metadata(TARGET reflex_rpc_tests OUTPUT_VARIABLE TEST_METADATA_FILES HEADERS tests/tests.hpp)
    add_custom_target(reflex_rpc_tests_metadata DEPENDS ${TEST_METADATA_FILES})
    add_dependencies(reflex_rpc_tests reflex_rpc_tests_metadata)
    target_sources(
        reflex_rpc_tests
        PRIVATE FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/tests" FILES ${TEST_METADATA_FILES}
    )

    include(Catch)
    catch_discover_tests(reflex_rpc_tests)
endif()

install(TARGETS rpc EXPORT reflex-rpcTargets FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    install(
        FILES "${CMAKE_BINARY_DIR}/${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/reflex_rpc.pdb"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endif()

set(METADATA_FILES_RELATIVE)
foreach(METADATA_FILENAME ${METADATA_FILES_ABS})
    cmake_path(RELATIVE_PATH METADATA_FILENAME BASE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include)
    list(APPEND METADATA_FILES_RELATIVE ${METADATA_FILENAME})
endforeach()

set(METADATA_LIST_FILENAME "${CMAKE_CURRENT_BINARY_DIR}/include/reflex_rpc_metadata_list.txt")
file(WRITE ${METADATA_LIST_FILENAME} "${METADATA_FILES_RELATIVE}")
foreach(METADATA_FILENAME ${METADATA_FILES_RELATIVE})
    cmake_path(GET METADATA_FILENAME PARENT_PATH PARENT_DIR)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/${METADATA_FILENAME}" DESTINATION "include/${PARENT_DIR}")
endforeach()
install(FILES ${METADATA_LIST_FILENAME} DESTINATION include)

if(REFLEX_RPC_INSTALL_CMAKE_CONFIG)
    install(
        EXPORT reflex-rpcTargets
        FILE reflex-rpcTargets.cmake
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/reflex-rpc"
        NAMESPACE reflex::
    )
    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        tools/cmake/reflex-rpcConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/reflex-rpcConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/reflex-rpc"
    )
    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/reflex-rpcConfig.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/reflex-rpc"
    )
endif()
