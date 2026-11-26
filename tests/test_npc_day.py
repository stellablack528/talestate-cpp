"""Conversation continuity, persistent task openings and private daily progress."""

from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest

from talestate import Session
from talestate.engine import ActionError
from talestate.provider import ModelError

WORLD = Path(__file__).resolve().parents[1] / "worlds/lighthouse"


class DayModel:
    configured = True
    api_key = "test"
    usage = {}

    def __init__(self):
        self.contexts = []
        self.fail_night = False
        self.fail_director = False

    def json_reply(self, instructions, context):
        self.contexts.append(deepcopy(context))
        if context.get("phase") == "night_director":
            if self.fail_director:
                raise ModelError("模拟日常统筹失败")
            return {"meetings": []}
        if context.get("phase") == "nightly":
            if self.fail_night:
                raise ModelError("模拟夜间服务失败")
            return {"facts": [{"who": "玩家", "when": "第1天", "what": "表示愿意等林岚整理好货架再聊。", "source": "玩家自述"}]}
        if "actions" in context:
            return {"action_id": None, "npc_id": context["selected_npc"]}
        if context["player_input"] == "今天打算做什么？":
            return {"text": "我今天要整理货架，也还等着你帮忙送药。", "activity": "正在整理货架。", "memory_update": {
                "facts": [{"who": "林岚", "when": "第1天", "what": "答应旅人今天整理货架。", "kind": "agreement"}], "plans": [
                    {"id": "shelves", "text": "整理货架", "requires_player": False},
                    {"id": "delivery", "text": "等玩家将药交给陈伯", "requires_player": True}]}}
        return {"text": "我记得上次的约定，会接着聊。", "activity": "正在照看店铺。"}


def go_to_bed(game):
    if game.view()["location"]["id"] != "home":
        game.perform("travel.home")
    if not game.engine.clock()["night"]:
        game.perform("wait_evening")


