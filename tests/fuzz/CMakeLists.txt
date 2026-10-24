if(NOT DMFR_BUILD_FUZZ)
    return()
endif()

set(FUZZ_TARGETS
    fuzz_scrubber
    fuzz_json_payload
    fuzz_ipc_framing
)

foreach(target ${FUZZ_TARGETS})
    add_executable(${target}
        ${target}.cpp
    )

    target_link_libraries(${target}
        PRIVATE
            dmfr::common
            dmfr::scrubber
    )

    target_compile_options(${target} PRIVATE -fsanitize=fuzzer)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer)
endforeach()
