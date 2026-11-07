#include "storyforge/agent/world_seed_generator.hpp"
#include "storyforge/agent/world_spec.hpp"
#include "storyforge/io/story_loader.hpp"
#include "storyforge/io/story_save.hpp"
#include "storyforge/runtime/story_runtime.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>
#include <vector>

namespace {

class FixedProvider final : public storyforge::agent::ILlmProvider {
public:
    std::string response;
    std::vector<std::string> queued;
    int calls{0};
    storyforge::agent::ChatResult chat(const storyforge::agent::ChatRequest& request) override {
        ++calls;
        if (!request.messages.empty() &&
            request.messages.front().content.find("WORLD_SPEC_V0") != std::string::npos) {
            const auto details = nlohmann::json::parse(response, nullptr, false);
            if (details.is_object() && details.contains("world_spec"))
                return storyforge::agent::ChatResponse{details["world_spec"].dump(), {}, {}};
            return storyforge::agent::ChatResponse{"{}", {}, {}};
        }
        const auto details = nlohmann::json::parse(response, nullptr, false);
        if (!request.messages.empty() &&
            request.messages.front().content.find("WORLD_CONTENT_V0") != std::string::npos) {
            if (!details.is_object()) return storyforge::agent::ChatResponse{"{}", {}, {}};
            auto content = details;
            content.erase("world_spec");
            content.erase("npcs");
            return storyforge::agent::ChatResponse{content.dump(), {}, {}};
        }
        if (!request.messages.empty() &&
            request.messages.front().content.find("WORLD_NPCS_V0") != std::string::npos) {
            if (!queued.empty()) {
                auto next = nlohmann::json::parse(queued.front(), nullptr, false);
                queued.erase(queued.begin());
                if (next.is_object() && next.contains("npcs"))
                    return storyforge::agent::ChatResponse{
                        nlohmann::json{{"npcs", next["npcs"]}}.dump(), {}, {}};
                return storyforge::agent::ChatResponse{"{}", {}, {}};
            }
            if (details.is_object() && details.contains("npcs"))
                return storyforge::agent::ChatResponse{
                    nlohmann::json{{"npcs", details["npcs"]}}.dump(), {}, {}};
            return storyforge::agent::ChatResponse{"{}", {}, {}};
        }
        if (!queued.empty()) {
            auto result = std::move(queued.front());
            queued.erase(queued.begin());
            return storyforge::agent::ChatResponse{std::move(result), {}, {}};
        }
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
        auto details = nlohmann::json::parse(provider.response);
        details["interaction"] = {{"id", "panel"}, {"scene", "gate"},
            {"target_name", "封锁的控制台"}, {"target_description", "控制台有一处钥匙孔。"},
            {"clue_id", "key"}, {"result", "钥匙启动控制台，旧记录亮起。"}};
        for (auto& npc : details["npcs"]) {
            npc["second_clue_id"] = npc["clue_id"] == "note" ? "ticket" : "note";
            npc["combined_topic"] = "比较两件证据";
            npc["combined_reply"] = "两份记录指向同一夜晚。";
            npc["event_topic"] = "询问控制台记录";
            npc["event_reply"] = "控制台记录证实了你的调查。";
        }
        if (title == "魔法学园") {
            details["world_spec"] = {
                {"attributes", {{{"id", "focus"}, {"name", "专注"}, {"initial", 3}, {"min", 0}, {"max", 6}},
                                {{"id", "energy"}, {"name", "精力"}, {"initial", 4}, {"min", 0}, {"max", 6}}}},
                {"identities", {{{"id", "scholar"}, {"name", "学者"}, {"description", "善于研读"}, {"modifiers", {{"focus", 1}}}},
                                {{"id", "wanderer"}, {"name", "旅人"}, {"description", "精力充沛"}, {"modifiers", {{"energy", 1}}}}}},
                {"actions", {{{"id", "study"}, {"name", "研读"}, {"description", "消耗精力提升专注"},
                              {"scene", "hall"}, {"requirements", nlohmann::json::array()},
                              {"costs", {{"energy", 2}}}, {"gains", {{"focus", 1}}}, {"once", false}}}}};
        } else {
            details["world_spec"] = {
                {"attributes", {{{"id", "signal"}, {"name", "信号知识"}, {"initial", 1}, {"min", 0}, {"max", 5}},
                                {{"id", "battery"}, {"name", "电量"}, {"initial", 3}, {"min", 0}, {"max", 5}}}},
                {"identities", {{{"id", "engineer"}, {"name", "工程师"}, {"description", "擅长维修"}, {"modifiers", {{"signal", 1}}}},
                                {{"id", "pilot"}, {"name", "驾驶员"}, {"description", "善于驾驶"}, {"modifiers", {{"battery", 1}}}}}},
                {"actions", {{{"id", "scan"}, {"name", "扫描"}, {"description", "消耗电量解析信号"},
                              {"scene", "hall"}, {"requirements", nlohmann::json::array()},
                              {"costs", {{"battery", 2}}}, {"gains", {{"signal", 1}}}, {"once", false}}}}};
        }
        if (title == "星际哨站") {
            details["goal"] = "调查失联信号";
            details["opening"] = "你抵达星际哨站大门，收到失联信号。";
            details["interaction"]["target_name"] = "维修终端";
            details["npcs"][0]["name"] = "哨站守卫";
            details["npcs"][1]["name"] = "信号研究员";
            for (auto& npc : details["npcs"]) {
                npc["relationship"] = npc["initial_relationship"];
                npc.erase("initial_relationship");
            }
        }
        provider.response = details.dump();
        auto generated = storyforge::agent::generate_world_seed_from_map(map_for(title), title + "发生了谜案", provider);
        REQUIRE(std::holds_alternative<storyforge::agent::WorldSeedDraft>(generated));
        const auto& json_text = std::get<storyforge::agent::WorldSeedDraft>(generated).story_json;
        auto loaded = storyforge::load_story_package_text(json_text);
        INFO(title);
        REQUIRE(loaded.report.valid());
        REQUIRE(loaded.package.has_value());
        const auto& package = *loaded.package;
        const auto sidecar = nlohmann::json::parse(
            std::get<storyforge::agent::WorldSeedDraft>(generated).world_spec_json);
        CHECK(sidecar["world_spec"] == details["world_spec"]);
        CHECK(sidecar["start_scene"] == "gate");
        REQUIRE(package.variables.size() == 3);
        REQUIRE(package.agent_actions.size() == 3);
        const auto identity_id = title == "魔法学园" ? "scholar" : "engineer";
        const auto resource_id = title == "魔法学园" ? "energy" : "battery";
        const auto growth_id = title == "魔法学园" ? "focus" : "signal";
        const auto action_id = title == "魔法学园" ? "study" : "scan";
        auto rules_state = storyforge::make_initial_state(package);
        const auto untouched = rules_state;
        auto before_identity = storyforge::execute_agent_requests(package, rules_state,
            {{storyforge::AgentActionKind::authored, std::string{"action.rule."} + action_id}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(before_identity));
        CHECK(rules_state == untouched);
        auto selected_identity = storyforge::execute_agent_requests(package, rules_state,
            {{storyforge::AgentActionKind::authored, std::string{"action.identity."} + identity_id}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(selected_identity));
        CHECK(std::get<std::string>(rules_state.variables.at(storyforge::VariableId{"var.identity"})) == identity_id);
        const auto selected_state = rules_state;
        auto repeated_identity = storyforge::execute_agent_requests(package, rules_state,
            {{storyforge::AgentActionKind::authored, "action.identity." + std::string{identity_id}}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(repeated_identity));
        CHECK(rules_state == selected_state);
        auto wrong_place = storyforge::execute_agent_requests(package, rules_state,
            {{storyforge::AgentActionKind::authored, "action.rule." + std::string{action_id}}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(wrong_place));
        CHECK(rules_state == selected_state);
        auto moved_for_growth = storyforge::execute_agent_requests(package, rules_state,
            {{storyforge::AgentActionKind::move, "scene.hall"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(moved_for_growth));
        const auto old_resource = std::get<std::int64_t>(rules_state.variables.at(
            storyforge::VariableId{"var.attribute." + std::string{resource_id}}));
        const auto old_growth = std::get<std::int64_t>(rules_state.variables.at(
            storyforge::VariableId{"var.attribute." + std::string{growth_id}}));
        auto grew = storyforge::execute_agent_requests(package, rules_state,
            {{storyforge::AgentActionKind::authored, "action.rule." + std::string{action_id}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(grew));
        CHECK(std::get<std::int64_t>(rules_state.variables.at(
            storyforge::VariableId{"var.attribute." + std::string{resource_id}})) == old_resource - 2);
        CHECK(std::get<std::int64_t>(rules_state.variables.at(
            storyforge::VariableId{"var.attribute." + std::string{growth_id}})) == old_growth + 1);
        bool eventually_blocked = false;
        for (int attempt = 0; attempt < 7; ++attempt) {
            const auto before_attempt = rules_state;
            auto next = storyforge::execute_agent_requests(package, rules_state,
                {{storyforge::AgentActionKind::authored, "action.rule." + std::string{action_id}}});
            if (std::holds_alternative<storyforge::RuntimeError>(next)) {
                CHECK(rules_state == before_attempt);
                eventually_blocked = true;
                break;
            }
        }
        CHECK(eventually_blocked);
        auto rules_save = storyforge::serialize_story_save(package, rules_state);
        REQUIRE(std::holds_alternative<std::string>(rules_save));
        auto restored_rules = storyforge::deserialize_story_save(package, std::get<std::string>(rules_save));
        REQUIRE(std::holds_alternative<storyforge::StoryState>(restored_rules));
        CHECK(std::get<storyforge::StoryState>(restored_rules) == rules_state);
        rules_state.variables[storyforge::VariableId{"var.attribute." + std::string{growth_id}}] =
            title == "魔法学园" ? std::int64_t{6} : std::int64_t{5};
        rules_state.variables[storyforge::VariableId{"var.attribute." + std::string{resource_id}}] =
            std::int64_t{4};
        const auto at_cap = rules_state;
        auto over_cap = storyforge::execute_agent_requests(package, rules_state,
            {{storyforge::AgentActionKind::authored, "action.rule." + std::string{action_id}}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(over_cap));
        CHECK(rules_state == at_cap);
        auto altered_response = provider.response;
        auto altered_details = nlohmann::json::parse(altered_response);
        altered_details["clues"][0]["description"] = "另一份记录。";
        altered_response = altered_details.dump();
        provider.response = altered_response;
        auto changed = storyforge::agent::generate_world_seed_from_map(
            map_for(title), title + "发生了谜案", provider);
        REQUIRE(std::holds_alternative<storyforge::agent::WorldSeedDraft>(changed));
        auto changed_package = storyforge::load_story_package_text(
            std::get<storyforge::agent::WorldSeedDraft>(changed).story_json);
        REQUIRE(changed_package.report.valid());
        REQUIRE(changed_package.package.has_value());
        CHECK(changed_package.package->id.value() != package.id.value());
        auto invalid_interaction = details;
        invalid_interaction["interaction"]["clue_id"] = "missing";
        provider.response = invalid_interaction.dump();
        auto rejected = storyforge::agent::generate_world_seed_from_map(
            map_for(title), title + "发生了谜案", provider);
        REQUIRE(std::holds_alternative<storyforge::agent::WorldSeedError>(rejected));
        auto invalid_spec = details;
        invalid_spec["world_spec"]["actions"][0]["costs"] = {{"unknown", 1}};
        provider.response = invalid_spec.dump();
        rejected = storyforge::agent::generate_world_seed_from_map(
            map_for(title), title + "发生了谜案", provider);
        REQUIRE(std::holds_alternative<storyforge::agent::WorldSeedError>(rejected));
        auto conflicting_relationship = details;
        conflicting_relationship["npcs"][0]["initial_relationship"] = 2;
        conflicting_relationship["npcs"][0]["relationship"] = -2;
        provider.response = conflicting_relationship.dump();
        rejected = storyforge::agent::generate_world_seed_from_map(
            map_for(title), title + "发生了谜案", provider);
        REQUIRE(std::holds_alternative<storyforge::agent::WorldSeedError>(rejected));
        auto invalid_combination = details;
        invalid_combination["npcs"][0]["second_clue_id"] = "ticket";
        provider.response = invalid_combination.dump();
        rejected = storyforge::agent::generate_world_seed_from_map(
            map_for(title), title + "发生了谜案", provider);
        REQUIRE(std::holds_alternative<storyforge::agent::WorldSeedError>(rejected));
        FixedProvider repairing;
        repairing.response = details.dump();
        auto invalid_npc = details;
        invalid_npc["npcs"][0]["clue_id"] = "missing";
        repairing.queued = {invalid_npc.dump(), details.dump()};
        auto repaired = storyforge::agent::generate_world_seed_from_map(
            map_for(title), title + "发生了谜案", repairing);
        REQUIRE(std::holds_alternative<storyforge::agent::WorldSeedDraft>(repaired));
        CHECK(repairing.calls == 4);
        REQUIRE(package.chapters.front().scenes.size() == 3);
        CHECK(package.agent_npcs.size() == 2);
        CHECK(package.agent_objects.size() == 7);
        REQUIRE(package.agent_item_rules.size() == 1);
        CHECK(package.agent_item_rules.front().item == storyforge::ItemId{"item.clue.key"});
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
        auto premature_use = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::use, "object.interaction.panel", {}, "item.clue.key"}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(premature_use));
        CHECK(state == before_evidence);
        auto premature_event = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, "npc.gate_keeper", "topic.event.panel"}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(premature_event));
        CHECK(state == before_evidence);
        auto conversation = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, "npc.gate_keeper", "topic.introduction"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(conversation));
        CHECK(state.npc_relationships.at(storyforge::NpcId{"npc.gate_keeper"}) == 0);
        if (title == "魔法学园") {
            auto first_clue = storyforge::execute_agent_requests(package, state,
                {{storyforge::AgentActionKind::take, "object.clue.note", {}}});
            REQUIRE(std::holds_alternative<storyforge::StoryDelta>(first_clue));
        }
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
        if (title == "星际哨站") {
            const auto only_ticket = state;
            auto premature_combination = storyforge::execute_agent_requests(package, state,
                {{storyforge::AgentActionKind::talk, "npc.gate_keeper", "topic.combined.ticket-note"}});
            REQUIRE(std::holds_alternative<storyforge::RuntimeError>(premature_combination));
            CHECK(state == only_ticket);
            auto other_clue = storyforge::execute_agent_requests(package, state,
                {{storyforge::AgentActionKind::take, "object.clue.note", {}}});
            REQUIRE(std::holds_alternative<storyforge::StoryDelta>(other_clue));
        }
        CHECK(state.inventory.at(storyforge::ItemId{"item.clue.note"}) == 1);
        auto combined = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, "npc.gate_keeper", "topic.combined.ticket-note"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(combined));
        CHECK(state.npc_memory.at(storyforge::NpcId{"npc.gate_keeper"})
            .contains("reviewed-combination-ticket-note"));
        const auto after_combined = state;
        auto repeated_combined = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, "npc.gate_keeper", "topic.combined.ticket-note"}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(repeated_combined));
        CHECK(state == after_combined);

        auto to_hall = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::move, "scene.hall", {}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(to_hall));
        auto to_tower = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::move, "scene.tower", {}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(to_tower));
        auto key = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::take, "object.clue.key", {}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(key));
        const auto at_tower = state;
        auto wrong_scene = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::use, "object.interaction.panel", {}, "item.clue.key"}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(wrong_scene));
        CHECK(state == at_tower);
        auto return_hall = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::move, "scene.hall", {}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(return_hall));
        auto return_gate = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::move, "scene.gate", {}}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(return_gate));
        auto used = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::use, "object.interaction.panel", {}, "item.clue.key"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(used));
        CHECK(state.completed_events.contains(storyforge::EventId{"event.interaction.panel"}));
        CHECK(state.inventory.at(storyforge::ItemId{"item.clue.key"}) == 1);
        const auto after_use = state;
        auto repeated_use = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::use, "object.interaction.panel", {}, "item.clue.key"}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(repeated_use));
        CHECK(state == after_use);
        auto reacted = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::talk, "npc.gate_keeper", "topic.event.panel"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(reacted));
        CHECK(state.npc_memory.at(storyforge::NpcId{"npc.gate_keeper"}).contains("reacted-panel"));
        auto saved_after_use = storyforge::serialize_story_save(package, state);
        REQUIRE(std::holds_alternative<std::string>(saved_after_use));
        auto restored_after_use = storyforge::deserialize_story_save(
            package, std::get<std::string>(saved_after_use));
        REQUIRE(std::holds_alternative<storyforge::StoryState>(restored_after_use));
        CHECK(std::get<storyforge::StoryState>(restored_after_use) == state);
    }
}

