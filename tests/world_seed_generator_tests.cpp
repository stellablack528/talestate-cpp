#include "storyforge/agent/world_seed_generator.hpp"
#include "storyforge/io/story_loader.hpp"
#include "storyforge/io/story_save.hpp"
#include "storyforge/runtime/story_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

namespace {

class FixedProvider final : public storyforge::agent::ILlmProvider {
public:
    std::string response;
    storyforge::agent::ChatResult chat(const storyforge::agent::ChatRequest&) override {
        return storyforge::agent::ChatResponse{response, {}, {}};
    }
};

storyforge::agent::WorldMapDraft map_for(const std::string& title) {
    nlohmann::json json = { {"title", title}, {"start", "gate"},
        {"locations", nlohmann::json::array({
            {{"id", "gate"}, {"title", "大门"}, {"description", "有人在此守望"}, {"x", 0}, {"y", 0}},
            {{"id", "hall"}, {"title", "大厅"}, {"description", "回声在此交织"}, {"x", 1}, {"y", 0}},
            {{"id", "tower"}, {"title", "塔楼"}, {"description", "能看见远方"}, {"x", 2}, {"y", 1}}})},
        {"routes", nlohmann::json::array({{{"from", "gate"}, {"to", "hall"}},
                                            {{"from", "hall"}, {"to", "tower"}}})}};
    return std::get<storyforge::agent::WorldMapDraft>(
        storyforge::agent::parse_world_map_draft(json.dump()));
}

} // namespace

