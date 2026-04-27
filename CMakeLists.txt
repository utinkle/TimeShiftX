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

# 默认策略：
# - 桌面端（Windows/macOS）默认 Qt 插件
# - 服务器/通用 Linux 默认 libcurl 插件
if(WIN32 OR APPLE)
    set(TIMESHIFTX_DEFAULT_NETWORK_BACKEND "qt")
    set(TIMESHIFTX_DEFAULT_ENABLE_QT ON)
    set(TIMESHIFTX_DEFAULT_ENABLE_LIBCURL OFF)
else()
    set(TIMESHIFTX_DEFAULT_NETWORK_BACKEND "libcurl")
    set(TIMESHIFTX_DEFAULT_ENABLE_QT OFF)
    set(TIMESHIFTX_DEFAULT_ENABLE_LIBCURL ON)
endif()

option(TIMESHIFTX_ENABLE_QT_PLUGIN "Build Qt network plugin target" ${TIMESHIFTX_DEFAULT_ENABLE_QT})
option(TIMESHIFTX_ENABLE_LIBCURL_PLUGIN "Build libcurl network plugin target" ${TIMESHIFTX_DEFAULT_ENABLE_LIBCURL})
set(TIMESHIFTX_NETWORK_BACKEND "${TIMESHIFTX_DEFAULT_NETWORK_BACKEND}" CACHE STRING "Active network backend: libcurl or qt")
set_property(CACHE TIMESHIFTX_NETWORK_BACKEND PROPERTY STRINGS libcurl qt)

# 当前版本限定单后端编译（与架构约束一致：Qt 与 libcurl 二选一）
if(TIMESHIFTX_NETWORK_BACKEND STREQUAL "qt")
    set(TIMESHIFTX_ENABLE_QT_PLUGIN ON CACHE BOOL "" FORCE)
    set(TIMESHIFTX_ENABLE_LIBCURL_PLUGIN OFF CACHE BOOL "" FORCE)
elseif(TIMESHIFTX_NETWORK_BACKEND STREQUAL "libcurl")
    set(TIMESHIFTX_ENABLE_QT_PLUGIN OFF CACHE BOOL "" FORCE)
    set(TIMESHIFTX_ENABLE_LIBCURL_PLUGIN ON CACHE BOOL "" FORCE)
endif()

add_library(timeshiftx_core STATIC
    src/catchup/catchup_engine.cpp
    src/epg/epg_manager.cpp
    src/facade/playback_facade.cpp
    src/net/network_plugin_manager.cpp
    src/net/network_service.cpp
    src/parser/m3u_parser.cpp
    src/parser/xtream_codes_parser.cpp
    src/utils/time_utils.cpp
)

if(TIMESHIFTX_ENABLE_LIBCURL_PLUGIN)
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

    add_library(timeshiftx_net_libcurl_plugin STATIC
        plugins/network_libcurl/src/libcurl_network_plugin.cpp
    )
endif()

if(TIMESHIFTX_ENABLE_QT_PLUGIN)
    find_package(Qt6 QUIET COMPONENTS Core Network Test)
    if(Qt6_FOUND)
        set(TIMESHIFTX_QT_LIBS Qt6::Core Qt6::Network)
        set(TIMESHIFTX_QT_TEST_LIB Qt6::Test)
    else()
        find_package(Qt5 REQUIRED COMPONENTS Core Network Test)
        set(TIMESHIFTX_QT_LIBS Qt5::Core Qt5::Network)
        set(TIMESHIFTX_QT_TEST_LIB Qt5::Test)
    endif()

    add_library(timeshiftx_net_qt_plugin STATIC
        plugins/network_qt/src/qt_network_plugin.cpp
    )
endif()

if(TIMESHIFTX_NETWORK_BACKEND STREQUAL "libcurl")
    if(NOT TIMESHIFTX_ENABLE_LIBCURL_PLUGIN)
        message(FATAL_ERROR "TIMESHIFTX_NETWORK_BACKEND=libcurl but TIMESHIFTX_ENABLE_LIBCURL_PLUGIN=OFF")
    endif()
elseif(TIMESHIFTX_NETWORK_BACKEND STREQUAL "qt")
    if(NOT TIMESHIFTX_ENABLE_QT_PLUGIN)
        message(FATAL_ERROR "TIMESHIFTX_NETWORK_BACKEND=qt but TIMESHIFTX_ENABLE_QT_PLUGIN=OFF")
    endif()
else()
    message(FATAL_ERROR "Unsupported TIMESHIFTX_NETWORK_BACKEND=${TIMESHIFTX_NETWORK_BACKEND}, expected libcurl or qt")
endif()

add_library(timeshiftx STATIC
    src/net/network_bootstrap.cpp
)

