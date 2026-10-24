add_library(dmfr_collectors STATIC
    git_collector.cpp
    build_collector.cpp
    system_collector.cpp
    env_collector.cpp
    collector_manager.cpp
)

target_include_directories(dmfr_collectors
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dmfr_collectors
    PUBLIC
        dmfr::common
        dmfr::scrubber
        dmfr::store
        dmfr::ipc
        spdlog::spdlog
        fmt::fmt
)

add_library(dmfr::collectors ALIAS dmfr_collectors)
