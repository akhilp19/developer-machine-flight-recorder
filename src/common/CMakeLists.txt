find_package(Threads REQUIRED)

add_library(dmfr_common STATIC
    event.cpp
    paths.cpp
    config.cpp
    process.cpp
)

target_include_directories(dmfr_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dmfr_common
    PUBLIC
        nlohmann_json::nlohmann_json
        fmt::fmt
        Threads::Threads
)

add_library(dmfr::common ALIAS dmfr_common)
