# 世界生成器

Python 标准库，无 Qt、无模型依赖的编译/校验/导出。以下命令从项目根目录运行。

## 创作窗口

```powershell
python -m tools.world_builder.ui
```

窗口可导入 UTF-8 文本/Markdown、直接填写原文，设置兼容 API 地址（含 `/v1`）、Key、模型和生成用世界 ID；也可点击「预览示例」无需 Key 使用。模型请求放在后台线程，结果与错误回到主线程显示。Key 仅在内存中，不写配置、草案或导出文件。

生成后在「可编辑 draft JSON」页修改内容，可打开/保存草案，点击「检查草案」或「编译并导出」。导出目标取草案 `definition.world_id`，固定为本项目根目录的 `worlds/<id>/`；上方世界 ID 输入框仅用于下一次生成。离线示例默认使用 `lighthouse-preview`，避免误覆盖已有 `lighthouse`。

导出已有目录前必须确认：覆盖 `world.json` 和本次 NPC 初始文件，保留 `media/` 及其他未涉及文件。已有背景、音乐、立绘文件不会被清理；但新 `world.json` 的 `background/bgm/portrait` 等引用仍以当前草案为准，需要保留时请在草案中保留这些字段。生成器不复制已有媒体、不修改玩家实际记忆或存档。

当前目录职责：`tools/world_builder/` 为生成器与创作窗口，`runtime/talestate/` 为统一 Python 运行时，`clients/renpy/` 为展示前端，根 `worlds/` 为世界模板；旧 C++/Qt 已归档至 `旧内容/原C++与Qt项目/`。窗口不启动运行时、前端或打包任务。

每局独立数据由运行时放入 `userdata/saves/<save_id>/`，本局世界与媒体在其 `world/` 子目录，实际进度不写回根模板。创作器仍导出根 `worlds/<id>/`，不直接修改已有存档。灯塔背景与立绘的稳定 ID、视觉 brief 和存档媒体交接约定见 [MEDIA-BRIEF.md](MEDIA-BRIEF.md)。

## 无 Key 预览、编辑与导出

```powershell
python -m tools.world_builder generate --preview --out tools/world_builder/work/draft.json
# 用文本编辑器修改 draft.json 的 definition / npc_seeds / requirements
python -m tools.world_builder compile tools/world_builder/work/draft.json --out tools/world_builder/work/bundle.json
python -m tools.world_builder validate tools/world_builder/work/bundle.json
python -m tools.world_builder export tools/world_builder/work/bundle.json --out tools/world_builder/work/lighthouse
```

产物为 `world.json`、`npcs/keeper.json`、`npcs/villager.json`，运行时加载导出目录。`--preview` 是固定灯塔示例，不会将任意输入伪装成生成结果。仓库内正式样例在 `worlds/lighthouse/`。

## 兼容模型 API

```powershell
$env:OPENAI_BASE_URL = 'https://your-provider.example/v1'
$env:OPENAI_MODEL = 'your-model'
$env:OPENAI_API_KEY = 'your-key'
python -m tools.world_builder generate --source your-world.md --world-id my-world --out tools/world_builder/work/my-draft.json
```

支持重复 `--source` 导入多个 UTF-8 文本/Markdown 文件；也可用 `--base-url`、`--model`、`--revision`、`--timeout`。Key 只从环境传入，不写入草案。无认证本地兼容服务可不设 Key。服务须支持 `/chat/completions` 和 `response_format: json_object`；失败直接报告，不自动切服务或反复付费请求。

所有原文直接参与一次模型生成，不用压缩摘要代替规则来源。生成前另存 `.sources.json`，成功后草案保留原文、需求映射、模型 token 用量。Ctrl+C 停止；已有草案可以继续编辑、编译、导出，不再调用模型。中断在模型请求期间时不能恢复远端推理，重跑生成会产生新请求。很长文档受服务上下文限制，本轮不实现分段编排和自动重试。

## Python 接口

```python
from tools.world_builder import generate, compile, validate, export

draft = generate([{"id": "brief", "text": "创作者的世界与规则"}], model="your-model")
# 编辑 draft；生成的草案允许不完善，compile 才决定是否可导出
bundle = compile(draft)
issues = validate(bundle["definition"])
result = export(bundle, "tools/world_builder/work/my-world")
```

