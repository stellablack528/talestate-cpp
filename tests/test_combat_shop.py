"""Local purchases, model price decisions, equipment and a complete slime day."""

from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest

from talestate import Session
from talestate.engine import ActionError
from talestate.exchange import shop_stock

WORLD = Path(__file__).resolve().parents[1] / "worlds/lighthouse"


class NoAPI:
    configured = False
    usage = {}

    def json_reply(self, *_):
        raise AssertionError("Local play must not call a model")


class PriceModel:
    configured = True
    usage = {}

    def json_reply(self, instructions, context):
        if "actions" in context:
            return {"action_id": None, "npc_id": "merchant"}
        return {"text": "好，燕麦面包现在三枚金币。", "activity": "正在整理货架上的面包。",
                "npc_update": {"decision": "accept", "reason": "接受商量后的新价格", "changes": {},
                               "shop_prices": {"shop_bread": 3}}}


class CombatAndShop(unittest.TestCase):
    def test_direct_purchase_and_equipment_slots_do_not_call_api(self):
        with tempfile.TemporaryDirectory() as root:
            game = Session(WORLD, root)
            game.provider = NoAPI()
            game.perform("travel.village")
            game.engine.state["entities"]["player"]["attributes"]["coins"] = 10
            game.buy("merchant", "shop_sword")
            game.buy("merchant", "shop_shield")
            game.equip("shop_sword")
            game.equip("shop_shield")
            self.assertEqual((game.view()["stats"]["health"], game.view()["stats"]["attack"], game.view()["stats"]["defense"]), (100, 10, 10))
            game.buy("merchant", "shop_dagger")
            game.equip("shop_dagger")
            self.assertEqual(game.view()["stats"]["attack"], 7)
            self.assertEqual(game.view()["equipment"]["weapon"], "shop_dagger")
            game.equip("shop_dagger", remove=True)
            self.assertEqual(game.view()["stats"]["attack"], 5)
            game.buy("merchant", "shop_bread")
            self.assertEqual(game.engine.state["entities"]["player"]["attributes"]["coins"], 0)
            before = game.snapshot()
            with self.assertRaises(ActionError):
                game.buy("merchant", "shop_armor")
            self.assertEqual(game.snapshot(), before)
            categories = {row["id"]: row["category"] for row in game.view()["inventory"]}
            self.assertEqual(categories["shop_bread"], "misc")
            self.assertEqual(categories["shop_sword"], "equipment")
            game.engine.state["owners"]["medicine"] = "player"
            self.assertEqual(next(i["category"] for i in game.view()["inventory"] if i["id"] == "medicine"), "quest")
            game.save()
            restored = Session.open_save(game.session_dir)
            self.assertEqual(restored.view()["stats"], game.view()["stats"])
            self.assertEqual(restored.snapshot(), game.snapshot())

    def test_ai_can_change_real_shop_prices_and_restore_them_with_progress(self):
        with tempfile.TemporaryDirectory() as root:
            game = Session(WORLD, root)
            game.perform("travel.village")
            game.engine.state["entities"]["player"]["attributes"]["coins"] = 6
            before = game.snapshot()
            game.provider = PriceModel()
            game.take_turn("你把燕麦面包改成三枚金币吧", "merchant")
            self.assertEqual(next(row["price"] for row in shop_stock(game.engine, "merchant") if row["id"] == "shop_bread"), 3)
            character = json.loads((game.session_dir / "characters/merchant.json").read_text(encoding="utf-8"))
            self.assertEqual(character["shop_prices"]["shop_bread"], 3)
            restored = Session.open_save(game.session_dir)
            restored.provider = NoAPI()
            restored.buy("merchant", "shop_bread")
            self.assertEqual(restored.engine.state["entities"]["player"]["attributes"]["coins"], 3)
            restored.restore(before)
            self.assertEqual(next(row["price"] for row in shop_stock(restored.engine, "merchant") if row["id"] == "shop_bread"), 1)

    def test_battle_rounds_guard_flee_and_mid_battle_save(self):
        with tempfile.TemporaryDirectory() as root:
            game = Session(WORLD, root)
            game.provider = NoAPI()
            game.perform("travel.village")
            game.perform("travel.forest")
            self.assertEqual(game.engine.clock()["label"], "08:45")
            game.perform("combat.start.slime_1")
            self.assertEqual({a["id"] for a in game.view()["actions"]}, {"combat.attack", "combat.guard", "combat.flee"})
            with self.assertRaises(ActionError):
                game.perform("travel.village")
            game.perform("combat.guard")
            self.assertEqual(game.view()["stats"]["health"], 97)
            game.perform("combat.attack")
            self.assertEqual(game.view()["stats"]["health"], 91)
            self.assertEqual(game.engine.state["enemies"]["slime_1"]["health"], 15)
            game.save()
            restored = Session.open_save(game.session_dir)
            self.assertEqual(restored.snapshot(), game.snapshot())
            restored.provider = NoAPI()
            restored.perform("combat.flee")
            self.assertIsNone(restored.view()["combat"])
            self.assertEqual(restored.engine.state["enemies"]["slime_1"]["health"], 15)

    def test_clear_forest_reward_once_and_sleep_refreshes_all_enemies(self):
        with tempfile.TemporaryDirectory() as root:
            game = Session(WORLD, root)
            game.provider = NoAPI()
            game.perform("travel.village")
            game.perform("travel.forest")
            for i in range(1, 4):
                game.perform("combat.start.slime_" + str(i))
                while game.view()["combat"]:
                    game.perform("combat.attack")
            self.assertEqual(game.view()["stats"]["health"], 46)
            self.assertEqual(game.view()["player"]["attributes"]["coins"], 6)
            before = game.snapshot()
            with self.assertRaises(ActionError):
                game.perform("combat.start.slime_1")
            self.assertEqual(before, game.snapshot())
            game.perform("travel.village")
            game.perform("travel.home")
            game.perform("wait_evening")
            from test_npc_day import DayModel
            game.provider = DayModel()
            game.perform("sleep")
            self.assertEqual(game.view()["stats"]["health"], 100)
            self.assertEqual(game.view()["clock"]["day"], 2)
            self.assertTrue(all(not row["defeated"] and row["health"] == 20 and row["spawn_day"] == 2
                                for row in game.engine.state["enemies"].values()))

    def test_defeat_returns_home_without_destroying_items_or_quests(self):
        with tempfile.TemporaryDirectory() as root:
            game = Session(WORLD, root)
            game.provider = NoAPI()
            game.perform("travel.village")
            game.perform("travel.forest")
            game.engine.state["entities"]["player"]["attributes"]["health"] = 1
            owners, quests = deepcopy(game.engine.state["owners"]), deepcopy(game.engine.state["quests"])
            game.perform("combat.start.slime_1")
            game.perform("combat.attack")
            self.assertEqual(game.view()["location"]["id"], "home")
            self.assertEqual(game.view()["stats"]["health"], 1)
            self.assertIsNone(game.view()["combat"])
            self.assertEqual(game.engine.state["owners"], owners)
            self.assertEqual(game.engine.state["quests"], quests)
