cmake_minimum_required(VERSION 3.16)

project(timeshiftx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(CHRONOS_ENABLE_STRICT_WARNINGS "Enable strict warnings" ON)
option(CHRONOS_ENABLE_TESTS "Build unit tests" ON)

include(FetchContent)

FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_8_0
    GIT_SHALLOW TRUE
)

set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(curl)

add_library(timeshiftx STATIC
    src/catchup/catchup_engine.cpp
    src/epg/epg_manager.cpp
    src/facade/playback_facade.cpp
    src/net/http_client.cpp
    src/net/request_queue.cpp
    src/parser/m3u_parser.cpp
    src/parser/xtream_codes_parser.cpp
    src/utils/time_utils.cpp
)

target_include_directories(timeshiftx
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        ${curl_SOURCE_DIR}/include
)

if(CHRONOS_ENABLE_STRICT_WARNINGS)
    if(MSVC)
        target_compile_options(timeshiftx PRIVATE /W4 /permissive-)
    else()
        target_compile_options(timeshiftx PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

target_compile_definitions(timeshiftx
    PUBLIC
        TIMESHIFTX_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(timeshiftx PUBLIC libcurl)

add_executable(timeshiftx_demo
    example/main.cpp
)

target_link_libraries(timeshiftx_demo
    PRIVATE
        timeshiftx
)

if(CHRONOS_ENABLE_TESTS)
    enable_testing()

    add_executable(timeshiftx_smoke_test
        tests/smoke_test.cpp
    )

    target_link_libraries(timeshiftx_smoke_test
        PRIVATE
            timeshiftx
    )

    add_test(NAME timeshiftx_smoke_test COMMAND timeshiftx_smoke_test)

    add_executable(m3u_parser_test
        tests/parser/m3u_parser_test.cpp
    )

    target_link_libraries(m3u_parser_test
        PRIVATE
            timeshiftx
    )

    add_test(NAME m3u_parser_test COMMAND m3u_parser_test)

    add_executable(xtream_codes_parser_test
        tests/parser/xtream_codes_parser_test.cpp
    )

    target_link_libraries(xtream_codes_parser_test
        PRIVATE
            timeshiftx
    )

    add_test(NAME xtream_codes_parser_test COMMAND xtream_codes_parser_test)

    add_executable(epg_manager_test
        tests/epg/epg_manager_test.cpp
    )

    target_link_libraries(epg_manager_test
        PRIVATE
            timeshiftx
    )

    add_test(NAME epg_manager_test COMMAND epg_manager_test)

    add_executable(catchup_engine_test
        tests/catchup/catchup_engine_test.cpp
    )

    target_link_libraries(catchup_engine_test
        PRIVATE
            timeshiftx
    )

    add_test(NAME catchup_engine_test COMMAND catchup_engine_test)

    add_executable(playback_facade_test
        tests/facade/playback_facade_test.cpp
    )

    target_link_libraries(playback_facade_test
        PRIVATE
            timeshiftx
    )

    add_test(NAME playback_facade_test COMMAND playback_facade_test)

    add_executable(end_to_end_test
        tests/integration/end_to_end_test.cpp
    )

    target_link_libraries(end_to_end_test
        PRIVATE
            timeshiftx
    )

    add_test(NAME end_to_end_test COMMAND end_to_end_test)

    add_executable(boundary_conditions_test
        tests/boundary/boundary_conditions_test.cpp
    )

    target_link_libraries(boundary_conditions_test
        PRIVATE
            timeshiftx
    )

    add_test(NAME boundary_conditions_test COMMAND boundary_conditions_test)

    add_executable(perf_benchmark_test
        tests/perf/perf_benchmark_test.cpp
    )

    target_link_libraries(perf_benchmark_test
        PRIVATE
            timeshiftx
    )

    add_test(NAME perf_benchmark_test COMMAND perf_benchmark_test)
endif()
