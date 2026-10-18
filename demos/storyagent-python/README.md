# StoryAgent Python Demo

这是 TaleState 的独立实验原型，整理自作者在 `D:\StoryAgent` 使用 uv 和 Trae 编写的初版。它保留了原来的 Python 游戏循环、规则引擎和 DeepSeek 叙事调用；公开示例场景改为原创的“钟塔学院”。本目录可以单独运行，不参与顶层 C++ 构建。

## 当前能做什么

玩家选择编号后，`StoryEngine` 根据 `data/story.json` 检查动作和等级要求，再决定是否改变所在场景。`DeepSeekClient` 只为当前场景生成一段文字；没有 API Key 或调用失败时，会显示兜底文案，规则循环仍可继续。

这是固定选项的最小可玩原型。玩家自然语言输入、Agent 规划、动态剧情、存档和 C++ 权威状态尚未接入；本目录的 `data/story.json` 也与主项目的 Story Package v1 格式不同。

## 运行

需要 Python 3.12+ 和 [uv](https://docs.astral.sh/uv/)。在本目录执行：

```powershell
uv sync --locked
uv run --locked python main.py
```

没有 API Key 也能体验规则与场景跳转。需要 AI 叙事时，复制 `.env.example` 为 `.env`，填写自己的 `DEEPSEEK_API_KEY`，并确认 `DEEPSEEK_MODEL` 与账号可用模型一致。`.env`、虚拟环境和生成文件不会提交到 Git。使用模型 API 可能产生服务商费用。

## 文件职责

| 文件 | 职责 |
|---|---|
| `main.py` | 控制台输入与回合循环 |
| `models.py` | 玩家状态数据 |
| `story_engine.py` | 场景引用检查、动作与等级判定、状态修改 |
| `llm_client.py` | DeepSeek API 调用、叙事提示与失败兜底 |
| `data/story.json` | 原创的最小场景图 |

主项目会借鉴这里的“模型负责叙述，规则引擎负责状态”边界；后续正式运行时能力以 C++ 主项目为准。
