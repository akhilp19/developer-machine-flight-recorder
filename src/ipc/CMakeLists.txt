add_library(dmfr_ipc STATIC
    ipc_client.cpp
    ipc_server.cpp
)

target_include_directories(dmfr_ipc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dmfr_ipc
    PUBLIC
        dmfr::common
        fmt::fmt
)

add_library(dmfr::ipc ALIAS dmfr_ipc)
