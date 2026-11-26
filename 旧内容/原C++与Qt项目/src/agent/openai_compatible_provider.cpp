#include "storyforge/agent/llm_provider.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

namespace storyforge::agent {
namespace {

constexpr std::size_t max_request_bytes = 2U * 1024U * 1024U;

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool is_loopback_http(const std::string& url) {
    constexpr auto prefix = "http://";
    if (!starts_with(url, prefix)) return false;

    const auto authority_start = std::char_traits<char>::length(prefix);
    const auto authority_end = url.find('/', authority_start);
    const auto authority = url.substr(authority_start, authority_end - authority_start);
    return authority == "localhost" || starts_with(authority, "localhost:") ||
           authority == "127.0.0.1" || starts_with(authority, "127.0.0.1:") ||
           authority == "[::1]" || starts_with(authority, "[::1]:");
}

std::string endpoint_for(std::string base_url) {
    while (!base_url.empty() && base_url.back() == '/') base_url.pop_back();
    return base_url + "/chat/completions";
}

std::string redact_secret(std::string value, const std::string& secret) {
    if (secret.empty()) return value;
    std::size_t position = 0;
    while ((position = value.find(secret, position)) != std::string::npos) {
        value.replace(position, secret.size(), "<redacted>");
        position += std::char_traits<char>::length("<redacted>");
    }
    return value;
}

std::optional<std::string> header_value(const std::map<std::string, std::string>& headers,
                                        const std::string& wanted) {
    const auto found = std::find_if(headers.begin(), headers.end(), [&](const auto& header) {
        if (header.first.size() != wanted.size()) return false;
        return std::equal(header.first.begin(), header.first.end(), wanted.begin(),
                          [](char left, char right) {
                              return std::tolower(static_cast<unsigned char>(left)) ==
                                     std::tolower(static_cast<unsigned char>(right));
                          });
    });
    if (found == headers.end()) return std::nullopt;
    return found->second;
}

std::string provider_error_message(const HttpResponse& response) {
    try {
        const auto payload = nlohmann::json::parse(response.body);
        if (payload.contains("error") && payload["error"].is_object() &&
            payload["error"].contains("message") && payload["error"]["message"].is_string()) {
            return payload["error"]["message"].get<std::string>();
        }
    } catch (const nlohmann::json::exception&) {
        // Fall through to a status-only diagnostic; arbitrary provider bodies are not echoed.
    }
    return "provider returned HTTP " + std::to_string(response.status);
}

std::size_t optional_size(const nlohmann::json& object, const char* key) {
    if (!object.contains(key) || !object[key].is_number_unsigned()) return 0;
    return object[key].get<std::size_t>();
}

} // namespace

std::vector<ConfigurationIssue> validate(const ProviderConfig& config) {
    std::vector<ConfigurationIssue> issues;
    if (config.base_url.empty()) {
        issues.push_back({"base_url", "provider base URL is required"});
    } else if (!starts_with(config.base_url, "https://") && !is_loopback_http(config.base_url)) {
        issues.push_back({"base_url", "HTTPS is required except for localhost model servers"});
    } else if (config.base_url.find('?') != std::string::npos ||
               config.base_url.find('#') != std::string::npos) {
        issues.push_back({"base_url", "provider base URL must not contain a query or fragment"});
    }
    if (config.api_key.empty()) issues.push_back({"api_key", "API key is required"});
    if (config.model.empty()) issues.push_back({"model", "model name is required"});
    if (config.timeout.count() <= 0) issues.push_back({"timeout", "timeout must be positive"});
    return issues;
}

std::string describe(const ProviderConfig& config) {
    std::ostringstream output;
    output << "OpenAI-compatible provider(base_url=" << config.base_url << ", model=" << config.model
           << ", api_key=<redacted>, timeout_ms=" << config.timeout.count() << ')';
    return output.str();
}

OpenAiCompatibleProvider::OpenAiCompatibleProvider(
    ProviderConfig config, std::shared_ptr<IHttpTransport> transport)
    : config_(std::move(config)), transport_(std::move(transport)) {}

ChatResult OpenAiCompatibleProvider::chat(const ChatRequest& request) {
    const auto issues = validate(config_);
    if (!issues.empty()) {
        return ProviderError{ProviderErrorCode::invalid_configuration,
                             issues.front().field + ": " + issues.front().message, std::nullopt,
                             false};
    }
    if (!transport_) {
        return ProviderError{ProviderErrorCode::invalid_configuration,
                             "HTTP transport is not configured", std::nullopt, false};
    }
    if (request.messages.empty()) {
        return ProviderError{ProviderErrorCode::invalid_request,
                             "at least one chat message is required", std::nullopt, false};
    }
    if (request.temperature && (*request.temperature < 0.0 || *request.temperature > 2.0)) {
        return ProviderError{ProviderErrorCode::invalid_request,
                             "temperature must be between 0 and 2", std::nullopt, false};
    }

    nlohmann::json payload{{"model", config_.model}, {"messages", nlohmann::json::array()}};
    for (const auto& message : request.messages) {
        nlohmann::json item{{"role", to_string(message.role)}, {"content", message.content}};
        if (message.name) item["name"] = *message.name;
        payload["messages"].push_back(std::move(item));
    }
    if (request.response_format == ResponseFormat::json_object) {
        payload["response_format"] = {{"type", "json_object"}};
    }
    if (request.temperature) payload["temperature"] = *request.temperature;
    if (request.max_output_tokens) payload["max_tokens"] = *request.max_output_tokens;

    const auto body = payload.dump();
    if (body.size() > max_request_bytes) {
        return ProviderError{ProviderErrorCode::invalid_request,
                             "serialized chat request exceeds the 2 MiB safety limit", std::nullopt,
                             false};
    }

    HttpRequest http_request;
    http_request.method = "POST";
    http_request.url = endpoint_for(config_.base_url);
    http_request.headers = {{"Accept", "application/json"},
                            {"Authorization", "Bearer " + config_.api_key},
                            {"Content-Type", "application/json"}};
    http_request.body = body;
    http_request.timeout = config_.timeout;

    const auto result = transport_->send(http_request);
    if (const auto* error = std::get_if<TransportError>(&result)) {
        return ProviderError{ProviderErrorCode::transport_failure,
                             redact_secret(error->message, config_.api_key), std::nullopt,
                             error->retryable};
    }

    const auto& response = std::get<HttpResponse>(result);
    if (response.body.size() > http_request.max_response_bytes) {
        return ProviderError{ProviderErrorCode::malformed_response,
                             "provider response exceeds the 4 MiB safety limit", response.status,
                             false};
    }
    if (response.status < 200 || response.status >= 300) {
        const bool retryable = response.status == 408 || response.status == 429 ||
                               response.status >= 500;
        return ProviderError{ProviderErrorCode::provider_rejected,
                             redact_secret(provider_error_message(response), config_.api_key),
                             response.status, retryable};
    }

    try {
        const auto response_json = nlohmann::json::parse(response.body);
        if (!response_json.contains("choices") || !response_json["choices"].is_array() ||
            response_json["choices"].empty() ||
            !response_json["choices"][0].contains("message") ||
            !response_json["choices"][0]["message"].contains("content") ||
            !response_json["choices"][0]["message"]["content"].is_string()) {
            return ProviderError{ProviderErrorCode::malformed_response,
                                 "provider response is missing choices[0].message.content",
                                 response.status, false};
        }

        ChatResponse chat_response;
        chat_response.content =
            response_json["choices"][0]["message"]["content"].get<std::string>();
        chat_response.provider_request_id = header_value(response.headers, "x-request-id");
        if (response_json.contains("usage") && response_json["usage"].is_object()) {
            const auto& usage = response_json["usage"];
            chat_response.usage.input_tokens = optional_size(usage, "prompt_tokens");
            chat_response.usage.output_tokens = optional_size(usage, "completion_tokens");
            chat_response.usage.total_tokens = optional_size(usage, "total_tokens");
        }
        return chat_response;
    } catch (const nlohmann::json::exception&) {
        return ProviderError{ProviderErrorCode::malformed_response,
                             "provider returned malformed JSON", response.status, false};
    }
}

const char* to_string(MessageRole role) noexcept {
    switch (role) {
    case MessageRole::system: return "system";
    case MessageRole::user: return "user";
    case MessageRole::assistant: return "assistant";
    case MessageRole::tool: return "tool";
    }
    return "user";
}

const char* to_string(ProviderErrorCode code) noexcept {
    switch (code) {
    case ProviderErrorCode::invalid_configuration: return "invalid_configuration";
    case ProviderErrorCode::invalid_request: return "invalid_request";
    case ProviderErrorCode::transport_failure: return "transport_failure";
    case ProviderErrorCode::provider_rejected: return "provider_rejected";
    case ProviderErrorCode::malformed_response: return "malformed_response";
    }
    return "transport_failure";
}

} // namespace storyforge::agent
