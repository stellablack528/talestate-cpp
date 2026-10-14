#include "storyforge/io/story_loader.hpp"

#include <filesystem>
#include <iostream>

#include <CLI/CLI.hpp>

int main(int argc, char** argv) {
    CLI::App app{"Validate and run deterministic StoryForge story packages"};
    app.set_version_flag("--version", "StoryForge C++ 0.1.0");

    std::filesystem::path package_path;
    auto* validate = app.add_subcommand("validate", "Validate a Story Package v1 directory or JSON file");
    validate->add_option("package", package_path, "Package directory or story.json path")->required();
    validate->fallthrough(false);
    app.require_subcommand(1);

    CLI11_PARSE(app, argc, argv);

    if (*validate) {
        const auto result = storyforge::load_story_package(package_path);
        for (const auto& issue : result.report.issues) {
            std::cerr << storyforge::to_string(issue.severity) << " [" << issue.code << "] ";
            if (!issue.path.empty()) std::cerr << issue.path << ": ";
            std::cerr << issue.message << '\n';
        }
        if (!result.report.valid()) {
            std::cerr << "validation failed: " << result.report.error_count() << " error(s), "
                      << result.report.warning_count() << " warning(s)\n";
            return 1;
        }
        std::cout << "valid Story Package v1: " << result.package->title << " ("
                  << result.package->id.value() << " @ " << result.package->version << ")\n";
        std::cout << result.package->chapters.size() << " chapter(s), "
                  << result.report.warning_count() << " warning(s)\n";
    }
    return 0;
}