target_include_directories(timeshiftx_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if(TIMESHIFTX_ENABLE_LIBCURL_PLUGIN)
    target_include_directories(timeshiftx_net_libcurl_plugin
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party
            ${curl_SOURCE_DIR}/include
    )
endif()

if(TIMESHIFTX_ENABLE_QT_PLUGIN)
    target_include_directories(timeshiftx_net_qt_plugin
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party
            ${CMAKE_CURRENT_SOURCE_DIR}/plugins/network_qt
    )
endif()

target_include_directories(timeshiftx
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if(CHRONOS_ENABLE_STRICT_WARNINGS)
    if(MSVC)
        target_compile_options(timeshiftx_core PRIVATE /W4 /permissive-)
        if(TIMESHIFTX_ENABLE_LIBCURL_PLUGIN)
            target_compile_options(timeshiftx_net_libcurl_plugin PRIVATE /W4 /permissive-)
        endif()
        if(TIMESHIFTX_ENABLE_QT_PLUGIN)
            target_compile_options(timeshiftx_net_qt_plugin PRIVATE /W4 /permissive-)
        endif()
        target_compile_options(timeshiftx PRIVATE /W4 /permissive-)
    else()
        target_compile_options(timeshiftx_core PRIVATE -Wall -Wextra -Wpedantic)
        if(TIMESHIFTX_ENABLE_LIBCURL_PLUGIN)
            target_compile_options(timeshiftx_net_libcurl_plugin PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        if(TIMESHIFTX_ENABLE_QT_PLUGIN)
            target_compile_options(timeshiftx_net_qt_plugin PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        target_compile_options(timeshiftx PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

target_compile_definitions(timeshiftx_core
    PUBLIC
        TIMESHIFTX_VERSION="${PROJECT_VERSION}"
)

target_compile_definitions(timeshiftx
    PUBLIC
        TIMESHIFTX_VERSION="${PROJECT_VERSION}"
)

if(TIMESHIFTX_NETWORK_BACKEND STREQUAL "libcurl")
    target_link_libraries(timeshiftx_net_libcurl_plugin
        PRIVATE
            libcurl
    )
    target_compile_definitions(timeshiftx PRIVATE TIMESHIFTX_NETWORK_BACKEND_LIBCURL=1)
else()
    target_link_libraries(timeshiftx_net_qt_plugin
        PRIVATE
            ${TIMESHIFTX_QT_LIBS}
    )
    target_compile_definitions(timeshiftx PRIVATE TIMESHIFTX_NETWORK_BACKEND_QT=1)
endif()

target_link_libraries(timeshiftx
    PUBLIC
        timeshiftx_core
)

if(TIMESHIFTX_NETWORK_BACKEND STREQUAL "libcurl")
    target_link_libraries(timeshiftx PRIVATE timeshiftx_net_libcurl_plugin)
else()
    target_link_libraries(timeshiftx PRIVATE timeshiftx_net_qt_plugin)
endif()

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

    if(TIMESHIFTX_NETWORK_BACKEND STREQUAL "qt")
        add_executable(qt_network_plugin_test
            plugins/network_qt/tests/qt_network_plugin_test.cpp
        )

        target_link_libraries(qt_network_plugin_test
            PRIVATE
                timeshiftx
                ${TIMESHIFTX_QT_TEST_LIB}
                ${TIMESHIFTX_QT_LIBS}
        )

        add_test(NAME qt_network_plugin_test COMMAND qt_network_plugin_test)
    elseif(TIMESHIFTX_NETWORK_BACKEND STREQUAL "libcurl")
        add_executable(libcurl_network_plugin_test
            plugins/network_libcurl/tests/libcurl_network_plugin_test.cpp
        )

        target_link_libraries(libcurl_network_plugin_test
            PRIVATE
                timeshiftx
        )

        add_test(NAME libcurl_network_plugin_test COMMAND libcurl_network_plugin_test)

        add_executable(libcurl_network_plugin_perf_test
            plugins/network_libcurl/tests/libcurl_network_plugin_perf_test.cpp
        )

        target_link_libraries(libcurl_network_plugin_perf_test
            PRIVATE
                timeshiftx
        )

        add_test(NAME libcurl_network_plugin_perf_test COMMAND libcurl_network_plugin_perf_test)
    endif()

    add_executable(network_injection_test
        tests/net/network_injection_test.cpp
    )

    target_link_libraries(network_injection_test
        PRIVATE
            timeshiftx
    )

    add_test(NAME network_injection_test COMMAND network_injection_test)

    add_executable(network_plugin_manager_test
        tests/net/network_plugin_manager_test.cpp
    )

    target_link_libraries(network_plugin_manager_test
        PRIVATE
            timeshiftx
    )

    add_test(NAME network_plugin_manager_test COMMAND network_plugin_manager_test)

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
