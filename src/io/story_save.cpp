#include "storyforge/io/story_save.hpp"

#include "storyforge/runtime/story_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

namespace storyforge {
namespace {

using Json = nlohmann::json;

SaveError malformed(std::string message = "存档已损坏或格式不正确，当前进度未改变。") {
    return {SaveErrorCode::malformed, std::move(message)};
}

bool has_fields(const Json& value, std::initializer_list<const char*> fields) {
    if (!value.is_object() || value.size() != fields.size()) return false;
    for (const char* field : fields)
        if (!value.contains(field)) return false;
    return true;
}

bool valid_id(const std::string& id) {
    if (id.empty() || id.front() < 'a' || id.front() > 'z') return false;
    for (unsigned char ch : id)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '.' || ch == '-')) return false;
    return true;
}

std::optional<std::int64_t> integer(const Json& value) {
    if (!value.is_number_integer()) return std::nullopt;
    if (value.is_number_unsigned() &&
        value.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    return value.get<std::int64_t>();
}

std::optional<StoryValue> story_value(const Json& value) {
    if (value.is_boolean()) return StoryValue{value.get<bool>()};
    if (value.is_string()) return StoryValue{value.get<std::string>()};
    if (value.is_number_integer()) {
        const auto number = integer(value);
        if (number) return StoryValue{*number};
    }
    if (value.is_number_float()) {
        const auto number = value.get<double>();
        if (std::isfinite(number)) return StoryValue{number};
    }
    return std::nullopt;
}

Json value_json(const StoryValue& value) {
    return std::visit([](const auto& part) -> Json { return part; }, value);
}

} // namespace

SaveTextResult serialize_story_save(const StoryPackage& package, const StoryState& state) {
    Json saved_state{
        {"position", {{"chapter", state.position.chapter.value()},
                      {"scene", state.position.scene.value()}, {"node", state.position.node.value()}}},
        {"turn", state.turn}, {"gold", state.gold},
        {"variables", Json::object()}, {"inventory", Json::object()},
        {"npc_memory", Json::object()}, {"npc_relationships", Json::object()},
        {"completed_events", Json::array()}, {"collected_objects", Json::array()},
        {"world_minute", state.world_minute}, {"appointments", Json::object()},
        {"dynamic_facts", Json::object()}, {"event_log", Json::array()}};
    for (const auto& [id, value] : state.variables)
        saved_state["variables"][id.value()] = value_json(value);
    for (const auto& [id, quantity] : state.inventory)
        saved_state["inventory"][id.value()] = quantity;
    for (const auto& [npc, memories] : state.npc_memory) {
        saved_state["npc_memory"][npc.value()] = Json::object();
        for (const auto& [key, value] : memories)
            saved_state["npc_memory"][npc.value()][key] = value_json(value);
    }
    for (const auto& [npc, score] : state.npc_relationships)
        saved_state["npc_relationships"][npc.value()] = score;
    for (const auto& event : state.completed_events)
        saved_state["completed_events"].push_back(event.value());
    for (const auto& object : state.collected_objects)
        saved_state["collected_objects"].push_back(object);
    for (const auto& [id, status] : state.appointments)
        saved_state["appointments"][id] = status == AppointmentStatus::booked ? "booked" :
            status == AppointmentStatus::attended ? "attended" : "missed";
    for (const auto& [id, fact] : state.dynamic_facts)
        saved_state["dynamic_facts"][id] = {{"kind", fact.kind}, {"source", fact.source},
            {"text", fact.text}, {"status", fact.status}, {"due_minute", fact.due_minute}};
    for (const auto& event : state.event_log)
        saved_state["event_log"].push_back({{"type", event.type}, {"fact_id", event.fact_id},
            {"turn", event.turn}, {"minute", event.minute}});

    const Json save{{"format", "talestate.save/v1"},
                    {"world", {{"id", package.id.value()}, {"version", package.version}}},
                    {"state", std::move(saved_state)}};
    try {
        auto text = save.dump(2);
        if (text.size() > max_story_save_bytes)
            return SaveError{SaveErrorCode::invalid_state, "存档内容超过 1 MiB，无法保存。"};
        if (std::holds_alternative<SaveError>(deserialize_story_save(package, text)))
            return SaveError{SaveErrorCode::invalid_state, "当前世界状态无效，未覆盖已有存档。"};
        return text;
    } catch (const Json::exception&) {
        return SaveError{SaveErrorCode::invalid_state, "当前世界状态无法写入存档。"};
    }
}

