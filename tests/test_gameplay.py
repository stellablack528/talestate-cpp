"""Core playthroughs: time, AI transactions, content and portable saves."""

from copy import deepcopy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading
import unittest
from zipfile import ZipFile

from talestate import Session
from talestate.engine import ActionError
from talestate.provider import ModelError

WORLD = Path(__file__).resolve().parents[1] / "worlds" / "lighthouse"


class TestModel:
    """Test-only model, never installed in the game or used as an API fallback."""
    configured = True
    api_key = "test"
    usage = {}

    def __init__(self):
        self.plan = {}
        self.contexts = []

    def json_reply(self, instructions, context):
        self.contexts.append(deepcopy(context))
        if context.get("phase") == "nightly":
            return {"facts": []}
        if context.get("phase") == "night_director":
            return {"meetings": []}
        if "actions" in context:
            return self.plan
        return {"text": "这是测试服务生成的人物回复。", "expression": "happy", "activity": "正在照看手头的事务。"}


def accept_and_deliver(game):
    game.perform("travel.village")
    game.perform("accept_delivery")
    game.perform("travel.lighthouse")
    game.perform("deliver_medicine")


class Gameplay(unittest.TestCase):
    def test_clock_routes_night_return_home_and_next_morning(self):
        with tempfile.TemporaryDirectory() as data:
            game = Session(WORLD, data)
            self.assertEqual(game.view()["clock"]["label"], "08:00")
            self.assertEqual(game.view()["map_size"], {"width": 12, "height": 8})
            game.perform("travel.village")
            self.assertEqual(game.view()["clock"]["label"], "08:30")
            game.perform("travel.lighthouse")
            self.assertEqual(game.view()["clock"]["label"], "10:30")
            self.assertEqual(game.engine.state["events"][-1]["minute"], 150)
            for _ in range(10):
                game.perform("wait")
            self.assertEqual(game.view()["clock"]["label"], "20:30")
            self.assertEqual([a["id"] for a in game.view()["actions"]], ["travel.village"])
            game.perform("travel.village")
            self.assertEqual([a["id"] for a in game.view()["actions"]], ["travel.home"])
            before = game.snapshot()
            with self.assertRaises(ActionError):
                game.perform("travel.lighthouse")
            self.assertEqual(before, game.snapshot())
            game.perform("travel.home")
            self.assertEqual(game.view()["clock"]["label"], "23:00")
            game.provider = TestModel()
            game.perform("sleep")
            self.assertEqual(game.view()["clock"]["day"], 2)
            self.assertEqual(game.view()["clock"]["label"], "08:00")
            self.assertFalse(game.view()["clock"]["night"])
            self.assertTrue(any(a["id"] == "travel.village" for a in game.view()["actions"]))
            game.save()
            self.assertEqual(game.snapshot(), Session.open_save(game.session_dir).snapshot())

    def test_ai_accept_delivery_reward_memory_and_restore(self):
        with tempfile.TemporaryDirectory() as data:
            game, model = Session(WORLD, data), TestModel()
            game.provider = model
            self.assertEqual(game.engine.state["owners"]["medicine"], "villager")
            game.perform("travel.village")
            model.plan = {"mode": "talk", "npc_id": "villager", "action_id": None}
            game.take_turn("你有什么委托？")
            self.assertEqual(game.engine.state["quests"]["deliver_medicine"], "available")
            self.assertEqual(game.engine.state["owners"]["medicine"], "villager")
            model.plan = {"action_id": "accept_delivery", "npc_id": "villager"}
            game.take_turn("我愿意帮忙，把药包给我吧")
            self.assertEqual(game.engine.state["quests"]["deliver_medicine"], "active")
            game.perform("travel.lighthouse")
            before_delivery = game.snapshot()
            model.plan = {"action_id": "deliver_medicine", "npc_id": "keeper"}
            game.take_turn("把林岚的药包交给陈伯")
            self.assertEqual(game.engine.state["owners"]["medicine"], "keeper")
            self.assertEqual(game.engine.state["entities"]["player"]["attributes"]["coins"], 5)
            self.assertEqual(game.engine.state["quests"]["deliver_medicine"], "completed")
            self.assertEqual(game.engine.state["quests"]["visit_keeper_tomorrow"], "active")
            self.assertNotIn("collect_compass", [a["id"] for a in game.view()["actions"]])
            self.assertEqual(game.view()["clock"]["label"], "10:45")
            with self.assertRaises(ActionError):
                game.perform("deliver_medicine")
            reply_context = model.contexts[-1]
            self.assertNotIn("result_state", reply_context)
            self.assertTrue(any(e["type"] == "quest_completed" for e in reply_context["witnessed_action"]))
            self.assertTrue(any(e["type"] == "quest_feedback" and e["result"] == "early" and e["npc"] == "keeper"
                                for e in reply_context["witnessed_action"]))
            self.assertEqual(reply_context["own_attributes"]["quest_trust"], 1)
            game.save()
            restored = Session.open_save(game.session_dir)
            self.assertEqual(game.snapshot(), restored.snapshot())
            restored.restore(before_delivery)
            self.assertEqual(restored.engine.state["owners"]["medicine"], "player")
            self.assertEqual(restored.dialogues.get("keeper", []), [])
            self.assertFalse(any(e["type"] == "quest_completed" for e in restored.memory.records["keeper"]["observations"]))

    def test_next_day_gift_then_free_exploration_and_ai_conversation(self):
        with tempfile.TemporaryDirectory() as data:
            game = Session(WORLD, data)
            game.provider = TestModel()
            accept_and_deliver(game)
            game.perform("travel.village")
            game.perform("travel.home")
            while not game.view()["clock"]["night"]:
                game.perform("wait")
            game.perform("sleep")
            game.perform("travel.village")
            game.perform("travel.lighthouse")
            game.perform("collect_compass")
            self.assertEqual(game.engine.state["owners"]["compass"], "player")
            self.assertEqual(game.engine.state["quests"]["visit_keeper_tomorrow"], "completed")
            before = dict(game.engine.state["owners"])
            game.perform(npc_id="keeper", player_input="说说你以前在海上的经历")
            self.assertEqual(before, game.engine.state["owners"])
            self.assertEqual(len(game.engine.state["quests"]), len(game.world["quests"]))
            with self.assertRaises(ActionError):
                game.perform("collect_compass")
            game.perform("travel.village")
            self.assertEqual(game.view()["location"]["id"], "village")

    def test_http_adapter_and_api_failure_leave_progress_unchanged(self):
        replies, requests = [], []
        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                requests.append(json.loads(self.rfile.read(int(self.headers["Content-Length"]))))
                status, message = replies.pop(0)
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(json.dumps({"choices": [{"message": {"content": message}}]}).encode())
            def log_message(self, *_):
                pass
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            with tempfile.TemporaryDirectory() as data:
                game = Session(WORLD, data)
                game.perform("travel.village")
                before = game.snapshot()
                with self.assertRaises(ModelError):
                    game.perform("accept_delivery")
                game.configure(f"http://127.0.0.1:{server.server_port}/v1", "test-only", "test-model")
                for status, message in [(503, "failure"), (200, "not-json"), (200, '{"text":""}')]:
                    replies.append((status, message))
                    with self.assertRaises(ModelError):
                        game.perform("accept_delivery")
                    self.assertEqual(before, game.snapshot())
                replies.append((200, '{"action_id":"invented","npc_id":null}'))
                with self.assertRaises(ModelError):
                    game.take_turn("凭空完成任务")
                self.assertEqual(before, game.snapshot())
                replies.append((200, '{"text":"测试HTTP角色回复","expression":"neutral","activity":"正在整理货架。"}'))
                game.perform("accept_delivery")
                self.assertEqual(game.engine.state["quests"]["deliver_medicine"], "active")
                self.assertIn("profile", json.loads(requests[-1]["messages"][1]["content"])["npc"])
                replies.extend([(200, '{"action_id":null,"npc_id":"villager"}'),
                    (200, json.dumps({"text": "我会记住你喜欢海风，也愿意相信你。", "activity": "正在整理货架。", "npc_update": {
                        "decision": "accept", "reason": "逐渐熟悉", "changes": {"attitude": "信任旅人"}},
                        "memory_update": {"facts": [{"who": "玩家", "when": "第1天", "what": "自述喜欢海风。"}], "plans": [{"id": "sort_shelves", "text": "整理今天的货架", "requires_player": False}]}}))])
                game.take_turn("我喜欢海风，你可以信任我", npc_id="villager")
                wire = requests[-2]["messages"]
                self.assertTrue(any(row["role"] == "assistant" and "测试HTTP角色回复" in row["content"] for row in wire))
                actual = json.loads((game.session_dir / "characters/villager.json").read_text(encoding="utf-8"))
                self.assertEqual(actual["settings"]["attitude"], "信任旅人")
                self.assertEqual(actual["notes"][0]["what"], "自述喜欢海风。")
                self.assertEqual(actual["plans"][0]["id"], "sort_shelves")
                self.assertNotIn("test-only", json.dumps(game.snapshot()))
        finally:
            server.shutdown()
            server.server_close()

    def test_generator_exports_the_same_time_and_quest_interface(self):
        from tools.world_builder import compile, export
        from tools.world_builder.builder import preview
        with tempfile.TemporaryDirectory() as data:
            export(compile(preview()), Path(data) / "world")
            game = Session(Path(data) / "world", Path(data) / "data")
            game.provider = TestModel()
            accept_and_deliver(game)
            self.assertEqual(game.engine.state["quests"]["deliver_medicine"], "completed")
            self.assertEqual(game.view()["clock"]["label"], "10:40")

    def test_portable_save_keeps_world_media_time_and_progress(self):
        from talestate.savegame import export_save, import_save
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game = Session(WORLD, root / "original")
            game.provider = TestModel()
            accept_and_deliver(game)
            (game.world_dir / "media/save-specific.txt").write_text("only this playthrough", encoding="utf-8")
            saved = game.snapshot()
            archive = export_save(game.session_dir, root / "journey.zip")
            folder = import_save(archive, root / "other-computer")
            restored = Session.open_save(folder)
            self.assertEqual(saved, restored.snapshot())
            self.assertEqual((restored.world_dir / "media/save-specific.txt").read_text(), "only this playthrough")
            self.assertNotEqual(restored.session_id, game.session_id)
            with ZipFile(root / "invalid.zip", "w") as invalid:
                invalid.writestr("../outside.txt", "escape")
            with self.assertRaises(ValueError):
                import_save(root / "invalid.zip", root / "other-computer")


if __name__ == "__main__":
    unittest.main()
