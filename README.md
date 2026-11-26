# TaleState · 自由旅途

创作者定义世界，玩家用自然语言行动。生成工具负责内容，统一运行时负责进展，Ren'Py 负责场景、人物和声音。

## 开始

- 双击 **开始游戏.cmd**：新 Ren'Py 玩家端。
- 双击 **世界创作器.cmd**：导入设定、生成/编辑世界、编译导出。
- **旧内容/启动旧版.cmd**：归档的 Qt 版本。

当前示例“海岸的一天”：从家出发，经村庄接取林岚的送药委托，步行至灯塔交给陈伯；夜间返家睡觉，第二天可以应邀领取旧罗盘，再自由探索、与人物交谈。

道路距离决定耗时，顶部时间条显示当前日期和时刻。NPC 发言根据角色背景、记忆和实际行动结果由 AI 生成，没有离线固定台词；接取、交付和赠物需要设置自己的模型 API。未配置 API 时仍可步行、休息和睡觉。物品、任务和奖励由运行时维护，API 异常不改变进度。

## 目录

```
runtime/talestate/     状态、动作、任务、NPC记忆、存档
clients/renpy/        玩家端：场景、立绘、对话、设置、音频
tools/world_builder/  创作窗口、CLI 与 Python API
worlds/<world_id>/    世界、NPC初始记忆、媒体清单和素材
userdata/             每局记录与存档（不提交）
scripts/              启动、同步与打包
tests/                关键游戏流程与存档迁移检查
docs/                 当前接口、素材与开发说明
旧内容/               原源码、旧游戏包、旧构建、审查与开发工具
dist/                 可运行分发包
```

编辑根 worlds/ 世界模板；启动脚本同步进 Ren'Py，不编辑 game/worlds/ 副本。每局在项目内 userdata/saves/<save_id>/ 保存独立世界副本、专属素材、进度与人物记忆，可整体导出 ZIP。userdata 不是系统用户目录。

## 文档

- [项目修改历程与目录说明](项目修改历程与目录说明.md)
- [项目长期注意事项](项目注意事项.md)
- [归档说明](旧内容/README.md)
- [游玩说明](本地游玩说明.md)
- [地图、时间与 AI 交互](docs/DAY-CYCLE.md)
- [世界接口](docs/architecture/world-definition-v1.md)
- [素材管理](docs/MEDIA.md)
- [独立存档与 ZIP 迁移](docs/SAVES.md)
- [开发与打包](docs/DEVELOPMENT.md)
- [世界生成器](tools/world_builder/README.md)
- [旧版审查](旧内容/历史审查/PROJECT-REVIEW-20261117.md)

当前采用 Ren'Py 8.6.0；新运行时与创作器仅使用 Python 标准库。
