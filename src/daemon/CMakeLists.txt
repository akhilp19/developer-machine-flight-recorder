add_executable(dmfrd
    main.cpp
)

target_link_libraries(dmfrd
    PRIVATE
        dmfr::common
        dmfr::ipc
        dmfr::store
        dmfr::scrubber
        dmfr::collectors
        dmfr::query
        dmfr::snapshot
        dmfr::diff
        spdlog::spdlog
        fmt::fmt
)

set_target_properties(dmfrd PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

install(TARGETS dmfrd RUNTIME DESTINATION bin)
