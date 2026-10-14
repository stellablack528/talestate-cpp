#include "storyforge/agent/llm_provider.hpp"

#include <memory>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

namespace {

class FakeTransport final : public storyforge::agent::IHttpTransport {
public:
    storyforge::agent::HttpRequest last_request;
    storyforge::agent::HttpResult next_result{
        storyforge::agent::HttpResponse{200, {{"X-Request-Id", "request-42"}},
                                       R"({"choices":[{"message":{"content":"{\"action\":\"wait\"}"}}],"usage":{"prompt_tokens":12,"completion_tokens":5,"total_tokens":17}})"}};
    int calls{0};

    storyforge::agent::HttpResult send(const storyforge::agent::HttpRequest& request) override {
        ++calls;
        last_request = request;
        return next_result;
    }
};

storyforge::agent::ProviderConfig valid_config() {
    return {"https://models.example.test/v1", "secret-test-key", "example-model",
            std::chrono::milliseconds{12345}};
}

} // namespace

TEST_CASE("OpenAI-compatible provider builds a structured BYOK request") {
    auto transport = std::make_shared<FakeTransport>();
    storyforge::agent::OpenAiCompatibleProvider provider{valid_config(), transport};

    storyforge::agent::ChatRequest request;
    request.messages = {{storyforge::agent::MessageRole::system, "Return a legal command", {}},
                        {storyforge::agent::MessageRole::user, "I wait", {}}};
    request.response_format = storyforge::agent::ResponseFormat::json_object;
    request.temperature = 0.2;
    request.max_output_tokens = 256;

    const auto result = provider.chat(request);
    REQUIRE(std::holds_alternative<storyforge::agent::ChatResponse>(result));
    const auto& response = std::get<storyforge::agent::ChatResponse>(result);
    CHECK(response.content == R"({"action":"wait"})");
    CHECK(response.provider_request_id == "request-42");
    CHECK(response.usage.total_tokens == 17);

    CHECK(transport->calls == 1);
    CHECK(transport->last_request.url == "https://models.example.test/v1/chat/completions");
    CHECK(transport->last_request.headers.at("Authorization") == "Bearer secret-test-key");
    CHECK(transport->last_request.timeout == std::chrono::milliseconds{12345});

    const auto body = nlohmann::json::parse(transport->last_request.body);
    CHECK(body["model"] == "example-model");
    CHECK(body["messages"].size() == 2);
    CHECK(body["response_format"]["type"] == "json_object");
    CHECK(body["max_tokens"] == 256);
}

TEST_CASE("provider diagnostics redact the user's API key") {
    const auto config = valid_config();
    const auto diagnostic = storyforge::agent::describe(config);
    CHECK(diagnostic.find(config.api_key) == std::string::npos);
    CHECK(diagnostic.find("<redacted>") != std::string::npos);
}

TEST_CASE("remote plaintext provider URLs are rejected before networking") {
    auto transport = std::make_shared<FakeTransport>();
    auto config = valid_config();
    config.base_url = "http://models.example.test/v1";
    storyforge::agent::OpenAiCompatibleProvider provider{config, transport};

    storyforge::agent::ChatRequest request;
    request.messages = {{storyforge::agent::MessageRole::user, "hello", {}}};
    const auto result = provider.chat(request);

    REQUIRE(std::holds_alternative<storyforge::agent::ProviderError>(result));
    CHECK(std::get<storyforge::agent::ProviderError>(result).code ==
          storyforge::agent::ProviderErrorCode::invalid_configuration);
    CHECK(transport->calls == 0);
}

TEST_CASE("provider errors preserve retry guidance without exposing credentials") {
    auto transport = std::make_shared<FakeTransport>();
    transport->next_result = storyforge::agent::HttpResponse{
        429, {}, R"({"error":{"message":"rate limit reached for secret-test-key"}})"};
    const auto config = valid_config();
    storyforge::agent::OpenAiCompatibleProvider provider{config, transport};
    storyforge::agent::ChatRequest request;
    request.messages = {{storyforge::agent::MessageRole::user, "hello", {}}};

    const auto result = provider.chat(request);
    REQUIRE(std::holds_alternative<storyforge::agent::ProviderError>(result));
    const auto& error = std::get<storyforge::agent::ProviderError>(result);
    CHECK(error.code == storyforge::agent::ProviderErrorCode::provider_rejected);
    CHECK(error.http_status == 429);
    CHECK(error.retryable);
    CHECK(error.message == "rate limit reached for <redacted>");
    CHECK(error.message.find(config.api_key) == std::string::npos);
}
