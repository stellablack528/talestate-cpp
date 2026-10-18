# Story Package v1 格式

## 顶层对象

`format` 固定为 `storyforge.package/v1`。`id` 是包的稳定标识，`version` 使用
SemVer 字符串。`entry` 指向初始章节、场景和节点；`variables` 声明有类型的初始
变量；`chapters` 保存剧情树。

所有 ID 建议使用小写 ASCII，并以命名空间前缀区分，例如 `chapter.arrival`、
`scene.hall`。Schema 对 ID 的约束是 `^[a-z][a-z0-9_.-]*$`。

## 层级和引用

```text
StoryPackage
└─ Chapter (entry_scene)
   └─ Scene (entry_node)
      └─ Node
         └─ Choice ── target_node（同一 Scene 内）
```

场景 ID 在整个包内唯一。节点 ID 只要求在所属场景内唯一；选项 ID 只要求在所属
节点内唯一。`switch_scene` 和 `advance_chapter` 分别跳到目标对象声明的入口。

## 值、条件与效果

`StoryValue` 支持字符串、布尔值、64 位整数和双精度数。

条件类型：

- `variable`：变量与值比较；运算符为 `eq/ne/lt/lte/gt/gte`。
- `has_item`、`gold`、`current_scene`、`event_triggered`、`npc_memory`。
- `all`、`any` 和 `not`：递归组合条件。

效果类型：

- `modify_variable`：`set` 或数值 `add`。
- `add_gold`、`add_item`、`remove_item`。
- `switch_scene`、`advance_chapter`。
- `write_npc_memory`、`complete_event`、`append_narrative`。

条件和效果在 C++ 中解析为 `std::variant`。`type` 只存在于 JSON 边界，核心逻辑
可使用 `std::visit` 做穷尽处理。

运行时要求变量比较值、`set`/`add` 效果与变量声明的初始类型一致；有序比较只用于数字。一个选项只能指定一种位置跳转：`target_node`、`switch_scene` 或 `advance_chapter`。`play` 执行时先在临时状态中检查整轮效果，只有全部成功才提交；物品或金币不足、数值溢出等失败不会留下半次状态变化。终局节点没有后续行动。

## 校验层次

1. JSON 语法与输入大小（当前上限 16 MiB）。
2. 必填字段、字段类型、枚举和最小值等结构约束。
3. 格式版本、ID 唯一性、入口归属和引用存在性。
4. 效果的类型规则，例如 `add` 的值必须是数字。
5. 从包入口沿显式位置跳转做图遍历，拒绝不可达节点。

Schema 是格式的机器可读规范；C++ 加载器在创建强类型对象前实施核心结构检查，
语义校验器再处理无法单靠 JSON Schema 表达的跨引用和图约束。

## 兼容性规则

v1 校验器只接受精确的 `storyforge.package/v1`。未来不兼容的结构使用新的格式 ID；
包自身的内容修订只增加 `version`。在迁移工具出现之前，不应静默改写未知格式。
