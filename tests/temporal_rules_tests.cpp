#include "storyforge/agent/story_agent.hpp"
#include "storyforge/io/story_loader.hpp"
#include "storyforge/io/story_save.hpp"
#include "storyforge/runtime/story_runtime.hpp"
#include "storyforge/validation/validation.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <optional>
#include <filesystem>
#include <utility>
#include <vector>

namespace {
class ScriptedProvider final : public storyforge::agent::ILlmProvider {
public:
    std::vector<std::string> replies;

    storyforge::agent::ChatResult chat(const storyforge::agent::ChatRequest&) override {
        REQUIRE_FALSE(replies.empty());
        auto content = replies.front();
        replies.erase(replies.begin());
        return storyforge::agent::ChatResponse{std::move(content), std::nullopt, {}};
    }
};

storyforge::StoryPackage world(const std::string& title) {
    nlohmann::json json = {{"format", "storyforge.package/v1"}, {"id", "test-" + title},
        {"title", title}, {"version", "1.0.0"},
        {"entry", {{"chapter", "chapter.main"}, {"scene", "scene.hall"}, {"node", "node.hall"}}},
        {"variables", nlohmann::json::array()},
        {"agent_npcs", nlohmann::json::array({{{"id", "npc.host"}, {"name", "主持人"},
            {"scene", "scene.hall"}, {"initial_relationship", 0},
            {"dialogue_rules", nlohmann::json::array()}}})},
        {"agent_routes", nlohmann::json::array({
            {{"from", "scene.hall"}, {"to", "scene.room"}, {"conditions", nlohmann::json::array()}},
            {{"from", "scene.room"}, {"to", "scene.hall"}, {"conditions", nlohmann::json::array()}}})},
        {"agent_appointments", nlohmann::json::array({
            {{"id", "appointment.first"}, {"title", "第一次会面"}, {"npc", "npc.host"},
             {"scene", "scene.room"}, {"start_minute", 60}, {"duration_minutes", 30}},
            {{"id", "appointment.overlap"}, {"title", "冲突会面"}, {"npc", "npc.host"},
             {"scene", "scene.room"}, {"start_minute", 70}, {"duration_minutes", 20}},
            {{"id", "appointment.later"}, {"title", "稍后会面"}, {"npc", "npc.host"},
             {"scene", "scene.room"}, {"start_minute", 120}, {"duration_minutes", 30}}})},
        {"chapters", nlohmann::json::array({{{"id", "chapter.main"}, {"title", title},
            {"entry_scene", "scene.hall"}, {"scenes", nlohmann::json::array({
                {{"id", "scene.hall"}, {"title", "大厅"}, {"entry_node", "node.hall"},
                 {"nodes", nlohmann::json::array({{{"id", "node.hall"}, {"text", "开场"},
                     {"choices", nlohmann::json::array()}}})}},
                {{"id", "scene.room"}, {"title", "活动室"}, {"entry_node", "node.room"},
                 {"nodes", nlohmann::json::array({{{"id", "node.room"}, {"text", "活动"},
                     {"choices", nlohmann::json::array()}}})}}})}}})}};
    auto loaded = storyforge::load_story_package_text(json.dump());
    REQUIRE(loaded.report.valid());
    REQUIRE(loaded.package.has_value());
    return *loaded.package;
}

TEST_CASE("agent can turn free text into checked appointments") {
    auto package = world("garden");
    auto state = storyforge::make_initial_state(package);
    ScriptedProvider provider;
    provider.replies = {
        R"({"actions":[{"type":"book","target":"appointment.first"}]})",
        "你与主持人约好稍后见面。",
        R"({"actions":[{"type":"wait","target":"60"}]})",
        "你等到约定的时间。",
        R"({"actions":[{"type":"move","target":"scene.room"},{"type":"attend","target":"appointment.first"}]})",
        "你准时抵达并完成会面。"};
    storyforge::agent::StoryAgent agent;
    for (const auto& action : {"跟主持人约好见面", "等一个小时", "去活动室赴约"}) {
        auto result = agent.take_turn(package, state, action, provider);
        REQUIRE(std::holds_alternative<storyforge::agent::AgentTurn>(result));
    }
    CHECK(state.world_minute == 60);
    CHECK(state.appointments.at("appointment.first") == storyforge::AppointmentStatus::attended);
}

TEST_CASE("calendar skip settles deadlines then moves an NPC at a sourced beat") {
    auto package = world("school-year");
    package.calendar = storyforge::StoryCalendar{"2026-09-01", 8,
        {{"秋学期", 0, 120}}, {{"短假", 5, 6}}};
    package.agent_timeline.push_back({storyforge::EventId{"event.club-opens"},
        10080, storyforge::NpcId{"npc.host"},
        storyforge::EventId{"event.orientation"}, 1,
        "根据入学介绍，在活动室主持社团开放日", storyforge::SceneId{"scene.room"}});
    package.agent_actions.push_back({storyforge::ChoiceId{"action.orientation"},
        "参加入学介绍", {},
        {storyforge::StoryEffect{storyforge::CompleteEventEffect{
            storyforge::EventId{"event.orientation"}}},
         storyforge::StoryEffect{storyforge::ModifyNpcRelationshipEffect{
            storyforge::NpcId{"npc.host"}, 1}}}, std::nullopt});
    REQUIRE(storyforge::validate_story_package(package).valid());
    auto invalid_source = package;
    invalid_source.agent_timeline.front().source_event =
        storyforge::EventId{"event.unknown"};
    CHECK_FALSE(storyforge::validate_story_package(invalid_source).valid());
    auto without_source = storyforge::make_initial_state(package);
    without_source.npc_relationships[storyforge::NpcId{"npc.host"}] = 1;
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(
        storyforge::execute_agent_requests(package, without_source,
            {{storyforge::AgentActionKind::skip, "next"}})));
    CHECK(without_source.world_minute == 0);
    auto without_relationship = storyforge::make_initial_state(package);
    without_relationship.completed_events.insert(storyforge::EventId{"event.orientation"});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(
        storyforge::execute_agent_requests(package, without_relationship,
            {{storyforge::AgentActionKind::skip, "next"}})));
    CHECK(without_relationship.world_minute == 0);
    auto state = storyforge::make_initial_state(package);
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(
        storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::authored, "action.orientation"}})));
    const auto holiday = storyforge::calendar_at(package, 6ULL * 1440);
    REQUIRE(holiday.has_value());
    CHECK(holiday->date == "2026-09-07");
    CHECK_FALSE(holiday->school_day);
    auto appointment = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::book, "appointment.first"}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(appointment));
    storyforge::AgentActionRequest promise;
    promise.kind = storyforge::AgentActionKind::promise;
    promise.target = "npc.host";
    promise.topic = "三天后核对社团登记名单";
    promise.id = "fact.club-list";
    promise.due_minute = 3000;
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(
        storyforge::execute_agent_requests(package, state, {promise})));
    auto skip = [&] {
        return storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::skip, "next"}});
    };
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(skip()));
    CHECK(state.world_minute == 60);
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(skip()));
    CHECK(state.world_minute == 3000);
    CHECK(state.appointments.at("appointment.first") == storyforge::AppointmentStatus::missed);
    CHECK(state.dynamic_facts.at("fact.club-list").status == "broken");
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(skip()));
    CHECK(state.world_minute == 10080);
    CHECK(state.completed_events.contains(storyforge::EventId{"event.club-opens"}));
    CHECK(storyforge::npc_current_scene(package.agent_npcs.front(), state) ==
          storyforge::SceneId{"scene.room"});
    const auto moment = storyforge::calendar_at(package, state.world_minute);
    REQUIRE(moment.has_value());
    CHECK(moment->date == "2026-09-08");
    CHECK(moment->school_day);
    CHECK(moment->term == "秋学期");
    const auto saved = storyforge::serialize_story_save(package, state);
    if (const auto* failure = std::get_if<storyforge::SaveError>(&saved))
        INFO(failure->message);
    REQUIRE(std::holds_alternative<std::string>(saved));
    const auto loaded = storyforge::deserialize_story_save(package, std::get<std::string>(saved));
    REQUIRE(std::holds_alternative<storyforge::StoryState>(loaded));
    CHECK(std::get<storyforge::StoryState>(loaded) == state);
    const auto before = state;
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(skip()));
    CHECK(state == before);
}

