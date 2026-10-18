#include "storyforge/validation/validation.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <sstream>
#include <type_traits>

namespace storyforge {
namespace {

template<class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

struct Location {
    const StoryChapter* chapter{};
    const StoryScene* scene{};
    const StoryNode* node{};
    std::string path;
};

std::string key(const ChapterId& chapter, const SceneId& scene, const NodeId& node) {
    return chapter.value() + "\x1f" + scene.value() + "\x1f" + node.value();
}

void require_unique(std::set<std::string>& values, const std::string& value,
                    const std::string& path, const std::string& kind,
                    ValidationReport& report) {
    if (!values.insert(value).second)
        report.add_error("semantic.duplicate_id", path, "duplicate " + kind + " id '" + value + "'");
}

const StoryChapter* find_chapter(const StoryPackage& package, const ChapterId& id) {
    const auto it = std::find_if(package.chapters.begin(), package.chapters.end(),
                                 [&](const auto& value) { return value.id == id; });
    return it == package.chapters.end() ? nullptr : &*it;
}

const StoryScene* find_scene(const StoryChapter& chapter, const SceneId& id) {
    const auto it = std::find_if(chapter.scenes.begin(), chapter.scenes.end(),
                                 [&](const auto& value) { return value.id == id; });
    return it == chapter.scenes.end() ? nullptr : &*it;
}

const StoryNode* find_node(const StoryScene& scene, const NodeId& id) {
    const auto it = std::find_if(scene.nodes.begin(), scene.nodes.end(),
                                 [&](const auto& value) { return value.id == id; });
    return it == scene.nodes.end() ? nullptr : &*it;
}

void validate_condition(const StoryCondition& condition, const std::string& path,
                        const std::map<std::string, StoryValue>& variables,
                        const std::set<std::string>& scenes,
                        ValidationReport& report) {
    std::visit(Overloaded{
        [&](const VariableComparison& value) {
            const auto found = variables.find(value.variable.value());
            if (found == variables.end())
                report.add_error("semantic.missing_reference", path + "/variable",
                                 "unknown variable '" + value.variable.value() + "'");
            else if (found->second.index() != value.value.index())
                report.add_error("semantic.invalid_condition", path + "/value",
                                 "comparison value must match the variable type");
            else if (value.op != CompareOperator::equal && value.op != CompareOperator::not_equal &&
                     !std::holds_alternative<std::int64_t>(found->second) &&
                     !std::holds_alternative<double>(found->second))
                report.add_error("semantic.invalid_condition", path + "/op",
                                 "ordered comparison requires a numeric variable");
        },
        [&](const CurrentSceneCondition& value) {
            if (!scenes.contains(value.scene.value()))
                report.add_error("semantic.missing_reference", path + "/scene",
                                 "unknown scene '" + value.scene.value() + "'");
        },
        [&](const AllCondition& value) {
            for (std::size_t i = 0; i < value.conditions.size(); ++i) {
                if (value.conditions[i]) validate_condition(*value.conditions[i], path + "/conditions/" + std::to_string(i), variables, scenes, report);
            }
        },
        [&](const AnyCondition& value) {
            for (std::size_t i = 0; i < value.conditions.size(); ++i) {
                if (value.conditions[i]) validate_condition(*value.conditions[i], path + "/conditions/" + std::to_string(i), variables, scenes, report);
            }
        },
        [&](const NotCondition& value) {
            if (value.condition) validate_condition(*value.condition, path + "/condition", variables, scenes, report);
        },
        [](const auto&) {}
    }, condition.data);
}

void validate_effect(const StoryEffect& effect, const std::string& path,
                     const std::map<std::string, StoryValue>& variables,
                     const std::set<std::string>& scenes,
                     const std::set<std::string>& chapters,
                     ValidationReport& report) {
    std::visit(Overloaded{
        [&](const ModifyVariableEffect& value) {
            const auto found = variables.find(value.variable.value());
            if (found == variables.end())
                report.add_error("semantic.missing_reference", path + "/variable",
                                 "unknown variable '" + value.variable.value() + "'");
            else if (found->second.index() != value.value.index())
                report.add_error("semantic.invalid_effect", path + "/value",
                                 "effect value must match the variable type");
            if (value.operation == VariableOperation::add &&
                (!std::holds_alternative<std::int64_t>(value.value) &&
                 !std::holds_alternative<double>(value.value)))
                report.add_error("semantic.invalid_effect", path + "/value",
                                 "add operation requires a numeric value");
        },
        [&](const SwitchSceneEffect& value) {
            if (!scenes.contains(value.scene.value()))
                report.add_error("semantic.missing_reference", path + "/scene",
                                 "unknown scene '" + value.scene.value() + "'");
        },
        [&](const AdvanceChapterEffect& value) {
            if (!chapters.contains(value.chapter.value()))
                report.add_error("semantic.missing_reference", path + "/chapter",
                                 "unknown chapter '" + value.chapter.value() + "'");
        },
        [&](const AddGoldEffect& value) {
            if (value.amount == 0) report.add_warning("semantic.no_op", path + "/amount", "zero gold change has no effect");
        },
        [](const auto&) {}
    }, effect.data);
}

} // namespace

bool ValidationReport::valid() const noexcept { return error_count() == 0; }

std::size_t ValidationReport::error_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(),
        [](const auto& issue) { return issue.severity == IssueSeverity::error; }));
}

