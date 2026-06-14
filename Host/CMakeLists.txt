cmake_minimum_required(VERSION 3.20)

# 64-bit host: holds the heavy data, no 32-bit address-space limit. Configure with -A x64.
project(WraithHost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ENG "${CMAKE_CURRENT_SOURCE_DIR}/..")

# StormLib (vendored) supplies MPQ reading for the host. Static, read-only use; no install/tests.
set(STORM_SKIP_INSTALL ON CACHE BOOL "" FORCE)
set(STORM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory("${ENG}/vendor/stormlib" "${CMAKE_BINARY_DIR}/stormlib")

add_executable(WraithHost
    main.cpp
    MpqStore.cpp                             # host MPQ archive set (StormLib)
    "${ENG}/src/Features/DB2/DB2.cpp"        # decoder (pure, no storage API)
    "${ENG}/src/Features/DB2/DB2File.cpp"    # table core (LoadBytes path)
    "${ENG}/src/Core/Logger.cpp"
    "${ENG}/vendor/flatbuffers/src/util.cpp") # FlatBuffers runtime (ClassicLocale, string utils)

target_include_directories(WraithHost PRIVATE
    "${ENG}/src"
    "${ENG}/vendor/flatbuffers/include"
    "${ENG}/vendor/stormlib/src")

target_link_libraries(WraithHost PRIVATE storm)

target_compile_definitions(WraithHost PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
