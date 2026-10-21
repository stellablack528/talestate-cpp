#include "storyforge/runtime/story_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>

namespace storyforge {
namespace {

template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

const StoryChapter* find_chapter(const StoryPackage& package, const ChapterId& id) {
    const auto found = std::find_if(package.chapters.begin(), package.chapters.end(),
                                    [&](const StoryChapter& chapter) { return chapter.id == id; });
    return found == package.chapters.end() ? nullptr : &*found;
}

const StoryScene* find_scene(const StoryChapter& chapter, const SceneId& id) {
    const auto found = std::find_if(chapter.scenes.begin(), chapter.scenes.end(),
                                    [&](const StoryScene& scene) { return scene.id == id; });
    return found == chapter.scenes.end() ? nullptr : &*found;
}

const StoryNode* find_node(const StoryScene& scene, const NodeId& id) {
    const auto found = std::find_if(scene.nodes.begin(), scene.nodes.end(),
                                    [&](const StoryNode& node) { return node.id == id; });
    return found == scene.nodes.end() ? nullptr : &*found;
}

std::pair<const StoryChapter*, const StoryScene*> find_scene_in_package(
    const StoryPackage& package, const SceneId& id) {
    for (const auto& chapter : package.chapters) {
        if (const auto* scene = find_scene(chapter, id)) return {&chapter, scene};
    }
    return {nullptr, nullptr};
}

template <typename T>
bool compare_ordered(const T& left, CompareOperator op, const T& right) {
    switch (op) {
    case CompareOperator::equal: return left == right;
    case CompareOperator::not_equal: return left != right;
    case CompareOperator::less: return left < right;
    case CompareOperator::less_equal: return left <= right;
    case CompareOperator::greater: return left > right;
    case CompareOperator::greater_equal: return left >= right;
    }
    return false;
}

bool compare_values(const StoryValue& left, CompareOperator op, const StoryValue& right) {
    if (left.index() != right.index()) return false;
    return std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        const auto& expected = std::get<T>(right);
        if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>) {
            return compare_ordered(value, op, expected);
        } else {
            return (op == CompareOperator::equal && value == expected) ||
                   (op == CompareOperator::not_equal && value != expected);
        }
    }, left);
}

bool checked_add(std::int64_t left, std::int64_t right, std::int64_t& result) {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    if ((right > 0 && left > maximum - right) ||
        (right < 0 && left < minimum - right)) return false;
    result = left + right;
    return true;
}

RuntimeError error(RuntimeErrorCode code, std::string message) {
    return {code, std::move(message)};
}

