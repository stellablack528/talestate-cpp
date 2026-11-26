#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <variant>
#include <vector>

namespace storyforge::agent {

// A small, genre-independent rule specification proposed by the world builder.
// Only these typed fields are compiled into authoritative game state and actions.
struct CompiledWorldSpec {
    nlohmann::json spec;
    nlohmann::json variables;
    nlohmann::json actions;
    std::string summary;
};

struct WorldSpecError { std::string message; };
using WorldSpecResult = std::variant<CompiledWorldSpec, WorldSpecError>;

[[nodiscard]] WorldSpecResult compile_world_spec(
    const nlohmann::json& raw, const std::vector<std::string>& scene_ids);

} // namespace storyforge::agent
