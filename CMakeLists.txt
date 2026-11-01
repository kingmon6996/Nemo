cmake_minimum_required(VERSION 3.20)

project(
    Nemo
    VERSION 0.2.1
    DESCRIPTION "Nemo: A high-performance embedded relational database engine"
    LANGUAGES CXX
)

# Nemo Options and strict compilation flags
include(cmake/NemoOptions.cmake)

# Ensure C++20 is used
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Add subdirectories
add_subdirectory(src)

if(NEMO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(NEMO_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()

option(NEMO_BUILD_PYTHON "Build Python bindings" OFF)
if(SKBUILD OR DEFINED ENV{NEMO_BUILD_PYTHON})
    set(NEMO_BUILD_PYTHON ON CACHE BOOL "Build Python bindings" FORCE)
endif()

if(NEMO_BUILD_PYTHON)
    find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
    find_package(pybind11 CONFIG QUIET)
    if(NOT pybind11_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v2.12.0
        )
        FetchContent_MakeAvailable(pybind11)
    endif()
    
    pybind11_add_module(_nemo src/python/module.cpp)
    target_link_libraries(_nemo PRIVATE nemo)
    install(TARGETS _nemo DESTINATION nemo)
endif()
