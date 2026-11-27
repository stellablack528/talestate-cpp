"""The same dynamic quest and disposition rules apply across world packs."""

from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from talestate import Session
from talestate.engine import ActionError
from talestate.perspective import role_context

WORLDS = Path(__file__).resolve().parents[1] / "worlds"


class ErrandModel:
    configured = True
    usage = {}

    def __init__(self):
        self.plan = {}
        self.request = None
        self.accept_trade = False

    def json_reply(self, instructions, context):
        if "actions" in context:
            return self.plan
        return {"text": "好，我听明白了。", "new_errand": self.request,
                "trade_decision": "accept" if self.accept_trade else "reject"}


class DynamicErrands(unittest.TestCase):
    def test_use_report_or_sell_same_quest_in_two_worlds(self):
        cases = [
            ("lighthouse", "village", "baker", "merchant", "lighthouse", "潮线标记粉"),
            ("star_academy", "court", "tutor", "student", "archive", "誊写用描线粉"),
        ]
        for world, start, giver, buyer, destination, item_name in cases:
            with self.subTest(world=world), TemporaryDirectory() as data:
                game = Session(WORLDS / world, data)
                game.perform("travel." + start)
                model = game.provider = ErrandModel()
                game.engine.state["entities"][buyer]["attributes"]["coins"] = 4
                model.plan = {"accept_new_errand": True}
                model.request = {"kind": "use", "location": destination, "name": item_name,
                                 "description": "一小袋一次性粉末。", "purpose": "在入口留下一道记号",
                                 "motive": "我今天走不开，想留个方便辨认的记号。",
                                 "title": "留一道记号", "closing": "记号已经留好，这件事办妥了。"}
                game.take_turn("我带过去用，用完回来告诉你。", giver)
                model.request = None
                key, quest = next(iter(game.engine.state["generated_quests"].items()))
                item = quest["item"]
                self.assertIn("走不开", next(q for q in game.view()["quests"] if q["id"] == key)["description"])
                starting = game.snapshot()
                with self.assertRaises(ActionError):
                    game.use_item(item)
                self.assertEqual(game.snapshot(), starting)

                game.perform("travel." + destination)
                self.assertTrue(next(row for row in game.view()["inventory"] if row["id"] == item)["errand_usable"])
                game.use_item(item)
                with self.assertRaises(ActionError):
                    game.use_item(item)
                game = Session.open_save(game.session_dir)
                game.provider = model
                game.perform("travel." + start)
                done = game.perform(key + ".report")
                self.assertTrue(any(quest["closing"] in line["text"] for line in done))
                self.assertEqual(game.engine.state["quests"][key], "completed")
                self.assertEqual(game.view()["player"]["attributes"]["honor"], 1)
                with self.assertRaises(ActionError):
                    game.perform(key + ".report")

                game.restore(starting)
                model.plan = {"trade": {"give": [item], "receive": [], "coins": -2}}
                game.take_turn("我把道具两金币卖给你。", buyer)
                self.assertEqual(game.engine.state["owners"][item], "player")
                self.assertEqual(game.view()["player"]["attributes"]["honor"], 0)
                model.accept_trade = True
                game.take_turn("成交，两金币卖给你。", buyer)
                self.assertEqual(game.engine.state["owners"][item], buyer)
                self.assertEqual(game.view()["player"]["attributes"]["coins"], 2)
                self.assertEqual(game.engine.state["quests"][key], "failed")
                self.assertEqual(game.view()["player"]["attributes"]["honor"], -3)
                game = Session.open_save(game.session_dir)
                game.provider = model
                known = role_context(game.engine, game.memory, giver, [], "")["known_quests"]
                self.assertEqual(next(q["known_status"] for q in known if q["id"] == key), "active")
                self.assertFalse(any(e["type"] == "entrusted_items_sold" for e in game.memory.records[giver]["observations"]))
                self.assertEqual(game.view()["player"]["attributes"]["honor"], -3)
                game.perform("disclose." + key)
                penalty = -1 if giver == "baker" else -3
                self.assertEqual(game.engine.state["entities"][giver]["attributes"]["quest_trust"], penalty)
                with self.assertRaises(ActionError):
                    game.perform("disclose." + key)
