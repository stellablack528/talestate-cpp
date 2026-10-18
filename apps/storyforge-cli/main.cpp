#include "storyforge/io/story_loader.hpp"
#include "storyforge/runtime/story_runtime.hpp"

#include <charconv>
#include <filesystem>
#include <iostream>
#include <string>
#include <variant>

#include <CLI/CLI.hpp>

namespace {

void print_issues(const storyforge::ValidationReport& report) {
    for (const auto& issue : report.issues) {
        std::cerr << storyforge::to_string(issue.severity) << " [" << issue.code << "] ";
        if (!issue.path.empty()) std::cerr << issue.path << ": ";
        std::cerr << issue.message << '\n';
    }
}

void print_inventory(const storyforge::StoryState& state) {
    std::cout << "背包：";
    if (state.inventory.empty()) {
        std::cout << "空\n";
        return;
    }
    bool first = true;
    for (const auto& [item, quantity] : state.inventory) {
        if (!first) std::cout << "，";
        std::cout << item.value() << " ×" << quantity;
        first = false;
    }
    std::cout << '\n';
}

int play_story(const storyforge::StoryPackage& package) {
    auto state = storyforge::make_initial_state(package);
    std::cout << "开始故事：《" << package.title << "》\n";

    while (true) {
        const auto* scene = storyforge::current_scene(package, state);
        const auto* node = storyforge::current_node(package, state);
        if (scene == nullptr || node == nullptr) {
            std::cerr << "运行错误：当前位置不存在\n";
            return 1;
        }

        std::cout << "\n【" << scene->title << "】\n" << node->text << "\n";
        print_inventory(state);
        if (node->terminal) {
            std::cout << "故事结束。\n";
            return 0;
        }

        for (std::size_t i = 0; i < node->choices.size(); ++i) {
            const auto& choice = node->choices[i];
            std::cout << i + 1 << ". " << choice.text;
            if (!storyforge::choice_is_available(choice, state))
                std::cout << " [条件未满足]";
            std::cout << '\n';
        }
        std::cout << "0. 退出\n请选择编号：" << std::flush;

        std::string input;
        if (!std::getline(std::cin, input) || input == "0") {
            std::cout << "已退出故事。\n";
            return 0;
        }
        std::size_t selection{};
        const auto [end, parse_error] = std::from_chars(input.data(), input.data() + input.size(), selection);
        if (parse_error != std::errc{} || end != input.data() + input.size() ||
            selection < 1 || selection > node->choices.size()) {
            std::cout << "请输入列表中的编号。\n";
            continue;
        }

        const auto result = storyforge::execute_command(
            package, state, storyforge::StoryCommand{node->choices[selection - 1].id});
        if (const auto* failure = std::get_if<storyforge::RuntimeError>(&result)) {
            std::cout << "行动失败：" << failure->message << "\n";
            continue;
        }
        for (const auto& event : std::get<storyforge::StoryDelta>(result).events)
            std::cout << "· " << event.text << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    CLI::App app{"Validate and run deterministic StoryForge story packages"};
    app.set_version_flag("--version", "StoryForge C++ 0.1.0");

    std::filesystem::path package_path;
    auto* validate = app.add_subcommand("validate", "Validate a Story Package v1 directory or JSON file");
    validate->add_option("package", package_path, "Package directory or story.json path")->required();
    validate->fallthrough(false);
    std::filesystem::path play_path;
    auto* play = app.add_subcommand("play", "Play a validated Story Package v1 in the console");
    play->add_option("package", play_path, "Package directory or story.json path")->required();
    play->fallthrough(false);
    app.require_subcommand(1);

    CLI11_PARSE(app, argc, argv);

    if (*validate) {
        const auto result = storyforge::load_story_package(package_path);
        print_issues(result.report);
        if (!result.report.valid()) {
            std::cerr << "validation failed: " << result.report.error_count() << " error(s), "
                      << result.report.warning_count() << " warning(s)\n";
            return 1;
        }
        std::cout << "valid Story Package v1: " << result.package->title << " ("
                  << result.package->id.value() << " @ " << result.package->version << ")\n";
        std::cout << result.package->chapters.size() << " chapter(s), "
                  << result.report.warning_count() << " warning(s)\n";
    } else if (*play) {
        const auto result = storyforge::load_story_package(play_path);
        print_issues(result.report);
        if (!result.report.valid() || !result.package) {
            std::cerr << "validation failed: " << result.report.error_count() << " error(s)\n";
            return 1;
        }
        return play_story(*result.package);
    }
    return 0;
}