`generate` 返回 `format/sources/requirements/definition/npc_seeds/generation`。`compile` 返回 `definition/npc_seeds/requirements/sources`；错误抛 `BuilderError`，其中 `issues` 为 code/path/message 列表。`validate` 返回诊断列表；运行时 `runtime/talestate/content.py` 存在时延迟调用其 `validate_world`，此前只做生成器本地检查。`export` 返回 world/files/content_hash。哈希只计算 world.json 的规范内容，与运行时一致；world_id 不随内容改变。修改世界或 NPC 种子时作者增加 revision。

每条需求保留 `id/source_ref/text/implemented_by`，后者引用 `actions/id` 等内容。明确未支持需求用 `unsupported` 标记并阻止编译；作者可修改需求和内容后再次编译。引用存在不等于完整语义证明，应在试玩中确认。没有地点/物品/NPC/任务固定数量；后续任务通过动作的 `quest_status` 条件依赖前序任务完成。

NPC 种子包含 npc_id/profile/initial_memories，编译时为每个 NPC 绑定 `npcs/<id>.json`。初始记忆与游玩记忆分离；生成器不写 userdata，不执行生成脚本，不发出任意事件。媒体仅保留作者给出的引用，不生成或覆盖媒体文件。

本轮不做复杂日程、多人或存档迁移。存读档、事务交付、派生 delivered 事件及奖励一次由统一运行时负责。

## revision 3 人物知情与面包小任务

离线预览已同步三地点昼夜、送药/次日罗盘及周穗送面包内容。新增实体 `baker`（周穗）、物品 `salt_bread`、任务 `deliver_bread`（giver=`baker`、recipient=`villager`）；次日起动作 `accept_bread_delivery` 实际交出面包并激活任务，`deliver_bread` 实际转给林岚后完成，无额外奖励。两动作均为 `ai_interaction:true`，只用既有 transfer/activate_quest 和 day 条件，不新增引擎效果。

所有 NPC 的 entity.profile 与 NPC 种子 profile/initial_memories 仅放人物经历和开局亲自获知的事实。`entity.author_notes` 是作者指导字符串，存放未来剧情、规则门槛和知情限制，**不能传给角色模型**。种子仍只有 npc_id/profile/initial_memories；不可把整份 entity 或全局任务/状态/玩家背包当作 NPC 知识。人物只能通过自有物品、亲历事件和直接交谈更新认知，私聊不自动互通。生成器提示已明确这一区分；是否实际隔离由运行时组装模型上下文负责。

## 当前样例与扩展字段

### 霍格沃兹《缺刻之夜》

[剧情稿](STAR-ACADEMY-STORY.md)和 `star_academy.draft.json` 保存作者主线、手写支线与需求映射。可用创作器打开该草案，或直接加载 `worlds/star_academy`。离线校验命令为 `python -m tools.world_builder validate tools/world_builder/star_academy.draft.json`。

主线在档案室调查或次晨赴艾琳约见后汇合，再到星塔校准。约见路线先在庭院得到诺拉同意。夜间统筹选中机会后，次晨 120 分钟内在庭院赴约。错过或未选时仍可查档。独立手写支线需从奥斯温手中接过银叶书签，再实际交还诺拉。运行时以提交的事件匹配 `complete_when`，数组中任一分支成立即可完成任务。

Revision 2 的查档路线先在庭院用 `issue_transcription_permit` 请艾琳签发纸件。许可物品 `transcription_permit` 以显式 `owner:null` 和 `category:quest` 声明。签发动作同时包含 `item_unissued`、同一签发者的 `npc_decision` 和 `issue_item`，同意后入玩家背包。在档案室用 `register_transcription_permit` 将纸件交给奥斯温留档，实际交验产生的 `delivered` 开放 `read_calibration_ledger`。主线完成后仍能补办并现场誊抄。

区域扩写使用 `python -m tools.world_builder merge-region BASE.json FRAGMENT.json --region REGION --out MERGED.json`。片段提供 `definition` 下的 ID 表（地点、实体、物品、行动、任务、法术、遭遇或夜间机会），也可提供顶层 `routes`、`npc_seeds`、`sources`、`requirements`。片段可引用已有记录。基线记录归作者，新增记录归首次加入的区域，`protected_refs` 可以额外锁定记录。同一区域可修改自己新增的未锁定记录，现有道路距离由作者直接编辑。输出前编译整份世界。生成的区域内容可先编辑为片段再合并。

### revision 5 对齐

format 仍为 `world-definition/v1`。默认模型 `deepseek-flash`，窗口、CLI `--model` 或 `OPENAI_MODEL` 均可覆盖；API 地址仍须匹配服务，不因模型名自动切换。本轮不修改示例 JSON，由主端同步。

