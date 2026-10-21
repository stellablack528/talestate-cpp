#include "storyforge/io/story_loader.hpp"
#include "storyforge/validation/validation.hpp"

#include <algorithm>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

namespace {

storyforge::StoryPackage valid_package() {
    const auto path = std::filesystem::path{STORYFORGE_SOURCE_DIR} / "examples/clockwork-academy";
    auto result = storyforge::load_story_package(path);
    REQUIRE(result.package.has_value());
    REQUIRE(result.report.valid());
    return std::move(*result.package);
}

bool has_code(const storyforge::ValidationReport& report, const std::string& code) {
    return std::any_of(report.issues.begin(), report.issues.end(),
                       [&](const auto& issue) { return issue.code == code; });
}

} // namespace

TEST_CASE("duplicate identifiers are rejected") {
    auto package = valid_package();
    package.variables.push_back(package.variables.front());
    const auto report = storyforge::validate_story_package(package);
    CHECK_FALSE(report.valid());
    CHECK(has_code(report, "semantic.duplicate_id"));
}

TEST_CASE("a choice cannot target a missing node") {
    auto package = valid_package();
    package.chapters.front().scenes.front().nodes.front().choices.front().target_node =
        storyforge::NodeId{"node.missing"};
    const auto report = storyforge::validate_story_package(package);
    CHECK_FALSE(report.valid());
    CHECK(has_code(report, "semantic.missing_reference"));
}

TEST_CASE("an invalid package entry is rejected") {
    auto package = valid_package();
    package.entry.scene = storyforge::SceneId{"scene.missing"};
    const auto report = storyforge::validate_story_package(package);
    CHECK_FALSE(report.valid());
    CHECK(has_code(report, "semantic.invalid_entry"));
}

TEST_CASE("unreachable nodes are reported as errors") {
    auto package = valid_package();
    package.chapters.front().scenes.front().nodes.push_back(
        storyforge::StoryNode{storyforge::NodeId{"node.hidden"}, "Nobody can reach this node.", {}, true});
    const auto report = storyforge::validate_story_package(package);
    CHECK_FALSE(report.valid());
    CHECK(has_code(report, "semantic.unreachable_node"));
}

TEST_CASE("adding a non-numeric value is an illegal effect") {
    auto package = valid_package();
    auto& effect = package.chapters.front().scenes.front().nodes.front().choices.front().effects.front();
    effect.data = storyforge::ModifyVariableEffect{
        storyforge::VariableId{"var.courage"}, storyforge::VariableOperation::add,
        storyforge::StoryValue{std::string{"not-a-number"}}};
    const auto report = storyforge::validate_story_package(package);
    CHECK_FALSE(report.valid());
    CHECK(has_code(report, "semantic.invalid_effect"));
}

TEST_CASE("a choice cannot combine node and scene jumps") {
    auto package = valid_package();
    auto& choice = package.chapters.front().scenes.front().nodes.front().choices.front();
    choice.effects.push_back(storyforge::StoryEffect{
        storyforge::SwitchSceneEffect{storyforge::SceneId{"scene.bell-hall"}}});
    const auto report = storyforge::validate_story_package(package);
    CHECK_FALSE(report.valid());
    CHECK(has_code(report, "semantic.invalid_effect"));
}

TEST_CASE("variable comparisons must use the declared value type") {
    auto package = valid_package();
    auto& condition = package.chapters.front().scenes.front().nodes[1].choices.front().conditions.front();
    auto& all = std::get<storyforge::AllCondition>(condition.data);
    auto& comparison = std::get<storyforge::VariableComparison>(all.conditions.front()->data);
    comparison.value = std::string{"true"};
    const auto report = storyforge::validate_story_package(package);
    CHECK_FALSE(report.valid());
    CHECK(has_code(report, "semantic.invalid_condition"));
}

TEST_CASE("parameterized routes and NPC locations require real unique world references") {
    auto package = valid_package();
    package.agent_routes.push_back(package.agent_routes.front());
    package.agent_routes.front().to = storyforge::SceneId{"scene.nowhere"};
    package.agent_npcs.front().scene = storyforge::SceneId{"scene.nowhere"};
    const auto report = storyforge::validate_story_package(package);
    CHECK_FALSE(report.valid());
    CHECK(has_code(report, "semantic.missing_reference"));

    auto duplicate = valid_package();
    duplicate.agent_routes.push_back(duplicate.agent_routes.front());
    duplicate.agent_npcs.push_back(duplicate.agent_npcs.front());
    const auto duplicate_report = storyforge::validate_story_package(duplicate);
    CHECK_FALSE(duplicate_report.valid());
    CHECK(has_code(duplicate_report, "semantic.duplicate_id"));
}

TEST_CASE("NPC dialogue rules require unique topics and valid non-moving effects") {
    auto duplicate = valid_package();
    duplicate.agent_npcs.front().dialogue_rules.push_back(
        duplicate.agent_npcs.front().dialogue_rules.front());
    const auto duplicate_report = storyforge::validate_story_package(duplicate);
    CHECK_FALSE(duplicate_report.valid());
    CHECK(has_code(duplicate_report, "semantic.duplicate_id"));

    auto invalid = valid_package();
    invalid.agent_npcs.front().dialogue_rules.front().effects.push_back(
        storyforge::StoryEffect{storyforge::SwitchSceneEffect{storyforge::SceneId{"scene.archive-room"}}});
    const auto invalid_report = storyforge::validate_story_package(invalid);
    CHECK_FALSE(invalid_report.valid());
    CHECK(has_code(invalid_report, "semantic.invalid_dialogue"));

    auto unknown_npc = valid_package();
    unknown_npc.agent_npcs.front().dialogue_rules.front().effects.push_back(
        storyforge::StoryEffect{storyforge::WriteNpcMemoryEffect{
            storyforge::NpcId{"npc.nonexistent"}, "shared-secret", true}});
    const auto reference_report = storyforge::validate_story_package(unknown_npc);
    CHECK_FALSE(reference_report.valid());
    CHECK(has_code(reference_report, "semantic.missing_reference"));
}
