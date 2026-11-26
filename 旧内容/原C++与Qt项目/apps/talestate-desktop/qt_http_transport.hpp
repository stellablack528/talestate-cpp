#pragma once

#include "storyforge/agent/llm_provider.hpp"

namespace talestate_desktop {

// Construct and use on the calling thread. No credentials are logged or stored.
class QtHttpTransport final : public storyforge::agent::IHttpTransport {
public:
    [[nodiscard]] storyforge::agent::HttpResult send(
        const storyforge::agent::HttpRequest& request) override;
};

} // namespace talestate_desktop
