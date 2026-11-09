# Story Package v1 格式

## 顶层对象

`format` 固定为 `storyforge.package/v1`。`id` 是包的稳定标识，`version` 使用
SemVer 字符串。`entry` 指向初始章节、场景和节点；`variables` 声明有类型的初始
变量；`chapters` 保存剧情树。可选的 `world_context` 描述世界设定；可选的
`agent_actions` 声明创作者预设的行动能力；可选的 `agent_routes` 声明场景间可走路线，
`agent_npcs` 声明可交谈 NPC 的位置、可选的对话规则及 `initial_relationship`（-100 至 100，默认 0）；`agent_objects` 声明可观察对象及可拿取物品；`agent_item_rules` 声明物品与 NPC 或现场对象的互动；`agent_appointments` 声明可预约的 NPC 日程。

`world_lore` 可按 `facts`、`rumors`、`secrets`、`rules`、`style` 保存长设定的分类结果。生成时会把非秘密部分压缩进 `world_context`，供开局和普通回合使用；`secrets` 留在世界包中，当前版本不会自动按 NPC 知情范围开放。创作者仍需通过受条件控制的对话或事件规则安排揭示。

`player_roles` 可选。每个身份至少包含 `id`、`name`、`description`、`attributes` 和 `starting_items`；可增加 `item_names`、`fixed_name`、`starting_gold`、`editable_fields`、`traits`、`trait_count`。设置 `fixed_name` 后角色姓名不可改；`editable_fields` 是建角时必须填写的标签；玩家须恰好选择 `trait_count` 项特质。起始金币与背包在确认角色时写入权威状态。属性初始值写入对应的 `var.attribute.<属性ID>` 变量；之后角色卡、行动判定与读档都读取这一份实时变量。

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

### Agent 行动能力

`agent_actions` 沿用选项的 `id`、`text`、`conditions`、`effects` 和可选
`target_node` 结构，但它们是整个世界包可调用的规则，不显示为固定菜单。
Agent 以结构化请求提交 `type`、`target` 和交谈时的 `topic`。`type` 可以是：

- `authored`：`target` 为 `agent_actions` 中的 ID，执行创作者声明的效果。
- `move`：`target` 为目标场景 ID；只允许沿 `agent_routes` 中从当前场景出发且条件满足的路线移动。
- `talk`：`target` 为 `agent_npcs` 中当前场景内的 NPC ID，`topic` 为话题（1–256 字节单行文字）。普通自由话题只记录 `last_discussed_topic`；若 `topic` 精确等于该 NPC 的 `dialogue_rules[].id`，引擎先检查条件，再提交创作者批准的回复和效果。条件不满足则拒绝整个回合，不能退化成普通闲聊绕过。
- `inspect`：`target` 为当前场景中 `agent_objects` 的 ID；返回创作者写的观察描述，不增加物品。
- `take`：`target` 为当前场景中带 `item` 字段的对象 ID；引擎把一件物品加入背包并记下对象已被拿走。不可拿取、已被拿走或不在场的对象都会被拒绝。
- `give`：`item` 为背包物品 ID，`target` 为当前地点 NPC ID。必须有匹配的 `agent_item_rules`；成功后转交一件物品，再执行创作者声明的效果。
- `use`：`item` 为背包物品 ID，`target` 为当前地点对象 ID。必须有匹配的 `agent_item_rules`；`consume: true` 时消耗一件物品，默认保留。
- `wait`：`target` 是 `1` 至 `240` 的整数字符串，表示经过多少分钟。已预约但错过结束时间的约定会标为失约。
- `book`：`target` 为 `agent_appointments` 中的 ID。玩家须与指定 NPC 同地，且尚未到约定开始时间；已有预约不能与它重叠，也不能重复预约。
- `attend`：`target` 为已预约的约定 ID。玩家须在指定地点，并处于约定的开始与结束时间之间；完成后记录已赴约，不能重复完成。
- `investigate`：`target` 为当前地点对象 ID，`id` 是新的 `fact.*` ID，`topic` 是短线索。引擎只确认玩家从这个对象进行了调查；线索标为**待核实**，不能当作已证明的真相。
- `promise`：`target` 为在场 NPC ID，`id` 为新的事实 ID，`topic` 为承诺内容，`due_minute` 为开局起算的未来期限。到期未履约记为违约。
- `fulfill` / `break_promise`：`target` 为已有且进行中的承诺事实 ID。履约要求与承诺对象同地；已处理的承诺不能重复领奖或反复处理。
- `ask_help`：向在场 NPC 记录求助请求。请求本身不代表 NPC 已答应，也不改变资源。
- `plan_event`：提出一项与在场 NPC 有关的计划；它先作为待执行计划保存，不代表 NPC 已经参加。
- `attend_plan`：在计划时间起 60 分钟内与相关 NPC 同场，才将计划记录为已发生；错过时间窗口则过期。

新事实必须具有唯一的 `fact.*` ID、现场来源和不超过 280 字节的单行描述。动态事实与事件记录进入权威状态和存档；整个行动组合失败时全部回滚。当前 C++ 校验来源、身份、时间与重复项，不能从自然语言中证明任意新剧情陈述为真，因此调查内容只能作为待核实线索。

