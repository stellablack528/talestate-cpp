"""New economy, night processing order and private NPC social progression."""

from copy import deepcopy
import json
from pathlib import Path
import tempfile
import threading
import unittest

from talestate import Session
from talestate.engine import ActionError
from talestate.provider import ModelError

WORLD = Path(__file__).resolve().parents[1] / "worlds/lighthouse"


class LifeModel:
    configured = True
    api_key = "test"
    usage = {}

    def __init__(self):
        self.contexts = []
        self.trade = None
        self.decision = "accept"
        self.social = False

    def json_reply(self, instructions, context):
        self.contexts.append(deepcopy(context))
        phase = context.get("phase")
        if phase == "nightly":
            return {"facts": []}
        if phase == "night_director":
            return {"meetings": [{"participants": ["merchant", "baker"], "topic": "daily"}] if self.social else []}
        if phase == "night_participant":
            if context["npc_id"] == "merchant":
                return {"activity": "收好货物，明天继续营业。", "share_facts": [],
                        "request": {"item": "shop_cheese", "recipient": "baker", "title": "帮马丁把乳酪送给周穗"}}
            return {"activity": "与马丁聊过之后，整理明天的面粉。", "share_facts": ["fact.0"]}
        if "actions" in context:
            return {"action_id": None, "npc_id": context["selected_npc"], "trade": deepcopy(self.trade)}
        return {"text": "好，我愿意。" if self.decision == "accept" else "抱歉，这件我想留着。",
                "activity": "正在照看自己的摊位。", "trade_decision": self.decision}


