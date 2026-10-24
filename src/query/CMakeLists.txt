add_library(dmfr_query STATIC
    query.cpp
)

target_include_directories(dmfr_query
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dmfr_query
    PUBLIC
        dmfr::common
        dmfr::store
        fmt::fmt
)

add_library(dmfr::query ALIAS dmfr_query)
