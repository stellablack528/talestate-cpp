#include "storyforge/io/story_loader.hpp"
#include "storyforge/io/story_save.hpp"
#include "storyforge/runtime/story_runtime.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace {

storyforge::StoryPackage harbor_world() {
    const auto path = std::filesystem::path{STORYFORGE_SOURCE_DIR} / "examples/harbor-lantern";
    auto loaded = storyforge::load_story_package(path);
    REQUIRE(loaded.report.valid());
    REQUIRE(loaded.package.has_value());
    return std::move(*loaded.package);
}

std::string save_text(const storyforge::StoryPackage& package, const storyforge::StoryState& state) {
    auto result = storyforge::serialize_story_save(package, state);
    REQUIRE(std::holds_alternative<std::string>(result));
    return std::get<std::string>(std::move(result));
}

} // namespace

TEST_CASE("harbor dialogue progress survives a save and can unlock the next move") {
    const auto package = harbor_world();
    auto state = storyforge::make_initial_state(package);
    auto talked = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::talk, "npc.alan", "topic.old-route"},
         {storyforge::AgentActionKind::authored, "action.take-wick", {}}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(talked));
    CHECK(state.position.scene == storyforge::SceneId{"scene.dock"});
    CHECK(state.variables.at(storyforge::VariableId{"var.knows-route"}) == storyforge::StoryValue{true});

    state.gold = 7;
    state.completed_events.insert(storyforge::EventId{"event.test-milestone"});
    const auto text = save_text(package, state);
    CHECK(text.find("api_key") == std::string::npos);
    CHECK(text.find("base_url") == std::string::npos);
    CHECK(text.find("model") == std::string::npos);
    auto restored = storyforge::deserialize_story_save(package, text);
    REQUIRE(std::holds_alternative<storyforge::StoryState>(restored));
    auto resumed = std::get<storyforge::StoryState>(std::move(restored));
    CHECK(resumed == state);
    CHECK(resumed.inventory.at(storyforge::ItemId{"item.wick"}) == 1);
    CHECK(resumed.npc_relationships.at(storyforge::NpcId{"npc.alan"}) == 1);
    CHECK(resumed.npc_memory.at(storyforge::NpcId{"npc.alan"}).contains("shared-route"));
    CHECK(resumed.completed_events.contains(storyforge::EventId{"event.test-milestone"}));

    auto moved = storyforge::execute_agent_requests(package, resumed,
        {{storyforge::AgentActionKind::move, "scene.lighthouse", {}}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(moved));
    CHECK(resumed.position.scene == storyforge::SceneId{"scene.lighthouse"});
}

TEST_CASE("save loader rejects wrong worlds and versions without returning a state") {
    auto package = harbor_world();
    const auto text = save_text(package, storyforge::make_initial_state(package));
    package.id = storyforge::StoryId{"different-world"};
    auto mismatch = storyforge::deserialize_story_save(package, text);
    REQUIRE(std::holds_alternative<storyforge::SaveError>(mismatch));
    CHECK(std::get<storyforge::SaveError>(mismatch).code == storyforge::SaveErrorCode::incompatible_world);
    package.id = storyforge::StoryId{"harbor-lantern"};
    package.version = "9.0.0";
    mismatch = storyforge::deserialize_story_save(package, text);
    REQUIRE(std::holds_alternative<storyforge::SaveError>(mismatch));
    CHECK(std::get<storyforge::SaveError>(mismatch).code == storyforge::SaveErrorCode::incompatible_world);
}

TEST_CASE("malformed or impossible saves are rejected before replacing progress") {
    const auto package = harbor_world();
    const auto initial = storyforge::make_initial_state(package);
    const auto original = save_text(package, initial);
    auto broken = storyforge::deserialize_story_save(package, "{not json");
    REQUIRE(std::holds_alternative<storyforge::SaveError>(broken));
    CHECK(std::get<storyforge::SaveError>(broken).code == storyforge::SaveErrorCode::malformed);

    auto json = nlohmann::json::parse(original);
    json["state"]["position"]["scene"] = "scene.missing";
    broken = storyforge::deserialize_story_save(package, json.dump());
    REQUIRE(std::holds_alternative<storyforge::SaveError>(broken));
    json = nlohmann::json::parse(original);
    json["state"]["variables"]["var.knows-route"] = "yes";
    broken = storyforge::deserialize_story_save(package, json.dump());
    REQUIRE(std::holds_alternative<storyforge::SaveError>(broken));
    json = nlohmann::json::parse(original);
    json["state"]["inventory"]["item.wick"] = -1;
    broken = storyforge::deserialize_story_save(package, json.dump());
    REQUIRE(std::holds_alternative<storyforge::SaveError>(broken));
    json = nlohmann::json::parse(original);
    json["api_key"] = "should-never-be-here";
    broken = storyforge::deserialize_story_save(package, json.dump());
    REQUIRE(std::holds_alternative<storyforge::SaveError>(broken));

    auto invalid = initial;
    invalid.position.scene = storyforge::SceneId{"scene.missing"};
    auto encoded = storyforge::serialize_story_save(package, invalid);
    REQUIRE(std::holds_alternative<storyforge::SaveError>(encoded));
    CHECK(std::get<storyforge::SaveError>(encoded).code == storyforge::SaveErrorCode::invalid_state);
    CHECK(initial == storyforge::make_initial_state(package));
}
