#pragma once

#include "storyforge/agent/llm_provider.hpp"
#include "storyforge/runtime/story_runtime.hpp"

#include <string>
#include <variant>
#include <vector>

namespace storyforge::agent {

struct AgentTurn {
    StoryDelta delta;
    std::vector<AgentActionRequest> actions;
    std::string narration;
    bool used_fallback_narration{false};
};

struct AgentFailure {
    std::string message;
};

using AgentTurnResult = std::variant<AgentTurn, AgentFailure>;

// Provider-independent RPG workflow. The model proposes parameterized requests;
// only the C++ runtime may change authoritative state.
class StoryAgent {
public:
    [[nodiscard]] AgentTurnResult take_turn(const StoryPackage& package, StoryState& state,
                                             const std::string& player_input,
                                             ILlmProvider& provider);

private:
    std::vector<std::string> recent_turns_;
};

} // namespace storyforge::agent
