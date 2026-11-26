# 世界生成器与运行时接口 v1

当前世界 format 仍为 world-definition/v1，样例 lighthouse 的 revision 为 2。生成器编写内容，运行时执行真实进展，Ren'Py 展示会话；单人优先。

## 文件布局

源模板 worlds/<world_id>/ 下包含 world.json、npcs/<npc_id>.json、media/manifest.json 及媒体。每局复制到项目内 userdata/saves/<save_id>/world/，实际记忆在同存档 npcs/，session.json 是完整会话真相；NPC 文件为便于阅读的同步投影。存档同时恢复状态和记忆，详见 [独立存档](../SAVES.md)。

## 世界字段

- world_id 稳定，revision 为整数，必需表为 locations/entities/items/actions/quests，可有 title/description。
- location：id/name/description，可选 position_km:[x,y]、background、bgm 和 conditions/text 组成的 descriptions 变体。
- entity：id/kind(player或npc)/name/location/attributes，可选 profile、memory_file、portrait。profile 写人物经历、性格、认知和动机；当前不使用固定 dialogues 台词表。
- item：id/name/owner，可选 description；owner 指实体，首版物品是单件实例。
- action：id/intent/actor/target/duration_minutes/preconditions/effects，可选 name/description/item/ai_interaction。人物接取、交付和赠物设置 ai_interaction:true，target 指对应 NPC。
- quest：id/title/initial_status/complete_when/rewards，可选 giver/description/hidden_until_active。状态为 available（待接取）、active、completed。

## 规则

前置条件支持 at(entity,location)、owned_by(item,entity)、quest_status(quest,value)、attribute(entity,attribute,compare,value)、day(value) 和 evidence(match)。evidence 匹配本局已提交的事件。属性比较支持 eq/gte/lte，day 表示当前天数 >= value。

动作效果：move(entity,location)、transfer(item,from,to)、add_attribute(entity,attribute,amount)、activate_quest(quest)。activate_quest 只允许 available → active。任务 rewards 支持属性变化和激活后续邀请，完成时只执行一次。

`complete_when` 接收单个匹配对象或一组任选其一的匹配对象。必需配对为 delivered/item/from/to、combat_won/enemy、spell_learned/spell、clue_found/clue、appointment_attended/visit、npc_decided/npc/decision。匹配对象也可以限定 action 等实际事件字段。任务奖励随首次匹配一起提交。

调查效果使用 `{op:investigate, entity, location, clue}`，玩家须在指定地点，产生 clue_found。人物决定使用 `{op:npc_decision, npc, decision}`。动作必须将该人物设为 target 并启用 ai_interaction。运行时只在该人物明确返回 decision_outcome=accept 后提交决定。

签发证明、介绍信等任务凭证时，先在 `items` 定义名称和内容，设置 `owner: null`、`category: quest`。签发动作以签发人为目标，启用 `ai_interaction`，含 `{op: item_unissued, item}` 前置条件以及同一 NPC 的 `npc_decision` 和 `{op: issue_item, item, npc}` 效果。NPC 确认同意后，运行时一次记录制作与交付事件，物品归入玩家背包。正式交验使用 `owned_by` 和指定接收人的 `transfer`，后续行动可匹配该动作产生的 `delivered` 证据。凭证通过这些动作流转。

`night_opportunities` 可列出作者允许的次日约见条目。字段为 id、npc、location、title、minutes 和可选 preconditions。夜间统筹从符合条件、来得及抵达的候选中至多选一项。角色次日起床时已抵达，停留 minutes 分钟。玩家须在时限内到场并选择赴约，才留下 appointment_attended；逾期会记录错过，角色返回原地点。

夜间角色可请求转交已有物品，也可写信。提案使用 `request:{recipient,title,letter:{name,text},return_receipt}`。运行时保存信件 ID、作者、收件人、来源事件和实际归属，并创建委托。如果需要回执，首段交付后再发出可选的回执委托。信件、回执、委托和约见进本局存档。

## 地图与时间

可选 map：size_km:{width,height}、walking_speed_kmh、routes:[{from,to,distance_km}]。道路双向且连通，运行时生成 travel.<destination> 相邻行动；路程除以速度，向上取整为分钟。地点 position_km 与地图尺寸供未来地图显示使用，实际耗时按道路，不按图片像素。

可选 clock 包含 start_minute、night_minute、home 和 conversation_minutes。海岸示例为 480/1200/home/5，即 08:00 开始、20:00 入夜。家中睡觉需要配置 API Key。夜间整理、计划和选定互动全部处理成功后提交次日，失败留在当晚重试。详见 [地图与时间](../DAY-CYCLE.md)。

## NPC 记忆与 AI

memory_file 指向 npcs/<id>.json，包含 npc_id/profile/initial_memories:[{id,text}]。初始认知是人物历史，不是当前状态；本局亲历与交谈按人物写文件并关联 event_id。完整记录保留在磁盘，模型上下文读取该人物最近信息和简单关键词召回。

自然语言首先解释为合法 action_id 或在场 npc_id。角色交互再将背景、记忆、当前时刻、拟提交结果交给模型，返回 text/expression；回复成功才提交真实事件、记忆和时间。API 异常直接报错，没有内置模拟台词回退。AI 发言与事实行独立标识；发言不能直接增加物品、奖励或完成事件。

当前样例的委托与邀请：村庄林岚交出药包并激活送药任务；玩家交给陈伯后奖励一次并激活次日邀请；day>=2 时陈伯可以实际转移旧罗盘给玩家。此后不增加预设新剧情。

## 媒体与创作工具

media/manifest.json 分 backgrounds、portraits、bgm、sfx、voice，路径相对于该世界目录。人物按 NPC 与表情 ID 引用，音频图片不塞进 world.json。允许占位图；替换同 ID 文件无需改玩法 hash。

tools/world_builder 提供 generate/edit/compile/validate/export，复用 runtime/talestate/content.py 的 validate_world(definition) -> list[dict]；compile/validate/export 不依赖模型。生成数量不由样例限制。世界内容生成后必须使用同一运行时可执行的规则，不能只靠叙述宣称行为已经实现。
