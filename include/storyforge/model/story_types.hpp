#pragma once

#include <cstdint>
#include <compare>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace storyforge {

template <typename Tag>
class StrongId {
public:
    StrongId() = default;
    explicit StrongId(std::string value) : value_(std::move(value)) {}

    [[nodiscard]] const std::string& value() const noexcept { return value_; }
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

    auto operator<=>(const StrongId&) const = default;

private:
    std::string value_;
};

struct StoryTag;
struct ChapterTag;
struct SceneTag;
struct NodeTag;
struct ChoiceTag;
struct VariableTag;
struct ItemTag;
struct EventTag;
struct NpcTag;

using StoryId = StrongId<StoryTag>;
using ChapterId = StrongId<ChapterTag>;
using SceneId = StrongId<SceneTag>;
using NodeId = StrongId<NodeTag>;
using ChoiceId = StrongId<ChoiceTag>;
using VariableId = StrongId<VariableTag>;
using ItemId = StrongId<ItemTag>;
using EventId = StrongId<EventTag>;
using NpcId = StrongId<NpcTag>;

using StoryValue = std::variant<std::int64_t, double, bool, std::string>;

struct StoryVariable {
    VariableId id;
    StoryValue initial_value;
};

enum class CompareOperator { equal, not_equal, less, less_equal, greater, greater_equal };

struct VariableComparison {
    VariableId variable;
    CompareOperator op{CompareOperator::equal};
    StoryValue value;
};
struct HasItemCondition { ItemId item; std::int64_t quantity{1}; };
struct GoldCondition { CompareOperator op{CompareOperator::greater_equal}; std::int64_t amount{0}; };
struct CurrentSceneCondition { SceneId scene; };
struct EventTriggeredCondition { EventId event; };
struct NpcMemoryCondition { NpcId npc; std::string key; StoryValue value; };

struct StoryCondition;
using ConditionPtr = std::shared_ptr<StoryCondition>;
struct AllCondition { std::vector<ConditionPtr> conditions; };
struct AnyCondition { std::vector<ConditionPtr> conditions; };
struct NotCondition { ConditionPtr condition; };

struct StoryCondition {
    using Data = std::variant<VariableComparison, HasItemCondition, GoldCondition,
                              CurrentSceneCondition, EventTriggeredCondition,
                              NpcMemoryCondition, AllCondition, AnyCondition, NotCondition>;
    Data data;
};

enum class VariableOperation { set, add };
struct ModifyVariableEffect {
    VariableId variable;
    VariableOperation operation{VariableOperation::set};
    StoryValue value;
};
struct AddGoldEffect { std::int64_t amount{0}; };
struct AddItemEffect { ItemId item; std::int64_t quantity{1}; };
struct RemoveItemEffect { ItemId item; std::int64_t quantity{1}; };
struct SwitchSceneEffect { SceneId scene; };
struct AdvanceChapterEffect { ChapterId chapter; };
struct WriteNpcMemoryEffect { NpcId npc; std::string key; StoryValue value; };
struct CompleteEventEffect { EventId event; };
struct AppendNarrativeEffect { std::string text; };

struct StoryEffect {
    using Data = std::variant<ModifyVariableEffect, AddGoldEffect, AddItemEffect,
                              RemoveItemEffect, SwitchSceneEffect, AdvanceChapterEffect,
                              WriteNpcMemoryEffect, CompleteEventEffect,
                              AppendNarrativeEffect>;
    Data data;
};

struct StoryChoice {
    ChoiceId id;
    std::string text;
    std::vector<StoryCondition> conditions;
    std::vector<StoryEffect> effects;
    std::optional<NodeId> target_node;
};

struct StoryNode {
    NodeId id;
    std::string text;
    std::vector<StoryChoice> choices;
    bool terminal{false};
};

struct StoryScene {
    SceneId id;
    std::string title;
    NodeId entry_node;
    std::vector<StoryNode> nodes;
};

struct StoryChapter {
    ChapterId id;
    std::string title;
    SceneId entry_scene;
    std::vector<StoryScene> scenes;
};

struct StoryEntry {
    ChapterId chapter;
    SceneId scene;
    NodeId node;
};

struct StoryPackage {
    std::string format;
    StoryId id;
    std::string title;
    std::string version;
    StoryEntry entry;
    std::vector<StoryVariable> variables;
    std::vector<StoryChapter> chapters;
};

// Runtime-facing value objects are defined now so later phases can add behavior
// without changing the public vocabulary established by Package v1.
struct StoryState {
    StoryEntry position;
    std::map<VariableId, StoryValue> variables;
    std::int64_t gold{0};
    std::map<ItemId, std::int64_t> inventory;
    std::map<NpcId, std::map<std::string, StoryValue>> npc_memory;
    std::set<EventId> completed_events;
};

struct StoryEvent {
    std::string type;
    std::string text;
};

struct StoryDelta {
    StoryState before;
    StoryState after;
    std::vector<StoryEvent> events;
};

struct StoryCommand {
    ChoiceId choice;
};

} // namespace storyforge
