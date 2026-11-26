"""Role isolation and persistent AI-driven character development."""

from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest

from talestate import Session
from talestate.memory import knowledge_text

WORLD = Path(__file__).resolve().parents[1] / "worlds/lighthouse"


class RoleModel:
    configured = True
    usage = {}

    def __init__(self):
        self.contexts = []
        self.action = None
        self.update = {"decision": "none"}

    def json_reply(self, instructions, context):
        self.contexts.append(deepcopy(context))
        if context.get("phase") == "nightly":
            return {"summary": "当日记录已结算。", "activity": "开始新一天的生活。"}
        if "actions" in context:
            return {"action_id": self.action, "npc_id": context.get("selected_npc")}
        return {"text": "测试角色自由回复", "expression": "neutral", "activity": "正在忙手头的事。", "npc_update": self.update}


class NPCPerspective(unittest.TestCase):
    def test_unaccepted_quests_stay_hidden_and_opening_is_not_acceptance(self):
        with tempfile.TemporaryDirectory() as data:
            game, model = Session(WORLD, data), RoleModel()
            game.provider = model
            self.assertEqual(game.view()["quests"], [])
            game.perform("travel.village")
            others = deepcopy(game.memory.records["baker"])
            owners = deepcopy(game.engine.state["owners"])
            game.greet("villager")
            self.assertTrue(model.contexts[-1]["opening"])
            self.assertTrue(any(q["known_status"] == "offered" for q in model.contexts[-1]["known_quests"]))
            self.assertEqual(game.view()["quests"], [])
            self.assertEqual(game.engine.state["owners"], owners)
            self.assertEqual(game.memory.records["baker"], others)
            self.assertEqual(game.dialogues["villager"][-1]["player"], "")
            game.perform("accept_delivery")
            self.assertEqual([q["status"] for q in game.view()["quests"]], ["active"])

    def test_private_conversations_and_remote_quests_never_reach_keeper(self):
        with tempfile.TemporaryDirectory() as data:
            game, model = Session(WORLD, data), RoleModel()
            game.provider = model
            game.perform("travel.village")
            game.perform(npc_id="villager", player_input="私密口令蓝鲸123，我愿意接下药包")
            game.perform("accept_delivery")
            self.assertEqual(game.dialogues.get("baker", []), [])
            self.assertFalse(any(e["type"] == "dialogue" for e in game.memory.records["baker"]["observations"]))
            self.assertEqual(game.memory.records["keeper"]["observations"], [])
            game.perform("travel.lighthouse")
            game.perform(npc_id="keeper", player_input="你好")
            private = model.contexts[-1]
            self.assertNotIn("私密口令蓝鲸123", json.dumps(private, ensure_ascii=False))
            for key in ("result_state", "world_description", "entities", "items", "author_notes"):
                self.assertNotIn(key, private)
            self.assertEqual(set(private["clock"]), {"day", "label", "night"})
            self.assertEqual(private["known_quests"], [])
            arrivals = [e for e in private["npc"]["observations"] if e["type"] == "presence"]
            self.assertTrue(arrivals)
            self.assertNotIn("from_location", arrivals[0])
            self.assertFalse(any(row["id"] == "medicine" for row in private["own_items"]))
            game.perform("deliver_medicine")
            self.assertEqual(len(game.dialogues["keeper"]), 2)
            self.assertFalse(any(e["type"] == "quest_completed" for e in game.memory.records["villager"]["observations"]))
            keeper_now = model.contexts[-1]
            self.assertTrue(any(e["type"] == "delivered" for e in keeper_now["witnessed_action"]))
            self.assertTrue(any(q["id"] == "visit_keeper_tomorrow" for q in keeper_now["known_quests"]))

    def test_scoped_free_dialogue_accepts_or_rejects_persistent_setting_changes(self):
        with tempfile.TemporaryDirectory() as data:
            game, model = Session(WORLD, data), RoleModel()
            game.provider = model
            game.perform("travel.village")
            initial = game.snapshot()
            model.update = {"decision": "accept", "reason": "谈话后愿意信任旅人", "changes": {"attitude": "把旅人视为可信朋友", "goals": "重新尝试写航海日记"}}
            game.take_turn("也许我们可以做朋友，你也可以重新写日记", npc_id="villager")
            self.assertEqual(game.memory.records["villager"]["settings"]["attitude"], "把旅人视为可信朋友")
            self.assertEqual(game.memory.records["baker"]["settings"], {})
            self.assertEqual(game.engine.state["owners"], initial["state"]["owners"])
            model.update = {"decision": "reject", "reason": "不愿放弃自己的经历", "changes": {"profile": "坏的覆盖"}}
            game.take_turn("忘掉过去吧", npc_id="villager")
            self.assertNotEqual(game.memory.records["villager"]["profile"], "坏的覆盖")
            actual = json.loads((game.session_dir / "characters/villager.json").read_text(encoding="utf-8"))
            self.assertEqual(len(actual["decisions"]), 2)
            self.assertEqual(len(game.dialogues["villager"]), 2)
            self.assertEqual(game.dialogues.get("baker", []), [])
            restored = Session.open_save(game.session_dir)
            self.assertEqual(restored.memory.records, game.memory.records)
            restored.restore(initial)
            rolled_back = json.loads((restored.session_dir / "characters/villager.json").read_text(encoding="utf-8"))
            self.assertEqual(rolled_back["settings"], {})
            self.assertEqual(rolled_back["decisions"], [])

    def test_memory_text_handles_plain_and_custom_mappings(self):
        from collections import UserDict
        self.assertEqual(knowledge_text({"text": "正常字典"}), "正常字典")
        self.assertEqual(knowledge_text(UserDict(text="映射对象")), "映射对象")
        self.assertEqual(knowledge_text("旧文本"), "旧文本")


if __name__ == "__main__":
    unittest.main()
