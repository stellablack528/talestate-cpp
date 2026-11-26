# 独立文件夹存档

最新状态：按用户要求，旧 saves/sessions/exports 已清空；当前全新存档为 userdata/saves/coast-day1/，从家、第 1 天 08:00 开始。下文旧 quick/0873bd7cf806 迁移记录属于此前阶段，不再作为当前存档。玩法见 [地图与时间](DAY-CYCLE.md)。

每一局是一个独立文件夹。参考 Minecraft 的世界目录与 ZIP 迁移方式，采用适合本项目的可读 JSON，不引入 NBT、区块文件或数据库。

```text
userdata/saves/<save_id>/
  save.json                 格式、存档ID、世界标题和修订信息
  session.json              当前实际状态、完整NPC记忆、历史与世界hash
  npcs/<npc_id>.json         便于查看的当前NPC记忆投影
  characters/<npc_id>.json  当前人物设定、长期笔记、个人计划与每日结算记录
  dialogues/<npc_id>.json   玩家查看的聊天显示记录，与 NPC 长期记忆分离
  snapshots/quick.json       手动保存的进度点（包括当时的记忆）
  world/
    world.json              这一局使用的世界定义
    npcs/<npc_id>.json       这一局的初始人物内容
    media/manifest.json     这一局的媒体清单
    media/...               背景、立绘、音频等实际素材
```

根 `worlds/` 是开新局时使用的世界模板。开局会复制整个世界到存档；素材端给当前旅途制作的图放入这份存档的 `world/media/`，合并该处清单，保留音频和其他已有内容。不会因更新根模板而改变既有存档。不同存档的同名 NPC 可以使用不同美术。

`session.json` 是当前进展的权威记录，NPC 文件用于阅读；不要分别修改它们来调整进度。动作后自动保存当前会话，点击“保存进度”另写 quick 快照；“继续游戏”恢复本局 quick，重新启动默认打开最近的文件夹存档。设置里可选择其他存档，或切换世界开始新局。

旧 `userdata/saves/<world_id>/quick.json` 保留。升级首次没有新文件夹存档时会尝试恢复旧 quick；当前灯塔 quick 已迁入 `userdata/saves/0873bd7cf806/`。旧 `userdata/sessions/` 暂留作历史数据，新游玩不继续写入它。

本轮三地点/昼夜更新后，用户要求清空旧进度重新开始；原 quick、旧文件夹存档和旧导出 ZIP 已清除，素材已保留到 revision 2 模板。文中旧编号仅记录当时迁移，不作为当前存档入口。

## ZIP 迁移

设置 → “导出当前存档 ZIP”，产出 `userdata/exports/<save_id>.zip`，包含世界、素材、当前进度、NPC 记忆和手动快照。把包传给另一台电脑，在设置中粘贴 ZIP 完整路径，点击“导入并打开存档”。每次导入建立新的 save_id，不覆盖其他存档。

也可手动压缩整个存档文件夹，传输后解压到另一台电脑的 `userdata/saves/`，保证 `save.json` 位于 `<save_id>/` 这一层。内置导入要求 ZIP 根层直接包含 save.json、session.json、world/ 等内容；内置导出符合这一布局。

命令行：

```powershell
$env:PYTHONPATH = "$PWD/runtime"
python -m talestate.savegame export userdata/saves/0873bd7cf806 userdata/exports/旅途.zip
python -m talestate.savegame import userdata/exports/旅途.zip --data-dir userdata
```

ZIP 不包含模型 Key 或模型配置。它会包含玩家输入、NPC 记忆和世界素材；分享前留意其中的私人内容。存档内部只保存相对媒体路径，换电脑后由客户端解析成实际路径。包的世界定义与状态 hash 需要匹配；增加或替换现有清单 ID 对应的图片不会改变世界定义 hash，不要为了换图改动玩法定义。

## 本次素材协作

设定端负责 world/location/NPC/media 稳定 ID 和视觉说明，素材端据此出图，运行时只消费存档中的媒体清单。当前对齐 `lighthouse` 世界、`harbor/lighthouse` 背景、`keeper.neutral/keeper.happy` 立绘；图中不固定画入交付结果或奖励等可变状态。

项目的 userdata 是项目内目录，不是系统用户目录。Ren'Py 音量等偏好也写入本地 userdata/renpy。游戏入口使用自带 SDK Python，创作器使用 .tools/python/ 中的本地 Python/tkinter；整体复制项目即可保留运行环境。独立游戏包有自己的内置环境和 userdata；它只供游玩，完整创作工具随项目目录保留。

参考：[Minecraft 官方世界 ZIP 备份说明](https://help.minecraft.net/hc/en-us/articles/40340122067085)、[官方跨设备迁移说明](https://help.minecraft.net/hc/en-us/articles/360053272471-Transfer-a-Minecraft-Java-Edition-World-to-a-New-Device-)。
