#pragma once

#include "storyforge/agent/llm_provider.hpp"

#include <string>

namespace storyforge::agent {

enum class ConnectionCheckCode {
    connected,
    invalid_configuration,
    network_failure,
    authentication_failure,
    model_or_endpoint_failure,
    rate_limited,
    provider_unavailable,
    incompatible_response,
    request_rejected
};

struct ConnectionCheckResult {
    ConnectionCheckCode code{ConnectionCheckCode::network_failure};
    // Fixed, user-facing diagnostics. Never includes the provider response or API key.
    std::string message;

    [[nodiscard]] bool connected() const noexcept { return code == ConnectionCheckCode::connected; }
};

// Sends one minimal, non-streaming chat request through the same provider path as gameplay.
// A success proves basic chat compatibility only, not JSON mode or narrative quality.
[[nodiscard]] ConnectionCheckResult check_connection(ILlmProvider& provider);

} // namespace storyforge::agent