std::optional<RuntimeError> apply_effect(const StoryPackage& package, StoryState& candidate,
                                         const StoryEffect& effect,
                                         std::vector<StoryEvent>& events) {
    return std::visit(Overloaded{
        [&](const ModifyVariableEffect& value) -> std::optional<RuntimeError> {
            auto found = candidate.variables.find(value.variable);
            if (found == candidate.variables.end())
                return error(RuntimeErrorCode::invalid_effect, "unknown variable: " + value.variable.value());
            if (found->second.index() != value.value.index())
                return error(RuntimeErrorCode::invalid_effect,
                             "variable type mismatch: " + value.variable.value());
            if (value.operation == VariableOperation::set) {
                found->second = value.value;
            } else if (auto* number = std::get_if<std::int64_t>(&found->second)) {
                std::int64_t result{};
                if (!checked_add(*number, std::get<std::int64_t>(value.value), result))
                    return error(RuntimeErrorCode::arithmetic_overflow, "variable addition overflow");
                *number = result;
            } else if (auto* decimal = std::get_if<double>(&found->second)) {
                const auto result = *decimal + std::get<double>(value.value);
                if (!std::isfinite(result))
                    return error(RuntimeErrorCode::arithmetic_overflow, "variable addition overflow");
                *decimal = result;
            } else {
                return error(RuntimeErrorCode::invalid_effect, "cannot add to a non-numeric variable");
            }
            events.push_back({"variable_changed", "变量 " + value.variable.value() + " 已更新"});
            return std::nullopt;
        },
        [&](const AddGoldEffect& value) -> std::optional<RuntimeError> {
            std::int64_t result{};
            if (!checked_add(candidate.gold, value.amount, result))
                return error(RuntimeErrorCode::arithmetic_overflow, "gold change overflow");
            if (result < 0)
                return error(RuntimeErrorCode::insufficient_resources, "金币不足");
            candidate.gold = result;
            events.push_back({"gold_changed", "金币现有 " + std::to_string(result)});
            return std::nullopt;
        },
        [&](const AddItemEffect& value) -> std::optional<RuntimeError> {
            if (value.quantity < 1)
                return error(RuntimeErrorCode::invalid_effect, "item quantity must be positive");
            const auto found = candidate.inventory.find(value.item);
            const auto owned = found == candidate.inventory.end() ? 0 : found->second;
            std::int64_t result{};
            if (!checked_add(owned, value.quantity, result))
                return error(RuntimeErrorCode::arithmetic_overflow, "item quantity overflow");
            candidate.inventory[value.item] = result;
            events.push_back({"item_added", "获得 " + value.item.value() + " ×" +
                                               std::to_string(value.quantity)});
            return std::nullopt;
        },
        [&](const RemoveItemEffect& value) -> std::optional<RuntimeError> {
            if (value.quantity < 1)
                return error(RuntimeErrorCode::invalid_effect, "item quantity must be positive");
            const auto found = candidate.inventory.find(value.item);
            if (found == candidate.inventory.end() || found->second < value.quantity)
                return error(RuntimeErrorCode::insufficient_resources,
                             "物品不足：" + value.item.value());
            found->second -= value.quantity;
            if (found->second == 0) candidate.inventory.erase(found);
            events.push_back({"item_removed", "消耗 " + value.item.value() + " ×" +
                                                 std::to_string(value.quantity)});
            return std::nullopt;
        },
        [&](const SwitchSceneEffect& value) -> std::optional<RuntimeError> {
            const auto [chapter, scene] = find_scene_in_package(package, value.scene);
            if (chapter == nullptr || find_node(*scene, scene->entry_node) == nullptr)
                return error(RuntimeErrorCode::invalid_effect, "unknown destination scene");
            candidate.position = {chapter->id, scene->id, scene->entry_node};
            events.push_back({"scene_changed", "进入 " + scene->title});
            return std::nullopt;
        },
        [&](const AdvanceChapterEffect& value) -> std::optional<RuntimeError> {
            const auto* chapter = find_chapter(package, value.chapter);
            if (chapter == nullptr)
                return error(RuntimeErrorCode::invalid_effect, "unknown destination chapter");
            const auto* scene = find_scene(*chapter, chapter->entry_scene);
            if (scene == nullptr || find_node(*scene, scene->entry_node) == nullptr)
                return error(RuntimeErrorCode::invalid_effect, "invalid destination chapter entry");
            candidate.position = {chapter->id, scene->id, scene->entry_node};
            events.push_back({"chapter_changed", "进入 " + chapter->title});
            return std::nullopt;
        },
        [&](const WriteNpcMemoryEffect& value) -> std::optional<RuntimeError> {
            candidate.npc_memory[value.npc][value.key] = value.value;
            events.push_back({"npc_memory_written", value.npc.value() + " 记住了 " + value.key});
            return std::nullopt;
        },
        [&](const ModifyNpcRelationshipEffect& value) -> std::optional<RuntimeError> {
            auto& score = candidate.npc_relationships[value.npc];
            std::int64_t result{};
            if (!checked_add(score, value.amount, result))
                return error(RuntimeErrorCode::arithmetic_overflow, "NPC relationship overflow");
            score = result;
            events.push_back({"npc_relationship_changed",
                              value.npc.value() + " 的关系值现为 " + std::to_string(result)});
            return std::nullopt;
        },
        [&](const CompleteEventEffect& value) -> std::optional<RuntimeError> {
            candidate.completed_events.insert(value.event);
            events.push_back({"event_completed", "事件 " + value.event.value() + " 已完成"});
            return std::nullopt;
        },
        [&](const AppendNarrativeEffect& value) -> std::optional<RuntimeError> {
            events.push_back({"narrative", value.text});
            return std::nullopt;
        }
    }, effect.data);
}

} // namespace

