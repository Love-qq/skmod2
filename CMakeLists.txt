cmake_minimum_required(VERSION 3.21)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

project(
    CastSprint
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("C:/dev/CommonLibSSE-NG" CommonLibSSE EXCLUDE_FROM_ALL)

find_package(SimpleIni CONFIG REQUIRED)

set(SOURCES
    src/main.cpp
    src/Config.cpp
    src/Config.h
    src/Hooks.cpp
    src/Hooks.h
    src/MovementPatch.cpp
    src/MovementPatch.h
    src/SprintPatch.cpp
    src/SprintPatch.h
    src/Diagnostics.cpp
    src/Diagnostics.h
)

add_library(CastSprint MODULE ${SOURCES})

target_include_directories(CastSprint PRIVATE
    src
)

target_precompile_headers(CastSprint PRIVATE
    "C:/dev/CommonLibSSE-NG/include/SKSE/Impl/PCH.h"
)

target_link_libraries(CastSprint PRIVATE
    CommonLibSSE::CommonLibSSE
    SimpleIni::SimpleIni
)

target_compile_definitions(CastSprint PRIVATE
    CASTSPRINT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    CASTSPRINT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    CASTSPRINT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
