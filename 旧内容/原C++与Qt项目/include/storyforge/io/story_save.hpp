#pragma once

#include "storyforge/model/story_types.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

namespace storyforge {

inline constexpr std::size_t max_story_save_bytes = 1024U * 1024U;

enum class SaveErrorCode { malformed, incompatible_world, invalid_state };

struct SaveError {
    SaveErrorCode code;
    std::string message;
};

using SaveTextResult = std::variant<std::string, SaveError>;
using LoadStateResult = std::variant<StoryState, SaveError>;

// The save format contains only authoritative world state. Provider credentials,
// model settings, narration, and transient Agent memory are never serialized.
[[nodiscard]] SaveTextResult serialize_story_save(const StoryPackage& package,
                                                   const StoryState& state);
[[nodiscard]] LoadStateResult deserialize_story_save(const StoryPackage& package,
                                                      std::string_view text);

} // namespace storyforge
