# SPDX-FileCopyrightText: Copyright 2026 Cathy J. Fitzpatrick <cathy@cathyjf.com>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 4.2)

set(DEVICEFS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/devicefs")
list(APPEND CMAKE_VS_GLOBALS
    "ForceImportAfterCppDefaultProps=${DEVICEFS_ROOT}/misc/ApplyMsvcOverrides.props")

project(remove-ipv6-addresses LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
include("${DEVICEFS_ROOT}/misc/MsvcFlags.cmake")
include("${DEVICEFS_ROOT}/misc/MsvcNativeCodeAnalysis.cmake")

set(WIL_INCLUDE_DIR "${DEVICEFS_ROOT}/dependencies/wil/include")
find_program(POWERSHELL_EXECUTABLE NAMES pwsh REQUIRED)

add_executable(remove-ipv6-addresses src/main.cpp)
target_sources(remove-ipv6-addresses PRIVATE
    FILE_SET CXX_MODULES FILES src/nsi.ixx)
target_sources(remove-ipv6-addresses PRIVATE
    "${DEVICEFS_ROOT}/src/resources/devicefs.manifest")
target_link_libraries(remove-ipv6-addresses PRIVATE
    iphlpapi ole32 uuid ws2_32)
target_include_directories(remove-ipv6-addresses SYSTEM PRIVATE
    "${WIL_INCLUDE_DIR}")
target_compile_definitions(remove-ipv6-addresses PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
    _MSVC_STL_HARDENING=1)
devicefs_enable_native_code_analysis(remove-ipv6-addresses
    "${WIL_INCLUDE_DIR}")
string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" LOWERCASE_ARCHITECTURE)
set_target_properties(remove-ipv6-addresses PROPERTIES
    OUTPUT_NAME "remove-ipv6-addresses-${LOWERCASE_ARCHITECTURE}")

install(TARGETS remove-ipv6-addresses RUNTIME DESTINATION .)

devicefs_add_native_code_analysis_check()
