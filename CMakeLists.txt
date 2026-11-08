cmake_minimum_required(VERSION 3.25)
project(WHY VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static CRT: the portable build must run without a VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(WHY_ASAN "Build with AddressSanitizer" OFF)

find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(wil CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)
find_path(WEBVIEW2_INCLUDE_DIR WebView2.h REQUIRED)
find_library(WEBVIEW2_LOADER_LIB NAMES WebView2LoaderStatic WebView2Loader.dll REQUIRED)

# Each version has a code name. Change it only when a new one is chosen for the project.
set(WHY_CODENAME "Vigil")
set(WHY_RELEASE_NAME "WHY - ${WHY_CODENAME}")

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00
    WHY_VERSION="${PROJECT_VERSION}" WHY_RELEASE_NAME="${WHY_RELEASE_NAME}")
add_compile_options(/W4 /WX /permissive- /utf-8 /EHsc /sdl /external:anglebrackets /external:W0)
if(WHY_ASAN)
    add_compile_options(/fsanitize=address)
    # The vcpkg libraries are not instrumented, so container annotations must match theirs.
    add_compile_definitions(_DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)
else()
    add_compile_options(/guard:cf)
    add_link_options(/guard:cf)
endif()

# Platform-independent logic: parsing, rules, investigation, storage. Fully unit tested.
add_library(why_core STATIC
    src/core/bugcheck.cpp
    src/core/devtools.cpp
    src/core/dump_header.cpp
    src/core/event_xml.cpp
    src/core/investigator.cpp
    src/core/redact.cpp
    src/core/serialize.cpp
    src/core/signals.cpp
    src/core/store.cpp
    src/core/text.cpp)
target_include_directories(why_core PUBLIC src)
target_link_libraries(why_core PUBLIC nlohmann_json::nlohmann_json unofficial::sqlite3::sqlite3)

# Windows data sources and the engine that runs them.
add_library(why_engine STATIC
    src/collect/devwatch.cpp
    src/collect/drivers.cpp
    src/collect/dumps.cpp
    src/collect/etw.cpp
    src/collect/eventlog.cpp
    src/collect/usn.cpp
    src/engine/engine.cpp
    src/engine/pipe.cpp
    src/engine/service.cpp
    src/engine/winutil.cpp)
target_link_libraries(why_engine PUBLIC why_core WIL::WIL
    wevtapi setupapi tdh advapi32 version shell32 ole32)

# The UI is one self-contained HTML file embedded in the executable.
set(WHY_UI_HTML "${CMAKE_SOURCE_DIR}/ui/dist/index.html")
file(GLOB_RECURSE WHY_UI_SOURCES CONFIGURE_DEPENDS ui/src/*)
list(APPEND WHY_UI_SOURCES "${CMAKE_SOURCE_DIR}/ui/index.html" "${CMAKE_SOURCE_DIR}/ui/package.json" "${CMAKE_SOURCE_DIR}/ui/vite.config.ts")
add_custom_command(OUTPUT "${WHY_UI_HTML}"
    COMMAND npm.cmd run build
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/ui"
    DEPENDS ${WHY_UI_SOURCES}
    COMMENT "Building the WHY? interface"
    VERBATIM)
add_custom_target(why_ui DEPENDS "${WHY_UI_HTML}")

set(WHY_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
configure_file(src/app/resources.rc.in "${CMAKE_BINARY_DIR}/resources.rc" @ONLY)
add_executable(why WIN32
    src/app/main.cpp
    src/app/monitor.cpp
    src/app/notify.cpp
    src/app/settings.cpp
    src/app/tray.cpp
    src/app/window.cpp
    "${CMAKE_BINARY_DIR}/resources.rc")
add_dependencies(why why_ui)
set_source_files_properties("${CMAKE_BINARY_DIR}/resources.rc" PROPERTIES OBJECT_DEPENDS "${WHY_UI_HTML};${CMAKE_SOURCE_DIR}/src/app/why.ico")
target_include_directories(why SYSTEM PRIVATE "${WEBVIEW2_INCLUDE_DIR}")
target_link_libraries(why PRIVATE why_engine "${WEBVIEW2_LOADER_LIB}"
    shell32 ole32 comdlg32 user32 gdi32 shlwapi)
target_link_options(why PRIVATE /MANIFEST:NO)

# why-run: a console program that runs a dev command and keeps its error output when it fails.
add_executable(why-run src/run/main.cpp)
target_link_libraries(why-run PRIVATE why_engine)

add_executable(why_tests
    tests/test_devtools.cpp
    tests/test_dump_header.cpp
    tests/test_event_xml.cpp
    tests/test_fuzz.cpp
    tests/test_investigator.cpp
    tests/test_redact.cpp
    tests/test_signals.cpp
    tests/test_store.cpp)
target_link_libraries(why_tests PRIVATE why_core Catch2::Catch2WithMain)

# Exercises the Windows sources against the real machine, without the UI.
add_executable(why_probe tests/probe.cpp src/app/monitor.cpp)
target_link_libraries(why_probe PRIVATE why_engine dbghelp shell32 ole32 shlwapi)

enable_testing()
add_test(NAME why_tests COMMAND why_tests)
