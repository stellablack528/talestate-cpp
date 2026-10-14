# StoryForge C++

StoryForge C++ 是一个使用现代 C++ 开发的、数据驱动且确定性的文字 RPG
剧情引擎。它将“内容”与“规则执行”分开：作者用 JSON 描述世界和分支，C++
库负责解析、校验，并在后续版本中原子地推进状态。

当前版本是 **Phase 0 + Phase 1 的最小实现**，包含 Story Package v1、强类型
数据模型、JSON 加载器、结构与语义校验器、`storyforge validate`、原创示例和
自动测试。尚未实现游玩运行时、持久化、Qt 适配器和 AI 生成。

## 安全边界

```text
用户世界设定 → 候选剧情 JSON → 结构校验 → C++ 语义校验
             → 自动模拟（后续阶段）→ 人工确认 → 可游玩剧情包
```

剧情 JSON 始终被视为不可信数据。它不能执行代码，不能直接修改数据库，也不能
绕过 C++ 规则。核心库不依赖 Qt、MySQL 或任何 AI 厂商。

## 构建

需要 CMake 3.24+ 和支持 C++20 的编译器。首次配置会下载固定版本的
nlohmann/json、CLI11 和 Catch2。

```bash
cmake -S . -B build -DSTORYFORGE_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Linux 和单配置生成器通常在 `build/storyforge` 生成程序；Visual Studio 等多配置
生成器通常在 `build/Release/storyforge.exe` 生成程序。

## 校验剧情包

```bash
storyforge validate examples/clockwork-academy
# 也可以直接传入 examples/clockwork-academy/story.json
```

成功时退出码为 `0`。结构错误、缺失引用、重复 ID、无效入口、非法效果或不可达
节点会产生错误并返回非零退出码。警告不会使校验失败。

原创示例“钟塔学院：失准的晨钟”有两个调查分支和两个结局，仅用于展示数据格式，
不依赖任何既有作品或 IP。

## 项目结构

```text
include/storyforge/       公共 C++ API 和通用类型
src/                      JSON 加载与语义校验实现
apps/storyforge-cli/      统一命令行入口
schemas/                  Story Package JSON Schema
examples/                 原创剧情包
tests/                    单元测试与错误输入
docs/format/              格式说明
docs/blog/                中文教学文章草稿
```

## Story Package v1

包入口由 `chapter + scene + node` 三元组确定。章节拥有场景，场景拥有节点，节点
拥有选项。条件与效果用带 `type` 判别字段的数据表示，在 C++ 中分别解析为
`std::variant`，而不是在运行时散布字符串判断。

完整字段说明见 [Story Package v1](docs/format/story-package-v1.md)，机器可读约束见
[JSON Schema](schemas/story-package-v1.schema.json)。

## 当前限制与路线

- Phase 1 加载器执行与 Schema 等价的核心结构检查，但尚未嵌入通用 JSON Schema
  引擎；发布管线可另外使用标准 Schema 工具校验 `schemas/` 中的文件。
- 可达性分析只把显式 `target_node`、`switch_scene` 和 `advance_chapter` 视为边，
  暂不证明条件是否可满足。
- `storyforge play`、确定性运行时和 `StoryDelta` 原子提交属于 Phase 2。
- 保存、SQLite 与迁移属于 Phase 4；AI Provider 与生成闭环属于 Phase 5。

贡献方式见 [CONTRIBUTING.md](CONTRIBUTING.md)，安全边界与报告方式见
[SECURITY.md](SECURITY.md)。项目采用 [MIT License](LICENSE)。