- 玩家基础 attributes：`health:100/max_health:100/attack:5/defense:5`，另有 coins。
- 物品可加 `category:quest/misc/equipment`；装备 `slot:weapon/offhand/body`，`bonuses:{attack:int,defense:int}`。保留旧有 kind/barter_value/所有权/初始 shop.prices 的独立含义。
- 世界可有 encounters 数组，每项 `id/name/location/health/attack/defense/reward_coins`。灯塔新增 forest，从 village 分支 1 km，遭遇 slime_1/slime_2/slime_3；这不是其他世界的生成模板。
- 商店按钮本地购买，不需要 API。对话改价提议为 `npc_update:{decision:"accept",shop_prices:{item_id:positive_int}}`，仅本人目录商品有效；运行时接受后覆盖 state.shop_prices，并同步角色文件。生成器只写初始 shop.prices，不写游玩覆盖值或 npc_update。

compile/export 保留附加字段并延迟调用共享 validate_world，不复制战斗、装备或商店运行逻辑。NPC 知情隔离、私聊边界及任务实际交付证据规则不变。本轮没有验证真实模型请求或战斗流程。

### revision 6 魔法对齐

玩家基础属性另有 `mana:50/max_mana:50/magic_attack:8`。世界可有 `spells:[{id,name,element,mana_cost:int,power:int}]`，element 为 `fire/water/ice/lightning`。NPC 的 `lessons:{spell_id:非负整数金币}` 引用法术，`shop.prices` 仍引用实际物品 ID，售价为正整数；不要把教学目录当作可转移物品。

装备 bonuses 可包含 magic_attack；消耗品可有 `consumable:{resource:"health"或"mana",amount:正整数}`。动作支持类型化 `consume(item,entity)`，当前持有者才能消费，资源已满时不消耗；实际成功消费才产生正式 consumed 事件。不能用模型发言、任意 emit 或 add_attribute 冒充喝药。任务完成匹配 consumed 事件的具体字段由主端统一，生成器目前的任务编译仍限 delivered，不自行猜测新的事件字段。

encounters 可增加 element 和 sprite，后者引用素材负责人提供的元素图 ID。学习、购买、喝药和施法均由本地运行时执行，模型不能直接改伤害或蓝量；AI 仍可提出本人商品改价或个人设定更新。NPC 上下文只读取本人教学目录和亲历教学/交换，不读取他人的私聊或学习记录。

独立种子 [mage.seed.json](mage.seed.json) 交主端接入：`npc_id=mage`，姓名伊莲娜，位置由世界实体设为 village。约40岁的欧洲中世纪女性学者，初始仅知本人四系基础教学和木法杖生意，不含玩家已学会/接受课程、他人私聊、预设售价或未约定的法术/物品 ID。种子结构仍为 npc_id/profile/initial_memories；不自动加入示例或现有世界。生成器保留 spells/lessons/consumable/encounters 附加字段并继续复用运行时校验，未实现第二套魔法逻辑。

预览已更新为三地点昼夜样例：地图宽高/道路距离、sleep 跨日、村庄接取送药、次日罗盘邀请。人物只写背景与初始记忆，不再生成固定台词。编译支持 available 任务、activate_quest、day 条件及后续任务激活奖励；运行时 AI 对话需要玩家自己的 API。详见 [运行接口](../../docs/architecture/world-definition-v1.md)。
## NPC 命名与参考

在创作窗口的「NPC 命名与参考」选项卡，可以加入文学摘记和作者给出的命名规则。参考资料只随生成请求发送，作为称谓和命名习惯的依据。内置《坎特伯雷故事集·总序》资料卡适合用来参考晚期中世纪英格兰的行业称谓和身份称呼。 [资料原文](https://www.gutenberg.org/files/2383/2383-h/2383-h.htm)

命名规则示例

```json
{
  "style": "现代欧美校园，统一用中文译名，允许不同文化背景的姓名",
  "script": "han",
  "fixed_names": {"new_student": "诺拉·贝内特"},
  "forbidden_names": ["诺拉·马尔福"]
}
```

固定名字按 NPC ID 核对。`script` 可设成 `han`、`latin` 或 `any`。启用命名规则后，编译会检查文字体系、固定名字、重名和禁用名单。文化与时代风格由模型根据作者设定判断。命令行可传 `--npc-naming 规则.json` 和 `--naming-reference 参考.md`。
