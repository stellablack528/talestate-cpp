# 为什么文字 RPG 需要独立的剧情引擎

> 对应里程碑：计划中的 `v0.1.0-schema`；正式发布时替换为实际 Commit。

## 要解决的问题

当对白、选项、金币、背包和场景跳转都写进 UI 回调时，一个小故事很快就会变成
无法验证的状态机。改一句剧情可能破坏存档，换一个界面又要复制规则。更危险的是，
如果以后让生成式 AI 参与写作，它输出的内容可能绕过游戏规则。

独立引擎给出一个清楚的边界：JSON 只表达候选内容，C++ 才解释规则。CLI、Qt 或
其他宿主只展示引擎事件，不自己猜测 JSON 的含义。

## 最小示例

一个选项只描述前置条件、效果和下一节点：

```json
{
  "id": "choice.inspect",
  "text": "观察齿轮上的刻痕",
  "conditions": [],
  "effects": [
    {
      "type": "modify_variable",
      "variable": "var.observed-gears",
      "operation": "set",
      "value": true
    }
  ],
  "target_node": "node.decision"
}
```

它不包含脚本，不调用数据库，也不知道画面如何显示。加载器将其转换成强类型对象，
校验器确认变量和目标节点确实存在。

## 架构与执行流程

```text
作者 / 未来的 AI
       │  候选 JSON
       ▼
JSON 语法与结构校验
       │  强类型 StoryPackage
       ▼
C++ 语义校验 ──→ 错误报告（重复 ID、坏引用、不可达节点）
       │  已确认的包
       ▼
确定性运行时（Phase 2）
       │  StoryEvent
       ▼
CLI / Qt / 其他宿主
```

## 关键代码解释

模型用强类型 ID 区分章节、场景和节点。条件与效果各自是一个 `std::variant`：

```cpp
struct StoryEffect {
    using Data = std::variant<ModifyVariableEffect, AddGoldEffect,
                              AddItemEffect, SwitchSceneEffect,
                              CompleteEventEffect, AppendNarrativeEffect>;
    Data data;
};
```

实际项目中的变体还覆盖移除物品、推进章节和 NPC 记忆。校验器用 `std::visit`
处理具体类型，因此添加一种效果时，编译器能帮助我们发现需要更新的访问逻辑。

## 为什么这样设计

数据与执行分开后，同一个剧情包能被 CLI 测试，也能接入 Qt。强类型模型把字符串
判别限制在加载边界；进入核心后，章节 ID 不会被意外当成节点 ID。跨引用校验和
可达性遍历发生在游玩之前，错误不会等到玩家走到某条冷门分支才暴露。

更重要的是，这给 AI 设置了能力上限。AI 可以建议 JSON，但不能决定一个引用是否
有效，不能写数据库，也不能执行任意代码。候选内容只有通过确定性检查才可能发布。

## 常见错误

- 把场景跳转写成 UI 按钮回调，导致不同客户端行为不一致。
- 用一个巨大的 `if (type == "...")` 在各处解释效果，遗漏新类型。
- 解析成功就视为可玩，没有检查入口、引用和不可达节点。
- 使用全局随机数，使测试和玩家问题无法复现。
- 把 API Key 放进示例 JSON、日志或测试夹具。

## 测试方法

本阶段测试包含三层：合法原创包必须加载；损坏 JSON 和缺字段输入必须在结构阶段
被拒绝；重复 ID、缺失引用、无效入口、非法数值效果和不可达节点必须在语义阶段
被拒绝。执行：

```bash
cmake -S . -B build -DSTORYFORGE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

最后再运行 `storyforge validate examples/clockwork-academy`，验证真实示例与测试使用
的是同一条加载路径。

## 下一阶段预告

下一篇将解释如何用 JSON Schema 定义可演化的 Story Package，并区分“字段类型
正确”和“故事语义正确”。随后进入确定性运行时：从 `StoryCommand` 计算完整的
`StoryDelta`，验证后一次性提交，避免只扣了金币却没有发放物品的半完成状态。
