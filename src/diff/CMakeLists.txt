add_library(dmfr_diff STATIC
    diff.cpp
)

target_include_directories(dmfr_diff
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dmfr_diff
    PUBLIC
        dmfr::snapshot
        nlohmann_json::nlohmann_json
)

add_library(dmfr::diff ALIAS dmfr_diff)
