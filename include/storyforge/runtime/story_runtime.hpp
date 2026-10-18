#pragma once

#include "storyforge/model/story_types.hpp"

#include <string>
#include <variant>

namespace storyforge {

enum class RuntimeErrorCode {
    invalid_position,
    story_finished,
    unknown_choice,
    condition_unmet,
    invalid_effect,
    insufficient_resources,
    arithmetic_overflow
};

struct RuntimeError {
    RuntimeErrorCode code;
    std::string message;
};

using CommandResult = std::variant<StoryDelta, RuntimeError>;

[[nodiscard]] StoryState make_initial_state(const StoryPackage& package);
[[nodiscard]] const StoryScene* current_scene(const StoryPackage& package, const StoryState& state);
[[nodiscard]] const StoryNode* current_node(const StoryPackage& package, const StoryState& state);
[[nodiscard]] bool evaluate_condition(const StoryCondition& condition, const StoryState& state);
[[nodiscard]] bool choice_is_available(const StoryChoice& choice, const StoryState& state);

// All effects are checked on a private copy. On failure, state is unchanged.
// The caller receives the committed before/after snapshot and events on success.
[[nodiscard]] CommandResult execute_command(const StoryPackage& package, StoryState& state,
                                            const StoryCommand& command);

[[nodiscard]] const char* to_string(RuntimeErrorCode code) noexcept;

} // namespace storyforge
