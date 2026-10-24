add_executable(dmfr
    main.cpp
)

target_link_libraries(dmfr
    PRIVATE
        dmfr::common
        dmfr::ipc
        dmfr::store
        dmfr::scrubber
        dmfr::collectors
        dmfr::snapshot
        dmfr::build
        dmfr::diff
        CLI11::CLI11
        fmt::fmt
)

set_target_properties(dmfr PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

install(TARGETS dmfr RUNTIME DESTINATION bin)
