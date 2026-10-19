#include "storyforge/agent/story_agent.hpp"
#include "storyforge/io/story_loader.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

storyforge::StoryPackage example(const std::string& name) {
    const auto path = std::filesystem::path{STORYFORGE_SOURCE_DIR} / "examples" / name;
    auto loaded = storyforge::load_story_package(path);
    REQUIRE(loaded.report.valid());
    REQUIRE(loaded.package.has_value());
    return std::move(*loaded.package);
}

class FakeProvider final : public storyforge::agent::ILlmProvider {
public:
    std::vector<storyforge::agent::ChatResult> responses;
    std::vector<storyforge::agent::ChatRequest> requests;

    storyforge::agent::ChatResult chat(const storyforge::agent::ChatRequest& request) override {
        requests.push_back(request);
        REQUIRE_FALSE(responses.empty());
        auto result = std::move(responses.front());
        responses.erase(responses.begin());
        return result;
    }
};

storyforge::agent::ChatResponse reply(std::string content) {
    return {std::move(content), std::nullopt, {}};
}

} // namespace

TEST_CASE("author-approved agent actions compose atomically across different worlds") {
    auto harbor = example("harbor-lantern");
    auto state = storyforge::make_initial_state(harbor);
    const auto before = state;
    auto invalid = storyforge::execute_agent_actions(harbor, state,
        {storyforge::ChoiceId{"action.take-wick"}, storyforge::ChoiceId{"action.light-beacon"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(invalid));
    CHECK(state == before);

    auto valid = storyforge::execute_agent_actions(harbor, state,
        {storyforge::ChoiceId{"action.ask-ferryman"}, storyforge::ChoiceId{"action.take-wick"}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(valid));
    CHECK(state.turn == 1);
    auto ending = storyforge::execute_agent_requests(harbor, state,
        {{storyforge::AgentActionKind::move, "scene.lighthouse", {}},
         {storyforge::AgentActionKind::authored, "action.light-beacon", {}}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(ending));
    CHECK(state.turn == 2);
    CHECK(state.inventory.empty());
    CHECK(state.npc_relationships.at(storyforge::NpcId{"npc.alan"}) == 1);
    CHECK(state.completed_events.contains(storyforge::EventId{"event.ships-saved"}));
    CHECK(storyforge::current_node(harbor, state)->terminal);

    auto academy = example("clockwork-academy");
    auto school_state = storyforge::make_initial_state(academy);
    auto forged = storyforge::execute_agent_actions(academy, school_state,
        {storyforge::ChoiceId{"action.give-me-gold"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(forged));
    CHECK(school_state.gold == 0);
    CHECK(school_state.turn == 0);
}

TEST_CASE("the same parameterized move and talk protocol works in both worlds") {
    struct Scenario {
        std::string world, local_npc, remote_npc, prerequisite, destination;
    };
    for (const auto& scenario : std::vector<Scenario>{
        {"clockwork-academy", "npc.tutor-lin", "npc.archivist", "action.report-tutor", "scene.archive-room"},
        {"harbor-lantern", "npc.alan", "npc.keeper", "action.ask-ferryman", "scene.lighthouse"}}) {
        auto package = example(scenario.world);
        auto state = storyforge::make_initial_state(package);
        const auto initial = state;
        auto blocked = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, scenario.local_npc, "昨夜的异象"},
             {storyforge::AgentActionKind::move, scenario.destination, {}}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(blocked));
        CHECK(state == initial); // Earlier talk also rolls back.

        auto absent = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, scenario.remote_npc, "你好"}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(absent));
        CHECK(state == initial);
        auto invented = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, "npc.invented", "你好"}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(invented));
        CHECK(state == initial);
        auto oversized = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, scenario.local_npc, std::string(257, 'x')}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(oversized));
        CHECK(state == initial);

        auto granted = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, scenario.local_npc, "昨夜的异象"},
             {storyforge::AgentActionKind::authored, scenario.prerequisite, {}},
             {storyforge::AgentActionKind::move, scenario.destination, {}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(granted));
        CHECK(state.position.scene.value() == scenario.destination);
        CHECK(state.turn == 1);
        CHECK(state.npc_memory.at(storyforge::NpcId{scenario.local_npc}).at("last_discussed_topic") ==
              storyforge::StoryValue{std::string{"昨夜的异象"}});

        const auto before_forgery = state;
        auto forged = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::move, "scene.nonexistent", {}}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(forged));
        CHECK(state == before_forgery);

        auto remote_talk = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, scenario.remote_npc, "还有别的线索吗"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(remote_talk));
        CHECK(state.npc_memory.at(storyforge::NpcId{scenario.remote_npc}).contains("last_discussed_topic"));
    }
}

