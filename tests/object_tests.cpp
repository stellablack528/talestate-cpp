#include "storyforge/agent/story_agent.hpp"
#include "storyforge/io/story_loader.hpp"
#include "storyforge/io/story_save.hpp"
#include "storyforge/runtime/story_runtime.hpp"
#include "storyforge/validation/validation.hpp"

#include <filesystem>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace {

storyforge::StoryPackage world(const std::string& name) {
    auto loaded = storyforge::load_story_package(
        std::filesystem::path{STORYFORGE_SOURCE_DIR} / "examples" / name);
    REQUIRE(loaded.report.valid());
    REQUIRE(loaded.package.has_value());
    return std::move(*loaded.package);
}

class ScriptedProvider final : public storyforge::agent::ILlmProvider {
public:
    std::vector<storyforge::agent::ChatResult> replies;
    storyforge::agent::ChatResult chat(const storyforge::agent::ChatRequest&) override {
        REQUIRE_FALSE(replies.empty());
        auto result = std::move(replies.front());
        replies.erase(replies.begin());
        return result;
    }
};

} // namespace

TEST_CASE("generic inspect and take work across both worlds and survive save") {
    for (const auto& [name, inspect_id, take_id, item_id] : {
        std::tuple{"harbor-lantern", "object.tide-chart", "object.shell-token", "item.shell-token"},
        std::tuple{"clockwork-academy", "object.bell-inscription", "object.brass-button", "item.brass-button"}}) {
        const auto package = world(name);
        auto state = storyforge::make_initial_state(package);
        const auto initial = state;

        auto inspected = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::inspect, inspect_id, {}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(inspected));
        CHECK(state.inventory.empty());
        CHECK(state.collected_objects.empty());
        CHECK(state.turn == initial.turn + 1);
        const auto& inspect_events = std::get<storyforge::StoryDelta>(inspected).events;
        REQUIRE(inspect_events.size() == 1);
        CHECK(inspect_events.front().type == "object_inspected");

        auto denied = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::take, inspect_id, {}}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(denied));
        CHECK(state.inventory.empty());

        auto taken = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::inspect, take_id, {}},
             {storyforge::AgentActionKind::take, take_id, {}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(taken));
        CHECK(state.inventory.at(storyforge::ItemId{item_id}) == 1);
        CHECK(state.collected_objects.contains(take_id));
        CHECK(state.turn == initial.turn + 2);

        const auto after_take = state;
        denied = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::take, take_id, {}}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(denied));
        CHECK(state == after_take);
        denied = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::inspect, take_id, {}}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(denied));
        CHECK(state == after_take);

        auto encoded = storyforge::serialize_story_save(package, state);
        REQUIRE(std::holds_alternative<std::string>(encoded));
        auto restored = storyforge::deserialize_story_save(package, std::get<std::string>(encoded));
        REQUIRE(std::holds_alternative<storyforge::StoryState>(restored));
        auto resumed = std::get<storyforge::StoryState>(std::move(restored));
        CHECK(resumed == state);
        denied = storyforge::execute_agent_requests(package, resumed,
            {{storyforge::AgentActionKind::take, take_id, {}}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(denied));
        CHECK(resumed == state);
    }
}

TEST_CASE("object actions reject wrong scenes and roll back composed turns") {
    const auto package = world("harbor-lantern");
    auto state = storyforge::make_initial_state(package);
    const auto initial = state;
    auto denied = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::take, "object.shell-token", {}},
         {storyforge::AgentActionKind::inspect, "object.missing", {}}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(denied));
    CHECK(state == initial);
    denied = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::inspect, "object.shell-token", "unwanted topic"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(denied));
    CHECK(state == initial);
    auto moved = state;
    moved.position.scene = storyforge::SceneId{"scene.lighthouse"};
    moved.position.node = storyforge::NodeId{"node.beacon-base"};
    const auto at_lighthouse = moved;
    denied = storyforge::execute_agent_requests(package, moved,
        {{storyforge::AgentActionKind::inspect, "object.tide-chart", {}}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(denied));
    CHECK(moved == at_lighthouse);
}

TEST_CASE("agent plans inspect and take without inventing object descriptions") {
    const auto package = world("harbor-lantern");
    auto state = storyforge::make_initial_state(package);
    storyforge::agent::StoryAgent agent;
    ScriptedProvider provider;
    provider.replies = {
        storyforge::agent::ChatResponse{R"({"actions":[{"type":"inspect","target":"object.shell-token"},{"type":"take","target":"object.shell-token"}]})", {}, {}},
        storyforge::agent::ChatResponse{"你把信物收了起来。", {}, {}}};
    auto result = agent.take_turn(package, state, "看看木箱旁的东西并拾起", provider);
    REQUIRE(std::holds_alternative<storyforge::agent::AgentTurn>(result));
    const auto& turn = std::get<storyforge::agent::AgentTurn>(result);
    CHECK(turn.actions.size() == 2);
    CHECK(turn.narration.find("贝壳信物") != std::string::npos);
    CHECK(state.inventory.at(storyforge::ItemId{"item.shell-token"}) == 1);
    CHECK(state.collected_objects.contains("object.shell-token"));
}

TEST_CASE("invalid object declarations and forged collected objects are rejected") {
    auto package = world("harbor-lantern");
    package.agent_objects.push_back(package.agent_objects.front());
    package.agent_objects.front().scene = storyforge::SceneId{"scene.nowhere"};
    auto report = storyforge::validate_story_package(package);
    CHECK_FALSE(report.valid());

    package = world("harbor-lantern");
    auto encoded = storyforge::serialize_story_save(package, storyforge::make_initial_state(package));
    REQUIRE(std::holds_alternative<std::string>(encoded));
    auto json = nlohmann::json::parse(std::get<std::string>(encoded));
    auto older = json;
    older["state"].erase("collected_objects");
    auto older_result = storyforge::deserialize_story_save(package, older.dump());
    REQUIRE(std::holds_alternative<storyforge::StoryState>(older_result));
    CHECK(std::get<storyforge::StoryState>(older_result).collected_objects.empty());
    json["state"]["collected_objects"].push_back("object.missing");
    auto decoded = storyforge::deserialize_story_save(package, json.dump());
    CHECK(std::holds_alternative<storyforge::SaveError>(decoded));
}
