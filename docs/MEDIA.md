# 场景、人物与声音素材

## 当前版本：欧洲中世纪视觉重制 v2

当前根清单已统一切换到欧洲中世纪沉静写实绘本风，以下 v1 说明仅为历史交付记录。稳定背景 ID home/village/lighthouse/harbor 不变，分别使用 `media/backgrounds/<id>-medieval-v2.png`（各836×470 RGB，近16:9）；三位 NPC keeper/villager/baker 的 neutral/happy 使用 `media/portraits/<id>/<expression>-medieval-v2.png`（各512×1024 RGBA，透明）。

石木、茅草/石板屋顶、木帆船、石造火光灯塔取代近现代海港元素。人物为欧洲面貌，林岚约40岁、蓝绿羊毛裙与亚麻围裙；周穗约50岁、暖赭工作裙与亚麻头巾；陈伯约65岁、灰短胡须、粗羊毛兜帽披肩与束腰上衣，均空手。移除现代针织帽、海员外套。人物名字、性格、职业、年龄层及任务不变。happy 基于同一三人中性图编辑，脸型服饰构图保持一致，有少量生成笔触变化。

本轮共3次内置 imagegen 调用，通过四背景拼幅、三人中性拼幅、同图微笑编辑后裁剪得到10张图，不做放大。清单所有引用文件存在，透明通道已检查；原音频文件 SHA-256 与音频清单项均保持。只更新根媒体及素材文档，未写任何存档、玩法JSON、runtime 或 clients；现有存档由主端同步。提示词与校验记录在根 `worlds/lighthouse/media/MEDIEVAL-PROMPTS.json`、`MEDIEVAL-DELIVERY.json`，运行不依赖工具缓存或临时创作源图。

当前素材：根 worlds/lighthouse/media/ 已有家、村庄专用背景，以及林岚、面包师周穗的 neutral/happy 透明立绘；原灯塔和陈伯图片保留。新局复制根模板媒体；现有存档由主端同步，本轮素材制作不直接改存档。下文旧存档编号仅为最初接入记录。

每个世界管理自己的素材，生成器和运行时只使用 ID，不在前端写死某个世界的文件名。

```
worlds/lighthouse/
  world.json
  npcs/keeper.json
  media/
    manifest.json
    backgrounds/harbor.webp
    portraits/keeper/neutral.png
    portraits/keeper/happy.png
    audio/bgm/sea_evening.ogg
    audio/sfx/delivery.wav
    audio/voice/keeper_thanks.ogg
```

清单中的路径相对于世界目录：

```json
{
  "backgrounds": {"harbor": {"file": "media/backgrounds/harbor.webp"}},
  "portraits": {
    "keeper.neutral": {"file": "media/portraits/keeper/neutral.png"},
    "keeper.happy": {"file": "media/portraits/keeper/happy.png"}
  },
  "bgm": {"sea_evening": {"file": "media/audio/bgm/sea_evening.ogg"}},
  "sfx": {"delivery": {"file": "media/audio/sfx/delivery.wav"}},
  "voice": {"keeper_thanks": {"file": "media/audio/voice/keeper_thanks.ogg"}},
  "event_sounds": {"delivered": "delivery"}
}
```

在 world.json 中：地点设置 `background: "harbor"` 和 `bgm: "sea_evening"`；NPC 设置 `portrait: "keeper"`；台词设置 `expression: "happy"` 和可选 `voice: "keeper_thanks"`。

背景按画面铺满，立绘按比例放入人物区域，适合透明 PNG/WebP。BGM 在 music 通道循环，事件音效使用 sound，台词语音使用 voice，互不占用同一轨道。缺失文件显示占位画面或静音，不阻止体验世界。

示例背景为 Ren'Py 代码绘制的场景占位，人物为字牌，不是正式美术。`scripts/make_demo_media.py` 可重新生成示范音频和清单，它会覆盖灯塔示例的媒体清单，不要用它重置自己的世界素材。

根 `worlds/` 是新游戏模板；`scripts/stage_game.py` 将模板同步到 `clients/renpy/game/worlds/`。生成器导出保留 media 文件夹，但草案中的引用应由创作者保留。

## 独立存档素材

每局使用自己的 `userdata/saves/<save_id>/world/` 副本。当前局素材写入该副本的 `media/` 和 `media/manifest.json`，清单路径仍相对于该存档的 `world/`，例如 `media/backgrounds/harbor-v1.png`。只为当前局换图时不要回写根模板，也不要运行示范素材脚本覆盖清单；保留原有音频条目和文件。