std::size_t ValidationReport::warning_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(),
        [](const auto& issue) { return issue.severity == IssueSeverity::warning; }));
}

void ValidationReport::add_error(std::string code, std::string path, std::string message) {
    issues.push_back({IssueSeverity::error, std::move(code), std::move(path), std::move(message)});
}

void ValidationReport::add_warning(std::string code, std::string path, std::string message) {
    issues.push_back({IssueSeverity::warning, std::move(code), std::move(path), std::move(message)});
}

void ValidationReport::append(const ValidationReport& other) {
    issues.insert(issues.end(), other.issues.begin(), other.issues.end());
}

std::string to_string(IssueSeverity severity) {
    return severity == IssueSeverity::error ? "error" : "warning";
}

ValidationReport validate_story_package(const StoryPackage& package) {
    ValidationReport report;
    if (package.format != "storyforge.package/v1")
        report.add_error("semantic.unsupported_format", "/format",
                         "expected 'storyforge.package/v1'");
    if (package.chapters.empty())
        report.add_error("semantic.empty_package", "/chapters", "at least one chapter is required");

    std::map<std::string, StoryValue> variables;
    std::set<std::string> chapter_ids;
    std::set<std::string> scene_ids;
    std::map<std::string, Location> locations;

    for (std::size_t i = 0; i < package.variables.size(); ++i) {
        const auto& variable = package.variables[i];
        if (!variables.emplace(variable.id.value(), variable.initial_value).second)
            report.add_error("semantic.duplicate_id", "/variables/" + std::to_string(i) + "/id",
                             "duplicate variable id '" + variable.id.value() + "'");
    }

    for (std::size_t ci = 0; ci < package.chapters.size(); ++ci) {
        const auto& chapter = package.chapters[ci];
        const auto chapter_path = "/chapters/" + std::to_string(ci);
        require_unique(chapter_ids, chapter.id.value(), chapter_path + "/id", "chapter", report);
        std::set<std::string> chapter_scene_ids;
        for (std::size_t si = 0; si < chapter.scenes.size(); ++si) {
            const auto& scene = chapter.scenes[si];
            const auto scene_path = chapter_path + "/scenes/" + std::to_string(si);
            require_unique(chapter_scene_ids, scene.id.value(), scene_path + "/id", "scene in chapter", report);
            require_unique(scene_ids, scene.id.value(), scene_path + "/id", "scene", report);
            std::set<std::string> node_ids;
            for (std::size_t ni = 0; ni < scene.nodes.size(); ++ni) {
                const auto& node = scene.nodes[ni];
                const auto node_path = scene_path + "/nodes/" + std::to_string(ni);
                require_unique(node_ids, node.id.value(), node_path + "/id", "node in scene", report);
                locations.emplace(key(chapter.id, scene.id, node.id), Location{&chapter, &scene, &node, node_path});
                std::set<std::string> choice_ids;
                for (std::size_t hi = 0; hi < node.choices.size(); ++hi)
                    require_unique(choice_ids, node.choices[hi].id.value(),
                                   node_path + "/choices/" + std::to_string(hi) + "/id",
                                   "choice in node", report);
                if (node.terminal && !node.choices.empty())
                    report.add_error("semantic.terminal_has_choices", node_path + "/choices",
                                     "terminal node must not contain choices");
                if (!node.terminal && node.choices.empty())
                    report.add_warning("semantic.dead_end", node_path,
                                       "non-terminal node has no choices");
            }
            if (!node_ids.contains(scene.entry_node.value()))
                report.add_error("semantic.missing_reference", scene_path + "/entry_node",
                                 "entry node '" + scene.entry_node.value() + "' is not in this scene");
        }
        if (!chapter_scene_ids.contains(chapter.entry_scene.value()))
            report.add_error("semantic.missing_reference", chapter_path + "/entry_scene",
                             "entry scene '" + chapter.entry_scene.value() + "' is not in this chapter");
    }

    for (const auto& [location_key, location] : locations) {
        (void)location_key;
        const auto& node = *location.node;
        for (std::size_t hi = 0; hi < node.choices.size(); ++hi) {
            const auto& choice = node.choices[hi];
            const auto choice_path = location.path + "/choices/" + std::to_string(hi);
            for (std::size_t i = 0; i < choice.conditions.size(); ++i)
                validate_condition(choice.conditions[i], choice_path + "/conditions/" + std::to_string(i), variables, scene_ids, report);
            for (std::size_t i = 0; i < choice.effects.size(); ++i)
                validate_effect(choice.effects[i], choice_path + "/effects/" + std::to_string(i), variables, scene_ids, chapter_ids, report);
            if (choice.target_node && find_node(*location.scene, *choice.target_node) == nullptr)
                report.add_error("semantic.missing_reference", choice_path + "/target_node",
                                 "target node '" + choice.target_node->value() + "' is not in this scene");
            const auto position_effects = std::count_if(choice.effects.begin(), choice.effects.end(), [](const StoryEffect& effect) {
                    return std::holds_alternative<SwitchSceneEffect>(effect.data) ||
                           std::holds_alternative<AdvanceChapterEffect>(effect.data);
                });
            if (position_effects + static_cast<int>(choice.target_node.has_value()) > 1)
                report.add_error("semantic.invalid_effect", choice_path,
                                 "a choice must not contain multiple position changes");
            const bool changes_position = choice.target_node.has_value() || position_effects > 0;
            if (!changes_position && !node.terminal)
                report.add_warning("semantic.choice_stays", choice_path,
                                   "choice has no target or position-changing effect");
        }
    }

    const StoryChapter* entry_chapter = find_chapter(package, package.entry.chapter);
    const StoryScene* entry_scene = entry_chapter == nullptr ? nullptr : find_scene(*entry_chapter, package.entry.scene);
    const StoryNode* entry_node = entry_scene == nullptr ? nullptr : find_node(*entry_scene, package.entry.node);
    if (entry_chapter == nullptr)
        report.add_error("semantic.invalid_entry", "/entry/chapter", "entry chapter does not exist");
    else if (entry_scene == nullptr)
        report.add_error("semantic.invalid_entry", "/entry/scene", "entry scene is not in the entry chapter");
    else if (entry_node == nullptr)
        report.add_error("semantic.invalid_entry", "/entry/node", "entry node is not in the entry scene");

    if (entry_node != nullptr) {
        std::set<std::string> reachable;
        std::deque<std::string> pending;
        pending.push_back(key(entry_chapter->id, entry_scene->id, entry_node->id));
        while (!pending.empty()) {
            const auto current_key = pending.front();
            pending.pop_front();
            if (!reachable.insert(current_key).second) continue;
            const auto found = locations.find(current_key);
            if (found == locations.end()) continue;
            const auto& location = found->second;
            for (const auto& choice : location.node->choices) {
                if (choice.target_node) pending.push_back(key(location.chapter->id, location.scene->id, *choice.target_node));
                for (const auto& effect : choice.effects) {
                    std::visit(Overloaded{
                        [&](const SwitchSceneEffect& value) {
                            for (const auto& chapter : package.chapters) {
                                if (const auto* scene = find_scene(chapter, value.scene))
                                    pending.push_back(key(chapter.id, scene->id, scene->entry_node));
                            }
                        },
                        [&](const AdvanceChapterEffect& value) {
                            if (const auto* chapter = find_chapter(package, value.chapter)) {
                                if (const auto* scene = find_scene(*chapter, chapter->entry_scene))
                                    pending.push_back(key(chapter->id, scene->id, scene->entry_node));
                            }
                        },
                        [](const auto&) {}
                    }, effect.data);
                }
            }
        }
        for (const auto& [node_key, location] : locations) {
            if (!reachable.contains(node_key))
                report.add_error("semantic.unreachable_node", location.path,
                                 "node '" + location.node->id.value() + "' is unreachable from the package entry");
        }
    }
    return report;
}

} // namespace storyforge
