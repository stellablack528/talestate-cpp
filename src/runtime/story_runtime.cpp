#include "storyforge/runtime/story_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <iomanip>
#include <optional>
#include <sstream>
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

// MSVC does not mark the aggregate StoryState move assignment as noexcept,
// although each member can be swapped without throwing. Commit memberwise.
void swap_story_state(StoryState& left, StoryState& right) noexcept {
    static_assert(std::is_nothrow_swappable_v<StoryEntry> &&
                  std::is_nothrow_swappable_v<decltype(StoryState::variables)> &&
                  std::is_nothrow_swappable_v<decltype(StoryState::inventory)> &&
                  std::is_nothrow_swappable_v<decltype(StoryState::npc_memory)> &&
                  std::is_nothrow_swappable_v<decltype(StoryState::npc_relationships)> &&
                  std::is_nothrow_swappable_v<decltype(StoryState::completed_events)> &&
                  std::is_nothrow_swappable_v<decltype(StoryState::collected_objects)> &&
                  std::is_nothrow_swappable_v<decltype(StoryState::appointments)> &&
                  std::is_nothrow_swappable_v<decltype(StoryState::dynamic_facts)> &&
                  std::is_nothrow_swappable_v<decltype(StoryState::event_log)>);
    using std::swap;
    swap(left.position, right.position);
    swap(left.variables, right.variables);
    swap(left.gold, right.gold);
    swap(left.inventory, right.inventory);
    swap(left.npc_memory, right.npc_memory);
    swap(left.npc_relationships, right.npc_relationships);
    swap(left.completed_events, right.completed_events);
    swap(left.collected_objects, right.collected_objects);
    swap(left.turn, right.turn);
    swap(left.world_minute, right.world_minute);
    swap(left.awake_minutes, right.awake_minutes);
    swap(left.fatigue, right.fatigue);
    swap(left.health, right.health);
    swap(left.ill, right.ill);
    swap(left.daily_beats, right.daily_beats);
    swap(left.appointments, right.appointments);
    swap(left.dynamic_facts, right.dynamic_facts);
    swap(left.event_log, right.event_log);
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

SceneId npc_current_scene(const AgentNpc& npc, const StoryState& state) {
    const auto memory = state.npc_memory.find(npc.id);
    if (memory != state.npc_memory.end()) {
        const auto moved = memory->second.find("scheduled_scene");
        if (moved != memory->second.end() && std::holds_alternative<std::string>(moved->second))
            return SceneId{std::get<std::string>(moved->second)};
    }
    return npc.scene;
}

std::optional<CalendarMoment> calendar_at(const StoryPackage& package,
                                         std::uint64_t world_minute) {
    if (!package.calendar) return std::nullopt;
    const auto& calendar = *package.calendar;
    if (calendar.start_date.size() != 10 || world_minute > max_world_minute)
        return std::nullopt;
    using namespace std::chrono;
    const auto origin = sys_days{year{std::stoi(calendar.start_date.substr(0, 4))} /
        month{static_cast<unsigned>(std::stoul(calendar.start_date.substr(5, 2)))} /
        day{static_cast<unsigned>(std::stoul(calendar.start_date.substr(8, 2)))}};
    const auto total = calendar.start_hour * 60ULL + world_minute;
    const auto offset = static_cast<std::uint32_t>(total / 1440);
    const auto date = year_month_day{origin + days{offset}};
    std::ostringstream date_text;
    date_text << int(date.year()) << '-' << std::setw(2) << std::setfill('0')
              << unsigned(date.month()) << '-' << std::setw(2) << unsigned(date.day());
    CalendarMoment result{date_text.str(), {}, false,
        static_cast<std::uint32_t>((total % 1440) / 60),
        static_cast<std::uint32_t>(total % 60)};
    for (const auto& term : calendar.terms)
        if (offset >= term.first_day && offset <= term.last_day) result.term = term.name;
    const auto day_of_week = weekday{origin + days{offset}}.iso_encoding();
    result.school_day = !result.term.empty() && day_of_week <= 5;
    for (const auto& holiday : calendar.holidays)
        if (offset >= holiday.first_day && offset <= holiday.last_day) result.school_day = false;
    return result;
}

StoryState make_initial_state(const StoryPackage& package) {
    StoryState state;
    state.position = package.entry;
    for (const auto& variable : package.variables)
        state.variables.emplace(variable.id, variable.initial_value);
    for (const auto& npc : package.agent_npcs)
        if (npc.initial_relationship != 0)
            state.npc_relationships.emplace(npc.id, npc.initial_relationship);
    return state;
}

std::map<std::string, std::int64_t> current_player_attributes(
    const StoryPackage& package, const StoryState& state) {
    std::map<std::string, std::int64_t> attributes;
    if (!state.player) return attributes;
    const auto role = std::find_if(package.player_roles.begin(), package.player_roles.end(),
        [&](const PlayerRole& value) { return value.id == state.player->role; });
    attributes = role == package.player_roles.end() ? state.player->attributes : role->attributes;
    for (auto& [id, value] : attributes) {
        const auto found = state.variables.find(VariableId{"var.attribute." + id});
        if (found != state.variables.end())
            if (const auto* number = std::get_if<std::int64_t>(&found->second)) value = *number;
    }
    return attributes;
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

bool timeline_ready(const AgentTimelineEvent& beat, const StoryState& state) {
    if (state.completed_events.contains(beat.id) ||
        (!beat.source_event.empty() && !state.completed_events.contains(beat.source_event)))
        return false;
    const auto relationship = state.npc_relationships.find(beat.npc);
    const auto score = relationship == state.npc_relationships.end() ? 0 : relationship->second;
    return score >= beat.min_relationship;
}

bool valid_daily_beat_id(const std::string& id) {
    if (id.empty() || id.size() > 64 || id.front() < 'a' || id.front() > 'z') return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '.' || ch == '-';
    });
}

