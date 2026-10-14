#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace storyforge::agent {

enum class MessageRole { system, user, assistant, tool };

struct ChatMessage {
    MessageRole role{MessageRole::user};
    std::string content;
    std::optional<std::string> name;
};

enum class ResponseFormat { text, json_object };

struct ChatRequest {
    std::vector<ChatMessage> messages;
    ResponseFormat response_format{ResponseFormat::text};
    std::optional<double> temperature;
    std::optional<std::size_t> max_output_tokens;
};

struct TokenUsage {
    std::size_t input_tokens{0};
    std::size_t output_tokens{0};
    std::size_t total_tokens{0};
};

struct ChatResponse {
    std::string content;
    std::optional<std::string> provider_request_id;
    TokenUsage usage;
};

enum class ProviderErrorCode {
    invalid_configuration,
    invalid_request,
    transport_failure,
    provider_rejected,
    malformed_response
};

struct ProviderError {
    ProviderErrorCode code{ProviderErrorCode::transport_failure};
    std::string message;
    std::optional<int> http_status;
    bool retryable{false};
};

using ChatResult = std::variant<ChatResponse, ProviderError>;

struct ProviderConfig {
    // Example: https://api.openai.com/v1. Plain HTTP is only accepted for loopback hosts.
    std::string base_url;
    std::string api_key;
    std::string model;
    std::chrono::milliseconds timeout{60000};
};

struct ConfigurationIssue {
    std::string field;
    std::string message;
};

[[nodiscard]] std::vector<ConfigurationIssue> validate(const ProviderConfig& config);

// Safe for diagnostics and UI. The API key is never included.
[[nodiscard]] std::string describe(const ProviderConfig& config);

struct HttpRequest {
    std::string method;
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    std::chrono::milliseconds timeout{60000};
    std::size_t max_response_bytes{4U * 1024U * 1024U};
};

struct HttpResponse {
    int status{0};
    std::map<std::string, std::string> headers;
    std::string body;
};

struct TransportError {
    std::string message;
    bool retryable{true};
};

using HttpResult = std::variant<HttpResponse, TransportError>;

// The host application owns networking. Implementations may use Qt Network,
// libcurl, WinHTTP, or another HTTPS stack without coupling it to the story engine.
class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    [[nodiscard]] virtual HttpResult send(const HttpRequest& request) = 0;
};

class ILlmProvider {
public:
    virtual ~ILlmProvider() = default;
    [[nodiscard]] virtual ChatResult chat(const ChatRequest& request) = 0;
};

// Adapter for the widely supported POST /chat/completions protocol. A user's
// provider URL, model, and API key are supplied at runtime (BYOK).
class OpenAiCompatibleProvider final : public ILlmProvider {
public:
    OpenAiCompatibleProvider(ProviderConfig config, std::shared_ptr<IHttpTransport> transport);

    [[nodiscard]] ChatResult chat(const ChatRequest& request) override;

private:
    ProviderConfig config_;
    std::shared_ptr<IHttpTransport> transport_;
};

[[nodiscard]] const char* to_string(MessageRole role) noexcept;
[[nodiscard]] const char* to_string(ProviderErrorCode code) noexcept;

} // namespace storyforge::agent