TEST_CASE("two world briefs generate loadable three-scene exploration seeds") {
    for (const auto& title : {std::string{"魔法学园"}, std::string{"星际哨站"}}) {
        FixedProvider provider;
        provider.response = R"({"goal":"查明塔楼异响","opening":"你抵达大门，听到塔楼传来异响。","clues":[{"id":"note","name":"纸条","scene":"gate","description":"纸上记着三短一长的灯号。"},{"id":"ticket","name":"车票","scene":"hall","description":"票面印着塔楼的旧标记。"},{"id":"key","name":"钥匙","scene":"tower","description":"钥匙齿痕与塔楼的锁吻合。"}],"npcs":[{"id":"gate_keeper","name":"守门人","scene":"gate","topic":"询问异响","reply":"昨夜塔楼亮过灯。","initial_relationship":-1,"clue_id":"ticket","evidence_topic":"拿车票追问","evidence_reply":"那是塔楼旧车次的票。"},{"id":"hall_scholar","name":"研究员","scene":"hall","topic":"询问线索","reply":"档案记载了塔楼的旧事。","initial_relationship":2,"clue_id":"note","evidence_topic":"拿纸条追问","evidence_reply":"这串灯号是求救讯号。"}]})";
        auto generated = storyforge::agent::generate_world_seed_from_map(map_for(title), title + "发生了谜案", provider);
        REQUIRE(std::holds_alternative<storyforge::agent::WorldSeedDraft>(generated));
        const auto& json_text = std::get<storyforge::agent::WorldSeedDraft>(generated).story_json;
        auto loaded = storyforge::load_story_package_text(json_text);
        INFO(title);
        REQUIRE(loaded.report.valid());
        REQUIRE(loaded.package.has_value());
        const auto& package = *loaded.package;
        auto altered_response = provider.response;
        const auto clue_text = altered_response.find("纸上记着三短一长的灯号。");
        REQUIRE(clue_text != std::string::npos);
        altered_response.replace(clue_text, std::string{"纸上记着三短一长的灯号。"}.size(),
                                 "纸上记着两短一长的灯号。");
        provider.response = altered_response;
        auto changed = storyforge::agent::generate_world_seed_from_map(
            map_for(title), title + "发生了谜案", provider);
        REQUIRE(std::holds_alternative<storyforge::agent::WorldSeedDraft>(changed));
        auto changed_package = storyforge::load_story_package_text(
            std::get<storyforge::agent::WorldSeedDraft>(changed).story_json);
        REQUIRE(changed_package.report.valid());
        REQUIRE(changed_package.package.has_value());
        CHECK(changed_package.package->id.value() != package.id.value());
        REQUIRE(package.chapters.front().scenes.size() == 3);
        CHECK(package.agent_npcs.size() == 2);
        CHECK(package.agent_objects.size() == 6);
        CHECK(package.agent_objects[0].item == storyforge::ItemId{"item.clue.note"});
        CHECK(package.agent_objects[1].item == storyforge::ItemId{"item.clue.ticket"});
        CHECK(package.agent_objects[2].item == storyforge::ItemId{"item.clue.key"});
        auto state = storyforge::make_initial_state(package);
        CHECK(state.npc_relationships.at(storyforge::NpcId{"npc.gate_keeper"}) == -1);
        const auto before_evidence = state;
        auto early = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, "npc.gate_keeper", "topic.evidence.ticket"}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(early));
        CHECK(std::get<storyforge::RuntimeError>(early).message.find("车票") != std::string::npos);
        CHECK(state == before_evidence);
        auto conversation = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, "npc.gate_keeper", "topic.introduction"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(conversation));
        CHECK(state.npc_relationships.at(storyforge::NpcId{"npc.gate_keeper"}) == 0);
        auto moved = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::move, "scene.hall", {}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(moved));
        CHECK(state.position.scene == storyforge::SceneId{"scene.hall"});
        auto taken = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::take, "object.clue.ticket", {}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(taken));
        CHECK(state.inventory.at(storyforge::ItemId{"item.clue.ticket"}) == 1);
        CHECK(state.collected_objects.contains("object.clue.ticket"));
        auto saved = storyforge::serialize_story_save(package, state);
        REQUIRE(std::holds_alternative<std::string>(saved));
        auto loaded_save = storyforge::deserialize_story_save(package, std::get<std::string>(saved));
        REQUIRE(std::holds_alternative<storyforge::StoryState>(loaded_save));
        CHECK(std::get<storyforge::StoryState>(loaded_save) == state);
        state = std::get<storyforge::StoryState>(std::move(loaded_save));
        auto back = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::move, "scene.gate", {}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(back));
        auto shown = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, "npc.gate_keeper", "topic.evidence.ticket"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(shown));
        CHECK(state.inventory.at(storyforge::ItemId{"item.clue.ticket"}) == 1);
        CHECK(state.npc_relationships.at(storyforge::NpcId{"npc.gate_keeper"}) == 1);
        CHECK(state.npc_memory.at(storyforge::NpcId{"npc.gate_keeper"}).contains("reviewed-clue-ticket"));
        const auto after_shown = state;
        auto repeated = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, "npc.gate_keeper", "topic.evidence.ticket"}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(repeated));
        CHECK(state == after_shown);
        auto other_clue = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::take, "object.clue.note", {}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(other_clue));
        CHECK(state.inventory.at(storyforge::ItemId{"item.clue.note"}) == 1);
    }
}

TEST_CASE("world seed rejects NPCs outside its selected locations") {
    FixedProvider provider;
    provider.response = R"({"goal":"找线索","opening":"你到了。","clues":[{"id":"a","name":"证据甲","scene":"gate","description":"描述"},{"id":"b","name":"证据乙","scene":"hall","description":"描述"},{"id":"c","name":"证据丙","scene":"tower","description":"描述"}],"npcs":[{"id":"a","name":"甲","scene":"unknown","topic":"询问","reply":"不知","initial_relationship":0,"clue_id":"a","evidence_topic":"出示","evidence_reply":"认得"},{"id":"b","name":"乙","scene":"hall","topic":"询问","reply":"不知","initial_relationship":0,"clue_id":"missing","evidence_topic":"出示","evidence_reply":"认得"}]})";
    auto result = storyforge::agent::generate_world_seed_from_map(map_for("城镇"), "城镇谜案", provider);
    CHECK(std::holds_alternative<storyforge::agent::WorldSeedError>(result));
}
