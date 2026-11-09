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

TEST_CASE("player identity, attributes and starter inventory survive saving") {
    auto package = harbor_world();
    storyforge::PlayerRole role;
    role.id = "role.reporter";
    role.name = "Reporter";
    role.attributes = {{"Insight", 7}, {"Will", 5}};
    role.starting_items = {{storyforge::ItemId{"item.notebook"}, 1}};
    package.player_roles.push_back(role);
    auto state = storyforge::make_initial_state(package);
    state.player = storyforge::PlayerProfile{"Avery", role.id, "New student", role.attributes};
    state.inventory = role.starting_items;
    const auto encoded = save_text(package, state);
    auto decoded = storyforge::deserialize_story_save(package, encoded);
    REQUIRE(std::holds_alternative<storyforge::StoryState>(decoded));
    CHECK(std::get<storyforge::StoryState>(decoded) == state);
    auto tampered = nlohmann::json::parse(encoded);
    tampered["state"]["player"]["attributes"]["Insight"] = 10;
    CHECK(std::holds_alternative<storyforge::SaveError>(
        storyforge::deserialize_story_save(package, tampered.dump())));
}

TEST_CASE("character creation choices and live growth injury recovery share save state") {
    auto package = harbor_world();
    storyforge::PlayerRole role;
    role.id = "role.protagonist";
    role.name = "Student";
    role.fixed_name = "Avery";
    role.attributes = {{"energy", 5}};
    role.starting_gold = 12;
    role.editable_fields = {"club"};
    role.traits = {"brave", "careful"};
    role.trait_count = 1;
    package.player_roles.push_back(role);
    package.variables.push_back({storyforge::VariableId{"var.attribute.energy"}, std::int64_t{5}});
    for (const auto& [id, amount] : {std::pair{"growth", 2}, {"injury", -3}, {"recovery", 1}})
        package.agent_actions.push_back({storyforge::ChoiceId{"action." + std::string{id}}, id, {},
            {storyforge::StoryEffect{storyforge::ModifyVariableEffect{
                storyforge::VariableId{"var.attribute.energy"}, storyforge::VariableOperation::add,
                std::int64_t{amount}}}}, std::nullopt});
    auto state = storyforge::make_initial_state(package);
    state.player = storyforge::PlayerProfile{"Avery", role.id, "New student", role.attributes,
        {{"club", "newspaper"}}, {"careful"}};
    state.gold = role.starting_gold;
    for (const auto& [action, expected] : {std::pair{"growth", 7}, {"injury", 4}, {"recovery", 5}}) {
        auto result = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::authored, "action." + std::string{action}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(result));
        CHECK(storyforge::current_player_attributes(package, state).at("energy") == expected);
    }
    CHECK(state.player->attributes.at("energy") == 5); // only the initialization snapshot
    const auto encoded = save_text(package, state);
    auto decoded = storyforge::deserialize_story_save(package, encoded);
    REQUIRE(std::holds_alternative<storyforge::StoryState>(decoded));
    const auto restored = std::get<storyforge::StoryState>(decoded);
    CHECK(restored == state);
    CHECK(storyforge::current_player_attributes(package, restored).at("energy") == 5);
    auto tampered = nlohmann::json::parse(encoded);
    tampered["state"]["player"]["traits"] = nlohmann::json::array({"unknown"});
    CHECK(std::holds_alternative<storyforge::SaveError>(
        storyforge::deserialize_story_save(package, tampered.dump())));
}

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