TEST_CASE("agent accepts typed requests and repairs impossible moves") {
    auto package = example("harbor-lantern");
    auto state = storyforge::make_initial_state(package);
    FakeProvider provider;
    provider.responses = {
        reply(R"({"actions":[{"type":"move","target":"scene.lighthouse"}]})"),
        reply(R"({"actions":[{"type":"talk","target":"npc.alan","topic":"旧航道"},{"type":"authored","target":"action.ask-ferryman"},{"type":"move","target":"scene.lighthouse"}]})"),
        reply("你问了阿澜，得知旧航道，随后抵达灯塔。")};
    storyforge::agent::StoryAgent agent;
    auto result = agent.take_turn(package, state, "问阿澜怎么去灯塔并过去", provider);
    REQUIRE(std::holds_alternative<storyforge::agent::AgentTurn>(result));
    CHECK(provider.requests.size() == 3);
    CHECK(provider.requests[1].messages.back().content.find("rejected") != std::string::npos);
    CHECK(state.position.scene == storyforge::SceneId{"scene.lighthouse"});
    CHECK(state.turn == 1);
    CHECK(std::get<storyforge::agent::AgentTurn>(result).actions.size() == 3);
}

TEST_CASE("agent repairs rejected plans and narrates only committed facts") {
    auto package = example("clockwork-academy");
    auto state = storyforge::make_initial_state(package);
    FakeProvider provider;
    provider.responses = {reply(R"({"actions":["action.repair-bell"]})"),
                          reply(R"({"actions":["action.inspect-gear","action.repair-bell"]})"),
                          reply("你观察齿轮后修好了晨钟。")};
    storyforge::agent::StoryAgent agent;
    auto result = agent.take_turn(package, state, "观察齿轮并修好钟", provider);
    REQUIRE(std::holds_alternative<storyforge::agent::AgentTurn>(result));
    const auto& turn = std::get<storyforge::agent::AgentTurn>(result);
    CHECK(provider.requests.size() == 3);
    CHECK(provider.requests[1].messages.back().content.find("rejected") != std::string::npos);
    CHECK(provider.requests[2].messages.back().content.find("event.bell-repaired") != std::string::npos);
    CHECK(state.turn == 1);
    CHECK(state.inventory.empty());
    CHECK(state.completed_events.contains(storyforge::EventId{"event.bell-repaired"}));
    CHECK_FALSE(turn.used_fallback_narration);
}

TEST_CASE("invalid model output never commits and narration failure uses factual fallback") {
    auto package = example("harbor-lantern");
    auto state = storyforge::make_initial_state(package);
    const auto before = state;
    FakeProvider bad;
    bad.responses = {reply("not json"), reply(R"({"actions":["action.unknown"]})")};
    storyforge::agent::StoryAgent agent;
    auto failure = agent.take_turn(package, state, "我突然拥有一万金币", bad);
    REQUIRE(std::holds_alternative<storyforge::agent::AgentFailure>(failure));
    CHECK(state == before);

    FakeProvider provider;
    provider.responses = {reply(R"({"actions":["action.take-wick"]})"),
        storyforge::agent::ProviderError{storyforge::agent::ProviderErrorCode::transport_failure,
                                        "temporary failure", std::nullopt, true}};
    auto result = agent.take_turn(package, state, "打开箱子取出灯芯", provider);
    REQUIRE(std::holds_alternative<storyforge::agent::AgentTurn>(result));
    const auto& turn = std::get<storyforge::agent::AgentTurn>(result);
    CHECK(turn.used_fallback_narration);
    CHECK(turn.narration.find("item.wick") != std::string::npos);
    CHECK(state.inventory.at(storyforge::ItemId{"item.wick"}) == 1);
}
