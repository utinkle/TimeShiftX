cmake_minimum_required(VERSION 3.16)

# 项目名称：ChronosStream Core
# 说明：该项目聚焦 IPTV 多协议解析、EPG 匹配与回看 URL 生成。
project(ChronosStreamCore VERSION 0.1.0 LANGUAGES CXX)

# -----------------------------
# C++ 标准与编译选项
# -----------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 默认构建类型：在未指定时使用 Debug 便于早期阶段调试。
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# 统一输出目录，便于后续集成与打包。
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# -----------------------------
# 依赖选项（Phase 1 仅声明）
# -----------------------------
option(CHRONOS_ENABLE_STRICT_WARNINGS "Enable strict warnings" ON)
option(CHRONOS_ENABLE_TESTS "Build unit tests" ON)

# -----------------------------
# 核心库目标
# -----------------------------
add_library(chronosstream_core STATIC
    src/catchup/catchup_engine.cpp
    src/epg/epg_manager.cpp
    src/facade/playback_facade.cpp
    src/net/http_client.cpp
    src/parser/m3u_parser.cpp
    src/parser/xtream_codes_parser.cpp
    src/utils/time_utils.cpp
)

# 公开头文件目录：供上层应用与测试复用。
target_include_directories(chronosstream_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# 编译器告警策略：Phase 1 优先保证代码质量基线。
if(CHRONOS_ENABLE_STRICT_WARNINGS)
    if(MSVC)
        target_compile_options(chronosstream_core PRIVATE /W4 /permissive-)
    else()
        target_compile_options(chronosstream_core PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

# 为后续模块区分准备统一导出宏（当前示例中仅作占位定义）。
target_compile_definitions(chronosstream_core
    PUBLIC
        CHRONOSSTREAM_CORE_VERSION="${PROJECT_VERSION}"
)

# libcurl：用于 Phase 2 的网络 M3U 下载能力。
find_package(CURL REQUIRED)
target_link_libraries(chronosstream_core PUBLIC CURL::libcurl)

# -----------------------------
# 示例可执行程序（可选）
# -----------------------------
add_executable(chronosstream_demo
    src/main.cpp
)

target_link_libraries(chronosstream_demo
    PRIVATE
        chronosstream_core
)

# -----------------------------
# 测试开关（Phase 1 提供入口）
# -----------------------------
if(CHRONOS_ENABLE_TESTS)
    enable_testing()

    add_executable(chronosstream_smoke_test
        tests/smoke_test.cpp
    )

    target_link_libraries(chronosstream_smoke_test
        PRIVATE
            chronosstream_core
    )

    add_test(NAME chronosstream_smoke_test COMMAND chronosstream_smoke_test)

    add_executable(m3u_parser_test
        tests/parser/m3u_parser_test.cpp
    )

    target_link_libraries(m3u_parser_test
        PRIVATE
            chronosstream_core
    )

    add_test(NAME m3u_parser_test COMMAND m3u_parser_test)

    add_executable(xtream_codes_parser_test
        tests/parser/xtream_codes_parser_test.cpp
    )

    target_link_libraries(xtream_codes_parser_test
        PRIVATE
            chronosstream_core
    )

    add_test(NAME xtream_codes_parser_test COMMAND xtream_codes_parser_test)

    add_executable(epg_manager_test
        tests/epg/epg_manager_test.cpp
    )

    target_link_libraries(epg_manager_test
        PRIVATE
            chronosstream_core
    )

    add_test(NAME epg_manager_test COMMAND epg_manager_test)

    add_executable(catchup_engine_test
        tests/catchup/catchup_engine_test.cpp
    )

    target_link_libraries(catchup_engine_test
        PRIVATE
            chronosstream_core
    )

    add_test(NAME catchup_engine_test COMMAND catchup_engine_test)

    add_executable(playback_facade_test
        tests/facade/playback_facade_test.cpp
    )

    target_link_libraries(playback_facade_test
        PRIVATE
            chronosstream_core
    )

    add_test(NAME playback_facade_test COMMAND playback_facade_test)

    add_executable(end_to_end_test
        tests/integration/end_to_end_test.cpp
    )

    target_link_libraries(end_to_end_test
        PRIVATE
            chronosstream_core
    )

    add_test(NAME end_to_end_test COMMAND end_to_end_test)

    add_executable(boundary_conditions_test
        tests/boundary/boundary_conditions_test.cpp
    )

    target_link_libraries(boundary_conditions_test
        PRIVATE
            chronosstream_core
    )

    add_test(NAME boundary_conditions_test COMMAND boundary_conditions_test)

    add_executable(perf_benchmark_test
        tests/perf/perf_benchmark_test.cpp
    )

    target_link_libraries(perf_benchmark_test
        PRIVATE
            chronosstream_core
    )

    add_test(NAME perf_benchmark_test COMMAND perf_benchmark_test)
endif()
