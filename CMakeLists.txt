cmake_minimum_required(VERSION 3.20)

project(binja_rewind VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(FetchContent)

option(BINJA_REWIND_BUILD_TESTS "Build tests" OFF)
option(BINJA_REWIND_USE_SYSTEM_QT "Use system Qt instead of qt-artifacts" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(BinjaQt)

if(NOT BINJA_REWIND_USE_SYSTEM_QT)
    binja_qt_configure()
endif()

set(BINJA_API_VERSION "" CACHE STRING "Binary Ninja API git revision (commit hash)")
if(NOT BINJA_API_VERSION)
    message(FATAL_ERROR "BINJA_API_VERSION is required. Set it to the commit hash in api_REVISION.txt from your Binary Ninja install.")
endif()

set(BN_API_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(binaryninjaapi
    GIT_REPOSITORY https://github.com/Vector35/binaryninja-api.git
    GIT_TAG        ${BINJA_API_VERSION}
    GIT_SUBMODULES_RECURSE ON
)

FetchContent_MakeAvailable(binaryninjaapi)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)

set(W1TN3SS_PATH ${CMAKE_SOURCE_DIR}/lib/w1tn3ss CACHE PATH "Path to w1tn3ss source")
if(NOT EXISTS ${W1TN3SS_PATH}/CMakeLists.txt)
    message(FATAL_ERROR "w1tn3ss not found at ${W1TN3SS_PATH}. Add the submodule or set W1TN3SS_PATH.")
endif()

set(W1_BUILD_ALL OFF CACHE BOOL "Build all w1tn3ss components" FORCE)
set(W1_BUILD_W1REWIND ON CACHE BOOL "Build w1rewind" FORCE)
set(W1_ENABLE_DEV_TOOLS OFF CACHE BOOL "Disable w1tn3ss dev tooling" FORCE)
set(W1_EXPORT_COMPILE_COMMANDS OFF CACHE BOOL "Disable w1tn3ss compile commands export" FORCE)
set(REDLOG_BUILD_EXAMPLES OFF CACHE BOOL "Disable redlog examples" FORCE)
set(REDLOG_BUILD_TESTS OFF CACHE BOOL "Disable redlog tests" FORCE)

add_subdirectory(${W1TN3SS_PATH} ${CMAKE_BINARY_DIR}/_deps/w1tn3ss)

add_library(rewind_core_lib STATIC
    src/rewind/core/mapping/address_mapper.cpp
    src/rewind/core/decode/bn_block_decoder.cpp
    src/rewind/core/decode/instruction_decoder.cpp
    src/rewind/core/breakpoints/breakpoint_provider.cpp
    src/rewind/core/update/update_builder.cpp
    src/rewind/core/functions/trace_function_definer.cpp
    src/rewind/core/engine/rewind_engine.cpp
    src/rewind/core/worker/rewind_worker.cpp
)

target_include_directories(rewind_core_lib
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/lib/third_party
)

target_link_libraries(rewind_core_lib
    PUBLIC
        w1::rewind::replay
        binaryninjaapi
)

add_library(rewind_ui SHARED
    src/rewind/ui/plugin/ui_plugin.cpp
    src/rewind/ui/widgets/sidebar_widget.cpp
    src/rewind/ui/widgets/controls_widget.cpp
    src/rewind/ui/actions/action_router.cpp
    src/rewind/ui/theme/icon_loader.cpp
    src/rewind/ui/theme/gradient_painter.cpp
    src/rewind/ui/settings/rewind_settings.cpp
    src/rewind/ui/widgets/elided_label.cpp
    src/rewind/ui/widgets/registers_view.cpp
    src/rewind/ui/widgets/trace_slice_view.cpp
    src/rewind/ui/widgets/session_view.cpp
    src/rewind/ui/controllers/sidebar_controller.cpp
    resources/rewind_icons.qrc
)

set_target_properties(rewind_ui PROPERTIES
    AUTORCC ON
)

target_include_directories(rewind_ui
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(rewind_ui
    PRIVATE
        rewind_core_lib
        binaryninjaapi
        binaryninjaui
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Svg
)

set_target_properties(rewind_ui PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

if(BINJA_REWIND_BUILD_TESTS)
    add_executable(rewind_core_test
        test/rewind_core/test_main.cpp
        test/rewind_core/path_utils_test.cpp
        test/rewind_core/permission_format_test.cpp
    )

    target_include_directories(rewind_core_test
        PRIVATE
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_SOURCE_DIR}/lib/third_party
    )

    target_link_libraries(rewind_core_test
        PRIVATE
            rewind_core_lib
    )

    set_target_properties(rewind_core_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test
        OUTPUT_NAME rewind_core_test
    )
endif()

find_program(CLANG_FORMAT NAMES clang-format clang-format-17 clang-format-16)
if(CLANG_FORMAT)
    file(GLOB_RECURSE FORMAT_SOURCES
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/src/*.hpp
        ${CMAKE_SOURCE_DIR}/test/*.cpp
        ${CMAKE_SOURCE_DIR}/test/*.hpp
    )

    list(FILTER FORMAT_SOURCES EXCLUDE REGEX "/lib/third_party/")

    add_custom_target(binja_rewind-format
        COMMAND ${CLANG_FORMAT} -i ${FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "formatting source files"
        VERBATIM
    )
endif()
