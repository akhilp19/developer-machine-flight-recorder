add_library(dmfr_plugins INTERFACE)

target_include_directories(dmfr_plugins
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dmfr_plugins
    INTERFACE
        dmfr::common
)

add_library(dmfr::plugins ALIAS dmfr_plugins)
