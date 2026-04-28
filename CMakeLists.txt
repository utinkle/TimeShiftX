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

add_library(timeshiftx STATIC
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

target_include_directories(timeshiftx
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
        target_compile_options(timeshiftx PRIVATE /W4 /permissive-)
        if(TIMESHIFTX_ENABLE_LIBCURL_PLUGIN)
            target_compile_options(timeshiftx_net_libcurl_plugin PRIVATE /W4 /permissive-)
        endif()
        if(TIMESHIFTX_ENABLE_QT_PLUGIN)
            target_compile_options(timeshiftx_net_qt_plugin PRIVATE /W4 /permissive-)
        endif()
        target_compile_options(timeshiftx PRIVATE /W4 /permissive-)
    else()
        target_compile_options(timeshiftx PRIVATE -Wall -Wextra -Wpedantic)
        if(TIMESHIFTX_ENABLE_LIBCURL_PLUGIN)
            target_compile_options(timeshiftx_net_libcurl_plugin PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        if(TIMESHIFTX_ENABLE_QT_PLUGIN)
            target_compile_options(timeshiftx_net_qt_plugin PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        target_compile_options(timeshiftx PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

target_compile_definitions(timeshiftx
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

if(TIMESHIFTX_ENABLE_QT_PLUGIN)
    add_executable(timeshiftx_qt_demo
        example/qt_demo.cpp
    )

    target_link_libraries(timeshiftx_qt_demo
        PRIVATE
            timeshiftx
            ${TIMESHIFTX_QT_LIBS}
    )

    # 设置 Qt 自动 MOC
    set_target_properties(timeshiftx_qt_demo PROPERTIES
        AUTOMOC ON
    )
endif()
