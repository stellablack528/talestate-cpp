#include "storyforge/agent/connection_check.hpp"

#include <variant>

namespace storyforge::agent {

ConnectionCheckResult check_connection(ILlmProvider& provider) {
    ChatRequest request;
    request.messages.push_back({MessageRole::user, "Reply with OK.", {}});
    const auto result = provider.chat(request);
    if (const auto* response = std::get_if<ChatResponse>(&result)) {
        if (response->content.empty())
            return {ConnectionCheckCode::incompatible_response,
                    "服务已响应，但没有返回可用的文字内容；请检查模型是否支持普通对话。"};
        return {ConnectionCheckCode::connected,
                "基础连接成功：这组地址、Key 和模型可以完成对话请求。"};
    }

    const auto& failure = std::get<ProviderError>(result);
    switch (failure.code) {
    case ProviderErrorCode::invalid_configuration:
        return {ConnectionCheckCode::invalid_configuration,
                "配置不完整或地址不安全；请检查 API 地址、Key 和模型名。"};
    case ProviderErrorCode::transport_failure:
        return {ConnectionCheckCode::network_failure,
                "网络连接失败；请检查地址、网络、代理或 TLS 证书。"};
    case ProviderErrorCode::malformed_response:
        return {ConnectionCheckCode::incompatible_response,
                "服务已响应，但返回格式不是兼容的对话结果。"};
    case ProviderErrorCode::invalid_request:
        return {ConnectionCheckCode::request_rejected,
                "连接测试请求未能构造成功；请检查客户端配置。"};
    case ProviderErrorCode::provider_rejected:
        break;
    }

    const int status = failure.http_status.value_or(0);
    if (status == 401 || status == 403)
        return {ConnectionCheckCode::authentication_failure,
                "认证或权限失败；请检查 API Key 是否有效、是否有权使用该模型。"};
    if (status == 404)
        return {ConnectionCheckCode::model_or_endpoint_failure,
                "模型或接口未找到；请检查模型名，以及 API 地址是否以正确的版本路径结尾。"};
    if (status == 400 || status == 422)
        return {ConnectionCheckCode::model_or_endpoint_failure,
                "模型或接口拒绝了基础对话请求；请检查模型名和兼容协议。"};
    if (status == 408)
        return {ConnectionCheckCode::network_failure,
                "模型服务响应超时；请稍后重试并检查网络。"};
    if (status == 429)
        return {ConnectionCheckCode::rate_limited,
                "服务限流或额度不足；请检查账户额度并稍后重试。"};
    if (status >= 500)
        return {ConnectionCheckCode::provider_unavailable,
                "模型服务暂时不可用；请稍后重试。"};
    return {ConnectionCheckCode::request_rejected,
            "模型服务拒绝了请求；请检查服务商的接口配置。"};
}

} // namespace storyforge::agent
