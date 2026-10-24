add_executable(dmfr_unit_tests
    test_event.cpp
    test_process.cpp
    test_scrubber.cpp
)

target_link_libraries(dmfr_unit_tests
    PRIVATE
        dmfr::common
        dmfr::scrubber
        Catch2::Catch2WithMain
        fmt::fmt
)

catch_discover_tests(dmfr_unit_tests)
