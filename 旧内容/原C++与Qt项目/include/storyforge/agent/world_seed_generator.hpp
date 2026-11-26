#pragma once

#include "storyforge/agent/world_map_generator.hpp"

#include <string>
#include <variant>

namespace storyforge::agent {
struct WorldSetting;

// First-stage world package: explorable scenes and conversations, without endings.
struct WorldSeedDraft {
    std::string story_json;
    std::string opening_goal;
    std::string world_spec_json;
};
struct WorldSeedError { std::string message; };
using WorldSeedResult = std::variant<WorldSeedDraft, WorldSeedError>;

[[nodiscard]] WorldSeedResult generate_world_seed_from_map(
    const WorldMapDraft& map, const std::string& world_brief, ILlmProvider& provider,
    const WorldSetting* setting = nullptr);

} // namespace storyforge::agent
