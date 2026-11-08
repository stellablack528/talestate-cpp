add_executable(storyforge_tests
    agent_tests.cpp
    connection_check_tests.cpp
    loader_tests.cpp
    object_tests.cpp
    item_interaction_tests.cpp
    provider_tests.cpp
    runtime_tests.cpp
    replay_tests.cpp
    save_tests.cpp
    temporal_rules_tests.cpp
    validator_tests.cpp
    world_map_generator_tests.cpp
    world_seed_generator_tests.cpp
)
target_link_libraries(storyforge_tests PRIVATE StoryForge::storyforge Catch2::Catch2WithMain)
target_compile_definitions(storyforge_tests PRIVATE STORYFORGE_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(storyforge_tests PRIVATE /utf-8)
endif()

include(Catch)
catch_discover_tests(storyforge_tests)