void settle_time(const StoryPackage& package, StoryState& state, std::uint64_t target,
                 std::vector<StoryEvent>& events, bool resting = false) {
    const auto start = state.world_minute;
    std::vector<std::uint64_t> milestones{target};
    for (const auto& appointment : package.agent_appointments) {
        const auto minute = appointment.start_minute + appointment.duration_minutes;
        if (minute > start && minute <= target) milestones.push_back(minute);
    }
    for (const auto& [id, fact] : state.dynamic_facts) {
        (void)id;
        if (fact.status != "open" || fact.due_minute == 0) continue;
        const auto minute = fact.kind == "planned_event" ? fact.due_minute + 60 : fact.due_minute;
        if (minute > start && minute <= target) milestones.push_back(minute);
    }
    for (const auto& beat : package.agent_timeline)
        if (beat.at_minute > start && beat.at_minute <= target)
            milestones.push_back(beat.at_minute);
    for (const auto& beat : state.daily_beats)
        if (!beat.occurred && beat.at_minute > start && beat.at_minute <= target)
            milestones.push_back(beat.at_minute);
    std::sort(milestones.begin(), milestones.end());
    milestones.erase(std::unique(milestones.begin(), milestones.end()), milestones.end());
    for (const auto minute : milestones) {
        if (!resting) {
            const auto before = state.awake_minutes > 960 ? (state.awake_minutes - 960) / 60 : 0;
            state.awake_minutes = std::min<std::uint64_t>(max_world_minute,
                state.awake_minutes + minute - state.world_minute);
            const auto after = state.awake_minutes > 960 ? (state.awake_minutes - 960) / 60 : 0;
            if (after > before) {
                state.fatigue = std::min<std::int64_t>(100,
                    state.fatigue + static_cast<std::int64_t>(std::min<std::uint64_t>(20, after - before)) * 5);
                events.push_back({"fatigue_changed", "疲劳值现为 " + std::to_string(state.fatigue)});
                if (state.fatigue >= 60) {
                    if (!state.ill) events.push_back({"became_ill", "过度疲劳使你生病了"});
                    state.ill = true;
                    state.health = std::max<std::int64_t>(0,
                        state.health - static_cast<std::int64_t>(std::min<std::uint64_t>(20, after - before)));
                    events.push_back({"health_changed", "生命值现为 " + std::to_string(state.health)});
                }
            }
        }
        state.world_minute = minute;
        for (auto& beat : state.daily_beats)
            if (!beat.occurred && beat.at_minute == minute) {
                beat.occurred = true;
                events.push_back({"daily_beat", beat.description});
            }
        for (const auto& appointment : package.agent_appointments) {
            auto status = state.appointments.find(appointment.id);
            if (status != state.appointments.end() && status->second == AppointmentStatus::booked &&
                minute >= appointment.start_minute + appointment.duration_minutes) {
                status->second = AppointmentStatus::missed;
                events.push_back({"appointment_missed", "错过了约定：" + appointment.title});
            }
        }
        for (auto& [id, fact] : state.dynamic_facts) {
            if (fact.status != "open" || fact.due_minute == 0) continue;
            if (fact.kind == "promise" && minute >= fact.due_minute)
                fact.status = "broken";
            else if (fact.kind == "planned_event" && minute >= fact.due_minute + 60)
                fact.status = "expired";
            else continue;
            state.event_log.push_back({fact.status, id, state.turn + 1, minute, {}});
            events.push_back({fact.status, fact.text});
        }
        for (const auto& beat : package.agent_timeline) {
            if (minute < beat.at_minute || !timeline_ready(beat, state)) continue;
            auto& memory = state.npc_memory[beat.npc];
            memory["current_plan"] = beat.plan;
            memory["plan_source"] = beat.source_event.empty() ?
                beat.id.value() : beat.source_event.value();
            memory["plan_minute"] = static_cast<std::int64_t>(minute);
            if (beat.scene) memory["scheduled_scene"] = beat.scene->value();
            state.completed_events.insert(beat.id);
            events.push_back({"npc_plan_changed", "人物计划更新：" + beat.plan});
        }
    }
    if (resting) {
        state.awake_minutes = 0;
        state.fatigue = std::max<std::int64_t>(0, state.fatigue - 40);
        state.ill = state.fatigue >= 60;
        events.push_back({"rested", "休息后疲劳值现为 " + std::to_string(state.fatigue)});
    }
}

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
    settle_time(package, candidate, candidate.world_minute, events);
    ++candidate.turn;

    StoryDelta delta{state, candidate, std::move(events)};
    swap_story_state(state, candidate);
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
    if (package.agent_actions.empty() && package.agent_routes.empty() &&
        package.agent_npcs.empty() && package.agent_objects.empty() &&
        package.agent_item_rules.empty() && package.agent_appointments.empty() &&
        package.agent_timeline.empty() &&
        std::any_of(actions.begin(), actions.end(), [](const AgentActionRequest& action) {
            return action.kind != AgentActionKind::sleep && action.kind != AgentActionKind::wait &&
                action.kind != AgentActionKind::skip;
        }))
        return error(RuntimeErrorCode::invalid_effect, "该世界未声明 Agent 行动规则");
    if (state.health == 0)
        return error(RuntimeErrorCode::story_finished, "生命耗尽，本局已经结束");
    if (actions.size() > 3)
        return error(RuntimeErrorCode::invalid_effect, "一回合最多执行三个行动");
    if (state.turn == std::numeric_limits<std::uint64_t>::max())
        return error(RuntimeErrorCode::arithmetic_overflow, "回合计数已达到上限");

    StoryState candidate = state;
    std::vector<StoryEvent> events;
    std::set<ChoiceId> seen;
    std::uint64_t activity_minutes = 0;
    for (const auto& request : actions) {
        const auto* current = current_node(package, candidate);
        if (current == nullptr)
            return error(RuntimeErrorCode::invalid_position, "当前位置不存在");
        if (current->terminal)
            return error(RuntimeErrorCode::story_finished, "故事已经结束");
        if (request.kind != AgentActionKind::give && request.kind != AgentActionKind::use &&
            !request.item.empty())
            return error(RuntimeErrorCode::invalid_effect, "这个行动不接受物品参数");
        const bool creates_fact = request.kind == AgentActionKind::investigate ||
            request.kind == AgentActionKind::promise || request.kind == AgentActionKind::ask_help ||
            request.kind == AgentActionKind::plan_event;
        if (!creates_fact && (!request.id.empty() || request.due_minute != 0))
            return error(RuntimeErrorCode::invalid_effect, "这个行动不接受新事实参数");
        const bool evaluates_lead = request.kind == AgentActionKind::corroborate ||
            request.kind == AgentActionKind::dispute;
        if (evaluates_lead ? request.evidence.empty() : !request.evidence.empty())
            return error(RuntimeErrorCode::invalid_effect, "证据来源参数与行动类型不匹配");
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
            if (!choice_is_available(choice, candidate)) {
                for (const auto& condition : found->conditions) {
                    if (evaluate_condition(condition, candidate)) continue;
                    if (std::holds_alternative<CurrentSceneCondition>(condition.data))
                        return error(RuntimeErrorCode::condition_unmet, "当前地点不能执行这个行动");
                    if (const auto* value = std::get_if<VariableComparison>(&condition.data)) {
                        if (value->variable == VariableId{"var.identity"})
                            return error(RuntimeErrorCode::condition_unmet,
                                value->op == CompareOperator::equal ? "身份已经选定，不能重复选择" :
                                "请先选择身份，再执行这个行动");
                        const auto current_value = candidate.variables.find(value->variable);
                        const auto actual = current_value != candidate.variables.end() &&
                            std::holds_alternative<std::int64_t>(current_value->second) ?
                            std::to_string(std::get<std::int64_t>(current_value->second)) : "未知";
                        const auto required = std::holds_alternative<std::int64_t>(value->value) ?
                            std::to_string(std::get<std::int64_t>(value->value)) : "非数值";
                        const char* comparison = value->op == CompareOperator::greater_equal ? "至少" :
                            value->op == CompareOperator::less_equal ? "至多" :
                            value->op == CompareOperator::equal ? "等于" :
                            value->op == CompareOperator::not_equal ? "不等于" :
                            value->op == CompareOperator::greater ? "大于" : "小于";
                        return error(RuntimeErrorCode::condition_unmet,
                            "行动条件不足：" + value->variable.value() + " 当前 " + actual +
                            "，需要" + comparison + " " + required);
                    }
                    if (const auto* negated = std::get_if<NotCondition>(&condition.data);
                        negated && negated->condition &&
                        std::holds_alternative<EventTriggeredCondition>(negated->condition->data))
                        return error(RuntimeErrorCode::condition_unmet, "这个一次性行动已经执行过");
                }
                return error(RuntimeErrorCode::condition_unmet, "行动条件未满足");
            }
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
            if (npc_current_scene(*found, candidate) != candidate.position.scene)
                return error(RuntimeErrorCode::condition_unmet, "这个 NPC 当前不在你所在的场景：" + request.target);
            const auto rule = std::find_if(found->dialogue_rules.begin(), found->dialogue_rules.end(),
                [&](const AgentDialogueRule& value) { return value.id == request.topic; });
            const auto remembered_fact = candidate.dynamic_facts.find(request.topic);
            if (remembered_fact != candidate.dynamic_facts.end() &&
                remembered_fact->second.source == request.target &&
                remembered_fact->second.kind != "lead") {
                candidate.npc_memory[found->id]["last_recalled_fact"] = request.topic;
                events.push_back({"npc_recalled", found->name + "曾听到这件事：" +
                    remembered_fact->second.text + "（当前状态：" +
                    remembered_fact->second.status + "）"});
            } else if (rule != found->dialogue_rules.end()) {
                for (const auto& condition : rule->conditions) {
                    if (const auto* required = std::get_if<HasItemCondition>(&condition.data);
                        required && !evaluate_condition(condition, candidate)) {
                        const auto object = std::find_if(package.agent_objects.begin(),
                            package.agent_objects.end(), [&](const AgentObject& value) {
                                return value.item == required->item;
                            });
                        return error(RuntimeErrorCode::condition_unmet,
                            "背包里没有这个话题需要的物品：" +
                            (object == package.agent_objects.end() ? required->item.value() : object->name));
                    }
                }
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
                candidate.npc_memory[found->id]["last_confirmed_reply_topic"] = request.topic;
            }
            candidate.npc_memory[found->id]["last_discussed_topic"] = request.topic;
            if (rule != found->dialogue_rules.end() ||
                (remembered_fact != candidate.dynamic_facts.end() &&
                 remembered_fact->second.source == request.target &&
                 remembered_fact->second.kind != "lead"))
                events.push_back({"npc_talked", "与 " + found->name + " 谈起：" + request.topic});
            else
                events.push_back({"npc_questioned", "你与 " + found->name + " 谈起：" +
                    request.topic + "；这段闲聊没有产生已确认的新事实。"});
        } else if (request.kind == AgentActionKind::inspect || request.kind == AgentActionKind::take) {
            if (!request.topic.empty())
                return error(RuntimeErrorCode::invalid_effect, "观察和拿取不接受话题参数");
            const auto found = std::find_if(package.agent_objects.begin(), package.agent_objects.end(),
                [&](const AgentObject& object) { return object.id == request.target; });
            if (found == package.agent_objects.end())
                return error(RuntimeErrorCode::unknown_choice, "世界中不存在这个对象：" + request.target);
            if (found->scene != candidate.position.scene)
                return error(RuntimeErrorCode::condition_unmet, "这个对象不在当前场景：" + request.target);
            if (candidate.collected_objects.contains(found->id))
                return error(RuntimeErrorCode::condition_unmet, "这个对象已经被拿走：" + request.target);
            if (request.kind == AgentActionKind::inspect) {
                events.push_back({"object_inspected", found->name + "：" + found->description});
            } else {
                if (!found->item)
                    return error(RuntimeErrorCode::condition_unmet, "这个对象不能拿取：" + request.target);
                if (auto failure = apply_effect(package, candidate,
                        StoryEffect{AddItemEffect{*found->item, 1}}, events)) return *failure;
                candidate.collected_objects.insert(found->id);
                events.push_back({"object_taken", "已从场景拿走 " + found->name});
            }
        } else if (request.kind == AgentActionKind::give || request.kind == AgentActionKind::use) {
            if (!request.topic.empty() || request.item.empty())
                return error(RuntimeErrorCode::invalid_effect, "给予或使用必须指定物品，且不接受话题参数");
            const auto rule = std::find_if(package.agent_item_rules.begin(), package.agent_item_rules.end(),
                [&](const AgentItemRule& value) {
                    return value.kind == request.kind && value.target == request.target &&
                           value.item.value() == request.item;
                });
            if (rule == package.agent_item_rules.end())
                return error(RuntimeErrorCode::unknown_choice, "这个世界没有声明该物品与目标的互动规则");
            if (request.kind == AgentActionKind::give) {
                const auto npc = std::find_if(package.agent_npcs.begin(), package.agent_npcs.end(),
                    [&](const AgentNpc& value) { return value.id.value() == request.target; });
                if (npc == package.agent_npcs.end() || npc_current_scene(*npc, candidate) != candidate.position.scene)
                    return error(RuntimeErrorCode::condition_unmet, "收取物品的 NPC 不在当前场景");
            } else {
                const auto object = std::find_if(package.agent_objects.begin(), package.agent_objects.end(),
                    [&](const AgentObject& value) { return value.id == request.target; });
                if (object == package.agent_objects.end() || object->scene != candidate.position.scene ||
                    candidate.collected_objects.contains(request.target))
                    return error(RuntimeErrorCode::condition_unmet, "使用物品的对象不在当前场景");
            }
            const auto owned = candidate.inventory.find(rule->item);
            if (owned == candidate.inventory.end() || owned->second < 1)
                return error(RuntimeErrorCode::insufficient_resources, "背包里没有要使用的物品：" + request.item);
            if (!std::all_of(rule->conditions.begin(), rule->conditions.end(),
                [&](const StoryCondition& condition) { return evaluate_condition(condition, candidate); }))
                return error(RuntimeErrorCode::condition_unmet, "这个物品互动的条件尚未满足");
            if (request.kind == AgentActionKind::give || rule->consume)
                if (auto failure = apply_effect(package, candidate,
                        StoryEffect{RemoveItemEffect{rule->item, 1}}, events)) return *failure;
            for (const auto& effect : rule->effects) {
                if (std::holds_alternative<SwitchSceneEffect>(effect.data) ||
                    std::holds_alternative<AdvanceChapterEffect>(effect.data))
                    return error(RuntimeErrorCode::invalid_effect, "物品互动不能直接移动玩家");
                if (auto failure = apply_effect(package, candidate, effect, events)) return *failure;
            }
            events.push_back({request.kind == AgentActionKind::give ? "item_given" : "item_used",
                              rule->description});
        } else if (request.kind == AgentActionKind::sleep) {
            if (!request.topic.empty() || request.daily_beats.size() > 3)
                return error(RuntimeErrorCode::invalid_effect, "睡眠最多规划三件次日事件");
            std::uint64_t minutes{};
            const auto [end, parse_error] = std::from_chars(
                request.target.data(), request.target.data() + request.target.size(), minutes);
            if (parse_error != std::errc{} || end != request.target.data() + request.target.size() ||
                minutes < 240 || minutes > 720 || candidate.world_minute > max_world_minute - minutes)
                return error(RuntimeErrorCode::invalid_effect, "睡眠时间须为 240–720 分钟");
            settle_time(package, candidate, candidate.world_minute + minutes, events, true);
            candidate.daily_beats.erase(std::remove_if(candidate.daily_beats.begin(),
                candidate.daily_beats.end(), [](const DailyBeat& beat) { return beat.occurred; }),
                candidate.daily_beats.end());
            if (candidate.daily_beats.size() + request.daily_beats.size() > 3)
                return error(RuntimeErrorCode::invalid_effect, "待发生的次日事件太多");
            std::set<std::string> beat_ids;
            for (const auto& beat : candidate.daily_beats) beat_ids.insert(beat.id);
            for (const auto& beat : request.daily_beats) {
                const auto* scene = find_scene_in_package(package, beat.scene).second;
                const bool npc_known = !beat.npc || std::any_of(package.agent_npcs.begin(),
                    package.agent_npcs.end(), [&](const AgentNpc& npc) { return npc.id == *beat.npc; });
                if (!valid_daily_beat_id(beat.id) || !beat_ids.insert(beat.id).second ||
                    scene == nullptr || !npc_known || beat.description.empty() ||
                    beat.description.size() > 500 || beat.occurred ||
                    beat.at_minute <= candidate.world_minute ||
                    beat.at_minute > candidate.world_minute + 1440 ||
                    beat.at_minute > max_world_minute)
                    return error(RuntimeErrorCode::invalid_effect, "次日事件的来源、地点或时间无效");
                candidate.daily_beats.push_back(beat);
            }
            events.push_back({"slept", "睡眠 " + std::to_string(minutes) + " 分钟，新的活动时段开始"});
        } else if (request.kind == AgentActionKind::wait ||
                   request.kind == AgentActionKind::skip) {
            if (!request.topic.empty())
                return error(RuntimeErrorCode::invalid_effect, "推进时间不接受话题参数");
            std::uint64_t target{};
            if (request.kind == AgentActionKind::wait) {
                std::uint64_t minutes{};
                const auto [end, parse_error] = std::from_chars(
                    request.target.data(), request.target.data() + request.target.size(), minutes);
                if (parse_error != std::errc{} || end != request.target.data() + request.target.size() ||
                    minutes < 1 || minutes > 240)
                    return error(RuntimeErrorCode::invalid_effect, "等待时间须为 1–240 分钟");
                if (candidate.world_minute > std::numeric_limits<std::uint64_t>::max() - minutes)
                    return error(RuntimeErrorCode::arithmetic_overflow, "世界时间已达到上限");
                target = candidate.world_minute + minutes;
            } else {
                if (request.target != "next")
                    return error(RuntimeErrorCode::invalid_effect, "时间跳跃目标须为 next");
                target = std::numeric_limits<std::uint64_t>::max();
                for (const auto& beat : package.agent_timeline)
                    if (beat.at_minute > candidate.world_minute && timeline_ready(beat, candidate))
                        target = std::min(target, beat.at_minute);
                for (const auto& appointment : package.agent_appointments) {
                    const auto status = candidate.appointments.find(appointment.id);
                    if (status != candidate.appointments.end() &&
                        status->second == AppointmentStatus::booked &&
                        appointment.start_minute > candidate.world_minute)
                        target = std::min(target, appointment.start_minute);
                }
                for (const auto& [id, fact] : candidate.dynamic_facts) {
                    (void)id;
                    if (fact.status == "open" && fact.due_minute > candidate.world_minute)
                        target = std::min(target, fact.due_minute);
                }
                if (target == std::numeric_limits<std::uint64_t>::max())
                    return error(RuntimeErrorCode::condition_unmet,
                        "当前没有可跳转的关键事件或未完成约定");
            }
            if (target > max_world_minute)
                return error(RuntimeErrorCode::condition_unmet, "时间跳跃不能越过十年边界");
            const auto elapsed = target - candidate.world_minute;
            settle_time(package, candidate, target, events);
            events.push_back({"time_advanced", "时间推进 " + std::to_string(elapsed) + " 分钟"});
        } else if (request.kind == AgentActionKind::book || request.kind == AgentActionKind::attend) {
            if (!request.topic.empty())
                return error(RuntimeErrorCode::invalid_effect, "约定行动不接受话题参数");
            const auto appointment = std::find_if(package.agent_appointments.begin(),
                package.agent_appointments.end(), [&](const AgentAppointment& value) {
                    return value.id == request.target;
                });
            if (appointment == package.agent_appointments.end())
                return error(RuntimeErrorCode::unknown_choice, "世界中没有这个约定：" + request.target);
            const auto status = candidate.appointments.find(appointment->id);
            if (request.kind == AgentActionKind::book) {
                const auto npc = std::find_if(package.agent_npcs.begin(), package.agent_npcs.end(),
                    [&](const AgentNpc& value) { return value.id == appointment->npc; });
                if (npc == package.agent_npcs.end() || npc_current_scene(*npc, candidate) != candidate.position.scene)
                    return error(RuntimeErrorCode::condition_unmet, "需要先找到发起约定的人物");
                if (candidate.world_minute >= appointment->start_minute || status != candidate.appointments.end())
                    return error(RuntimeErrorCode::condition_unmet, "约定已开始、结束或已经作出");
                for (const auto& other : package.agent_appointments) {
                    const auto booked = candidate.appointments.find(other.id);
                    if (booked == candidate.appointments.end() || booked->second != AppointmentStatus::booked)
                        continue;
                    if (appointment->start_minute < other.start_minute + other.duration_minutes &&
                        other.start_minute < appointment->start_minute + appointment->duration_minutes)
                        return error(RuntimeErrorCode::condition_unmet, "与已有约定的时间冲突：" + other.title);
                }
                candidate.appointments.emplace(appointment->id, AppointmentStatus::booked);
                events.push_back({"appointment_booked", "已答应参加：" + appointment->title});
            } else {
                if (status == candidate.appointments.end() || status->second != AppointmentStatus::booked)
                    return error(RuntimeErrorCode::condition_unmet, "尚未作出这个约定，或约定已结束");
                if (candidate.position.scene != appointment->scene)
                    return error(RuntimeErrorCode::condition_unmet, "尚未到达约定地点");
                if (candidate.world_minute < appointment->start_minute ||
                    candidate.world_minute >= appointment->start_minute + appointment->duration_minutes)
                    return error(RuntimeErrorCode::condition_unmet, "当前不在约定的时间窗口内");
                status->second = AppointmentStatus::attended;
                events.push_back({"appointment_attended", "已履行约定：" + appointment->title});
            }
        } else if (creates_fact) {
            if (request.id.size() < 3 || request.id.size() > 64 ||
                request.id.rfind("fact.", 0) != 0 ||
                !std::all_of(request.id.begin(), request.id.end(), [](unsigned char ch) {
                    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                           ch == '.' || ch == '_' || ch == '-';
                }) || candidate.dynamic_facts.contains(request.id))
                return error(RuntimeErrorCode::invalid_effect, "事实 ID 无效或已存在");
            const auto normalized = [&] (const std::string& input) {
                std::string value;
                bool pending_space = false;
                for (unsigned char ch : input) {
                    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                        pending_space = !value.empty();
                    } else {
                        if (pending_space) value.push_back(' ');
                        pending_space = false;
                        value.push_back(ch >= 'A' && ch <= 'Z' ?
                            static_cast<char>(ch - 'A' + 'a') : static_cast<char>(ch));
                    }
                }
                return value;
            };
            const auto content = normalized(request.topic);
            static const std::set<std::string> placeholders{
                "promise", "request", "plan", "lead", "clue", "help",
                "承诺", "请求", "计划", "线索", "帮忙", "传闻"};
            if (content.size() < 8 || placeholders.contains(content) || request.topic.size() > 280 ||
                request.topic.find('\n') != std::string::npos || candidate.dynamic_facts.size() >= 128)
                return error(RuntimeErrorCode::invalid_effect,
                    "事实描述必须写明具体事项，不能只填 promise、request 等占位词");
            const auto npc = std::find_if(package.agent_npcs.begin(), package.agent_npcs.end(),
                [&](const AgentNpc& value) { return value.id.value() == request.target; });
            const auto object = std::find_if(package.agent_objects.begin(), package.agent_objects.end(),
                [&](const AgentObject& value) { return value.id == request.target; });
            const bool npc_here = npc != package.agent_npcs.end() &&
                npc_current_scene(*npc, candidate) == candidate.position.scene;
            const bool object_here = object != package.agent_objects.end() &&
                object->scene == candidate.position.scene &&
                !candidate.collected_objects.contains(object->id);
            if (request.kind == AgentActionKind::investigate ? !object_here : !npc_here)
                return error(RuntimeErrorCode::condition_unmet, "事实来源不存在或不在当前地点");
            if ((request.kind == AgentActionKind::promise || request.kind == AgentActionKind::plan_event) ?
                (request.due_minute <= candidate.world_minute ||
                 request.due_minute > max_world_minute) : request.due_minute != 0)
                return error(RuntimeErrorCode::condition_unmet, "期限必须在世界日历的十年范围内");
            for (const auto& [id, fact] : candidate.dynamic_facts) {
                (void)id;
                if (fact.source == request.target && fact.kind ==
                    (request.kind == AgentActionKind::investigate ? "lead" :
                     request.kind == AgentActionKind::promise ? "promise" :
                     request.kind == AgentActionKind::ask_help ? "help_request" : "planned_event") &&
                    normalized(fact.text) == content)
                    return error(RuntimeErrorCode::condition_unmet, "同一来源的这项事实已经记录");
            }
            const auto kind = request.kind == AgentActionKind::investigate ? "lead" :
                request.kind == AgentActionKind::promise ? "promise" :
                request.kind == AgentActionKind::ask_help ? "help_request" : "planned_event";
            candidate.dynamic_facts.emplace(request.id, DynamicFact{request.id, kind,
                request.target, content, "open", request.due_minute});
            if (request.kind == AgentActionKind::promise || request.kind == AgentActionKind::ask_help)
                candidate.npc_memory[npc->id][request.kind == AgentActionKind::promise ?
                    "last_promise_id" : "last_help_request_id"] = request.id;
            candidate.event_log.push_back({"created", request.id, state.turn + 1, candidate.world_minute, {}});
            events.push_back({"fact_created", "已记录" + std::string{kind == std::string{"lead"} ? "待核实线索：" : "约定或计划："} + content});
        } else if (evaluates_lead) {
            if (!request.topic.empty())
                return error(RuntimeErrorCode::invalid_effect, "线索跟进不接受自由描述");
            const auto found = candidate.dynamic_facts.find(request.target);
            if (found == candidate.dynamic_facts.end() || found->second.kind != "lead" ||
                found->second.status != "open")
                return error(RuntimeErrorCode::condition_unmet, "没有可跟进的待核实线索");
            if (request.evidence == found->second.source)
                return error(RuntimeErrorCode::condition_unmet, "不能用原始来源重复证明线索");
            const bool object_here = std::any_of(package.agent_objects.begin(), package.agent_objects.end(),
                [&](const AgentObject& object) { return object.id == request.evidence &&
                    object.scene == candidate.position.scene &&
                    !candidate.collected_objects.contains(object.id); });
            const bool npc_here = std::any_of(package.agent_npcs.begin(), package.agent_npcs.end(),
                [&](const AgentNpc& npc) { return npc.id.value() == request.evidence &&
                    npc_current_scene(npc, candidate) == candidate.position.scene; });
            if (!object_here && !npc_here)
                return error(RuntimeErrorCode::condition_unmet, "独立来源不存在或不在当前地点");
            found->second.status = request.kind == AgentActionKind::corroborate ? "corroborated" : "disputed";
            candidate.event_log.push_back({found->second.status, request.target,
                state.turn + 1, candidate.world_minute, request.evidence});
            events.push_back({"lead_updated", "线索已获得独立来源的" +
                std::string{found->second.status == "corroborated" ? "支持" : "质疑"} +
                "；这仍不是世界真相的最终证明：" + found->second.text});
        } else if (request.kind == AgentActionKind::withdraw_help ||
                   request.kind == AgentActionKind::respond_help) {
            const auto found = candidate.dynamic_facts.find(request.target);
            if (found == candidate.dynamic_facts.end() || found->second.kind != "help_request" ||
                found->second.status != "open")
                return error(RuntimeErrorCode::condition_unmet, "没有可处理的求助请求");
            if (request.kind == AgentActionKind::withdraw_help) {
                if (!request.topic.empty())
                    return error(RuntimeErrorCode::invalid_effect, "撤回求助不接受话题参数");
                found->second.status = "withdrawn";
            } else {
                const auto npc = std::find_if(package.agent_npcs.begin(), package.agent_npcs.end(),
                    [&](const AgentNpc& value) { return value.id.value() == found->second.source; });
                if (npc == package.agent_npcs.end() || npc_current_scene(*npc, candidate) != candidate.position.scene)
                    return error(RuntimeErrorCode::condition_unmet, "求助对象当前不在场");
                const bool accepted = request.topic.rfind("help.accept.", 0) == 0;
                const bool declined = request.topic.rfind("help.decline.", 0) == 0;
                const auto rule = std::find_if(npc->dialogue_rules.begin(), npc->dialogue_rules.end(),
                    [&](const AgentDialogueRule& value) { return value.id == request.topic; });
                if ((!accepted && !declined) || rule == npc->dialogue_rules.end() ||
                    !std::all_of(rule->conditions.begin(), rule->conditions.end(),
                        [&](const StoryCondition& condition) { return evaluate_condition(condition, candidate); }))
                    return error(RuntimeErrorCode::condition_unmet, "世界包没有授权这个人物作出该回应");
                for (const auto& effect : rule->effects) {
                    if (std::holds_alternative<SwitchSceneEffect>(effect.data) ||
                        std::holds_alternative<AdvanceChapterEffect>(effect.data))
                        return error(RuntimeErrorCode::invalid_effect, "NPC 回应不能直接移动玩家");
                    if (auto failure = apply_effect(package, candidate, effect, events)) return *failure;
                }
                found->second.status = accepted ? "accepted" : "declined";
                events.push_back({"npc_dialogue", npc->name + "：" + rule->reply});
            }
            candidate.event_log.push_back({found->second.status, request.target,
                state.turn + 1, candidate.world_minute, {}});
            events.push_back({"help_updated", "求助请求状态：" + found->second.status});
        } else if (request.kind == AgentActionKind::attend_plan) {
            if (!request.topic.empty()) return error(RuntimeErrorCode::invalid_effect, "参加计划不接受描述");
            const auto found = candidate.dynamic_facts.find(request.target);
            if (found == candidate.dynamic_facts.end() || found->second.kind != "planned_event" ||
                found->second.status != "open")
                return error(RuntimeErrorCode::condition_unmet, "没有可参加的计划事件");
            const auto npc = std::find_if(package.agent_npcs.begin(), package.agent_npcs.end(),
                [&](const AgentNpc& value) { return value.id.value() == found->second.source; });
                if (npc == package.agent_npcs.end() || npc_current_scene(*npc, candidate) != candidate.position.scene ||
                candidate.world_minute < found->second.due_minute ||
                candidate.world_minute >= found->second.due_minute + 60)
                return error(RuntimeErrorCode::condition_unmet, "人物、地点或时间不满足计划条件");
            found->second.status = "occurred";
            candidate.event_log.push_back({"occurred", request.target,
                state.turn + 1, candidate.world_minute, {}});
            events.push_back({"planned_event_occurred", found->second.text});
        } else if (request.kind == AgentActionKind::fulfill || request.kind == AgentActionKind::break_promise) {
            if (!request.topic.empty()) return error(RuntimeErrorCode::invalid_effect, "履约行动不接受描述");
            const auto found = candidate.dynamic_facts.find(request.target);
            if (found == candidate.dynamic_facts.end() || found->second.kind != "promise" ||
                found->second.status != "open")
                return error(RuntimeErrorCode::condition_unmet, "没有可处理的未完成承诺");
            const auto npc = std::find_if(package.agent_npcs.begin(), package.agent_npcs.end(),
                [&](const AgentNpc& value) { return value.id.value() == found->second.source; });
            if (request.kind == AgentActionKind::fulfill &&
                (npc == package.agent_npcs.end() || npc_current_scene(*npc, candidate) != candidate.position.scene))
                return error(RuntimeErrorCode::condition_unmet, "履约需要与承诺对象在同一地点");
            found->second.status = request.kind == AgentActionKind::fulfill ? "fulfilled" : "broken";
            candidate.event_log.push_back({found->second.status, request.target,
                state.turn + 1, candidate.world_minute, {}});
            events.push_back({"promise_updated", found->second.text});
        } else {
            return error(RuntimeErrorCode::invalid_effect, "未知的行动类型");
        }
        if (candidate.player && request.kind != AgentActionKind::wait &&
            request.kind != AgentActionKind::skip && request.kind != AgentActionKind::sleep)
            activity_minutes += request.kind == AgentActionKind::move ? 30 : 10;
    }
    // A composed action sequence is one player turn, even though each capability
    // is evaluated against the candidate state left by the preceding one.
    if (activity_minutes && candidate.world_minute <= max_world_minute - activity_minutes)
        settle_time(package, candidate, candidate.world_minute + activity_minutes, events);
    else
        settle_time(package, candidate, candidate.world_minute, events);
    bool planned_awake_day = false;
    for (const auto& request : actions) {
        if (request.kind == AgentActionKind::sleep || request.daily_beats.empty()) continue;
        const auto day_at = [&](std::uint64_t minute) {
            const auto start_hour = package.calendar ? package.calendar->start_hour : 0U;
            return (minute + static_cast<std::uint64_t>(start_hour) * 60U) / 1440U;
        };
        if (planned_awake_day || day_at(candidate.world_minute) <= day_at(state.world_minute) ||
            request.daily_beats.size() > 2)
            return error(RuntimeErrorCode::invalid_effect, "清醒跨日事件只能在进入新的一天时规划一次");
        planned_awake_day = true;
        candidate.daily_beats.erase(std::remove_if(candidate.daily_beats.begin(),
            candidate.daily_beats.end(), [](const DailyBeat& beat) { return beat.occurred; }),
            candidate.daily_beats.end());
        if (candidate.daily_beats.size() + request.daily_beats.size() > 3)
            return error(RuntimeErrorCode::invalid_effect, "待发生的次日事件太多");
        std::set<std::string> ids;
        for (const auto& beat : candidate.daily_beats) ids.insert(beat.id);
        for (const auto& beat : request.daily_beats) {
            const bool npc_known = !beat.npc || std::any_of(package.agent_npcs.begin(),
                package.agent_npcs.end(), [&](const AgentNpc& npc) { return npc.id == *beat.npc; });
            if (!valid_daily_beat_id(beat.id) || !ids.insert(beat.id).second ||
                find_scene_in_package(package, beat.scene).second == nullptr || !npc_known ||
                beat.description.empty() || beat.description.size() > 500 || beat.occurred ||
                beat.at_minute <= candidate.world_minute ||
                beat.at_minute > candidate.world_minute + 1440 || beat.at_minute > max_world_minute)
                return error(RuntimeErrorCode::invalid_effect, "清醒跨日事件的来源、地点或时间无效");
            candidate.daily_beats.push_back(beat);
        }
        events.push_back({"day_planned", "新的一天已有待发生的事件"});
    }
    candidate.turn = state.turn + 1;
    StoryDelta delta{state, candidate, std::move(events)};
    swap_story_state(state, candidate);
    return delta;
}

CommandResult execute_agent_actions(const StoryPackage& package, StoryState& state,
                                     const std::vector<ChoiceId>& actions) {
    std::vector<AgentActionRequest> requests;
    requests.reserve(actions.size());
    for (const auto& action : actions)
        requests.push_back({AgentActionKind::authored, action.value(), {}, {}, {}, 0, {}, {}});
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
