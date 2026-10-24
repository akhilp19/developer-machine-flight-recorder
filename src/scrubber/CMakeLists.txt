add_library(dmfr_scrubber STATIC
    scrubber.cpp
)

target_include_directories(dmfr_scrubber
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dmfr_scrubber
    PUBLIC
        dmfr::common
        dmfr::snapshot
        OpenSSL::SSL
        fmt::fmt
)

add_library(dmfr::scrubber ALIAS dmfr_scrubber)