LoadStateResult deserialize_story_save(const StoryPackage& package, std::string_view text) {
    if (text.empty() || text.size() > max_story_save_bytes) return malformed();
    try {
        const auto save = Json::parse(text.begin(), text.end(), nullptr, false);
        if (save.is_discarded() || !has_fields(save, {"format", "world", "state"}) ||
            !save["format"].is_string() || save["format"] != "talestate.save/v1")
            return malformed("不支持这个存档格式，当前进度未改变。");
        const auto& world = save["world"];
        if (!has_fields(world, {"id", "version"}) || !world["id"].is_string() ||
            !world["version"].is_string()) return malformed();
        if (world["id"] != package.id.value() || world["version"] != package.version)
            return SaveError{SaveErrorCode::incompatible_world,
                             "存档与当前世界包的 ID 或版本不一致，请导入原来的世界包。当前进度未改变。"};

        const auto& raw = save["state"];
        const bool dynamic_format = has_fields(raw, {"position", "turn", "gold", "variables", "inventory",
                              "npc_memory", "npc_relationships", "completed_events", "collected_objects",
                              "world_minute", "appointments", "dynamic_facts", "event_log"});
        const bool temporal_format = dynamic_format || has_fields(raw, {"position", "turn", "gold", "variables", "inventory",
                              "npc_memory", "npc_relationships", "completed_events", "collected_objects",
                              "world_minute", "appointments"});
        const bool current_format = temporal_format || has_fields(raw, {"position", "turn", "gold", "variables", "inventory",
                              "npc_memory", "npc_relationships", "completed_events", "collected_objects"});
        const bool older_format = has_fields(raw, {"position", "turn", "gold", "variables", "inventory",
                              "npc_memory", "npc_relationships", "completed_events"});
        if (!current_format && !older_format) return malformed();
        const auto& position = raw["position"];
        if (!has_fields(position, {"chapter", "scene", "node"}) ||
            !position["chapter"].is_string() || !position["scene"].is_string() ||
            !position["node"].is_string() || !raw["turn"].is_number_unsigned()) return malformed();
        const auto gold = integer(raw["gold"]);
        if (!gold || *gold < 0) return malformed();

        StoryState state = make_initial_state(package);
        state.position = {ChapterId{position["chapter"].get<std::string>()},
                          SceneId{position["scene"].get<std::string>()},
                          NodeId{position["node"].get<std::string>()}};
        if (current_node(package, state) == nullptr) return malformed("存档中的地点不存在于当前世界包。");
        state.turn = raw["turn"].get<std::uint64_t>();
        state.gold = *gold;
        if (temporal_format) {
            if (!raw["world_minute"].is_number_unsigned() || !raw["appointments"].is_object())
                return malformed();
            state.world_minute = raw["world_minute"].get<std::uint64_t>();
            for (const auto& [id, value] : raw["appointments"].items()) {
                if (!valid_id(id) || !value.is_string()) return malformed();
                const auto appointment = std::find_if(package.agent_appointments.begin(),
                    package.agent_appointments.end(), [&](const AgentAppointment& item) {
                        return item.id == id;
                    });
                if (appointment == package.agent_appointments.end()) return malformed();
                const auto status = value.get<std::string>();
                if (status == "booked" && state.world_minute < appointment->start_minute + appointment->duration_minutes)
                    state.appointments.emplace(id, AppointmentStatus::booked);
                else if (status == "attended" && state.world_minute >= appointment->start_minute)
                    state.appointments.emplace(id, AppointmentStatus::attended);
                else if (status == "missed" && state.world_minute >= appointment->start_minute + appointment->duration_minutes)
                    state.appointments.emplace(id, AppointmentStatus::missed);
                else return malformed();
            }
            for (std::size_t i = 0; i < package.agent_appointments.size(); ++i) {
                const auto& first = package.agent_appointments[i];
                if (state.appointments.find(first.id) == state.appointments.end() ||
                    state.appointments.at(first.id) != AppointmentStatus::booked) continue;
                for (std::size_t j = i + 1; j < package.agent_appointments.size(); ++j) {
                    const auto& second = package.agent_appointments[j];
                    if (state.appointments.find(second.id) == state.appointments.end() ||
                        state.appointments.at(second.id) != AppointmentStatus::booked) continue;
                    if (first.start_minute < second.start_minute + second.duration_minutes &&
                        second.start_minute < first.start_minute + first.duration_minutes)
                        return malformed("存档包含时间冲突的约定，当前进度未改变。");
                }
            }
        }
        if (dynamic_format) {
            if (!raw["dynamic_facts"].is_object() || !raw["event_log"].is_array() ||
                raw["dynamic_facts"].size() > 128 || raw["event_log"].size() > 512) return malformed();
            for (const auto& [id, value] : raw["dynamic_facts"].items()) {
                if (!valid_id(id) || id.rfind("fact.", 0) != 0 ||
                    !has_fields(value, {"kind", "source", "text", "status", "due_minute"}) ||
                    !value["kind"].is_string() || !value["source"].is_string() ||
                    !value["text"].is_string() || !value["status"].is_string() ||
                    !value["due_minute"].is_number_unsigned()) return malformed();
                const auto kind = value["kind"].get<std::string>();
                const auto status = value["status"].get<std::string>();
                const auto source = value["source"].get<std::string>();
                const auto content = value["text"].get<std::string>();
                const auto due = value["due_minute"].get<std::uint64_t>();
                if ((kind != "lead" && kind != "promise" && kind != "help_request" && kind != "planned_event") ||
                    (status != "open" && status != "broken" && status != "fulfilled" &&
                     status != "occurred" && status != "expired") ||
                    content.empty() || content.size() > 280 || !valid_id(source) ||
                    (due > state.world_minute && due - state.world_minute > 10080)) return malformed();
                const bool object_source = std::any_of(package.agent_objects.begin(), package.agent_objects.end(),
                    [&](const AgentObject& object) { return object.id == source; });
                const bool npc_source = std::any_of(package.agent_npcs.begin(), package.agent_npcs.end(),
                    [&](const AgentNpc& npc) { return npc.id.value() == source; });
                if ((kind == "lead" ? !object_source : !npc_source) ||
                    ((kind == "lead" || kind == "help_request") && (status != "open" || due != 0)) ||
                    (kind == "planned_event" && (due == 0 ||
                        (status != "open" && status != "occurred" && status != "expired"))) ||
                    (kind == "promise" && (due == 0 || status == "occurred"))) return malformed();
                state.dynamic_facts.emplace(id, DynamicFact{id, kind, source, content, status, due});
            }
            for (const auto& value : raw["event_log"]) {
                if (!has_fields(value, {"type", "fact_id", "turn", "minute"}) ||
                    !value["type"].is_string() || !value["fact_id"].is_string() ||
                    !value["turn"].is_number_unsigned() || !value["minute"].is_number_unsigned())
                    return malformed();
                const auto id = value["fact_id"].get<std::string>();
                const auto turn = value["turn"].get<std::uint64_t>();
                const auto minute = value["minute"].get<std::uint64_t>();
                if (!state.dynamic_facts.contains(id) || turn > state.turn || minute > state.world_minute)
                    return malformed();
                state.event_log.push_back({value["type"].get<std::string>(), id, turn, minute});
            }
            std::map<std::string, std::string> replayed;
            std::uint64_t last_turn = 0;
            std::uint64_t last_minute = 0;
            for (const auto& event : state.event_log) {
                if (event.turn < last_turn || event.minute < last_minute) return malformed();
                last_turn = event.turn;
                last_minute = event.minute;
                const auto& fact = state.dynamic_facts.at(event.fact_id);
                if (event.type == "created") {
                    if (replayed.contains(event.fact_id) ||
                        (fact.due_minute && (fact.due_minute <= event.minute ||
                         fact.due_minute - event.minute > 10080))) return malformed();
                    replayed[event.fact_id] = "open";
                } else {
                    const auto found = replayed.find(event.fact_id);
                    if (found == replayed.end() || found->second != "open" ||
                        (fact.kind == "promise" && event.type != "fulfilled" && event.type != "broken") ||
                        (fact.kind == "planned_event" && event.type != "occurred" && event.type != "expired") ||
                        (fact.kind != "promise" && fact.kind != "planned_event")) return malformed();
                    found->second = event.type;
                }
            }
            if (replayed.size() != state.dynamic_facts.size()) return malformed();
            for (const auto& [id, fact] : state.dynamic_facts)
                if (replayed.at(id) != fact.status) return malformed();
        }

        const auto& variables = raw["variables"];
        if (!variables.is_object() || variables.size() != state.variables.size()) return malformed();
        for (auto& [id, value] : state.variables) {
            const auto it = variables.find(id.value());
            if (it == variables.end()) return malformed();
            auto parsed = story_value(*it);
            if (!parsed || parsed->index() != value.index()) return malformed();
            value = std::move(*parsed);
        }

        const auto& inventory = raw["inventory"];
        if (!inventory.is_object()) return malformed();
        for (const auto& [id, quantity] : inventory.items()) {
            const auto count = integer(quantity);
            if (!valid_id(id) || !count || *count <= 0) return malformed();
            state.inventory.emplace(ItemId{id}, *count);
        }

        const auto& memories = raw["npc_memory"];
        if (!memories.is_object()) return malformed();
        for (const auto& [npc, entries] : memories.items()) {
            if (!valid_id(npc) || !entries.is_object()) return malformed();
            auto& memory = state.npc_memory[NpcId{npc}];
            for (const auto& [key, value] : entries.items()) {
                auto parsed = story_value(value);
                if (key.empty() || !parsed) return malformed();
                memory.emplace(key, std::move(*parsed));
            }
        }

        const auto& relationships = raw["npc_relationships"];
        if (!relationships.is_object()) return malformed();
        for (const auto& [npc, value] : relationships.items()) {
            const auto score = integer(value);
            if (!valid_id(npc) || !score) return malformed();
            state.npc_relationships[NpcId{npc}] = *score;
        }

        const auto& events = raw["completed_events"];
        if (!events.is_array()) return malformed();
        for (const auto& value : events) {
            if (!value.is_string()) return malformed();
            const auto id = value.get<std::string>();
            if (!valid_id(id) || !state.completed_events.emplace(EventId{id}).second)
                return malformed();
        }
        if (current_format) {
            const auto& objects = raw["collected_objects"];
            if (!objects.is_array()) return malformed();
            for (const auto& value : objects) {
                if (!value.is_string()) return malformed();
                const auto id = value.get<std::string>();
                const auto found = std::find_if(package.agent_objects.begin(), package.agent_objects.end(),
                    [&](const AgentObject& object) { return object.id == id && object.item.has_value(); });
                if (!valid_id(id) || found == package.agent_objects.end() ||
                    !state.collected_objects.insert(id).second) return malformed();
            }
        }
        return state;
    } catch (const Json::exception&) {
        return malformed();
    }
}

} // namespace storyforge