TEST_CASE("one skip can cross seven school years without daily turns") {
    auto package = world("long-school");
    package.calendar = storyforge::StoryCalendar{"2026-09-01", 8,
        {{"七年课程", 0, 3650}}, {}};
    constexpr std::uint64_t graduation = 2557ULL * 1440;
    package.agent_timeline.push_back({storyforge::EventId{"event.graduation"},
        graduation, storyforge::NpcId{"npc.host"}, {}, -100,
        "准备毕业典礼", storyforge::SceneId{"scene.room"}});
    auto state = storyforge::make_initial_state(package);
    storyforge::AgentActionRequest promise;
    promise.kind = storyforge::AgentActionKind::promise;
    promise.target = "npc.host";
    promise.topic = "毕业前归还借用的书籍";
    promise.id = "fact.return-books";
    promise.due_minute = graduation;
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(
        storyforge::execute_agent_requests(package, state, {promise})));
    const auto jumped = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::skip, "next"}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(jumped));
    CHECK(state.turn == 2);
    CHECK(state.world_minute == graduation);
    CHECK(state.dynamic_facts.at("fact.return-books").status == "broken");
    CHECK(state.completed_events.contains(storyforge::EventId{"event.graduation"}));
    const auto moment = storyforge::calendar_at(package, state.world_minute);
    REQUIRE(moment.has_value());
    CHECK(moment->date == "2033-09-01");
    const auto saved = storyforge::serialize_story_save(package, state);
    REQUIRE(std::holds_alternative<std::string>(saved));
    const auto restored = storyforge::deserialize_story_save(package, std::get<std::string>(saved));
    REQUIRE(std::holds_alternative<storyforge::StoryState>(restored));
    CHECK(std::get<storyforge::StoryState>(restored) == state);
}
}

