cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(MultiplayerEditor VERSION 1.0.0)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# TLS (for wss:// servers like Render) - mbedTLS builds the same on every platform
CPMAddPackage(
    NAME mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.2/mbedtls-3.6.2.tar.bz2
    OPTIONS "ENABLE_PROGRAMS OFF" "ENABLE_TESTING OFF" "USE_SHARED_MBEDTLS_LIBRARY OFF"
            "USE_STATIC_MBEDTLS_LIBRARY ON" "MBEDTLS_FATAL_WARNINGS OFF" "GEN_FILES OFF"
)
# hand the in-tree mbedTLS to IXWebSocket instead of letting it search the system
set(MBEDTLS_FOUND TRUE)
set(MBEDTLS_INCLUDE_DIRS ${mbedtls_SOURCE_DIR}/include)
set(MBEDTLS_LIBRARIES mbedtls mbedx509 mbedcrypto)

# WebSocket client (ws:// and wss://)
CPMAddPackage(
    NAME ixwebsocket
    GITHUB_REPOSITORY machinezone/IXWebSocket
    GIT_TAG v11.4.6
    OPTIONS "USE_TLS ON" "USE_MBED_TLS ON" "USE_ZLIB OFF" "IXWEBSOCKET_INSTALL OFF" "BUILD_SHARED_LIBS OFF"
)
target_compile_definitions(ixwebsocket PRIVATE IXWEBSOCKET_USE_MBED_TLS_MIN_VERSION_3)
target_link_libraries(${PROJECT_NAME} ixwebsocket)

setup_geode_mod(${PROJECT_NAME})

# Net.cpp must include the Windows socket headers before <windows.h>,
# so it can't use Geode's precompiled header (which pulls windows.h in first).
set_source_files_properties(src/Net.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN)
endif()