StoryState make_initial_state(const StoryPackage& package) {
    StoryState state;
    state.position = package.entry;
    for (const auto& variable : package.variables)
        state.variables.emplace(variable.id, variable.initial_value);
    return state;
}

const StoryScene* current_scene(const StoryPackage& package, const StoryState& state) {
    const auto* chapter = find_chapter(package, state.position.chapter);
    return chapter == nullptr ? nullptr : find_scene(*chapter, state.position.scene);
}

const StoryNode* current_node(const StoryPackage& package, const StoryState& state) {
    const auto* scene = current_scene(package, state);
    return scene == nullptr ? nullptr : find_node(*scene, state.position.node);
}

bool evaluate_condition(const StoryCondition& condition, const StoryState& state) {
    return std::visit(Overloaded{
        [&](const VariableComparison& value) {
            const auto found = state.variables.find(value.variable);
            return found != state.variables.end() &&
                   compare_values(found->second, value.op, value.value);
        },
        [&](const HasItemCondition& value) {
            const auto found = state.inventory.find(value.item);
            return value.quantity > 0 && found != state.inventory.end() &&
                   found->second >= value.quantity;
        },
        [&](const GoldCondition& value) {
            return compare_ordered(state.gold, value.op, value.amount);
        },
        [&](const CurrentSceneCondition& value) {
            return state.position.scene == value.scene;
        },
        [&](const EventTriggeredCondition& value) {
            return state.completed_events.contains(value.event);
        },
        [&](const NpcMemoryCondition& value) {
            const auto npc = state.npc_memory.find(value.npc);
            if (npc == state.npc_memory.end()) return false;
            const auto memory = npc->second.find(value.key);
            return memory != npc->second.end() && memory->second == value.value;
        },
        [&](const NpcRelationshipCondition& value) {
            const auto found = state.npc_relationships.find(value.npc);
            const auto score = found == state.npc_relationships.end() ? 0 : found->second;
            return compare_ordered(score, value.op, value.value);
        },
        [&](const AllCondition& value) {
            return std::all_of(value.conditions.begin(), value.conditions.end(),
                               [&](const ConditionPtr& part) {
                                   return part && evaluate_condition(*part, state);
                               });
        },
        [&](const AnyCondition& value) {
            return std::any_of(value.conditions.begin(), value.conditions.end(),
                               [&](const ConditionPtr& part) {
                                   return part && evaluate_condition(*part, state);
                               });
        },
        [&](const NotCondition& value) {
            return value.condition && !evaluate_condition(*value.condition, state);
        }
    }, condition.data);
}

bool choice_is_available(const StoryChoice& choice, const StoryState& state) {
    return std::all_of(choice.conditions.begin(), choice.conditions.end(),
                       [&](const StoryCondition& condition) {
                           return evaluate_condition(condition, state);
                       });
}

namespace {

CommandResult execute_choice(const StoryPackage& package, StoryState& state,
                             const StoryChoice& choice) {
    const auto* node = current_node(package, state);
    if (node == nullptr)
        return error(RuntimeErrorCode::invalid_position, "当前位置不存在");
    if (node->terminal)
        return error(RuntimeErrorCode::story_finished, "故事已经结束");
    if (!choice_is_available(choice, state))
        return error(RuntimeErrorCode::condition_unmet, "行动条件未满足");
    if (state.turn == std::numeric_limits<std::uint64_t>::max())
        return error(RuntimeErrorCode::arithmetic_overflow, "回合计数已达到上限");

    const auto position_effects = std::count_if(choice.effects.begin(), choice.effects.end(),
        [](const StoryEffect& effect) {
            return std::holds_alternative<SwitchSceneEffect>(effect.data) ||
                   std::holds_alternative<AdvanceChapterEffect>(effect.data);
        });
    if (position_effects + static_cast<int>(choice.target_node.has_value()) > 1)
        return error(RuntimeErrorCode::invalid_effect, "一个选项不能同时跳转到多个位置");

    StoryState candidate = state;
    std::vector<StoryEvent> events;
    for (const auto& effect : choice.effects) {
        if (auto failure = apply_effect(package, candidate, effect, events)) return *failure;
    }
    if (choice.target_node) {
        const auto* scene = current_scene(package, state);
        if (scene == nullptr || find_node(*scene, *choice.target_node) == nullptr)
            return error(RuntimeErrorCode::invalid_effect, "目标节点不存在");
        candidate.position.node = *choice.target_node;
    }
    ++candidate.turn;

    StoryDelta delta{state, candidate, std::move(events)};
    using std::swap;
    static_assert(std::is_nothrow_swappable_v<StoryState>);
    swap(state, candidate);
    return delta;
}

} // namespace

