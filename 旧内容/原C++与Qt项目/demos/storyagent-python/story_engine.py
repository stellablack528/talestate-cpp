# story_engine.py - 剧情规则引擎
# 所有游戏规则（能不能进场景、如何跳转、状态是否合法）都由这里的 Python 代码决定。
# LLM 完全不参与这里的判断。
import json
from pathlib import Path

from models import PlayerState

# 剧情图文件位置：固定为本文件同级目录下的 data/story.json
STORY_PATH = Path(__file__).parent / "data" / "story.json"


class StoryEngine:
    """剧情引擎：加载剧情图、校验动作、执行场景跳转。

    职责边界：
    - 能进入哪个场景：由本类依据 story.json 判断
    - 动作是否合法：由本类判断
    - 修改 PlayerState.current_scene：只有本类的 perform_action 会修改
    """

    def __init__(self, story_path=STORY_PATH):
        # 读取并解析剧情图
        with open(story_path, "r", encoding="utf-8") as f:
            self.story = json.load(f)
        self.scenes = self.story["scenes"]

        # 启动时校验剧情图：所有跳转目标必须是已存在的场景
        for scene_id, scene in self.scenes.items():
            for action_id, action in scene["actions"].items():
                if action["next_scene"] not in self.scenes:
                    raise ValueError(
                        f"剧情图配置错误：场景 {scene_id} 的动作 {action_id} "
                        f"指向了不存在的场景 {action['next_scene']}"
                    )

    def get_scene(self, scene_id):
        """按 id 获取场景数据，场景不存在时直接抛异常（禁止不存在的场景）。"""
        scene = self.scenes.get(scene_id)
        if scene is None:
            raise KeyError(f"场景不存在: {scene_id}")
        return scene

    def get_current_scene(self, player):
        """获取玩家当前所在的场景数据。"""
        return self.get_scene(player.current_scene)

    def get_available_actions(self, player):
        """返回当前场景可以选择的动作，格式为 [(action_id, 显示文本), ...]。"""
        scene = self.get_current_scene(player)
        return [
            (action_id, action["label"])
            for action_id, action in scene["actions"].items()
        ]

    def perform_action(self, player, action_id):
        """校验并执行一个动作。返回 (是否成功, 提示信息)。

        校验规则全部由 Python 完成，不会询问 LLM。
        """
        scene = self.get_current_scene(player)

        # 规则 1：动作必须在当前场景中存在（禁止不存在的 action）
        action = scene["actions"].get(action_id)
        if action is None:
            return False, f"【行动失败】\n当前场景没有这个动作：{action_id}"

        # 规则 2：如果动作有等级要求，玩家必须达到
        required_level = action.get("required_level")
        if required_level is not None and player.level < required_level:
            target_name = self.get_scene(action["next_scene"])["name"]
            return False, (
                "【行动失败】\n"
                f"你当前等级不足，需要达到 {required_level} 级才能进入{target_name}。"
            )

        # 所有校验通过，执行跳转：整个游戏中唯一修改 current_scene 的地方
        player.current_scene = action["next_scene"]
        return True, ""
