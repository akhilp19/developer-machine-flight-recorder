add_library(dmfr_snapshot STATIC
    snapshot.cpp
)

target_include_directories(dmfr_snapshot
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dmfr_snapshot
    PUBLIC
        dmfr::common
        nlohmann_json::nlohmann_json
)

add_library(dmfr::snapshot ALIAS dmfr_snapshot)
