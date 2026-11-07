#include "storyforge/agent/world_spec.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <set>
#include <tuple>

namespace storyforge::agent {
namespace {
using Json = nlohmann::json;

bool id_ok(const std::string& id) {
    return !id.empty() && id.size() <= 32 && id.front() >= 'a' && id.front() <= 'z' &&
        std::all_of(id.begin(), id.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
}

bool string_field(const Json& object, const char* key, std::string& out, std::size_t limit) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_string()) return false;
    out = object[key].get<std::string>();
    return !out.empty() && out.size() <= limit;
}

bool integer_field(const Json& object, const char* key, std::int64_t& out) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_number_integer()) return false;
    const auto& number = object[key];
    if (number.is_number_unsigned() && number.get<std::uint64_t>() > 1000000) return false;
    out = number.get<std::int64_t>();
    return out >= -1000000 && out <= 1000000;
}

Json comparison(const std::string& id, const char* op, std::int64_t value) {
    return {{"type", "variable"}, {"variable", "var.attribute." + id},
            {"op", op}, {"value", value}};
}

Json change(const std::string& id, std::int64_t amount) {
    return {{"type", "modify_variable"}, {"variable", "var.attribute." + id},
            {"operation", "add"}, {"value", amount}};
}

struct Attribute { std::string name; std::int64_t initial{}, minimum{}, maximum{}; };

bool known_keys(const Json& object, std::initializer_list<const char*> allowed) {
    if (!object.is_object()) return false;
    for (auto it = object.begin(); it != object.end(); ++it)
        if (std::find_if(allowed.begin(), allowed.end(), [&](const char* key) {
                return it.key() == key;
            }) == allowed.end()) return false;
    return true;
}

bool amount_map(const Json& object, const char* key,
                const std::map<std::string, Attribute>& attributes,
                std::map<std::string, std::int64_t>& out, bool positive) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_object()) return false;
    for (auto it = object[key].begin(); it != object[key].end(); ++it) {
        if (!attributes.contains(it.key()) || !it.value().is_number_integer()) return false;
        if (it.value().is_number_unsigned() && it.value().get<std::uint64_t>() > 1000000) return false;
        auto amount = it.value().get<std::int64_t>();
        if (amount < -1000 || amount > 1000 || (positive && amount <= 0)) return false;
        if (amount != 0) out.emplace(it.key(), amount);
    }
    return true;
}
} // namespace