class NPCDay(unittest.TestCase):
    def test_ambient_status_is_cached_and_only_a_new_task_calls_model(self):
        import importlib.util
        spec = importlib.util.spec_from_file_location("client_check", WORLD.parents[1] / "clients/renpy/game/python-packages/talestate_client.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as data:
            game, model = Session(WORLD, data), DayModel()
            game.provider = model
            game.perform("travel.village")
            client = module.Client(WORLD.parents[1] / "clients/renpy")
            client.session = game
            client.open_chat("baker")
            self.assertFalse(client.busy)
            self.assertEqual(model.contexts, [])
            client.open_chat("villager")
            client.thread.join(timeout=2)
            client.poll()
            self.assertFalse(client.error)
            self.assertTrue(client.npc_activity("villager"))
            count = len(model.contexts)
            client.open_chat("villager")
            self.assertEqual(len(model.contexts), count)
            client.session = Session.open_save(game.session_dir)
            client.session.provider = model
            self.assertTrue(client.npc_activity("villager"))
            client.open_chat("villager")
            self.assertEqual(len(model.contexts), count)

    def test_long_term_memory_keeps_important_facts_not_a_full_transcript(self):
        class FactsModel(DayModel):
            def json_reply(self, instructions, context):
                return {"text": "这是详细回复原文，不应成为长期记忆。", "activity": "正在售卖刚出炉的面包。",
                        "memory_update": {"facts": [{"who": "玩家", "when": "第1天早上", "what": "自述名叫罗文", "source": "player_claim"}]}}
        with tempfile.TemporaryDirectory() as data:
            game = Session(WORLD, data)
            game.provider = FactsModel()
            game.perform("travel.village")
            game.perform(npc_id="villager", player_input="这是很长的闲聊原文，我名叫罗文。")
            record = game.memory.records["villager"]
            self.assertNotIn("conversations", record)
            self.assertEqual(record["notes"][0]["who"], "玩家")
            self.assertEqual(record["notes"][0]["what"], "自述名叫罗文")
            memory_text = (game.session_dir / "npcs/villager.json").read_text(encoding="utf-8")
            self.assertNotIn("很长的闲聊原文", memory_text)
            self.assertNotIn("详细回复原文", memory_text)
            self.assertTrue((game.session_dir / "dialogues/villager.json").exists())
            restored = Session.open_save(game.session_dir)
            self.assertEqual(game.dialogues, restored.dialogues)
            self.assertNotIn("conversations", restored.memory.context("villager"))

    def test_offline_sleep_preserves_unfinished_delivery(self):
        with tempfile.TemporaryDirectory() as data:
            game = Session(WORLD, data)
            game.provider = DayModel()
            game.perform("travel.village")
            game.perform("accept_delivery")
            self.assertEqual(game.engine.state["quests"]["deliver_medicine"], "active")
            owners = deepcopy(game.engine.state["owners"])
            go_to_bed(game)
            self.assertEqual(game.engine.clock()["label"], "20:00")
            self.assertNotIn("deliver_medicine", game.engine.state.get("overdue_quests", {}))
            game.configure("https://api.deepseek.com/v1", "", "deepseek-flash")
            morning = game.perform("sleep")
            self.assertEqual((game.engine.clock()["day"], game.engine.clock()["label"]), (2, "08:00"))
            self.assertTrue(any("林岚对逾期很不满" in line["text"] for line in morning))
            self.assertEqual(game.engine.state["owners"], owners)
            self.assertEqual(game.engine.state["quests"]["deliver_medicine"], "active")
            saved = Session.open_save(game.session_dir)
            self.assertEqual(saved.engine.state, game.engine.state)
            game = saved
            with self.assertRaises(ActionError):
                game.perform("sleep")
            game.perform("travel.village")
            self.assertNotIn("wait_evening", [a["id"] for a in game.view()["actions"]])
            model = game.provider = DayModel()
            game.take_turn("药还送得过去吗？", npc_id="villager")
            self.assertEqual(model.contexts[-1]["own_attributes"]["quest_trust"], -3)
            self.assertTrue(any(quest["id"] == "deliver_medicine" and quest["known_feedback"] == "overdue"
                                for quest in model.contexts[-1]["known_quests"]))
            self.assertNotIn("quest_trust", game.engine.state["entities"]["baker"]["attributes"])
            game.perform("travel.lighthouse")
            game.perform("deliver_medicine")
            self.assertEqual(game.engine.state["quests"]["deliver_medicine"], "completed")
            self.assertEqual(game.view()["player"]["attributes"]["coins"], 5)
            feedback = [event["result"] for event in game.engine.state["events"]
                        if event["type"] == "quest_feedback" and event["quest"] == "deliver_medicine"]
            self.assertEqual(feedback, ["overdue", "late"])

    def test_task_opening_does_not_repeat_after_revisit_or_reload(self):
        with tempfile.TemporaryDirectory() as data:
            game, model = Session(WORLD, data), DayModel()
            game.provider = model
            game.perform("travel.village")
            self.assertTrue(game.has_task_opening("villager"))
            game.greet("villager")
            count = len(model.contexts)
            game.greet("villager")
            self.assertEqual(len(model.contexts), count)
            self.assertFalse(game.has_task_opening("baker"))
            game.perform("travel.home")
            game.perform("travel.village")
            restored = Session.open_save(game.session_dir)
            restored.provider = model
            restored.greet("villager")
            self.assertEqual(len(model.contexts), count)

    def test_plans_complete_on_sleep_without_sharing_private_memories(self):
        with tempfile.TemporaryDirectory() as data:
            game, model = Session(WORLD, data), DayModel()
            game.provider = model
            game.perform("travel.village")
            game.take_turn("今天打算做什么？", npc_id="villager")
            game.take_turn("那就等你整理好再聊", npc_id="villager")
            planner = [c for c in model.contexts if "actions" in c][-1]
            self.assertIn("整理货架", planner["conversation_history"][-1]["npc"])
            self.assertEqual(planner["npc_context"]["notes"][-1]["kind"], "agreement")
            self.assertEqual(model.contexts[-1]["npc"]["notes"][-1]["kind"], "agreement")
            self.assertIn("爽快", model.contexts[-1]["npc"]["voice"])
            before_sleep = game.snapshot()
            others = deepcopy(game.memory.records["keeper"])
            owners, quests = deepcopy(game.engine.state["owners"]), deepcopy(game.engine.state["quests"])
            go_to_bed(game)
            model.fail_director = True
            calls = len(model.contexts)
            lines = game.perform("sleep")
            self.assertEqual([row.get("phase") for row in model.contexts[calls:]], ["nightly", "night_director"])
            self.assertEqual(game.engine.clock()["day"], 2)
            self.assertTrue(any("没能整理" in line["text"] for line in lines))
            record = game.memory.records["villager"]
            self.assertEqual(record["summary_days"]["1"]["status"], "completed")
            self.assertNotIn("night_in_progress", game.engine.state)
            self.assertEqual([p["status"] for p in record["plans"]], ["completed", "planned"])
            self.assertIn("已完成", record["plans"][0]["result"])
            self.assertEqual(game.memory.records["keeper"], others)
            self.assertEqual(game.engine.state["owners"], owners)
            self.assertEqual(game.engine.state["quests"], quests)
            disk = json.loads((game.session_dir / "characters/villager.json").read_text(encoding="utf-8"))
            self.assertEqual(disk["plans"][0]["status"], "completed")
            restored = Session.open_save(game.session_dir)
            self.assertEqual(restored.memory.records["villager"], record)
            restored.provider = model
            restored.perform("travel.village")
            restored.take_turn("货架整理好了吗？", npc_id="villager")
            self.assertEqual(model.contexts[-1]["npc"]["plans"][0]["status"], "completed")
            restored.restore(before_sleep)
            self.assertEqual(restored.memory.records["villager"]["plans"][0]["status"], "planned")
            self.assertEqual(restored.memory.records["villager"]["daily_log"], [])
            legacy = deepcopy(before_sleep)
            for row in legacy["npc_memories"].values():
                for field in ("notes", "plans", "daily_state", "daily_log", "task_openings"):
                    row.pop(field, None)
            restored.restore(legacy)
            self.assertEqual(restored.memory.records["villager"]["notes"], [])
            self.assertEqual(restored.memory.records["villager"]["plans"], [])

    def test_full_current_day_history_and_only_talked_npc_is_summarized(self):
        with tempfile.TemporaryDirectory() as data:
            game, model = Session(WORLD, data), DayModel()
            game.provider = model
            game.perform("travel.village")
            game.perform(npc_id="villager", player_input="今天打算做什么？")
            for i in range(25):
                game.perform(npc_id="villager", player_input="闲聊" + str(i))
            self.assertEqual(len(game.recent_dialogues("villager")), 12)
            self.assertEqual(len(model.contexts[-1]["conversation_history"]), 12)
            self.assertEqual(model.contexts[-1]["npc"]["notes"][0]["kind"], "agreement")
            go_to_bed(game)
            game.perform("sleep")
            summaries = [c for c in model.contexts if c.get("phase") == "nightly"]
            self.assertEqual(len(summaries), 2)
            self.assertTrue(all(row["npc_id"] == "villager" for row in summaries))
            self.assertEqual(sum(len(row["dialogues"]) for row in summaries), 26)
            self.assertEqual(game.recent_dialogues("villager"), [])
            self.assertEqual(len(game.dialogues["villager"]), 26)
            self.assertNotIn("summary_days", game.memory.records["keeper"])


if __name__ == "__main__":
    unittest.main()
