#include "storyforge/io/story_loader.hpp"
#include "storyforge/runtime/story_runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <variant>

#include <catch2/catch_test_macros.hpp>

namespace {

storyforge::StoryPackage example_package() {
    const auto path = std::filesystem::path{STORYFORGE_SOURCE_DIR} / "examples/clockwork-academy";
    auto loaded = storyforge::load_story_package(path);
    REQUIRE(loaded.report.valid());
    REQUIRE(loaded.package.has_value());
    return std::move(*loaded.package);
}

storyforge::StoryDelta successful(storyforge::CommandResult result) {
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(result));
    return std::get<storyforge::StoryDelta>(std::move(result));
}

} // namespace

TEST_CASE("observing the gear changes authoritative state and unlocks repair") {
    const auto package = example_package();
    auto state = storyforge::make_initial_state(package);
    const auto before = state;

    const auto first = successful(storyforge::execute_command(
        package, state, {storyforge::ChoiceId{"choice.inspect"}}));
    CHECK(first.before == before);
    CHECK(first.after == state);
    CHECK(state.turn == 1);
    CHECK(state.position.node == storyforge::NodeId{"node.decision"});
    CHECK(state.variables.at(storyforge::VariableId{"var.observed-gears"}) ==
          storyforge::StoryValue{true});
    CHECK(state.inventory.at(storyforge::ItemId{"item.etched-gear"}) == 1);
    CHECK_FALSE(first.events.empty());

    const auto* decision = storyforge::current_node(package, state);
    REQUIRE(decision != nullptr);
    CHECK(storyforge::choice_is_available(decision->choices.front(), state));

    const auto second = successful(storyforge::execute_command(
        package, state, {storyforge::ChoiceId{"choice.repair"}}));
    CHECK(second.before.inventory.at(storyforge::ItemId{"item.etched-gear"}) == 1);
    CHECK(state.inventory.empty());
    CHECK(state.completed_events.contains(storyforge::EventId{"event.bell-repaired"}));
    CHECK(state.position.node == storyforge::NodeId{"node.quiet-ending"});
    CHECK(state.turn == 2);
    REQUIRE(storyforge::current_node(package, state) != nullptr);
    CHECK(storyforge::current_node(package, state)->terminal);
}