WorldSpecResult compile_world_spec(const Json& raw, const std::vector<std::string>& scene_ids) {
    if (!known_keys(raw, {"attributes", "identities", "actions"}))
        return WorldSpecError{"world_spec 只支持 attributes、identities 和 actions 三类规则，不能添加未执行的字段。"};
    if (!raw.contains("attributes") || !raw["attributes"].is_array() ||
        raw["attributes"].size() < 1 || raw["attributes"].size() > 8 ||
        !raw.contains("identities") || !raw["identities"].is_array() ||
        raw["identities"].size() < 2 || raw["identities"].size() > 4 ||
        !raw.contains("actions") || !raw["actions"].is_array() ||
        raw["actions"].empty() || raw["actions"].size() > 8)
        return WorldSpecError{"世界规则需要 1–8 项属性、2–4 个身份和 1–8 个可执行行动。"};

    std::map<std::string, Attribute> attributes;
    Json variables = Json::array({Json{{"id", "var.identity"}, {"initial", ""}}});
    std::string summary = "可选择身份：";
    for (const auto& item : raw["attributes"]) {
        std::string id, name;
        std::int64_t initial{}, minimum{}, maximum{};
        if (!known_keys(item, {"id", "name", "initial", "min", "max"}) ||
            !string_field(item, "id", id, 32) || !id_ok(id) ||
            !string_field(item, "name", name, 80) ||
            !integer_field(item, "initial", initial) ||
            !integer_field(item, "min", minimum) ||
            !integer_field(item, "max", maximum) ||
            minimum > initial || initial > maximum || maximum - minimum > 1000 ||
            !attributes.emplace(id, Attribute{name, initial, minimum, maximum}).second)
            return WorldSpecError{"属性 ID、初始值或上下限无效。"};
        variables.push_back({{"id", "var.attribute." + id}, {"initial", initial}});
    }

    Json actions = Json::array();
    std::set<std::string> identity_ids;
    std::vector<std::map<std::string, std::int64_t>> initial_states;
    for (const auto& item : raw["identities"]) {
        std::string id, name, description;
        std::map<std::string, std::int64_t> modifiers;
        if (!known_keys(item, {"id", "name", "description", "modifiers"}) ||
            !string_field(item, "id", id, 32) || !id_ok(id) ||
            !string_field(item, "name", name, 80) ||
            !string_field(item, "description", description, 300) ||
            !identity_ids.insert(id).second ||
            !amount_map(item, "modifiers", attributes, modifiers, false))
            return WorldSpecError{"身份 ID、名称、说明或属性修正无效。"};
        Json effects = Json::array({Json{{"type", "modify_variable"},
            {"variable", "var.identity"}, {"operation", "set"}, {"value", id}}});
        std::string identity_text = "选择身份「" + name + "」：" + description;
        std::map<std::string, std::int64_t> initial_state;
        for (const auto& [attribute, limits] : attributes)
            initial_state.emplace(attribute, limits.initial);
        for (const auto& [attribute, amount] : modifiers) {
            const auto& limits = attributes.at(attribute);
            if (limits.initial + amount < limits.minimum ||
                limits.initial + amount > limits.maximum)
                return WorldSpecError{"身份「" + name + "」的属性修正超出上下限。"};
            effects.push_back(change(attribute, amount));
            initial_state[attribute] += amount;
            identity_text += "；" + attributes.at(attribute).name +
                (amount > 0 ? "+" : "") + std::to_string(amount);
        }
        effects.push_back({{"type", "append_narrative"}, {"text", "已选择身份：" + name}});
        initial_states.push_back(std::move(initial_state));
        actions.push_back({{"id", "action.identity." + id},
            {"text", identity_text},
            {"conditions", Json::array({Json{{"type", "variable"},
                {"variable", "var.identity"}, {"op", "eq"}, {"value", ""}}})},
            {"effects", std::move(effects)}});
        if (identity_ids.size() > 1) summary += "、";
        summary += name;
    }

    std::set<std::string> action_ids;
    bool has_initial_action = false;
    for (const auto& item : raw["actions"]) {
        std::string id, name, description, scene;
        std::map<std::string, std::int64_t> costs, gains;
        if (!known_keys(item, {"id", "name", "description", "scene", "requirements", "costs", "gains", "once"}) ||
            !string_field(item, "id", id, 32) || !id_ok(id) ||
            !string_field(item, "name", name, 80) ||
            !string_field(item, "description", description, 300) ||
            !action_ids.insert(id).second ||
            !amount_map(item, "costs", attributes, costs, true) ||
            !amount_map(item, "gains", attributes, gains, true))
            return WorldSpecError{"行动 ID、说明或属性花费/收益无效。"};
        if (item.contains("scene") && item["scene"] != nullptr) {
            if (!string_field(item, "scene", scene, 32) ||
                std::find(scene_ids.begin(), scene_ids.end(), scene) == scene_ids.end())
                return WorldSpecError{"行动「" + name + "」引用了未知地点。"};
        }
        bool once = false;
        if (item.contains("once")) {
            if (!item["once"].is_boolean()) return WorldSpecError{"行动 once 必须是布尔值。"};
            once = item["once"].get<bool>();
        }
        Json conditions = Json::array({Json{{"type", "variable"},
            {"variable", "var.identity"}, {"op", "ne"}, {"value", ""}}});
        if (!scene.empty()) conditions.push_back({{"type", "current_scene"}, {"scene", "scene." + scene}});
        Json effects = Json::array();
        if (!item.contains("requirements") || !item["requirements"].is_array() ||
            item["requirements"].size() > 8)
            return WorldSpecError{"行动「" + name + "」需要 requirements 数组。"};
        std::vector<std::tuple<std::string, std::string, std::int64_t>> requirements;
        for (const auto& requirement : item["requirements"]) {
            std::string attribute, op;
            std::int64_t value{};
            if (!known_keys(requirement, {"attribute", "op", "value"}) ||
                !string_field(requirement, "attribute", attribute, 32) ||
                !attributes.contains(attribute) ||
                !string_field(requirement, "op", op, 3) ||
                (op != "eq" && op != "gte" && op != "lte") ||
                !integer_field(requirement, "value", value) ||
                value < attributes.at(attribute).minimum || value > attributes.at(attribute).maximum)
                return WorldSpecError{"行动「" + name + "」的条件引用或比较值无效。"};
            conditions.push_back(comparison(attribute, op.c_str(), value));
            requirements.emplace_back(attribute, op, value);
        }
        bool has_change = false;
        bool feasible_delta = true;
        for (const auto& [attribute, limits] : attributes) {
            const auto cost = costs.contains(attribute) ? costs.at(attribute) : 0;
            const auto gain = gains.contains(attribute) ? gains.at(attribute) : 0;
            const auto delta = gain - cost;
            if (delta > limits.maximum - limits.minimum ||
                delta < limits.minimum - limits.maximum) feasible_delta = false;
            if (cost > 0) conditions.push_back(comparison(attribute, "gte", limits.minimum + cost));
            if (delta > 0) conditions.push_back(comparison(attribute, "lte", limits.maximum - delta));
            if (delta < 0) conditions.push_back(comparison(attribute, "gte", limits.minimum - delta));
            if (delta != 0) { effects.push_back(change(attribute, delta)); has_change = true; }
        }
        if (!has_change) return WorldSpecError{"行动「" + name + "」必须实际改变至少一项属性。"};
        if (!feasible_delta) return WorldSpecError{"行动「" + name + "」的属性变化超出可用范围。"};
        for (const auto& state : initial_states) {
            bool available = true;
            for (const auto& [attribute, op, value] : requirements) {
                const auto current = state.at(attribute);
                available = available && (op == "eq" ? current == value :
                    op == "gte" ? current >= value : current <= value);
            }
            for (const auto& [attribute, limits] : attributes) {
                const auto cost = costs.contains(attribute) ? costs.at(attribute) : 0;
                const auto gain = gains.contains(attribute) ? gains.at(attribute) : 0;
                const auto current = state.at(attribute);
                available = available && current >= limits.minimum + cost &&
                    current + gain - cost >= limits.minimum &&
                    current + gain - cost <= limits.maximum;
            }
            has_initial_action = has_initial_action || available;
        }
        if (once) {
            const auto event = "event.action." + id;
            conditions.push_back({{"type", "not"}, {"condition", Json{{"type", "event_triggered"}, {"event", event}}}});
            effects.push_back({{"type", "complete_event"}, {"event", event}});
        }
        effects.push_back({{"type", "append_narrative"}, {"text", "已完成行动：" + name}});
        std::string action_text = name + "：" + description;
        if (!scene.empty()) action_text += "；地点 " + scene;
        for (const auto& [attribute, amount] : costs)
            action_text += "；消耗" + attributes.at(attribute).name + " " + std::to_string(amount);
        for (const auto& [attribute, amount] : gains)
            action_text += "；获得" + attributes.at(attribute).name + " " + std::to_string(amount);
        actions.push_back({{"id", "action.rule." + id},
            {"text", action_text},
            {"conditions", std::move(conditions)}, {"effects", std::move(effects)}});
    }
    if (!has_initial_action)
        return WorldSpecError{"所有身份的初始状态都无法执行任何成长行动，请调整条件或资源。"};
    return CompiledWorldSpec{raw, std::move(variables), std::move(actions), std::move(summary)};
}

} // namespace storyforge::agent
