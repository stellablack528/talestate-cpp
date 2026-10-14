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