CommandResult execute_command(const StoryPackage& package, StoryState& state,
                              const StoryCommand& command) {
    const auto* node = current_node(package, state);
    if (node == nullptr)
        return error(RuntimeErrorCode::invalid_position, "当前位置不存在");
    if (node->terminal)
        return error(RuntimeErrorCode::story_finished, "故事已经结束");
    const auto found = std::find_if(node->choices.begin(), node->choices.end(),
        [&](const StoryChoice& choice) { return choice.id == command.choice; });
    if (found == node->choices.end())
        return error(RuntimeErrorCode::unknown_choice, "当前没有这个选项");
    return execute_choice(package, state, *found);
}

CommandResult execute_agent_requests(const StoryPackage& package, StoryState& state,
                                     const std::vector<AgentActionRequest>& actions) {
    const auto* node = current_node(package, state);
    if (node == nullptr)
        return error(RuntimeErrorCode::invalid_position, "当前位置不存在");
    if (node->terminal)
        return error(RuntimeErrorCode::story_finished, "故事已经结束");
    if (package.agent_actions.empty() && package.agent_routes.empty() && package.agent_npcs.empty())
        return error(RuntimeErrorCode::invalid_effect, "该世界未声明 Agent 行动规则");
    if (actions.size() > 3)
        return error(RuntimeErrorCode::invalid_effect, "一回合最多执行三个行动");
    if (state.turn == std::numeric_limits<std::uint64_t>::max())
        return error(RuntimeErrorCode::arithmetic_overflow, "回合计数已达到上限");

    StoryState candidate = state;
    std::vector<StoryEvent> events;
    std::set<ChoiceId> seen;
    for (const auto& request : actions) {
        const auto* current = current_node(package, candidate);
        if (current == nullptr)
            return error(RuntimeErrorCode::invalid_position, "当前位置不存在");
        if (current->terminal)
            return error(RuntimeErrorCode::story_finished, "故事已经结束");
        if (request.kind == AgentActionKind::authored) {
            if (!request.topic.empty())
                return error(RuntimeErrorCode::invalid_effect, "预设行动不接受话题参数");
            const ChoiceId action_id{request.target};
            if (!seen.insert(action_id).second)
                return error(RuntimeErrorCode::invalid_effect, "同一回合不能重复执行同一行动");
            const auto found = std::find_if(package.agent_actions.begin(), package.agent_actions.end(),
                [&](const AgentAction& action) { return action.id == action_id; });
            if (found == package.agent_actions.end())
                return error(RuntimeErrorCode::unknown_choice, "Agent 提出了未授权的行动：" + request.target);
            const StoryChoice choice{found->id, found->text, found->conditions,
                                     found->effects, found->target_node};
            auto result = execute_choice(package, candidate, choice);
            if (auto* failure = std::get_if<RuntimeError>(&result)) return *failure;
            const auto& delta = std::get<StoryDelta>(result);
            events.insert(events.end(), delta.events.begin(), delta.events.end());
            candidate.turn = state.turn;
        } else if (request.kind == AgentActionKind::move) {
            if (!request.topic.empty())
                return error(RuntimeErrorCode::invalid_effect, "移动行动不接受话题参数");
            const auto found = std::find_if(package.agent_routes.begin(), package.agent_routes.end(),
                [&](const AgentRoute& route) {
                    return route.from == candidate.position.scene && route.to.value() == request.target;
                });
            if (found == package.agent_routes.end())
                return error(RuntimeErrorCode::condition_unmet, "当前地点没有通往目标场景的路线：" + request.target);
            if (!std::all_of(found->conditions.begin(), found->conditions.end(),
                [&](const StoryCondition& condition) { return evaluate_condition(condition, candidate); }))
                return error(RuntimeErrorCode::condition_unmet, "通往目标场景的路线条件未满足：" + request.target);
            if (auto failure = apply_effect(package, candidate,
                    StoryEffect{SwitchSceneEffect{found->to}}, events)) return *failure;
        } else if (request.kind == AgentActionKind::talk) {
            if (request.topic.empty() || request.topic.size() > 256 ||
                std::any_of(request.topic.begin(), request.topic.end(), [](unsigned char ch) { return ch < 0x20; }))
                return error(RuntimeErrorCode::invalid_effect, "交谈话题须为 1–256 字节的单行文字");
            const auto found = std::find_if(package.agent_npcs.begin(), package.agent_npcs.end(),
                [&](const AgentNpc& npc) { return npc.id.value() == request.target; });
            if (found == package.agent_npcs.end())
                return error(RuntimeErrorCode::unknown_choice, "世界中不存在这个 NPC：" + request.target);
            if (found->scene != candidate.position.scene)
                return error(RuntimeErrorCode::condition_unmet, "这个 NPC 当前不在你所在的场景：" + request.target);
            const auto rule = std::find_if(found->dialogue_rules.begin(), found->dialogue_rules.end(),
                [&](const AgentDialogueRule& value) { return value.id == request.topic; });
            if (rule != found->dialogue_rules.end()) {
                if (!std::all_of(rule->conditions.begin(), rule->conditions.end(),
                    [&](const StoryCondition& condition) { return evaluate_condition(condition, candidate); }))
                    return error(RuntimeErrorCode::condition_unmet,
                                 "这个 NPC 此时不能透露该话题：" + request.topic);
                for (const auto& effect : rule->effects) {
                    if (std::holds_alternative<SwitchSceneEffect>(effect.data) ||
                        std::holds_alternative<AdvanceChapterEffect>(effect.data))
                        return error(RuntimeErrorCode::invalid_effect,
                                     "NPC 对话规则不能直接移动玩家");
                    if (auto failure = apply_effect(package, candidate, effect, events)) return *failure;
                }
                events.push_back({"npc_dialogue", found->name + "：" + rule->reply});
            }
            candidate.npc_memory[found->id]["last_discussed_topic"] = request.topic;
            events.push_back({"npc_talked", "与 " + found->name + " 谈起：" + request.topic});
        } else {
            return error(RuntimeErrorCode::invalid_effect, "未知的行动类型");
        }
    }
    // A composed action sequence is one player turn, even though each capability
    // is evaluated against the candidate state left by the preceding one.
    candidate.turn = state.turn + 1;
    StoryDelta delta{state, candidate, std::move(events)};
    using std::swap;
    swap(state, candidate);
    return delta;
}

CommandResult execute_agent_actions(const StoryPackage& package, StoryState& state,
                                     const std::vector<ChoiceId>& actions) {
    std::vector<AgentActionRequest> requests;
    requests.reserve(actions.size());
    for (const auto& action : actions)
        requests.push_back({AgentActionKind::authored, action.value(), {}});
    return execute_agent_requests(package, state, requests);
}

const char* to_string(RuntimeErrorCode code) noexcept {
    switch (code) {
    case RuntimeErrorCode::invalid_position: return "invalid_position";
    case RuntimeErrorCode::story_finished: return "story_finished";
    case RuntimeErrorCode::unknown_choice: return "unknown_choice";
    case RuntimeErrorCode::condition_unmet: return "condition_unmet";
    case RuntimeErrorCode::invalid_effect: return "invalid_effect";
    case RuntimeErrorCode::insufficient_resources: return "insufficient_resources";
    case RuntimeErrorCode::arithmetic_overflow: return "arithmetic_overflow";
    }
    return "invalid_effect";
}

} // namespace storyforge
