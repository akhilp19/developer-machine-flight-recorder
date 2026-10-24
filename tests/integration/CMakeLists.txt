add_executable(dmfr_integration_tests
    test_store.cpp
    test_snapshot.cpp
    test_diff.cpp
    test_build_workflow.cpp
)

# Shell hooks and wrapper integration are POSIX-only. Portable store/snapshot/diff
# tests still run on Windows while native execution/IPC are not implemented.
if(UNIX)
    target_sources(dmfr_integration_tests PRIVATE test_git_collector.cpp test_build_collector.cpp)
endif()

target_link_libraries(dmfr_integration_tests
    PRIVATE
        dmfr::common
        dmfr::store
        dmfr::scrubber
        dmfr::collectors
        dmfr::snapshot
        dmfr::build
        dmfr::diff
        Catch2::Catch2WithMain
        fmt::fmt
)

catch_discover_tests(dmfr_integration_tests)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND UNIX)
    add_test(NAME record_cli_regressions COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_record_cli.py --cli $<TARGET_FILE:dmfr>)
    set_tests_properties(record_cli_regressions PROPERTIES TIMEOUT 45)
endif()
