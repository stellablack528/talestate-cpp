# models.py - 存放游戏数据结构（相当于 C++ 里的 struct）
from dataclasses import dataclass


@dataclass
class PlayerState:
    """玩家状态：只保存数据，不包含任何游戏逻辑。

    LLM 永远不会修改这个对象，只有 StoryEngine 有权修改。
    """

    name: str           # 玩家名字
    level: int          # 玩家等级
    current_scene: str  # 玩家当前所在场景的 id（对应 story.json 里的键名）
