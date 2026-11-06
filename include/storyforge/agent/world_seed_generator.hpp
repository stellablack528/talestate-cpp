#pragma once

#include "storyforge/agent/world_map_generator.hpp"

#include <string>
#include <variant>

namespace storyforge::agent {

// First-stage world package: explorable scenes and conversations, without endings.
struct WorldSeedDraft {
    std::string story_json;
    std::string opening_goal;
};
struct WorldSeedError { std::string message; };
using WorldSeedResult = std::variant<WorldSeedDraft, WorldSeedError>;

[[nodiscard]] WorldSeedResult generate_world_seed_from_map(
    const WorldMapDraft& map, const std::string& world_brief, ILlmProvider& provider);

} // namespace storyforge::agent