TEST_CASE("world seed rejects incomplete model proposals") {
    FixedProvider provider;
    provider.response = R"({"goal":"找线索","opening":"你到了。","clues":[{"id":"a","name":"证据甲","scene":"gate","description":"描述"},{"id":"b","name":"证据乙","scene":"hall","description":"描述"},{"id":"c","name":"证据丙","scene":"tower","description":"描述"}],"npcs":[{"id":"a","name":"甲","scene":"unknown","topic":"询问","reply":"不知","initial_relationship":0,"clue_id":"a","evidence_topic":"出示","evidence_reply":"认得"},{"id":"b","name":"乙","scene":"hall","topic":"询问","reply":"不知","initial_relationship":0,"clue_id":"missing","evidence_topic":"出示","evidence_reply":"认得"}]})";
    auto result = storyforge::agent::generate_world_seed_from_map(map_for("城镇"), "城镇谜案", provider);
    CHECK(std::holds_alternative<storyforge::agent::WorldSeedError>(result));
}

TEST_CASE("world spec rejects invalid references and bounds") {
    nlohmann::json spec = {
        {"attributes", {{{"id", "skill"}, {"name", "技艺"}, {"initial", 2}, {"min", 0}, {"max", 5}}}},
        {"identities", {{{"id", "maker"}, {"name", "工匠"}, {"description", "擅长制作"}, {"modifiers", {{"skill", 1}}}},
                        {{"id", "traveler"}, {"name", "旅人"}, {"description", "擅长探索"}, {"modifiers", nlohmann::json::object()}}}},
        {"actions", {{{"id", "practice"}, {"name", "练习"}, {"description", "提升技艺"},
                      {"scene", "gate"}, {"requirements", nlohmann::json::array()},
                      {"costs", nlohmann::json::object()}, {"gains", {{"skill", 1}}}, {"once", false}}}}};
    CHECK(std::holds_alternative<storyforge::agent::CompiledWorldSpec>(
        storyforge::agent::compile_world_spec(spec, {"gate"})));
    spec["unsupported_rule"] = "the model invented an unenforced rule";
    CHECK(std::holds_alternative<storyforge::agent::WorldSpecError>(
        storyforge::agent::compile_world_spec(spec, {"gate"})));
    spec.erase("unsupported_rule");
    spec["actions"][0]["gains"] = {{"unknown", 1}};
    CHECK(std::holds_alternative<storyforge::agent::WorldSpecError>(
        storyforge::agent::compile_world_spec(spec, {"gate"})));
    spec["actions"][0]["gains"] = {{"skill", 1}};
    spec["identities"][0]["modifiers"] = {{"skill", 4}};
    CHECK(std::holds_alternative<storyforge::agent::WorldSpecError>(
        storyforge::agent::compile_world_spec(spec, {"gate"})));
    spec["identities"][0]["modifiers"] = {{"skill", 1}};
    spec["actions"][0]["scene"] = "missing";
    CHECK(std::holds_alternative<storyforge::agent::WorldSpecError>(
        storyforge::agent::compile_world_spec(spec, {"gate"})));
    spec["actions"][0]["scene"] = "gate";
    spec["actions"][0]["requirements"] = nlohmann::json::array({
        {{"attribute", "skill"}, {"op", "gte"}, {"value", 5}}});
    CHECK(std::holds_alternative<storyforge::agent::WorldSpecError>(
        storyforge::agent::compile_world_spec(spec, {"gate"})));
}
