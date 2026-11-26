# 灯塔视觉交接（2026-11-17）

## revision 3 更新

当前世界仍为 `lighthouse`，地点为 `home/village/lighthouse`；下文最初 `harbor` 是旧 revision 的历史交接，不用于当前三地点。素材端现负责根模板媒体清单，不修改玩法或现有存档。

已双向确认新增 `baker`（周穗），portrait base=`baker`、清单 `baker.neutral/baker.happy`：约50岁女性，深棕夹灰盘发、暖赭工作上衣、米白围裙、空手，全身透明，两表情保持同脸同衣构图。林岚 `villager.neutral/happy` 约40岁、黑发低髻、蓝绿实用外套与米色围裙，空手。场景 `village` 有杂货铺及石炉面包铺，不加人物、UI或任务状态。

周穗继承父亲石炉，昨天与林岚约好次日取一只海盐面包。任务 `deliver_bread`：giver=`baker`、recipient=`villager`、item=`salt_bread`，次日起玩家可明确接取后送到邻铺。不在立绘中固定画面包、金币或已接受委托的状态。

## 首次送药样例交接记录

已与素材 agent 对齐，本次不改玩法或 world-definition/v1。以下外观为本次美术选择，不是原设定规定的年龄、性别或新增规则。

| 内容 | 稳定 ID / 清单键 | 视觉 brief |
| --- | --- | --- |
| 世界 | `lighthouse` | 写实绘本海港风，蓝灰海雾、暖琥珀灯光、旧白灰石塔 |
| 港口地点 / 背景 | `harbor` / `backgrounds.harbor` | 海边栈桥、通向灯塔的小路；远景灯塔与另一背景呼应 |
| 灯塔地点 / 背景 | `lighthouse` / `backgrounds.lighthouse` | 海岸石塔门外、路径和海面，为叠加人物留空间 |
| 守塔人实体 / 立绘前缀 | `keeper` / `keeper` | 年长男性、灰短胡须、藏青旧海员外套、针织帽，无武器 |
| 等待台词立绘 | `portraits["keeper.neutral"]` | 平静等待；台词 `waiting` 的 expression 为 `neutral` |
| 感谢台词立绘 | `portraits["keeper.happy"]` | 克制感谢；台词 `thanks` 的 expression 为 `happy` |

背景不画入人物、药品交付状态、金币、文字或 UI，同一背景适配任务前后。两表情同脸、同服装、同构图；人物透明竖图，保留完整轮廓，不将药品或金币画成固定持有物。背景建议 16:9，最终尺寸由素材 agent 与前端约定。

## 文件与存档边界

素材 agent 在 `tools/art_work/` 生成，等待主 agent 给出本次存档的绝对路径后接入。不要根据现有目录猜测某个 save_id。

根 `worlds/<world_id>/` 是创作模板；每局使用 `userdata/saves/<save_id>/`，其中包括 `save.json`、`session.json`、`npcs/`、`world/world.json`、`world/npcs/`、`world/media/` 和 `snapshots/`。本次游玩素材只接到指定存档的 `world/media/`，实际进度不回写模板。

清单在存档的 `world/media/manifest.json`，file 路径相对于该存档的 `world/`：

```json
{
  "backgrounds": {
    "harbor": {"file": "media/backgrounds/harbor.webp"},
    "lighthouse": {"file": "media/backgrounds/lighthouse.webp"}
  },
  "portraits": {
    "keeper.neutral": {"file": "media/portraits/keeper/neutral.png"},
    "keeper.happy": {"file": "media/portraits/keeper/happy.png"}
  }
}
```

这是图片清单的局部示意，不是覆盖整个 manifest 的指令；接入时保留原 BGM、SFX、voice 和 event_sounds。现有 `sea_evening` BGM 与 `delivery` 音效 ID 不变。不改共享模板 world.json，不重新运行示范媒体生成脚本。

## 生成器后续媒体 brief 输出约定

本轮仅完成实际交接，不增加生成调用、导出副作用或必填模块。今后需要自动输出时，可在 draft 的独立创作资料中记录一组 `kind / subject_id / media_id / brief`：kind 为 background 或 portrait，portrait 的 media_id 使用 `keeper.neutral` 等完整清单键。引用已有稳定实体和媒体 ID；外观建议与权威世界事实分开，素材就绪后才更新对应目标的清单。不将图像数据或绝对存档路径写入世界协议。
