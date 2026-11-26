# 示例素材

区域地图 coast-map-v1.png 使用内置 GPT Image 一次生成，1672×941 RGB，无透明、文字、UI、人物或怪物。欧洲中世纪羊皮纸手绘风，北海水、南陆地森林，家—村庄—东海岬灯塔及村庄—森林支路，不增加港口可去地点。maps.region 图片复制、清单合并及存档同步由主端接手；素材端仅补记录。完整提示词、目标坐标与 SHA-256 见 MAP-PROMPT.json；精确路线、地点名字及当前位置由前端叠加。

当前正式版本为欧洲中世纪视觉重制 v2。manifest 中四个背景和三位人物的 neutral/happy 均指向 *-medieval-v2.png；旧 v1 图片仅保留历史版本，不再被当前清单引用。内置 GPT Image/imagegen 共三次调用生成四背景拼幅、三人中性透明拼幅及同图微笑编辑，再裁剪为十张最终图片。欧洲面貌、中世纪石木建筑、亚麻羊毛皮革服饰，移除现代针织帽和海员外套。名字、职业、性格和年龄层保持，未修改玩法。完整提示词见 MEDIEVAL-PROMPTS.json，尺寸与哈希见 MEDIEVAL-DELIVERY.json。所有运行图片在本目录内，音频保留。

正式背景与人物 PNG 使用内置 GPT Image/imagegen 为本项目生成，采用统一海港写实绘本风。背景含 harbor、lighthouse、home、village；人物含 keeper、villager（林岚）、baker（周穗）的 neutral/happy 表情。home/village 及两位新人物采用合并出图后裁剪，减少模型调用。完整提示词与生成记录保存在项目 tools/art_work/（本机创作目录，不是运行依赖）。缺图时仍可使用 Ren'Py 界面代码占位。
sea_evening.wav 为本项目用正弦合成的原创示范音频，无第三方录音。
素材路径相对于世界目录，bgm、sfx 和 voice 分开管理。


Forest background and transparent slime sprite: generated with built-in imagegen for this project. Paths: backgrounds/forest-medieval-v1.png, monsters/slime-v1.png. Original prompts and SHA256: COMBAT-PROMPTS.json.
