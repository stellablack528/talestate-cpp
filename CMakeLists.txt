cmake_minimum_required(VERSION 3.24)

project(
    StoryForgeCpp
    VERSION 0.1.0
    DESCRIPTION "A deterministic, data-driven story engine for text RPGs"
    LANGUAGES CXX
)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STORYFORGE_BUILD_TESTS "Build StoryForge tests" ON)
option(STORYFORGE_BUILD_DESKTOP "Build the optional Qt desktop player" ON)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    CLI11
    URL https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.4.2.tar.gz
    URL_HASH SHA256=f2d893a65c3b1324c50d4e682c0cdc021dd0477ae2c048544f39eed6654b699a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json CLI11)

add_library(storyforge
    src/agent/connection_check.cpp
    src/agent/openai_compatible_provider.cpp
    src/agent/story_agent.cpp
    src/io/story_loader.cpp
    src/runtime/story_runtime.cpp
    src/validation/story_validator.cpp
)
add_library(StoryForge::storyforge ALIAS storyforge)

target_include_directories(storyforge
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(storyforge PUBLIC nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(storyforge PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(storyforge PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(STORYFORGE_BUILD_DESKTOP)
    find_package(Qt6 COMPONENTS Widgets Network QUIET)
    if(Qt6_FOUND)
        add_executable(talestate_desktop
            apps/talestate-desktop/main.cpp
            apps/talestate-desktop/qt_http_transport.cpp
        )
        target_link_libraries(talestate_desktop PRIVATE StoryForge::storyforge Qt6::Widgets Qt6::Network)
        target_compile_definitions(talestate_desktop PRIVATE STORYFORGE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
        if(MSVC)
            target_compile_options(talestate_desktop PRIVATE /W4 /EHsc /utf-8)
        else()
            target_compile_options(talestate_desktop PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        if(STORYFORGE_BUILD_TESTS)
            add_test(NAME desktop_smoke COMMAND talestate_desktop --smoke-test)
            set_tests_properties(desktop_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
            add_executable(qt_transport_tests
                tests/qt_transport_tests.cpp
                apps/talestate-desktop/qt_http_transport.cpp
            )
            target_include_directories(qt_transport_tests PRIVATE apps/talestate-desktop)
            target_link_libraries(qt_transport_tests PRIVATE StoryForge::storyforge Qt6::Network Catch2::Catch2WithMain)
            add_test(NAME qt_transport_loopback COMMAND qt_transport_tests)
        endif()
    else()
        message(STATUS "Qt6 Widgets/Network not found; desktop player is skipped")
    endif()
endif()

add_executable(storyforge_cli apps/storyforge-cli/main.cpp)
set_target_properties(storyforge_cli PROPERTIES OUTPUT_NAME storyforge)
target_link_libraries(storyforge_cli PRIVATE StoryForge::storyforge CLI11::CLI11)
if(MSVC)
    target_compile_options(storyforge_cli PRIVATE /utf-8)
endif()

if(STORYFORGE_BUILD_TESTS)
    include(CTest)
    FetchContent_Declare(
        Catch2
        URL https://codeload.github.com/catchorg/Catch2/tar.gz/refs/tags/v3.7.1
        URL_HASH SHA256=c991b247a1a0d7bb9c39aa35faf0fe9e19764213f28ffba3109388e62ee0269c
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    add_subdirectory(tests)
endif()
