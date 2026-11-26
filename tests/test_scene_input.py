"""Scene input stays distinct from private NPC dialogue."""

from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from talestate import Session


WORLD = Path(__file__).resolve().parents[1] / "worlds" / "lighthouse"


class SceneModel:
    configured = True
    usage = {}

    def __init__(self):
        self.plan = {}
        self.reply = {"text": "先问村里人吧。", "activity": "正在擦拭罗盘。"}

    def json_reply(self, instructions, context):
        return self.plan if "actions" in context else self.reply


class SceneInput(unittest.TestCase):
    def test_look_uses_current_scene_with_no_npc_or_a_lone_npc(self):
        with TemporaryDirectory() as data:
            game = Session(WORLD, data)
            self.assertIn("海边的小屋", game.take_turn("观察四周")[-1]["text"])
            game.perform("travel.village")
            self.assertIn("面包房", game.take_turn("看看周围")[-1]["text"])
            game.perform("travel.lighthouse")
            before = game.snapshot()["state"]
            lines = game.take_turn("观察四周，主要寻找是否有值钱的东西")
            self.assertEqual(lines[-1]["kind"], "narration")
            self.assertIn("石塔", lines[-1]["text"])
            self.assertIn("还无法确认", lines[-1]["text"])
            self.assertEqual(game.snapshot()["state"], before)
            self.assertEqual(game.dialogues, {})

    def test_free_scene_action_and_dialogue_without_an_activity(self):
        with TemporaryDirectory() as data:
            game = Session(WORLD, data)
            game.perform("travel.village")
            game.perform("travel.lighthouse")
            model = game.provider = SceneModel()
            model.plan = {"mode": "scene", "npc_id": "keeper", "reply": "你沿着石塔的外墙看了看。"}
            lines = game.take_turn("绕到石塔背后看看石缝")
            self.assertEqual(lines[-1]["text"], model.plan["reply"])
            self.assertEqual(game.dialogues, {})

            model.plan = {"mode": "talk", "npc_id": "keeper"}
            game.take_turn("我问陈伯这条路通到哪")
            model.reply = {"text": "我也不知道哪里卖那种地图。"}
            lines = game.take_turn("我去哪里可以得到一份前往王国的地图？", npc_id="keeper")
            self.assertEqual(lines[-1]["kind"], "dialogue")
            self.assertEqual(lines[-1]["text"], model.reply["text"])
            self.assertEqual(game.memory.records["keeper"]["daily_state"]["activity"], "正在擦拭罗盘。")
            restored = Session.open_save(game.session_dir)
            self.assertEqual(restored.dialogues["keeper"][-1]["npc"], model.reply["text"])
