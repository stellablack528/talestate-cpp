add_executable(storyforge_tests
    loader_tests.cpp
    validator_tests.cpp
)
target_link_libraries(storyforge_tests PRIVATE StoryForge::storyforge Catch2::Catch2WithMain)
target_compile_definitions(storyforge_tests PRIVATE STORYFORGE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

include(Catch)
catch_discover_tests(storyforge_tests)
