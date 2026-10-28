#include "storyforge/io/story_save.hpp"

#include "storyforge/runtime/story_runtime.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
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
        {"completed_events", Json::array()}};
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
        if (!has_fields(raw, {"position", "turn", "gold", "variables", "inventory",
                              "npc_memory", "npc_relationships", "completed_events"})) return malformed();
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
            state.npc_relationships.emplace(NpcId{npc}, *score);
        }

        const auto& events = raw["completed_events"];
        if (!events.is_array()) return malformed();
        for (const auto& value : events) {
            if (!value.is_string()) return malformed();
            const auto id = value.get<std::string>();
            if (!valid_id(id) || !state.completed_events.emplace(EventId{id}).second)
                return malformed();
        }
        return state;
    } catch (const Json::exception&) {
        return malformed();
    }
}

} // namespace storyforge