TEST_CASE("reporting does not allow repair but can reach the other ending") {
    const auto package = example_package();
    auto state = storyforge::make_initial_state(package);
    successful(storyforge::execute_command(package, state,
                                           {storyforge::ChoiceId{"choice.report"}}));

    const auto before_rejected = state;
    const auto rejected = storyforge::execute_command(
        package, state, {storyforge::ChoiceId{"choice.repair"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(rejected));
    CHECK(std::get<storyforge::RuntimeError>(rejected).code ==
          storyforge::RuntimeErrorCode::condition_unmet);
    CHECK(state == before_rejected);
    CHECK(state.npc_memory.at(storyforge::NpcId{"npc.tutor-lin"}).at("reported-the-chime") ==
          storyforge::StoryValue{true});

    successful(storyforge::execute_command(package, state,
                                           {storyforge::ChoiceId{"choice.seek-help"}}));
    CHECK(state.position.node == storyforge::NodeId{"node.archive-ending"});
    CHECK(state.completed_events.contains(storyforge::EventId{"event.bell-contained"}));
    CHECK(storyforge::current_node(package, state)->terminal);
}

TEST_CASE("a late effect failure leaves every prior effect uncommitted") {
    auto package = example_package();
    auto& choice = package.chapters.front().scenes.front().nodes.front().choices.front();
    choice.effects.push_back(storyforge::StoryEffect{
        storyforge::RemoveItemEffect{storyforge::ItemId{"item.missing"}, 1}});
    auto state = storyforge::make_initial_state(package);
    const auto before = state;

    const auto result = storyforge::execute_command(
        package, state, {storyforge::ChoiceId{"choice.inspect"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(result));
    CHECK(std::get<storyforge::RuntimeError>(result).code ==
          storyforge::RuntimeErrorCode::insufficient_resources);
    CHECK(state == before);
}

TEST_CASE("invalid choices and terminal actions never advance the turn") {
    const auto package = example_package();
    auto state = storyforge::make_initial_state(package);
    const auto invalid = storyforge::execute_command(
        package, state, {storyforge::ChoiceId{"choice.repair"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(invalid));
    CHECK(std::get<storyforge::RuntimeError>(invalid).code ==
          storyforge::RuntimeErrorCode::unknown_choice);
    CHECK(state.turn == 0);

    successful(storyforge::execute_command(package, state,
                                           {storyforge::ChoiceId{"choice.inspect"}}));
    successful(storyforge::execute_command(package, state,
                                           {storyforge::ChoiceId{"choice.repair"}}));
    const auto finished = storyforge::execute_command(
        package, state, {storyforge::ChoiceId{"choice.inspect"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(finished));
    CHECK(std::get<storyforge::RuntimeError>(finished).code ==
          storyforge::RuntimeErrorCode::story_finished);
    CHECK(state.turn == 2);
}

TEST_CASE("integer overflow and incompatible variable types reject the whole action") {
    auto package = example_package();
    auto& courage = package.variables.front();
    courage.initial_value = std::numeric_limits<std::int64_t>::max();
    auto state = storyforge::make_initial_state(package);
    const auto before = state;

    const auto overflow = storyforge::execute_command(
        package, state, {storyforge::ChoiceId{"choice.report"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(overflow));
    CHECK(std::get<storyforge::RuntimeError>(overflow).code ==
          storyforge::RuntimeErrorCode::arithmetic_overflow);
    CHECK(state == before);

    courage.initial_value = std::string{"brave"};
    state = storyforge::make_initial_state(package);
    const auto wrong_type = storyforge::execute_command(
        package, state, {storyforge::ChoiceId{"choice.report"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(wrong_type));
    CHECK(std::get<storyforge::RuntimeError>(wrong_type).code ==
          storyforge::RuntimeErrorCode::invalid_effect);
    CHECK(state == storyforge::make_initial_state(package));
}

TEST_CASE("composed conditions read committed state") {
    const auto package = example_package();
    auto state = storyforge::make_initial_state(package);

    auto observed = std::make_shared<storyforge::StoryCondition>(storyforge::StoryCondition{
        storyforge::VariableComparison{storyforge::VariableId{"var.observed-gears"},
                                       storyforge::CompareOperator::equal, true}});
    auto has_gear = std::make_shared<storyforge::StoryCondition>(storyforge::StoryCondition{
        storyforge::HasItemCondition{storyforge::ItemId{"item.etched-gear"}, 1}});
    const storyforge::StoryCondition both{storyforge::AllCondition{{observed, has_gear}}};
    const storyforge::StoryCondition either{storyforge::AnyCondition{{observed, has_gear}}};
    const storyforge::StoryCondition not_both{storyforge::NotCondition{
        std::make_shared<storyforge::StoryCondition>(both)}};

    CHECK_FALSE(storyforge::evaluate_condition(both, state));
    CHECK_FALSE(storyforge::evaluate_condition(either, state));
    CHECK(storyforge::evaluate_condition(not_both, state));
    successful(storyforge::execute_command(package, state,
                                           {storyforge::ChoiceId{"choice.inspect"}}));
    CHECK(storyforge::evaluate_condition(both, state));
    CHECK(storyforge::evaluate_condition(either, state));
    CHECK_FALSE(storyforge::evaluate_condition(not_both, state));
}

TEST_CASE("scene effects enter the destination scene's entry node") {
    auto package = example_package();
    auto& chapter = package.chapters.front();
    chapter.scenes.push_back(storyforge::StoryScene{
        storyforge::SceneId{"scene.workshop"}, "校准室", storyforge::NodeId{"node.workshop-entry"},
        {{storyforge::NodeId{"node.workshop-entry"}, "钟摆正在摇晃。", {}, true}}});
    auto& choice = chapter.scenes.front().nodes.front().choices.front();
    choice.target_node.reset();
    choice.effects.push_back(storyforge::StoryEffect{
        storyforge::SwitchSceneEffect{storyforge::SceneId{"scene.workshop"}}});
    auto state = storyforge::make_initial_state(package);

    successful(storyforge::execute_command(package, state,
                                           {storyforge::ChoiceId{"choice.inspect"}}));
    CHECK(state.position.scene == storyforge::SceneId{"scene.workshop"});
    CHECK(state.position.node == storyforge::NodeId{"node.workshop-entry"});
    REQUIRE(storyforge::current_scene(package, state) != nullptr);
    CHECK(storyforge::current_scene(package, state)->title == "校准室");
}

TEST_CASE("gold and memory conditions read committed events and resources") {
    auto package = example_package();
    auto& report = package.chapters.front().scenes.front().nodes.front().choices[1];
    report.effects.push_back(storyforge::StoryEffect{storyforge::AddGoldEffect{5}});
    auto state = storyforge::make_initial_state(package);

    successful(storyforge::execute_command(package, state,
                                           {storyforge::ChoiceId{"choice.report"}}));
    CHECK(state.gold == 5);
    CHECK(storyforge::evaluate_condition(
        storyforge::StoryCondition{storyforge::GoldCondition{
            storyforge::CompareOperator::greater_equal, 5}}, state));
    CHECK(storyforge::evaluate_condition(
        storyforge::StoryCondition{storyforge::NpcMemoryCondition{
            storyforge::NpcId{"npc.tutor-lin"}, "reported-the-chime", true}}, state));

    successful(storyforge::execute_command(package, state,
                                           {storyforge::ChoiceId{"choice.seek-help"}}));
    CHECK(storyforge::evaluate_condition(
        storyforge::StoryCondition{storyforge::EventTriggeredCondition{
            storyforge::EventId{"event.bell-contained"}}}, state));
}

TEST_CASE("an overdrawn gold effect does not commit an earlier gain") {
    auto package = example_package();
    auto& inspect = package.chapters.front().scenes.front().nodes.front().choices.front();
    inspect.effects.push_back(storyforge::StoryEffect{storyforge::AddGoldEffect{5}});
    inspect.effects.push_back(storyforge::StoryEffect{storyforge::AddGoldEffect{-6}});
    auto state = storyforge::make_initial_state(package);
    const auto before = state;

    const auto result = storyforge::execute_command(
        package, state, {storyforge::ChoiceId{"choice.inspect"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(result));
    CHECK(std::get<storyforge::RuntimeError>(result).code ==
          storyforge::RuntimeErrorCode::insufficient_resources);
    CHECK(state == before);
}

TEST_CASE("chapter effects enter the destination chapter's entry scene and node") {
    auto package = example_package();
    package.chapters.push_back(storyforge::StoryChapter{
        storyforge::ChapterId{"chapter.after"}, "第二章", storyforge::SceneId{"scene.after"},
        {{storyforge::SceneId{"scene.after"}, "新场景", storyforge::NodeId{"node.after"},
          {{storyforge::NodeId{"node.after"}, "新章开始。", {}, true}}}}});
    auto& report = package.chapters.front().scenes.front().nodes.front().choices[1];
    report.target_node.reset();
    report.effects.push_back(storyforge::StoryEffect{
        storyforge::AdvanceChapterEffect{storyforge::ChapterId{"chapter.after"}}});
    auto state = storyforge::make_initial_state(package);

    successful(storyforge::execute_command(package, state,
                                           {storyforge::ChoiceId{"choice.report"}}));
    CHECK(state.position.chapter == storyforge::ChapterId{"chapter.after"});
    CHECK(state.position.scene == storyforge::SceneId{"scene.after"});
    CHECK(state.position.node == storyforge::NodeId{"node.after"});
    REQUIRE(storyforge::current_node(package, state) != nullptr);
    CHECK(storyforge::current_node(package, state)->terminal);
}

TEST_CASE("NPC relationship changes are typed, conditional, and atomic on overflow") {
    auto package = example_package();
    auto& choice = package.chapters.front().scenes.front().nodes.front().choices.front();
    const storyforge::NpcId npc{"npc.tutor-lin"};
    choice.effects.push_back(storyforge::StoryEffect{
        storyforge::ModifyNpcRelationshipEffect{npc, 2}});
    auto state = storyforge::make_initial_state(package);
    CHECK(storyforge::evaluate_condition(storyforge::StoryCondition{
        storyforge::NpcRelationshipCondition{npc, storyforge::CompareOperator::equal, 0}}, state));
    successful(storyforge::execute_command(package, state,
                                           {storyforge::ChoiceId{"choice.inspect"}}));
    CHECK(state.npc_relationships.at(npc) == 2);
    CHECK(storyforge::evaluate_condition(storyforge::StoryCondition{
        storyforge::NpcRelationshipCondition{npc, storyforge::CompareOperator::greater_equal, 2}}, state));

    choice.effects.push_back(storyforge::StoryEffect{
        storyforge::ModifyNpcRelationshipEffect{npc, std::numeric_limits<std::int64_t>::max()}});
    state = storyforge::make_initial_state(package);
    const auto before = state;
    const auto rejected = storyforge::execute_command(package, state,
                                                       {storyforge::ChoiceId{"choice.inspect"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(rejected));
    CHECK(std::get<storyforge::RuntimeError>(rejected).code ==
          storyforge::RuntimeErrorCode::arithmetic_overflow);
    CHECK(state == before);
}
