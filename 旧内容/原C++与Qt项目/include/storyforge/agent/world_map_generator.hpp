#pragma once

#include "storyforge/agent/llm_provider.hpp"

#include <string>
#include <variant>
#include <vector>

namespace storyforge::agent {

struct WorldMapLocation {
    std::string id;
    std::string title;
    std::string description;
    int x{0};
    int y{0};
};

struct WorldMapRoute {
    std::string from;
    std::string to;
};

// A candidate map. It never changes a running world or session.
struct WorldMapDraft {
    std::string title;
    std::string start;
    std::vector<WorldMapLocation> locations;
    std::vector<WorldMapRoute> routes;
};

struct WorldMapError { std::string message; };
using WorldMapResult = std::variant<WorldMapDraft, WorldMapError>;

[[nodiscard]] WorldMapResult parse_world_map_draft(const std::string& json_text);
[[nodiscard]] WorldMapResult generate_world_map_draft(const std::string& world_brief,
                                                      ILlmProvider& provider,
                                                      std::size_t exact_locations = 0);
[[nodiscard]] std::string serialize_world_map_draft(const WorldMapDraft& draft);

} // namespace storyforge::agent
