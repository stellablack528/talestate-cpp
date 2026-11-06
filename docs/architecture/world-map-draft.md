# 世界观地图草案

桌面端的“根据世界观生成地图草案”使用当前 BYOK 配置，把用户的一段世界观发给兼容模型。模型返回 3–8 个地点及 2–12 条无向路线。C++ 核心检查 JSON、字段长度、地点 ID 和坐标唯一性、路线端点、重复路线及从起点出发的连通性。失败时会把结构错误送回模型修复一次。通过检查的结果可以预览、导出为独立 JSON 文件。

这是**地图草案**，并非完整 Story Package。地点、路线和描述可以辅助创作者设计世界；NPC、物品、行动规则、开局和结局仍须创建并通过 Story Package 校验。生成过程不修改当前游玩状态，也不导出 API Key。

## 开源参考与采用范围

- [TaleWeave AI](https://github.com/ssube/taleweave-ai)（MIT）：参考“从简短提示生成结构化世界数据”的设计思路。TaleState 的 JSON 结构、C++ 校验和 Qt 预览均为本项目实现，未复制其代码。
- [IluMap](https://github.com/ilumetric/IluMap)（MIT）：参考地图数据与可视呈现分离的方向。它更偏地图编辑，并非直接的世界观文字生成器；本项目未复制其代码。

地图草案采用 JSON，包含 `title`、`start`、`locations` 和 `routes`。地点有 `id`、`title`、`description`、`x`、`y`；路线有 `from`、`to`。坐标在 0–4 网格内，只表达示意位置，不含地形或通行规则。正式游戏规则必须由世界包声明并由引擎裁决。
