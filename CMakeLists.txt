cmake_minimum_required(VERSION 3.20)
project(UndekMacro VERSION 1.28.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(UNDEK_BUILD_TESTS "Build unit tests" ON)

# nlohmann/json is the only third-party header. Look for it in this order:
#   1. -DUNDEK_JSON_DIR=<folder containing nlohmann/json.hpp>
#   2. third_party/nlohmann/json.hpp, which you can just drop in for offline builds
#   3. download the pinned header and check its hash
set(UNDEK_JSON_VERSION "3.11.3")
set(UNDEK_JSON_SHA256 "9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6")
set(UNDEK_JSON_DIR "" CACHE PATH "Folder that already contains nlohmann/json.hpp")

if(UNDEK_JSON_DIR)
    set(UNDEK_JSON_INCLUDE ${UNDEK_JSON_DIR})
elseif(EXISTS ${CMAKE_SOURCE_DIR}/third_party/nlohmann/json.hpp)
    set(UNDEK_JSON_INCLUDE ${CMAKE_SOURCE_DIR}/third_party)
else()
    set(UNDEK_JSON_INCLUDE ${CMAKE_BINARY_DIR}/_deps/json)
    set(UNDEK_JSON_HEADER ${UNDEK_JSON_INCLUDE}/nlohmann/json.hpp)
    if(NOT EXISTS ${UNDEK_JSON_HEADER})
        message(STATUS "Fetching nlohmann/json ${UNDEK_JSON_VERSION}...")
        file(DOWNLOAD
            https://github.com/nlohmann/json/releases/download/v${UNDEK_JSON_VERSION}/json.hpp
            ${UNDEK_JSON_HEADER}
            EXPECTED_HASH SHA256=${UNDEK_JSON_SHA256}
            SHOW_PROGRESS
            TLS_VERIFY ON
            STATUS json_dl_status
        )
        list(GET json_dl_status 0 json_dl_code)
        list(GET json_dl_status 1 json_dl_text)
        if(NOT json_dl_code EQUAL 0 OR NOT EXISTS ${UNDEK_JSON_HEADER})
            file(REMOVE ${UNDEK_JSON_HEADER})
            message(FATAL_ERROR
                "Could not get nlohmann/json.hpp (${json_dl_text}). For an offline build, "
                "put the header at third_party/nlohmann/json.hpp or configure with "
                "-DUNDEK_JSON_DIR=<folder containing nlohmann/json.hpp>.")
        endif()
    endif()
endif()

set(UNDEK_CORE_SOURCES
    src/core/Vec2.h
    src/core/HumanizationProfile.cpp
    src/core/HumanizationSession.cpp
    src/core/RandomizationEngine.cpp
    src/core/PathGenerator.cpp
    src/core/ActionScheduler.cpp
    src/input/Win32SendInputBackend.cpp
    src/input/InterceptionBackend.cpp
    src/input/ThrottledInputBackend.cpp
    src/input/InputBackendFactory.cpp
    src/input/RecorderTape.cpp
    src/input/InputRecorder.cpp
    src/screen/ScreenSampler.cpp
    src/vision/Image.cpp
    src/vision/ScreenGrab.cpp
    src/script/Lexer.cpp
    src/script/Parser.cpp
    src/script/DebugController.cpp
    src/script/Vm.cpp
    src/script/BuiltinFunctions.cpp
    src/script/Checker.cpp
    src/script/PicturePath.cpp
    src/script/FunctionRegistry.cpp
    src/app/ScriptRunner.cpp
    src/app/AppPaths.cpp
    src/app/AppSettings.cpp
    src/app/FirstRun.cpp
    src/app/FileLog.cpp
    src/app/SingleInstance.cpp
    src/util/AtomicFile.cpp
    src/core/ScanKey.cpp
    src/core/RunSlot.cpp
    src/core/ProfileStats.cpp
    src/app/SoundFx.cpp
    src/app/main_cli.cpp
    src/bindings/BindingConfig.cpp
    src/bindings/HotkeyParser.cpp
    src/bindings/HotkeyRegistry.cpp
    src/bindings/HotkeyHost.cpp
    src/bindings/HotkeyService.cpp
    src/bindings/MousePressQueue.cpp
    src/bindings/MouseHotkeyWatcher.cpp
)

if(WIN32)
    include(FetchContent)
    FetchContent_Declare(imgui
        URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.5.zip
        URL_HASH SHA256=15c60ad10e331378cda381002418f42ea2e98b9c05758eb493b23181bb38ca45
    )
    FetchContent_MakeAvailable(imgui)

    set(UNDEK_APP_SOURCES
        src/editor/main.cpp
        src/editor/UndekApp.cpp
        src/editor/MacroLibrary.cpp
        src/editor/RecordedActions.cpp
        src/editor/MacroBuilder.cpp
        src/editor/UiTheme.cpp
        src/editor/TrayIcon.cpp
    )
    set(UNDEK_IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    )

    add_executable(undek
        ${UNDEK_CORE_SOURCES}
        ${UNDEK_APP_SOURCES}
        ${UNDEK_IMGUI_SOURCES}
        resources/undek.rc
    )

    target_include_directories(undek PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${UNDEK_JSON_INCLUDE}
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_compile_definitions(undek PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN
        IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
        IMGUI_IMPL_WIN32_DISABLE_LINKING_XINPUT
        UNDEK_VERSION="${PROJECT_VERSION}"
    )
    set_target_properties(undek PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(undek PRIVATE user32 gdi32 d3d11 dxgi shell32 winmm)
endif()

if(UNDEK_BUILD_TESTS)
    enable_testing()

    add_executable(test_gaussian tests/test_gaussian.cpp src/core/HumanizationProfile.cpp
        src/core/HumanizationSession.cpp src/core/RandomizationEngine.cpp src/util/AtomicFile.cpp)
    target_include_directories(test_gaussian PRIVATE ${CMAKE_SOURCE_DIR}/src ${UNDEK_JSON_INCLUDE})

    add_executable(test_script tests/test_script.cpp
        src/core/HumanizationProfile.cpp src/core/HumanizationSession.cpp src/core/RandomizationEngine.cpp
        src/core/PathGenerator.cpp src/util/AtomicFile.cpp src/core/ScanKey.cpp src/core/RunSlot.cpp src/core/ProfileStats.cpp
        src/vision/Image.cpp src/vision/ScreenGrab.cpp
        src/core/ActionScheduler.cpp src/screen/ScreenSampler.cpp src/script/Lexer.cpp src/script/Parser.cpp
        src/script/DebugController.cpp src/script/Vm.cpp src/script/BuiltinFunctions.cpp
        src/script/FunctionRegistry.cpp src/script/Checker.cpp src/script/PicturePath.cpp
        src/app/AppPaths.cpp src/app/FileLog.cpp src/app/SingleInstance.cpp src/app/AppSettings.cpp
        src/bindings/BindingConfig.cpp
        src/app/ScriptRunner.cpp src/input/InputBackendFactory.cpp src/input/InterceptionBackend.cpp
        src/input/Win32SendInputBackend.cpp
        src/input/ThrottledInputBackend.cpp src/input/RecorderTape.cpp
        src/input/InputRecorder.cpp
        src/bindings/MousePressQueue.cpp
        src/editor/RecordedActions.cpp src/editor/MacroBuilder.cpp src/editor/MacroLibrary.cpp)
    target_include_directories(test_script PRIVATE ${CMAKE_SOURCE_DIR}/src ${UNDEK_JSON_INCLUDE})
    target_compile_definitions(test_script PRIVATE
        UNDEK_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
        UNDEK_PROFILES_DIR="${CMAKE_SOURCE_DIR}/profiles")
    if(WIN32)
        target_compile_definitions(test_script PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_link_libraries(test_script PRIVATE user32 gdi32)
    endif()

    add_executable(test_bezier tests/test_bezier.cpp src/core/HumanizationProfile.cpp
        src/core/HumanizationSession.cpp src/core/RandomizationEngine.cpp src/core/PathGenerator.cpp
        src/util/AtomicFile.cpp)
    target_include_directories(test_bezier PRIVATE ${CMAKE_SOURCE_DIR}/src ${UNDEK_JSON_INCLUDE})

    add_executable(test_vision tests/test_vision.cpp src/vision/Image.cpp)
    target_include_directories(test_vision PRIVATE ${CMAKE_SOURCE_DIR}/src)

    add_executable(test_firstrun tests/test_firstrun.cpp src/app/FirstRun.cpp
        src/core/HumanizationProfile.cpp src/util/AtomicFile.cpp src/bindings/BindingConfig.cpp
        src/screen/ScreenSampler.cpp)
    target_include_directories(test_firstrun PRIVATE ${CMAKE_SOURCE_DIR}/src ${UNDEK_JSON_INCLUDE})
    if(WIN32)
        target_link_libraries(test_firstrun PRIVATE user32 gdi32)
    endif()

    add_executable(test_robustness tests/test_robustness.cpp src/script/Lexer.cpp
        src/script/Parser.cpp src/script/BuiltinFunctions.cpp src/script/FunctionRegistry.cpp
        src/vision/Image.cpp src/app/AppPaths.cpp)
    target_include_directories(test_robustness PRIVATE ${CMAKE_SOURCE_DIR}/src)

    find_package(Threads REQUIRED)
    add_executable(test_functions tests/test_functions.cpp src/script/FunctionRegistry.cpp)
    target_include_directories(test_functions PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_functions PRIVATE Threads::Threads)

    add_test(NAME gaussian COMMAND test_gaussian)
    add_test(NAME bezier COMMAND test_bezier)
    add_test(NAME script COMMAND test_script)
    add_test(NAME vision COMMAND test_vision)
    add_test(NAME robustness COMMAND test_robustness)
    add_test(NAME functions COMMAND test_functions)
    add_test(NAME firstrun COMMAND test_firstrun)
endif()

# Fuzzers need clang, because libFuzzer comes with it. They aren't built by
# default and aren't part of the test run: enable them, then leave one running
# for as long as you have the patience for.
option(UNDEK_BUILD_FUZZERS "Build libFuzzer targets (clang only)" OFF)

if(UNDEK_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "UNDEK_BUILD_FUZZERS needs clang, since libFuzzer ships with it.")
    endif()

    add_executable(fuzz_script fuzz/fuzz_script.cpp
        src/script/Lexer.cpp src/script/Parser.cpp src/script/BuiltinFunctions.cpp
        src/script/FunctionRegistry.cpp src/vision/Image.cpp src/app/AppPaths.cpp)
    target_include_directories(fuzz_script PRIVATE ${CMAKE_SOURCE_DIR}/src)

    add_executable(fuzz_bmp fuzz/fuzz_bmp.cpp src/vision/Image.cpp)
    target_include_directories(fuzz_bmp PRIVATE ${CMAKE_SOURCE_DIR}/src)

    foreach(target fuzz_script fuzz_bmp)
        target_compile_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined -g)
        target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
endif()