class DailyLife(unittest.TestCase):
    def test_story_routes_require_evidence_and_missed_visit_keeps_investigation_open(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            fixture = root / "fixture"
            fixture.mkdir()
            world = json.loads((WORLD / "world.json").read_text(encoding="utf-8"))
            clue = {"type": "clue_found", "clue": "observatory_log"}
            promise = {"type": "npc_decided", "npc": "villager", "decision": "invite_keeper"}
            world["actions"] += [
                {"id": "search_log", "name": "Check log", "actor": "player", "target": "village", "intent": "investigate",
                 "preconditions": [{"op": "at", "entity": "player", "location": "village"}],
                 "effects": [{"op": "investigate", "entity": "player", "location": "village", "clue": "observatory_log"}]},
                {"id": "ask_help", "name": "Ask for help", "actor": "player", "target": "villager", "intent": "request",
                 "ai_interaction": True, "effects": [{"op": "npc_decision", "npc": "villager", "decision": "invite_keeper"}]},
            ]
            world["quests"].append({"id": "test_chapter", "title": "Find cause", "initial_status": "active",
                "complete_when": [clue, {"type": "appointment_attended", "visit": "keeper_help"}],
                "rewards": [{"op": "add_attribute", "entity": "player", "attribute": "coins", "amount": 1}]})
            world["night_opportunities"] = [{"id": "keeper_help", "npc": "keeper", "location": "village",
                "title": "Visit keeper", "minutes": 60, "preconditions": [{"op": "evidence", "match": promise}]}]
            (fixture / "world.json").write_text(json.dumps(world), encoding="utf-8")

            class StoryModel(LifeModel):
                def json_reply(self, instructions, context):
                    if context.get("phase") == "night_director":
                        return {"meetings": [], "visit": next((row["id"] for row in context["opportunities"]), None)}
                    reply = super().json_reply(instructions, context)
                    if context.get("proposed_decision"):
                        reply["decision_outcome"] = self.decision
                    return reply

            for route in ("investigate", "attend", "miss"):
                game, model = Session(fixture, root), StoryModel()
                game.provider = model
                game.perform("travel.village")
                if route == "investigate":
                    game.perform("search_log")
                    game.perform("search_log")
                else:
                    model.decision = "reject"
                    game.perform("ask_help")
                    self.assertFalse(any(e["type"] == "npc_decided" for e in game.engine.state["events"]))
                    model.decision = "accept"
                    game.perform("ask_help")
                    game.perform("travel.home")
                    game.perform("wait_evening")
                    game.perform("sleep")
                    game = Session.open_save(game.session_dir)
                    game.provider = model
                    self.assertEqual(game.engine.state["entities"]["keeper"]["location"], "village")
                    game.perform("travel.village")
                    if route == "miss":
                        game.perform("wait")
                        self.assertEqual(game.engine.state["visits"]["keeper_help"]["status"], "missed")
                        self.assertEqual(game.engine.state["entities"]["keeper"]["location"], "lighthouse")
                        with self.assertRaises(ActionError):
                            game.perform("visit.keeper_help")
                        game.perform("search_log")
                    else:
                        game.perform("visit.keeper_help")
                        with self.assertRaises(ActionError):
                            game.perform("visit.keeper_help")
                self.assertEqual(game.engine.state["quests"]["test_chapter"], "completed")
                self.assertEqual(game.view()["player"]["attributes"]["coins"], 1)
                evidence = [e["type"] for e in game.engine.state["events"]]
                self.assertIn("appointment_attended" if route == "attend" else "clue_found", evidence)

    def test_generated_letter_and_return_receipt_survive_a_saved_game(self):
        class LetterModel(LifeModel):
            def json_reply(self, instructions, context):
                if context.get("phase") == "night_participant":
                    request = None
                    if context["npc_id"] == "merchant":
                        request = {"letter": {"name": "Private letter", "text": "Please save me a loaf."},
                                   "recipient": "baker", "title": "Deliver a note", "return_receipt": True}
                    return {"activity": "Preparing for tomorrow.", "request": request}
                return super().json_reply(instructions, context)

        with tempfile.TemporaryDirectory() as root:
            game, model = Session(WORLD, root), LetterModel()
            game.provider = model
            model.social = True
            game.perform("wait_evening")
            game.perform("sleep")
            quest_id, quest = next(iter(game.engine.state["generated_quests"].items()))
            letter = quest["complete_when"]["item"]
            self.assertIn(letter, game.engine.items)
            self.assertNotIn(letter, game.engine.base_items)
            game.perform("travel.village")
            game.perform(quest_id + ".accept")
            self.assertIn(letter, {row["id"] for row in game.view()["inventory"]})
            game = Session.open_save(game.session_dir)
            game.provider = model
            game.perform(quest_id + ".deliver")
            follow_up = "receipt." + quest_id
            self.assertEqual(game.engine.state["quests"][follow_up], "available")
            game.perform(follow_up + ".accept")
            game.perform(follow_up + ".deliver")
            with self.assertRaises(ActionError):
                game.perform(follow_up + ".deliver")
            self.assertEqual(game.engine.state["quests"][follow_up], "completed")
            self.assertEqual(len(game.engine.state["generated_items"]), 2)

    def test_dialogue_creates_new_delivery_with_player_consent(self):
        class ErrandModel(LifeModel):
            request = None
            action = None
            accept = False

            def json_reply(self, instructions, context):
                reply = super().json_reply(instructions, context)
                if "actions" in context:
                    if self.action:
                        assert self.action in {row["id"] for row in context["actions"]}
                    return {**reply, "action_id": self.action, "accept_new_errand": self.accept}
                if self.request is not None:
                    assert context["new_errand"]["player_agreed_to_deliver"] is self.accept
                    reply["new_errand"] = self.request
                return reply

        for accept in (False, True):
            with self.subTest(accept_immediately=accept), tempfile.TemporaryDirectory() as root:
                game = Session(WORLD, root)
                model = game.provider = ErrandModel()
                game.perform("travel.village")
                model.request = {"kind": "parcel", "name": "试吃饼干小包", "description": "周穗刚包好的两块试吃饼干。",
                                 "recipient": "missing_npc", "title": "把试吃饼干捎给陈伯"}
                before = game.snapshot()
                with self.assertRaises(ModelError):
                    game.take_turn("你能包点饼干吗？", "baker")
                self.assertEqual(game.snapshot(), before)
                model.request["recipient"] = "keeper"
                model.accept = accept
                game.take_turn("请包点试吃饼干，我送给陈伯。" if accept else "能给陈伯包点试吃饼干吗？", "baker")
                quest_id, quest = next(iter(game.engine.state["generated_quests"].items()))
                item = quest["complete_when"]["item"]
                if not accept:
                    self.assertEqual(game.engine.state["owners"][item], "baker")
                    self.assertNotIn(quest_id, {row["id"] for row in game.view()["quests"]})
                    model.action, model.request = quest_id + ".accept", None
                    game.take_turn("行，我帮你把饼干带去。", "baker")
                self.assertEqual(game.engine.state["quests"][quest_id], "active")
                self.assertIn(item, {row["id"] for row in game.view()["inventory"]})
                self.assertEqual(game.engine.state["quests"]["deliver_medicine"], before["state"]["quests"]["deliver_medicine"])
                self.assertFalse(any(event.get("item") == item for event in game.memory.records["keeper"]["observations"]))

                game = Session.open_save(game.session_dir)
                model = game.provider = ErrandModel()
                model.trade = {"give": [item], "receive": [], "coins": 0}
                with self.assertRaises(ActionError):
                    game.take_turn("那包饼干给你。", "merchant")
                self.assertEqual(game.engine.state["owners"][item], "player")
                model.trade = None
                game.perform("travel.lighthouse")
                model.action = quest_id + ".deliver"
                game.take_turn("我把周穗包的试吃饼干交给你。", "keeper")
                self.assertEqual(game.engine.state["owners"][item], "keeper")
                self.assertEqual(game.engine.state["quests"][quest_id], "completed")
                displayed = next(row for row in model.contexts[-1]["own_items"] if row["id"] == item)
                self.assertEqual(displayed["author"], "周穗")
                with self.assertRaises(ActionError):
                    game.perform(quest_id + ".deliver")

    def test_dialogue_survey_requires_visit_and_can_offer_followup(self):
        class SurveyModel(LifeModel):
            action = None
            quest = None

            def json_reply(self, instructions, context):
                reply = super().json_reply(instructions, context)
                if "actions" in context:
                    return {**reply, "action_id": self.action, "accept_new_errand": True}
                return {**reply, "new_errand": self.quest}

        with tempfile.TemporaryDirectory() as root:
            game = Session(WORLD, root)
            model = game.provider = SurveyModel()
            game.perform("travel.village")
            model.quest = {"kind": "survey", "location": "lighthouse", "focus": "看看灯塔路上起雾没有", "title": "看海路的雾"}
            game.take_turn("我去灯塔看看路上有没有雾，回来告诉你。", "baker")
            key = next(iter(game.engine.state["generated_quests"]))
            self.assertEqual(game.engine.state["quests"][key], "active")
            for suffix in (".check", ".report"):
                with self.assertRaises(ActionError):
                    game.perform(key + suffix)
            game.perform("travel.lighthouse")
            game.perform(key + ".check")
            self.assertFalse(any(event["type"] == "survey_checked" for event in game.memory.records["baker"]["observations"]))
            game = Session.open_save(game.session_dir)
            game.provider = model
            game.perform("travel.village")
            model.action = key + ".report"
            model.quest = {"kind": "letter", "recipient": "villager", "title": "带口信给林岚",
                           "name": "周穗的便条", "description": "路上有雾，晚点出门。"}
            game.take_turn("我已经到灯塔看过了，路上有雾。", "baker")
            self.assertEqual(game.engine.state["quests"][key], "completed")
            followup = next(q for q in game.engine.state["generated_quests"].values() if q.get("follows") == key)
            self.assertEqual(game.engine.state["quests"][followup["id"]], "available")
            self.assertEqual(game.engine.state["owners"][followup["complete_when"]["item"]], "baker")
            self.assertTrue(any(e["type"] == "survey_reported" for e in model.contexts[-1]["witnessed_action"]))

    def test_purchase_gift_decline_and_no_partial_exchange(self):
        with tempfile.TemporaryDirectory() as root:
            game, model = Session(WORLD, root), LifeModel()
            game.provider = model
            game.perform("travel.village")
            game.engine.state["entities"]["player"]["attributes"]["coins"] = 5
            model.trade = {"give": [], "receive": ["shop_bread"], "coins": 1}
            game.take_turn("我要买一个燕麦面包", "merchant")
            self.assertEqual(game.engine.state["owners"]["shop_bread"], "player")
            self.assertEqual(game.engine.state["entities"]["player"]["attributes"]["coins"], 4)
            self.assertEqual(game.engine.state["entities"]["merchant"]["attributes"]["coins"], 21)
            model.trade = {"give": [], "receive": ["shop_sword"], "coins": 4}
            game.take_turn("再买把铁剑", "merchant")
            self.assertEqual(game.engine.state["entities"]["player"]["attributes"]["coins"], 0)
            before = game.snapshot()
            model.trade = {"give": [], "receive": ["shop_shield"], "coins": 3}
            with self.assertRaises(ActionError):
                game.take_turn("再买木盾", "merchant")
            self.assertEqual(game.snapshot(), before)
            model.trade = {"give": ["shop_bread"], "receive": [], "coins": 0}
            model.decision = "reject"
            game.take_turn("请你吃面包", "baker")
            self.assertEqual(game.engine.state["owners"]["shop_bread"], "player")
            model.decision = "accept"
            game.take_turn("这个新鲜的面包送给你", "baker")
            self.assertEqual(game.engine.state["owners"]["shop_bread"], "baker")
            response_context = model.contexts[-1]
            self.assertNotIn("player_items", response_context)
            game.save()
            self.assertEqual(Session.open_save(game.session_dir).snapshot(), game.snapshot())
            self.assertNotIn("shop_bread", [row["id"] for row in game.view()["inventory"]])

    def test_social_shares_only_selected_facts_and_creates_a_real_optional_errand(self):
        with tempfile.TemporaryDirectory() as root:
            game, model = Session(WORLD, root), LifeModel()
            game.provider = model
            model.social = True
            game.memory.remember("merchant", {"facts": [{"who": "旅人", "what": "秘密暗号紫琥珀", "when": "第1天"}]}, "private.1", 1)
            game.memory.remember("baker", {"facts": [{"who": "周穗", "what": "明天准备多烤燕麦面包", "when": "第1天"}]}, "private.2", 1)
            keeper = deepcopy(game.memory.records["keeper"])
            owners = deepcopy(game.engine.state["owners"])
            game.perform("wait_evening")
            game.perform("sleep")
            self.assertEqual(game.engine.clock()["day"], 2)
            contexts = [c for c in model.contexts if c.get("phase") == "night_participant"]
            self.assertEqual(len(contexts), 2)
            self.assertNotIn("紫琥珀", json.dumps(contexts[1], ensure_ascii=False))
            director = [c for c in model.contexts if c.get("phase") == "night_director"][0]
            self.assertIn("紫琥珀", json.dumps(director, ensure_ascii=False))
            self.assertTrue(any("多烤" in row["what"] for row in game.memory.records["merchant"]["notes"]))
            self.assertNotIn("紫琥珀", json.dumps(game.memory.records["baker"], ensure_ascii=False))
            self.assertEqual(game.memory.records["keeper"], keeper)
            self.assertEqual(game.engine.state["owners"], owners)
            key = next(iter(game.engine.state["generated_quests"]))
            self.assertNotIn(key, [q["id"] for q in game.view()["quests"]])
            game = Session.open_save(game.session_dir)
            game.provider = model
            game.perform("travel.village")
            self.assertTrue(game.has_task_opening("merchant"))
            game.perform(key + ".accept")
            self.assertEqual(game.engine.state["owners"]["shop_cheese"], "player")
            self.assertEqual(game.engine.state["quests"][key], "active")
            game.perform(key + ".deliver")
            self.assertEqual(game.engine.state["quests"][key], "completed")
            self.assertEqual(game.engine.state["owners"]["shop_cheese"], "baker")
            with self.assertRaises(ActionError):
                game.perform(key + ".deliver")

    def test_fast_forward_discards_an_in_flight_night_reply(self):
        import importlib.util

        spec = importlib.util.spec_from_file_location("client_sleep", WORLD.parents[1] / "clients/renpy/game/python-packages/talestate_client.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        started, release = threading.Event(), threading.Event()

        class WaitingModel(LifeModel):
            def json_reply(self, instructions, context):
                if context.get("phase") == "nightly":
                    started.set()
                    if not release.wait(5):
                        raise ModelError("测试等待超时")
                return super().json_reply(instructions, context)

        with tempfile.TemporaryDirectory() as root:
            game = Session(WORLD, root)
            game.provider = WaitingModel()
            game.perform("travel.village")
            game.perform(npc_id="baker", player_input="你好")
            game.perform("travel.home")
            game.perform("wait_evening")
            client = module.Client(WORLD.parents[1] / "clients/renpy")
            client.session = game
            client.perform("sleep")
            try:
                self.assertTrue(started.wait(3))
                self.assertTrue(client.sleeping)
                self.assertEqual(game.engine.clock()["day"], 1)
                client.fast_forward_sleep()
                client.fast_forward_sleep()
                self.assertFalse(client.busy)
                self.assertEqual(game.engine.clock()["day"], 2)
                committed = game.snapshot()
                self.assertEqual(Session.open_save(game.session_dir).engine.state, game.engine.state)
            finally:
                release.set()
                client.thread.join(5)
            self.assertFalse(client.thread.is_alive())
            client.poll()
            self.assertEqual(game.snapshot(), committed)
            self.assertNotIn("night_director", [ctx.get("phase") for ctx in game.provider.contexts])

    def test_invalid_npc_night_reply_cannot_block_morning(self):
        for malformed, expected in (
            ({"share_facts": []}, "activity"),
            ({"activity": "收好货物", "share_facts": ["fact.unknown"]}, "分享事实 ID"),
        ):
            with self.subTest(malformed=malformed), tempfile.TemporaryDirectory() as root:
                class BadNightModel(LifeModel):
                    def json_reply(self, instructions, context):
                        if context.get("phase") == "night_participant":
                            return malformed
                        return super().json_reply(instructions, context)

                game = Session(WORLD, root)
                game.provider = BadNightModel()
                game.provider.social = True
                game.perform("wait_evening")
                game.engine.state["night_in_progress"] = True  # A save left by the old sleep flow.
                game.save()
                game = Session.open_save(game.session_dir)
                game.provider = BadNightModel()
                game.provider.social = True
                game.perform("sleep")
                self.assertEqual((game.engine.clock()["day"], game.engine.clock()["label"]), (2, "08:00"))
                self.assertNotIn("night_in_progress", game.engine.state)
                self.assertNotIn("npc_encounter", [event["type"] for event in game.engine.state["events"]])
                reports = sorted((game.session_dir / "diagnostics").glob("*.json"))
                self.assertEqual(len(reports), 1)
                self.assertIn(expected, json.loads(reports[0].read_text(encoding="utf-8"))["reason"])
