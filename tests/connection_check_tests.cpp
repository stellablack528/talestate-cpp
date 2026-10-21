#include "storyforge/agent/connection_check.hpp"

#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

class FakeProvider final : public storyforge::agent::ILlmProvider {
public:
    storyforge::agent::ChatResult next{storyforge::agent::ChatResponse{"OK", std::nullopt, {}}};
    storyforge::agent::ChatRequest last_request;
    int calls{0};

    storyforge::agent::ChatResult chat(const storyforge::agent::ChatRequest& request) override {
        ++calls;
        last_request = request;
        return next;
    }
};

} // namespace

TEST_CASE("connection check sends one minimal chat request without a world package") {
    FakeProvider provider;
    const auto result = storyforge::agent::check_connection(provider);
    CHECK(result.connected());
    CHECK(provider.calls == 1);
    REQUIRE(provider.last_request.messages.size() == 1);
    CHECK(provider.last_request.messages.front().role == storyforge::agent::MessageRole::user);
    CHECK(provider.last_request.response_format == storyforge::agent::ResponseFormat::text);
    CHECK_FALSE(provider.last_request.max_output_tokens.has_value());

    provider.next = storyforge::agent::ChatResponse{"", std::nullopt, {}};
    const auto empty = storyforge::agent::check_connection(provider);
    CHECK(empty.code == storyforge::agent::ConnectionCheckCode::incompatible_response);
}

TEST_CASE("connection check classifies failures without displaying provider secrets") {
    using storyforge::agent::ConnectionCheckCode;
    using storyforge::agent::ProviderError;
    using storyforge::agent::ProviderErrorCode;
    struct Case {
        ProviderError error;
        ConnectionCheckCode expected;
    };
    const std::vector<Case> cases{
        {{ProviderErrorCode::invalid_configuration, "secret-key", std::nullopt, false},
         ConnectionCheckCode::invalid_configuration},
        {{ProviderErrorCode::transport_failure, "secret-key", std::nullopt, true},
         ConnectionCheckCode::network_failure},
        {{ProviderErrorCode::provider_rejected, "secret-key", 401, false},
         ConnectionCheckCode::authentication_failure},
        {{ProviderErrorCode::provider_rejected, "secret-key", 403, false},
         ConnectionCheckCode::authentication_failure},
        {{ProviderErrorCode::provider_rejected, "secret-key", 404, false},
         ConnectionCheckCode::model_or_endpoint_failure},
        {{ProviderErrorCode::provider_rejected, "secret-key", 422, false},
         ConnectionCheckCode::model_or_endpoint_failure},
        {{ProviderErrorCode::provider_rejected, "secret-key", 429, true},
         ConnectionCheckCode::rate_limited},
        {{ProviderErrorCode::provider_rejected, "secret-key", 503, true},
         ConnectionCheckCode::provider_unavailable},
        {{ProviderErrorCode::malformed_response, "secret-key", 200, false},
         ConnectionCheckCode::incompatible_response},
    };
    for (const auto& scenario : cases) {
        FakeProvider provider;
        provider.next = scenario.error;
        const auto result = storyforge::agent::check_connection(provider);
        CHECK(result.code == scenario.expected);
        CHECK_FALSE(result.connected());
        CHECK(result.message.find("secret-key") == std::string::npos);
    }
}