TEST_CASE("time promises and schedule work in two unrelated worlds") {
    for (const auto& title : {std::string{"campus"}, std::string{"monastery"}}) {
        auto package = world(title);
        auto state = storyforge::make_initial_state(package);
        auto booked = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::book, "appointment.first"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(booked));
        CHECK(state.appointments.at("appointment.first") == storyforge::AppointmentStatus::booked);
        auto original_save = storyforge::serialize_story_save(package, state);
        REQUIRE(std::holds_alternative<std::string>(original_save));
        auto forged = nlohmann::json::parse(std::get<std::string>(original_save));
        forged["state"]["appointments"]["appointment.overlap"] = "booked";
        CHECK(std::holds_alternative<storyforge::SaveError>(
            storyforge::deserialize_story_save(package, forged.dump())));
        const auto before_conflict = state;
        auto conflict = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::book, "appointment.overlap"}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(conflict));
        CHECK(state == before_conflict);
        auto later = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::book, "appointment.later"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(later));
        const auto before_wrong_place = state;
        auto wrong_place = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::attend, "appointment.first"}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(wrong_place));
        CHECK(state == before_wrong_place);
        auto waited = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::wait, "60"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(waited));
        CHECK(state.world_minute == 60);
        auto moved = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::move, "scene.room"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(moved));
        auto attended = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::attend, "appointment.first"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(attended));
        CHECK(state.appointments.at("appointment.first") == storyforge::AppointmentStatus::attended);
        auto saved = storyforge::serialize_story_save(package, state);
        REQUIRE(std::holds_alternative<std::string>(saved));
        auto restored = storyforge::deserialize_story_save(package, std::get<std::string>(saved));
        REQUIRE(std::holds_alternative<storyforge::StoryState>(restored));
        CHECK(std::get<storyforge::StoryState>(restored) == state);
        auto advanced = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::wait, "100"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(advanced));
        CHECK(state.appointments.at("appointment.later") == storyforge::AppointmentStatus::missed);
        const auto after_miss = state;
        auto repeat = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::attend, "appointment.first"}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(repeat));
        CHECK(state == after_miss);
        for (const auto& invalid : {"0", "241", "-1", "abc"}) {
            auto rejected = storyforge::execute_agent_requests(package, state,
                {{storyforge::AgentActionKind::wait, invalid}});
            REQUIRE(std::holds_alternative<storyforge::RuntimeError>(rejected));
            CHECK(state == after_miss);
        }
        auto fresh = storyforge::make_initial_state(package);
        auto rollback = storyforge::execute_agent_requests(package, fresh,
            {{storyforge::AgentActionKind::book, "appointment.first"},
             {storyforge::AgentActionKind::book, "appointment.overlap"}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(rollback));
        CHECK(fresh == storyforge::make_initial_state(package));
    }
}

