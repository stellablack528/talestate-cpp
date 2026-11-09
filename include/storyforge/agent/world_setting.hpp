#pragma once

#include "storyforge/agent/llm_provider.hpp"

#include <array>
#include <string>
#include <variant>
#include <vector>

namespace storyforge::agent {

struct WorldSetting {
    std::string compact_brief;
    // facts, rumors, secrets, rules, style. Secrets stay out of ordinary turns.
    std::array<std::vector<std::string>, 5> sections;
};
struct WorldSettingError { std::string message; };
using WorldSettingResult = std::variant<WorldSetting, WorldSettingError>;

[[nodiscard]] WorldSettingResult prepare_world_setting(
    const std::string& document, ILlmProvider& provider);

} // namespace storyforge::agent