时间从开局的第 0 分钟开始。第一版只有成功执行 `wait` 才推进时间；移动、交谈等普通行动尚无自动耗时。世界包中的约定使用从开局起算的分钟，`start_minute` 取 1 至 10080，`duration_minutes` 取 1 至 240，且结束时间不超过 10080。约定状态和当前时间随游戏进度保存；API Key 不进入存档。

```json
"agent_appointments": [
  { "id": "appointment.meet-alan", "title": "与阿澜核对潮汐记录",
    "npc": "npc.alan", "scene": "scene.dock",
    "start_minute": 60, "duration_minutes": 30 }
]
```

世界包中的声明示意：

```json
"agent_routes": [
  { "from": "scene.dock", "to": "scene.lighthouse", "conditions": [] }
],
"agent_objects": [
  { "id": "object.shell-token", "name": "贝壳信物", "scene": "scene.dock",
    "description": "旧港徽记刻在贝壳上。", "item": "item.shell-token" }
],
"agent_item_rules": [
  { "type": "give", "item": "item.shell-token", "target": "npc.alan",
    "description": "阿澜收下信物。", "conditions": [],
    "effects": [{ "type": "modify_npc_relationship", "npc": "npc.alan", "amount": 1 }] },
  { "type": "use", "item": "item.shell-token", "target": "object.tide-chart",
    "description": "信物与地图印记吻合。", "consume": false,
    "conditions": [], "effects": [{ "type": "complete_event", "event": "event.chart-matched" }] }
],
"agent_npcs": [
  { "id": "npc.alan", "name": "阿澜", "scene": "scene.dock", "dialogue_rules": [
    { "id": "topic.old-route", "description": "询问旧航道", "reply": "退潮时沿礁石外侧走。",
      "conditions": [{ "type": "variable", "variable": "var.knows-route", "op": "eq", "value": false }],
      "effects": [{ "type": "modify_variable", "variable": "var.knows-route", "operation": "set", "value": true }] }
  ] }
]
```

例如 `{"actions":[{"type":"talk","target":"npc.alan","topic":"topic.old-route"},
{"type":"move","target":"scene.lighthouse"}]}`。一次玩家输入最多组合三个行动，
按顺序在候选状态上检查，任何一步失败则整轮回滚。空行动列表只推进一个叙述回合，
不改变物品、人物关系、地点等世界事实。旧版只返回行动 ID 的模型输出仍可解析为
`authored`，但新世界包应使用带类型的协议。

这允许不同自然语言输入在相同世界规则下组合出不同路径，但当前版本仍要求创作者
事先声明可改变状态的行动能力、可走路线与 NPC 位置；模型不能凭空发明新物品、新规则或任意状态字段。
对象 `id` 在世界包内唯一，`scene` 必须引用已有场景。省略 `item` 时只能观察。
观察描述只会在成功执行 `inspect` 后作为已确认事件返回；已拿走对象的 ID 保存在权威状态和存档中。
旧版存档没有该字段时按尚未拿取处理。新对象应使用尚未被旧版预设行动发放的物品 ID，避免同一物品被不同规则重复授予。
`agent_item_rules` 按 `type`、`item` 和 `target` 的组合唯一。目标必须引用已有 NPC（`give`）或对象（`use`）。引擎检查目标在场、背包至少有一件物品及全部条件；规则效果与扣物品作为同一回合原子提交，失败时全部回滚。规则效果不能直接移动玩家。`give` 总是转交一件物品；`consume` 只用于 `use`。
对话规则的 `id` 在同一 NPC 内唯一；`description` 供 Agent 选择话题，`reply` 是创作者核准的 NPC 答复，只有执行成功才进入已提交事件。对话效果可沿用普通效果，但不能直接切换玩家场景或章节。未命中规则的话题仍可自由交谈，不过不会解锁线索或修改其他事实。生成式叙述本身尚无自动事实核查；判定线索是否真的获得，应以 C++ 提交的状态和事件为准。
`target_node` 目标须在执行时的当前场景存在；跨场景移动使用 `agent_routes`，
或在预设行动中使用 `switch_scene` 效果。`agent_routes` 的 `conditions` 可使用下述条件类型。
当前 NPC 位置为静态声明，尚不支持 NPC 自主移动；结局仍可由终局节点表示，更通用的
动态结局条件留待后续版本。

`StoryValue` 支持字符串、布尔值、64 位整数和双精度数。

条件类型：

- `variable`：变量与值比较；运算符为 `eq/ne/lt/lte/gt/gte`。
- `has_item`、`gold`、`current_scene`、`event_triggered`、`npc_memory`、`npc_relationship`。
- `all`、`any` 和 `not`：递归组合条件。

效果类型：

- `modify_variable`：`set` 或数值 `add`。
- `add_gold`、`add_item`、`remove_item`。
- `switch_scene`、`advance_chapter`。
- `write_npc_memory`、`modify_npc_relationship`、`complete_event`、`append_narrative`。

NPC 关系值默认从 `0` 开始，可用 `npc_relationship` 条件比较；
`modify_npc_relationship` 增减关系值时做 64 位溢出检查。

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