TEST_CASE("dynamic facts and commitments are sourced and atomic") {
    auto loaded = storyforge::load_story_package(
        std::filesystem::path{STORYFORGE_SOURCE_DIR} / "examples" / "harbor-lantern");
    REQUIRE(loaded.package.has_value());
    auto& package = *loaded.package;
    auto state = storyforge::make_initial_state(package);
    auto lead = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::investigate, "object.tide-chart", "潮汐记录有待核对", {},
          "fact.tide-record"}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(lead));
    CHECK(state.dynamic_facts.at("fact.tide-record").kind == "lead");
    const auto before_repeat = state;
    auto repeat = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::investigate, "object.tide-chart", "潮汐记录有待核对", {},
          "fact.other-id"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(repeat));
    CHECK(state == before_repeat);
    auto promise = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::promise, "npc.alan", "明天核对潮汐记录", {},
          "fact.help-alan", 120}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(promise));
    auto saved = storyforge::serialize_story_save(package, state);
    REQUIRE(std::holds_alternative<std::string>(saved));
    auto restored = storyforge::deserialize_story_save(package, std::get<std::string>(saved));
    REQUIRE(std::holds_alternative<storyforge::StoryState>(restored));
    CHECK(std::get<storyforge::StoryState>(restored) == state);
    const auto before_invalid = state;
    auto invalid = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::ask_help, "npc.unknown", "请帮忙查证", {}, "fact.help-unknown"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(invalid));
    CHECK(state == before_invalid);
    auto fulfilled = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::fulfill, "fact.help-alan"}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(fulfilled));
    CHECK(state.dynamic_facts.at("fact.help-alan").status == "fulfilled");
    CHECK(state.event_log.size() == 3);
    auto plan = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::plan_event, "npc.alan", "一起核对航道", {},
          "fact.route-meeting", 60}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(plan));
    auto premature = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::attend_plan, "fact.route-meeting"}});
    REQUIRE(std::holds_alternative<storyforge::RuntimeError>(premature));
    auto passed = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::wait, "60"},
         {storyforge::AgentActionKind::attend_plan, "fact.route-meeting"}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(passed));
    CHECK(state.dynamic_facts.at("fact.route-meeting").status == "occurred");
    auto late = storyforge::make_initial_state(package);
    auto proposed = storyforge::execute_agent_requests(package, late,
        {{storyforge::AgentActionKind::plan_event, "npc.alan", "一起看海图", {},
          "fact.late-meeting", 60}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(proposed));
    auto elapsed = storyforge::execute_agent_requests(package, late,
        {{storyforge::AgentActionKind::wait, "120"}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(elapsed));
    CHECK(late.dynamic_facts.at("fact.late-meeting").status == "expired");
}

TEST_CASE("leads and help requests develop through authorized sources and survive saves") {
    auto loaded = storyforge::load_story_package(
        std::filesystem::path{STORYFORGE_SOURCE_DIR} / "examples" / "harbor-lantern");
    REQUIRE(loaded.package.has_value());
    auto package = *loaded.package;
    auto state = storyforge::make_initial_state(package);
    auto lead = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::investigate, "object.tide-chart", "图表时间需要复核", {},
          "fact.chart-time"}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(lead));
    auto before = state;
    storyforge::AgentActionRequest wrong{storyforge::AgentActionKind::corroborate, "fact.chart-time"};
    wrong.evidence = "object.tide-chart";
    CHECK(std::holds_alternative<storyforge::RuntimeError>(
        storyforge::execute_agent_requests(package, state, {wrong})));
    CHECK(state == before);
    wrong.evidence = "npc.alan";
    auto supported = storyforge::execute_agent_requests(package, state, {wrong});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(supported));
    CHECK(state.dynamic_facts.at("fact.chart-time").status == "corroborated");
    CHECK(state.event_log.back().source == "npc.alan");
    before = state;
    CHECK(std::holds_alternative<storyforge::RuntimeError>(
        storyforge::execute_agent_requests(package, state, {wrong})));
    CHECK(state == before);

    auto help = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::ask_help, "npc.alan", "帮忙核对图表", {}, "fact.ask-alan"}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(help));
    auto unauthorized = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::respond_help, "fact.ask-alan", "help.accept.route"}});
    CHECK(std::holds_alternative<storyforge::RuntimeError>(unauthorized));
    CHECK(state.dynamic_facts.at("fact.ask-alan").status == "open");
    const auto npc = std::find_if(package.agent_npcs.begin(), package.agent_npcs.end(),
        [](const storyforge::AgentNpc& value) { return value.id.value() == "npc.alan"; });
    REQUIRE(npc != package.agent_npcs.end());
    npc->dialogue_rules.push_back({"help.accept.route", "答应协助核对", "我会帮你核对。", {}, {}});
    auto accepted = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::respond_help, "fact.ask-alan", "help.accept.route"}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(accepted));
    CHECK(state.dynamic_facts.at("fact.ask-alan").status == "accepted");
    auto encoded = storyforge::serialize_story_save(package, state);
    REQUIRE(std::holds_alternative<std::string>(encoded));
    auto decoded = storyforge::deserialize_story_save(package, std::get<std::string>(encoded));
    REQUIRE(std::holds_alternative<storyforge::StoryState>(decoded));
    CHECK(std::get<storyforge::StoryState>(decoded) == state);
    const auto before_batch = state;
    auto rejected_batch = storyforge::execute_agent_requests(package, state,
        {{storyforge::AgentActionKind::ask_help, "npc.alan", "一起看图", {}, "fact.batch-help"},
         {storyforge::AgentActionKind::respond_help, "fact.batch-help", "help.accept.unknown"}});
    CHECK(std::holds_alternative<storyforge::RuntimeError>(rejected_batch));
    CHECK(state == before_batch);

    auto other = storyforge::make_initial_state(package);
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(
        storyforge::execute_agent_requests(package, other,
            {{storyforge::AgentActionKind::investigate, "object.tide-chart", "时间有疑点", {},
              "fact.other-chart"}})));
    storyforge::AgentActionRequest disputed{storyforge::AgentActionKind::dispute, "fact.other-chart"};
    disputed.evidence = "npc.alan";
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(
        storyforge::execute_agent_requests(package, other, {disputed})));
    CHECK(other.dynamic_facts.at("fact.other-chart").status == "disputed");
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(
        storyforge::execute_agent_requests(package, other,
            {{storyforge::AgentActionKind::ask_help, "npc.alan", "帮忙查证", {}, "fact.ask-again"}})));
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(
        storyforge::execute_agent_requests(package, other,
            {{storyforge::AgentActionKind::withdraw_help, "fact.ask-again"}})));
    CHECK(other.dynamic_facts.at("fact.ask-again").status == "withdrawn");
}

