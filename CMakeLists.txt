cmake_minimum_required(VERSION 3.20)

project(dmfr
    VERSION 0.1.0
    DESCRIPTION "Developer Machine Flight Recorder"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(DMFR_BUILD_TESTS "Build tests" ON)
option(DMFR_BUILD_FUZZ "Build fuzz tests" OFF)
option(DMFR_ENABLE_SANITIZERS "Enable sanitizers in debug builds" OFF)
option(DMFR_TREAT_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

# Global compile options
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
    add_compile_options(/W4)
    if(DMFR_TREAT_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(DMFR_TREAT_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
endif()

if(DMFR_ENABLE_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# Find dependencies via vcpkg
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# Arrow/Parquet are optional for Parquet export in v1
find_package(Arrow QUIET)
find_package(Parquet QUIET)
if(Arrow_FOUND AND Parquet_FOUND)
    add_definitions(-DDMFR_HAS_ARROW=1)
endif()

if(DMFR_BUILD_TESTS)
    find_package(Catch2 CONFIG REQUIRED)
    include(Catch)
endif()

# Core library and executables
add_subdirectory(src/common)
add_subdirectory(src/ipc)
add_subdirectory(src/store)
add_subdirectory(src/scrubber)
add_subdirectory(src/collectors)
add_subdirectory(src/query)
add_subdirectory(src/snapshot)
add_subdirectory(src/diff)
add_subdirectory(src/build)
add_subdirectory(src/plugins)
add_subdirectory(src/daemon)
add_subdirectory(src/cli)

if(DMFR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
    add_subdirectory(tests/integration)
endif()

if(DMFR_BUILD_FUZZ)
    add_subdirectory(tests/fuzz)
endif()

# Install targets
install(TARGETS dmfrd dmfr
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY docs/ DESTINATION share/doc/dmfr)
