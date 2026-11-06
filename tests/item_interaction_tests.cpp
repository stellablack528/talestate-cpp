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
        auto next = std::move(replies.front());
        replies.erase(replies.begin());
        return next;
    }
};

} // namespace

TEST_CASE("give and use apply the same item protocol in both worlds") {
    for (const auto& [name, object, item, npc, target, given, used] : {
        std::tuple{"harbor-lantern", "object.shell-token", "item.shell-token", "npc.alan",
                   "object.tide-chart", "event.shell-given", "event.chart-matched"},
        std::tuple{"clockwork-academy", "object.brass-button", "item.brass-button", "npc.tutor-lin",
                   "object.bell-inscription", "event.button-given", "event.inscription-matched"}}) {
        const auto package = world(name);
        auto state = storyforge::make_initial_state(package);
        const auto initial = state;
        auto no_item = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::give, npc, {}, item}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(no_item));
        CHECK(state == initial);

        auto given_result = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::take, object, {}},
             {storyforge::AgentActionKind::give, npc, {}, item}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(given_result));
        CHECK(state.inventory.find(storyforge::ItemId{item}) == state.inventory.end());
        CHECK(state.completed_events.contains(storyforge::EventId{given}));
        CHECK(state.npc_relationships.at(storyforge::NpcId{npc}) == 1);
        auto encoded = storyforge::serialize_story_save(package, state);
        REQUIRE(std::holds_alternative<std::string>(encoded));
        auto restored = storyforge::deserialize_story_save(package, std::get<std::string>(encoded));
        REQUIRE(std::holds_alternative<storyforge::StoryState>(restored));
        CHECK(std::get<storyforge::StoryState>(restored) == state);

        state = storyforge::make_initial_state(package);
        auto used_result = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::take, object, {}},
             {storyforge::AgentActionKind::use, target, {}, item}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(used_result));
        CHECK(state.completed_events.contains(storyforge::EventId{used}));
        const auto expected_count = std::string(name) == "harbor-lantern" ? 1 : 0;
        CHECK(state.inventory.count(storyforge::ItemId{item}) == expected_count);
        const auto after_use = state;
        auto repeated = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::use, target, {}, item}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(repeated));
        CHECK(state == after_use);
    }
}

TEST_CASE("invalid item targets and late failures leave the whole turn unchanged") {
    const auto package = world("harbor-lantern");
    auto state = storyforge::make_initial_state(package);
    const auto initial = state;
    auto denied = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::take, "object.shell-token", {}},
         {storyforge::AgentActionKind::give, "npc.keeper", {}, "item.shell-token"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(denied));
    CHECK(state == initial);
    denied = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::give, "npc.alan", {}, "item.invented"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(denied));
    CHECK(state == initial);
    denied = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::use, "object.tide-chart", {}, "item.shell-token"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(denied));
    CHECK(state == initial);
}

TEST_CASE("agent parses a natural language item transfer into checked requests") {
    const auto package = world("harbor-lantern");
    auto state = storyforge::make_initial_state(package);
    storyforge::agent::StoryAgent agent;
    ScriptedProvider provider;
    provider.replies = {
        storyforge::agent::ChatResponse{R"({"actions":[{"type":"take","target":"object.shell-token"},{"type":"give","target":"npc.alan","item":"item.shell-token"}]})", {}, {}},
        storyforge::agent::ChatResponse{"你把信物交给阿澜。", {}, {}}};
    auto result = agent.take_turn(package, state, "捡起贝壳给阿澜", provider);
    REQUIRE(std::holds_alternative<storyforge::agent::AgentTurn>(result));
    CHECK(state.completed_events.contains(storyforge::EventId{"event.shell-given"}));
    CHECK(state.inventory.empty());
}

TEST_CASE("item rule validation rejects duplicate and unknown targets") {
    auto package = world("harbor-lantern");
    package.agent_item_rules.push_back(package.agent_item_rules.front());
    package.agent_item_rules.back().target = "npc.missing";
    package.agent_item_rules.push_back(package.agent_item_rules.front());
    CHECK_FALSE(storyforge::validate_story_package(package).valid());
}
