#include "storyforge/agent/world_setting.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace {
class ClassificationProvider final : public storyforge::agent::ILlmProvider {
public:
    std::vector<std::string> excerpts;
    bool secret_in_summary{false};
    storyforge::agent::ChatResult chat(const storyforge::agent::ChatRequest& request) override {
        if (request.messages.front().content.find("Classify this world-setting") != std::string::npos) {
            excerpts.push_back(request.messages[1].content);
            return storyforge::agent::ChatResponse{
                nlohmann::json{{"facts", {"校区有三栋楼"}}, {"rumors", {"传闻有密道"}},
                    {"secrets", {"校长藏着钥匙"}}, {"rules", {"一天只有两次行动"}},
                    {"style", {"克制的文风"}}}.dump(), {}, {}};
        }
        if (request.messages[1].content.find("校长藏着钥匙") != std::string::npos)
            secret_in_summary = true;
        return storyforge::agent::ChatResponse{
            R"({"brief":"校园有三栋楼；玩家每天最多行动两次；传闻密道尚未证实。"})", {}, {}};
    }
};
}

TEST_CASE("long setting is classified in full and secrets stay outside playable brief") {
    ClassificationProvider provider;
    const std::string document = "开头标记\n" + std::string(8500, 'x') + "\n结尾标记";
    auto result = storyforge::agent::prepare_world_setting(document, provider);
    REQUIRE(std::holds_alternative<storyforge::agent::WorldSetting>(result));
    const auto& setting = std::get<storyforge::agent::WorldSetting>(result);
    REQUIRE(provider.excerpts.size() >= 4);
    CHECK(provider.excerpts.front().find("开头标记") != std::string::npos);
    CHECK(provider.excerpts.back().find("结尾标记") != std::string::npos);
    CHECK(setting.sections[2].size() == provider.excerpts.size());
    CHECK(setting.compact_brief.size() < 4000);
    CHECK_FALSE(provider.secret_in_summary);
    CHECK(setting.compact_brief.find("校长藏着钥匙") == std::string::npos);
}