TEST_CASE("the same help resolution protocol works in unrelated worlds") {
    for (const auto& title : {std::string{"campus"}, std::string{"monastery"}}) {
        auto package = world(title);
        package.agent_npcs.front().dialogue_rules.push_back(
            {"help.decline.busy", "拒绝协助", "现在没有时间帮你。", {}, {}});
        auto state = storyforge::make_initial_state(package);
        auto request = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::ask_help, "npc.host", "请求帮忙", {}, "fact.ask-host"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(request));
        auto response = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::respond_help, "fact.ask-host", "help.decline.busy"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(response));
        CHECK(state.dynamic_facts.at("fact.ask-host").status == "declined");
        const auto before_repeat = state;
        CHECK(std::holds_alternative<storyforge::RuntimeError>(
            storyforge::execute_agent_requests(package, state,
                {{storyforge::AgentActionKind::respond_help, "fact.ask-host", "help.decline.busy"}})));
        CHECK(state == before_repeat);
    }
}

TEST_CASE("concrete promises survive saves and NPCs recall only committed content") {
    for (const auto& title : {std::string{"campus"}, std::string{"monastery"}}) {
        auto package = world(title);
        auto state = storyforge::make_initial_state(package);
        const auto initial = state;
        auto placeholder = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::promise, "npc.host", "promise", {}, "fact.empty", 120}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(placeholder));
        CHECK(state == initial);
        auto promised = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::promise, "npc.host", "明天核对活动资料", {}, "fact.check-records", 120}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(promised));
        CHECK(state.npc_memory.at(storyforge::NpcId{"npc.host"}).at("last_promise_id") ==
            storyforge::StoryValue{std::string{"fact.check-records"}});
        const auto before_duplicate = state;
        auto duplicate = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::promise, "npc.host", " 明天核对活动资料 ", {}, "fact.second-id", 150}});
        REQUIRE(std::holds_alternative<storyforge::RuntimeError>(duplicate));
        CHECK(state == before_duplicate);
        auto saved = storyforge::serialize_story_save(package, state);
        REQUIRE(std::holds_alternative<std::string>(saved));
        auto restored = storyforge::deserialize_story_save(package, std::get<std::string>(saved));
        REQUIRE(std::holds_alternative<storyforge::StoryState>(restored));
        auto resumed = std::get<storyforge::StoryState>(restored);
        auto recalled = storyforge::execute_agent_requests(package, resumed,
            {{storyforge::AgentActionKind::talk, "npc.host", "fact.check-records"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(recalled));
        const auto& events = std::get<storyforge::StoryDelta>(recalled).events;
        CHECK(std::any_of(events.begin(), events.end(), [](const storyforge::StoryEvent& event) {
            return event.type == "npc_recalled" && event.text.find("明天核对活动资料") != std::string::npos;
        }));
        CHECK(std::none_of(events.begin(), events.end(), [](const storyforge::StoryEvent& event) {
            return event.type == "npc_dialogue";
        }));
        auto question = storyforge::execute_agent_requests(package, resumed,
            {{storyforge::AgentActionKind::talk, "npc.host", "你能告诉我秘密吗"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(question));
        CHECK(std::get<storyforge::StoryDelta>(question).events.front().type == "npc_questioned");
    }
}

TEST_CASE("sleep plans a sourced next-day event and fatigue survives save and time jumps") {
    auto package = world("rest-cycle");
    auto state = storyforge::make_initial_state(package);
    for (int i = 0; i < 7; ++i) {
        const auto advanced = storyforge::execute_agent_requests(package, state,
            {{storyforge::AgentActionKind::wait, "240"}});
        REQUIRE(std::holds_alternative<storyforge::StoryDelta>(advanced));
    }
    CHECK(state.world_minute == 1680);
    CHECK(state.fatigue >= 60);
    CHECK(state.ill);
    CHECK(state.health < 100);

    storyforge::AgentActionRequest sleep{storyforge::AgentActionKind::sleep, "480"};
    sleep.daily_beats.push_back({"day.8.1", storyforge::SceneId{"scene.hall"},
        storyforge::NpcId{"npc.host"}, state.world_minute + 600,
        "主持人将在大厅发布新的活动消息", false});
    const auto slept = storyforge::execute_agent_requests(package, state, {sleep});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(slept));
    CHECK(state.world_minute == 2160);
    CHECK(state.fatigue < 60);
    CHECK_FALSE(state.ill);
    REQUIRE(state.daily_beats.size() == 1);
    CHECK_FALSE(state.daily_beats.front().occurred);

    const auto saved = storyforge::serialize_story_save(package, state);
    REQUIRE(std::holds_alternative<std::string>(saved));
    const auto loaded = storyforge::deserialize_story_save(package, std::get<std::string>(saved));
    REQUIRE(std::holds_alternative<storyforge::StoryState>(loaded));
    auto resumed = std::get<storyforge::StoryState>(loaded);
    CHECK(resumed == state);

    const auto reached = storyforge::execute_agent_requests(package, resumed,
        {{storyforge::AgentActionKind::wait, "120"}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(reached));
    CHECK(resumed.daily_beats.front().occurred);
    CHECK(std::count_if(std::get<storyforge::StoryDelta>(reached).events.begin(),
        std::get<storyforge::StoryDelta>(reached).events.end(),
        [](const storyforge::StoryEvent& event) { return event.type == "daily_beat"; }) == 1);
    const auto later = storyforge::execute_agent_requests(package, resumed,
        {{storyforge::AgentActionKind::wait, "60"}});
    REQUIRE(std::holds_alternative<storyforge::StoryDelta>(later));
    CHECK(std::none_of(std::get<storyforge::StoryDelta>(later).events.begin(),
        std::get<storyforge::StoryDelta>(later).events.end(),
        [](const storyforge::StoryEvent& event) { return event.type == "daily_beat"; }));
}

TEST_CASE("invalid next-day references roll back sleep and vitals") {
    auto package = world("invalid-day");
    auto state = storyforge::make_initial_state(package);
    const auto before = state;
    storyforge::AgentActionRequest sleep{storyforge::AgentActionKind::sleep, "480"};
    sleep.daily_beats.push_back({"day.1.1", storyforge::SceneId{"scene.hall"},
        storyforge::NpcId{"npc.missing"}, 600, "虚构人物的事件", false});
    CHECK(std::holds_alternative<storyforge::RuntimeError>(
        storyforge::execute_agent_requests(package, state, {sleep})));
    CHECK(state == before);
}

TEST_CASE("awake-day events require a real day boundary and preserve state on rejection") {
    auto package = world("invalid-awake-day");
    auto state = storyforge::make_initial_state(package);
    const auto before = state;
    storyforge::AgentActionRequest wait{storyforge::AgentActionKind::wait, "60"};
    wait.daily_beats.push_back({"day.1.1", storyforge::SceneId{"scene.hall"},
        storyforge::NpcId{"npc.host"}, 180, "主持人将在大厅公布消息", false});
    CHECK(std::holds_alternative<storyforge::RuntimeError>(
        storyforge::execute_agent_requests(package, state, {wait})));
    CHECK(state == before);
}