2026-11-17 首批真实图片通过内置 GPT Image/imagegen 生成，已接入存档 `0873bd7cf806`：

| 清单 ID | 存档 world 内的文件 | 尺寸 | 透明 |
| --- | --- | --- | --- |
| harbor | media/backgrounds/harbor-v1.png | 1672×941 | 否 |
| lighthouse | media/backgrounds/lighthouse-v1.png | 1672×941 | 否 |
| keeper.neutral | media/portraits/keeper/neutral-v1.png | 1024×1536 | 是，RGBA |
| keeper.happy | media/portraits/keeper/happy-v1.png | 1024×1536 | 是，RGBA |

视觉采用统一写实绘本海港风：蓝灰海雾、暖琥珀灯光、旧白灰石塔；背景无人物和交付状态。守塔人是灰短胡须、藏青海员外套与针织帽的年长男性，两表情分别为平静等待与克制感谢，不固定持有药品或金币。这些是美术选择，不新增玩法事实。

本机临时创作目录 `tools/art_work/` 已加入 Git 忽略，保留四张原始 PNG、完整提示词 `prompts.json`、接入记录及 SHA-256 `delivery.json`。首次接入仅更新四个图片清单项，已有视觉引用无需修改；当时的 `save.json`、`session.json`、`world.json` 和原音频均已核对哈希未变。图片后续已保留到根模板。

实现参考 Ren'Py 官方 [图像与图层](https://www.renpy.org/doc/html/displaying_images.html) 和 [音频通道](https://www.renpy.org/doc/html/audio.html)。

## 三地点新局更新

清空旧进度前，四张已生成图片复制到根 worlds/lighthouse/media/ 模板，保证新局继续使用。港湾图复用于村庄背景，灯塔背景和 keeper.neutral/happy 保留；家使用代码占位小屋、林岚使用人物字牌。旧存档 0873bd7cf806 已不再是活动入口，图片 provenance 仍可见 tools/art_work 的提示词与交付记录。

## 家、村庄与新人物补图

以上占位及港湾复用已由本轮真实素材替换。与设定端确认稳定 ID 后，使用内置 GPT Image 共三次调用：一次生成双背景图；一次生成林岚/周穗双人中性透明图；一次编辑双人微笑差分。按上下或左右中线裁剪，不放大，保留原始 alpha。

| 清单 ID | 根 world 内的最终图片 | 尺寸 | 透明 |
| --- | --- | --- | --- |
| home | media/backgrounds/home-v1.png | 1182×665 | 否 |
| village | media/backgrounds/village-v1.png | 1182×665 | 否 |
| villager.neutral | media/portraits/villager/neutral-v1.png | 768×1024 | 是，RGBA |
| villager.happy | media/portraits/villager/happy-v1.png | 768×1024 | 是，RGBA |
| baker.neutral | media/portraits/baker/neutral-v1.png | 768×1024 | 是，RGBA |
| baker.happy | media/portraits/baker/happy-v1.png | 768×1024 | 是，RGBA |

家为临海石屋，村庄为含杂货铺及朴素石炉面包铺的街道广场，无人物/UI/交付状态。林岚采用约40岁黑发低髻、蓝绿外套、米色围裙；周穗采用约50岁深棕夹灰盘发、暖赭工作上衣、米白围裙，两人空手。neutral 平静专注，happy 克制微笑。差分来自编辑同一双人图，衣着与人物身份一致，模型编辑有少量笔触和轮廓变化。

六张最终图片及 manifest 均在项目根模板内，运行不依赖生成工具缓存或 tools/art_work。后者仅保留创作源图、coast-expansion-prompts.json 完整提示词和 coast-expansion-delivery.json 尺寸/哈希记录。原 keeper/lighthouse/harbor 图片哈希保持不变；音频和其他清单项保留。本轮未写 runtime、clients、玩法 JSON 或存档进度。


## ???????revision5?

???? backgrounds.forest ? media/backgrounds/forest-medieval-v1.png?1672?941 RGB??monsters.slime ? media/monsters/slime-v1.png?1536?1024 RGBA?alpha0..254??????imagegen?????????SHA256??media/COMBAT-PROMPTS.json??????????????NPC??????????????4c74a402f4db?????????????????/?????????????????????????????????????
