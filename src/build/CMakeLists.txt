add_library(dmfr_build STATIC build.cpp)
target_include_directories(dmfr_build PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(dmfr_build PUBLIC dmfr::diff dmfr::store dmfr::scrubber)
add_library(dmfr::build ALIAS dmfr_build)
