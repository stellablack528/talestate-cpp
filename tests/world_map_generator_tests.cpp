#include "storyforge/agent/world_map_generator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <vector>

namespace {

constexpr const char* valid_map = R"({"title":"雾海群岛","start":"harbor","locations":[{"id":"harbor","title":"码头","description":"船只和线索","x":0,"y":0},{"id":"tower","title":"灯塔","description":"观察海面","x":2,"y":1},{"id":"cave","title":"海洞","description":"隐藏的通道","x":4,"y":2}],"routes":[{"from":"harbor","to":"tower"},{"from":"tower","to":"cave"}]})";

class QueueProvider final : public storyforge::agent::ILlmProvider {
public:
    std::vector<storyforge::agent::ChatResult> responses;
    std::vector<storyforge::agent::ChatRequest> requests;
    storyforge::agent::ChatResult chat(const storyforge::agent::ChatRequest& request) override {
        requests.push_back(request);
        auto response = responses.front();
        responses.erase(responses.begin());
        return response;
    }
};

} // namespace

TEST_CASE("world map draft has reachable locations and round trips through JSON") {
    auto result = storyforge::agent::parse_world_map_draft(valid_map);
    REQUIRE(std::holds_alternative<storyforge::agent::WorldMapDraft>(result));
    const auto& draft = std::get<storyforge::agent::WorldMapDraft>(result);
    CHECK(draft.locations.size() == 3);
    auto encoded = storyforge::agent::serialize_world_map_draft(draft);
    CHECK(std::holds_alternative<storyforge::agent::WorldMapDraft>(
        storyforge::agent::parse_world_map_draft(encoded)));
}

TEST_CASE("world map rejects invalid routes and disconnected locations") {
    auto unknown = nlohmann::json::parse(valid_map);
    unknown["routes"][1]["to"] = "other";
    CHECK(std::holds_alternative<storyforge::agent::WorldMapError>(
        storyforge::agent::parse_world_map_draft(unknown.dump())));
    auto disconnected = nlohmann::json::parse(valid_map);
    disconnected["locations"].push_back({{"id", "island"}, {"title", "孤岛"},
        {"description", "无人抵达"}, {"x", 3}, {"y", 4}});
    CHECK(std::holds_alternative<storyforge::agent::WorldMapError>(
        storyforge::agent::parse_world_map_draft(disconnected.dump())));
}

TEST_CASE("world map generation repairs rejected topology without changing game state") {
    QueueProvider provider;
    provider.responses = {
        storyforge::agent::ChatResponse{"{}", std::nullopt, {}},
        storyforge::agent::ChatResponse{valid_map, std::nullopt, {}}
    };
    auto result = storyforge::agent::generate_world_map_draft("雾海上有一座灯塔和海洞", provider);
    REQUIRE(std::holds_alternative<storyforge::agent::WorldMapDraft>(result));
    CHECK(provider.requests.size() == 2);
    CHECK(provider.requests[0].response_format == storyforge::agent::ResponseFormat::json_object);
    CHECK(provider.requests[1].messages.size() == 4);
}
