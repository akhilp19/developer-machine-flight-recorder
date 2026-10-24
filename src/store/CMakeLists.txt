add_library(dmfr_store STATIC
    store.cpp
)

target_include_directories(dmfr_store
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dmfr_store
    PUBLIC
        dmfr::common
        dmfr::snapshot
        SQLite3::SQLite3
        fmt::fmt
)

if(Arrow_FOUND)
    target_link_libraries(dmfr_store
        PUBLIC
            Arrow::arrow_static
            Parquet::parquet_static
    )
endif()

add_library(dmfr::store ALIAS dmfr_store)
