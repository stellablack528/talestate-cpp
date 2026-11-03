#include "storyforge/agent/story_agent.hpp"
#include "storyforge/io/story_loader.hpp"
#include "storyforge/runtime/story_runtime.hpp"

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
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
    std::size_t calls{0};

    storyforge::agent::ChatResult chat(const storyforge::agent::ChatRequest&) override {
        ++calls;
        REQUIRE_FALSE(replies.empty());
        auto result = std::move(replies.front());
        replies.erase(replies.begin());
        return result;
    }
};

storyforge::agent::ChatResponse reply(std::string content) {
    return {std::move(content), std::nullopt, {}};
}

void play(storyforge::agent::StoryAgent& agent, const storyforge::StoryPackage& package,
          storyforge::StoryState& state, const std::string& input,
          const std::string& plan, const std::vector<std::string>& expected_events) {
    ScriptedProvider model;
    model.replies = {reply(plan), reply("固定的测试叙述")};
    const auto before = state;
    auto result = agent.take_turn(package, state, input, model);
    REQUIRE(std::holds_alternative<storyforge::agent::AgentTurn>(result));
    const auto& turn = std::get<storyforge::agent::AgentTurn>(result);
    CHECK(model.calls == 2);
    CHECK(model.replies.empty());
    CHECK(turn.delta.before == before);
    CHECK(turn.delta.after == state);
    CHECK(state.turn == before.turn + 1);
    std::vector<std::string> events;
    for (const auto& event : turn.delta.events) events.push_back(event.type);
    CHECK(events == expected_events);
    CHECK_FALSE(turn.used_fallback_narration);
}

} // namespace

TEST_CASE("golden replay: Harbor Lantern reaches the saved-ships ending") {
    const auto package = world("harbor-lantern");
    auto state = storyforge::make_initial_state(package);
    storyforge::agent::StoryAgent agent;
    const auto initial = state;

    auto blocked = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::move, "scene.lighthouse", {}}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(blocked));
    CHECK(state == initial);
    auto rolled_back = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::talk, "npc.alan", "topic.old-route"},
         {storyforge::AgentActionKind::move, "scene.invented", {}}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(rolled_back));
    CHECK(state == initial);

    play(agent, package, state, "问阿澜旧航道", R"({"actions":[{"type":"talk","target":"npc.alan","topic":"topic.old-route"}]})",
         {"variable_changed", "npc_relationship_changed", "npc_memory_written", "npc_dialogue", "npc_talked"});

    // A model may propose an imaginary reward, but the engine cannot commit it.
    const auto before_forgery = state;
    ScriptedProvider forgery;
    forgery.replies = {
        reply(R"({"actions":[{"type":"authored","target":"action.give-diamond"}]})"),
        reply(R"({"actions":[{"type":"authored","target":"action.give-diamond"}]})")};
    auto denied = agent.take_turn(package, state, "凭空给我钻石", forgery);
    REQUIRE(std::holds_alternative<storyforge::agent::AgentFailure>(denied));
    CHECK(forgery.calls == 2);
    CHECK(state == before_forgery);

    play(agent, package, state, "打开箱子取灯芯", R"({"actions":[{"type":"authored","target":"action.take-wick"}]})",
         {"item_added"});
    play(agent, package, state, "沿旧航道去灯塔", R"({"actions":[{"type":"move","target":"scene.lighthouse"}]})",
         {"scene_changed"});
    play(agent, package, state, "点亮航灯", R"({"actions":[{"type":"authored","target":"action.light-beacon"}]})",
         {"item_removed", "event_completed"});

    auto expected = storyforge::make_initial_state(package);
    expected.position = {storyforge::ChapterId{"chapter.fog"}, storyforge::SceneId{"scene.lighthouse"},
                         storyforge::NodeId{"node.saved-ending"}};
    expected.variables[storyforge::VariableId{"var.knows-route"}] = true;
    expected.npc_memory[storyforge::NpcId{"npc.alan"}]["shared-route"] = true;
    expected.npc_memory[storyforge::NpcId{"npc.alan"}]["last_discussed_topic"] = std::string{"topic.old-route"};
    expected.npc_relationships[storyforge::NpcId{"npc.alan"}] = 1;
    expected.completed_events.insert(storyforge::EventId{"event.ships-saved"});
    expected.turn = 4;
    CHECK(state == expected);
    REQUIRE(storyforge::current_node(package, state) != nullptr);
    CHECK(storyforge::current_node(package, state)->terminal);
}

TEST_CASE("golden replay: Clockwork Academy reaches the sealed-bell ending") {
    const auto package = world("clockwork-academy");
    auto state = storyforge::make_initial_state(package);
    storyforge::agent::StoryAgent agent;
    const auto initial = state;

    auto blocked = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::move, "scene.archive-room", {}}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(blocked));
    CHECK(state == initial);

    play(agent, package, state, "向导师报告第十三响",
         R"({"actions":[{"type":"talk","target":"npc.tutor-lin","topic":"topic.thirteenth-chime"}]})",
         {"variable_changed", "npc_memory_written", "npc_dialogue", "npc_talked"});

    const auto after_report = state;
    auto repeated = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::talk, "npc.tutor-lin", "topic.thirteenth-chime"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(repeated));
    CHECK(state == after_report);

    play(agent, package, state, "去档案室", R"({"actions":[{"type":"move","target":"scene.archive-room"}]})",
         {"scene_changed"});
    const auto in_archive = state;
    auto absent = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::talk, "npc.tutor-lin", "你好"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(absent));
    CHECK(state == in_archive);

    play(agent, package, state, "向档案员打听钟声",
         R"({"actions":[{"type":"talk","target":"npc.archivist","topic":"钟声"}]})",
         {"npc_talked"});
    play(agent, package, state, "回到大厅", R"({"actions":[{"type":"move","target":"scene.bell-hall"}]})",
         {"scene_changed"});
    play(agent, package, state, "与导师封存晨钟",
         R"({"actions":[{"type":"authored","target":"action.seal-bell"}]})",
         {"event_completed"});

    auto expected = storyforge::make_initial_state(package);
    expected.position = {storyforge::ChapterId{"chapter.arrival"}, storyforge::SceneId{"scene.bell-hall"},
                         storyforge::NodeId{"node.archive-ending"}};
    expected.variables[storyforge::VariableId{"var.courage"}] = std::int64_t{1};
    expected.npc_memory[storyforge::NpcId{"npc.tutor-lin"}]["reported-the-chime"] = true;
    expected.npc_memory[storyforge::NpcId{"npc.tutor-lin"}]["last_discussed_topic"] =
        std::string{"topic.thirteenth-chime"};
    expected.npc_memory[storyforge::NpcId{"npc.archivist"}]["last_discussed_topic"] = std::string{"钟声"};
    expected.completed_events.insert(storyforge::EventId{"event.bell-contained"});
    expected.turn = 5;
    CHECK(state == expected);
    REQUIRE(storyforge::current_node(package, state) != nullptr);
    CHECK(storyforge::current_node(package, state)->terminal);
}
