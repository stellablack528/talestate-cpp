#include "storyforge/io/story_loader.hpp"

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

namespace {
std::filesystem::path source_path(const std::string& value) {
    return std::filesystem::path{STORYFORGE_SOURCE_DIR} / value;
}
}

TEST_CASE("the Clockwork Academy example loads and validates") {
    const auto result = storyforge::load_story_package(source_path("examples/clockwork-academy"));
    REQUIRE(result.package.has_value());
    CHECK(result.report.valid());
    CHECK(result.package->id.value() == "clockwork-academy");
    CHECK(result.package->chapters.size() == 1);
}

TEST_CASE("malformed JSON is rejected") {
    const auto result = storyforge::load_story_package(source_path("tests/fixtures/malformed.json"));
    CHECK_FALSE(result.package.has_value());
    REQUIRE(result.report.error_count() == 1);
    CHECK(result.report.issues.front().code == "json.parse");
}

TEST_CASE("missing required properties are rejected structurally") {
    const auto result = storyforge::load_story_package(source_path("tests/fixtures/missing-required.json"));
    CHECK_FALSE(result.package.has_value());
    CHECK_FALSE(result.report.valid());
    CHECK(result.report.error_count() >= 1);
}

TEST_CASE("unknown properties and malformed identifiers are rejected") {
    const auto result = storyforge::load_story_package(source_path("tests/fixtures/invalid-shape.json"));
    CHECK_FALSE(result.package.has_value());
    CHECK_FALSE(result.report.valid());
    CHECK(result.report.error_count() >= 3);
}
