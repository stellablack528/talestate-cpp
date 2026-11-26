#include "storyforge/io/story_loader.hpp"
#include "storyforge/io/story_save.hpp"
#include "storyforge/runtime/story_runtime.hpp"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <filesystem>
#include <string>
#include <variant>

namespace {
using namespace storyforge;

AgentActionRequest action(AgentActionKind kind, std::string target,
                          std::string topic = {}, std::string id = {},
                          std::uint64_t due = 0) {
    AgentActionRequest result;
    result.kind = kind;
    result.target = std::move(target);
    result.topic = std::move(topic);
    result.id = std::move(id);
    result.due_minute = due;
    return result;
}

void replay(const std::string& name, bool clues_first) {
    const auto loaded = load_story_package(
        std::filesystem::u8path(STORYFORGE_SOURCE_DIR) / "examples" / name);
    REQUIRE(loaded.package.has_value());
    REQUIRE(loaded.report.valid());
    const auto& world = *loaded.package;
    auto state = make_initial_state(world);
    const auto npc = std::find_if(world.agent_npcs.begin(), world.agent_npcs.end(),
        [&](const AgentNpc& value) { return value.scene == state.position.scene; });
    const auto object = std::find_if(world.agent_objects.begin(), world.agent_objects.end(),
        [&](const AgentObject& value) { return value.scene == state.position.scene; });
    REQUIRE(npc != world.agent_npcs.end());
    REQUIRE(object != world.agent_objects.end());
    const auto fact = "fact.replay.promise";
    const auto lead = "fact.replay.lead";
    auto commit = [&](AgentActionRequest request) {
        auto result = execute_agent_requests(world, state, {request});
        REQUIRE(std::holds_alternative<StoryDelta>(result));
    };
    auto promise = [&] {
        commit(action(AgentActionKind::promise, npc->id.value(),
            "明天核对公开记录并说明结果", fact, 1440));
    };
    auto investigate = [&] {
        commit(action(AgentActionKind::inspect, object->id));
        commit(action(AgentActionKind::investigate, object->id,
            "公开资料上有待核对的细节", lead));
    };
    if (clues_first) { investigate(); promise(); }
    else { promise(); investigate(); }
    REQUIRE(state.dynamic_facts.size() == 2);

    const auto saved = serialize_story_save(world, state);
    REQUIRE(std::holds_alternative<std::string>(saved));
    auto restored = deserialize_story_save(world, std::get<std::string>(saved));
    REQUIRE(std::holds_alternative<StoryState>(restored));
    REQUIRE(std::get<StoryState>(restored) == state);
    state = std::get<StoryState>(std::move(restored));

    const auto before = state;
    auto duplicate = execute_agent_requests(world, state,
        {action(AgentActionKind::promise, npc->id.value(),
            "明天核对公开记录并说明结果", "fact.replay.duplicate", 1440)});
    REQUIRE(std::holds_alternative<RuntimeError>(duplicate));
    CHECK(state == before);
    auto unauthorized = execute_agent_requests(world, state,
        {action(AgentActionKind::authored, "action.invented-reward")});
    REQUIRE(std::holds_alternative<RuntimeError>(unauthorized));
    CHECK(state == before);

    commit(action(AgentActionKind::wait, "30"));
    CHECK(state.world_minute == 30);
    commit(action(clues_first ? AgentActionKind::fulfill : AgentActionKind::break_promise,
                  fact));
    CHECK(state.dynamic_facts.at(fact).status ==
          (clues_first ? "fulfilled" : "broken"));
    const auto after = state;
    auto repeated = execute_agent_requests(world, state,
        {action(AgentActionKind::break_promise, fact)});
    REQUIRE(std::holds_alternative<RuntimeError>(repeated));
    CHECK(state == after);
}
} // namespace

TEST_CASE("two worlds replay promises and leads in both orders without duplicate effects") {
    for (const auto& name : {"harbor-lantern", "clockwork-academy"}) {
        replay(name, false);
        replay(name, true);
    }
}
