"""Lesson evidence, elemental turns, mana and consumables all run without API."""

from copy import deepcopy
from pathlib import Path
import tempfile
import unittest

from talestate import Session
from talestate.engine import ActionError

WORLD = Path(__file__).resolve().parents[1] / "worlds/lighthouse"


class NoAPI:
    configured = False
    usage = {}

    def json_reply(self, *_):
        raise AssertionError("Magic mechanics must not call a model")


def setup(root):
    game = Session(WORLD, root)
    game.provider = NoAPI()
    game.perform("travel.village")
    return game


class Magic(unittest.TestCase):
    def test_lessons_staff_and_save_restore_preserve_private_evidence(self):
        with tempfile.TemporaryDirectory() as root:
            game = setup(root)
            before = game.snapshot()
            keeper = deepcopy(game.memory.records["keeper"])
            for key in ("firebolt", "waterbolt", "frostbolt", "spark"):
                game.learn("mage", key)
            self.assertEqual(len(game.view()["spells"]), 4)
            self.assertEqual(len([e for e in game.memory.records["mage"]["observations"] if e["type"] == "spell_learned"]), 4)
            self.assertEqual(game.memory.records["keeper"], keeper)
            snapshot = game.snapshot()
            with self.assertRaises(ActionError):
                game.learn("mage", "firebolt")
            self.assertEqual(game.snapshot(), snapshot)
            game.engine.state["entities"]["player"]["attributes"]["coins"] = 8
            game.buy("merchant", "shop_sword")
            game.equip("shop_sword")
            game.buy("mage", "apprentice_staff")
            game.equip("apprentice_staff")
            self.assertEqual((game.view()["stats"]["attack"], game.view()["stats"]["magic_attack"]), (6, 13))
            game.save()
            restored = Session.open_save(game.session_dir)
            self.assertEqual(restored.snapshot(), game.snapshot())
            restored.restore(before)
            self.assertEqual(restored.view()["spells"], [])
            self.assertFalse(any(e["type"] == "spell_learned" for e in restored.memory.records["mage"]["observations"]))

    def test_vaporize_and_same_element_resistance_with_insufficient_mana_rollback(self):
        with tempfile.TemporaryDirectory() as root:
            game = setup(root)
            game.learn("mage", "firebolt")
            game.learn("mage", "waterbolt")
            game.perform("travel.forest")
            game.perform("combat.start.slime_1")
            game.perform("combat.cast.firebolt")
            self.assertEqual(game.engine.state["enemies"]["slime_1"]["health"], 15)
            self.assertEqual(game.view()["stats"]["mana"], 44)
            self.assertEqual(game.view()["stats"]["health"], 94)
            game.engine.state["entities"]["player"]["attributes"]["mana"] = 0
            before = game.snapshot()
            with self.assertRaises(ActionError):
                game.perform("combat.cast.waterbolt")
            self.assertEqual(game.snapshot(), before)
            game.engine.state["entities"]["player"]["attributes"]["mana"] = 50
            game.perform("combat.cast.waterbolt")
            self.assertTrue(game.engine.state["enemies"]["slime_1"]["defeated"])
            self.assertEqual(game.view()["stats"]["health"], 94)
            self.assertEqual(game.view()["player"]["attributes"]["coins"], 2)
            self.assertTrue(any(e["type"] == "elemental_reaction" and e["reaction"] == "蒸发" and e["multiplier"] == 2
                                for e in game.engine.state["events"]))

    def test_freeze_cooldown_and_superconduct_physical_window(self):
        with tempfile.TemporaryDirectory() as root:
            game = setup(root)
            game.learn("mage", "frostbolt")
            game.learn("mage", "spark")
            game.perform("travel.forest")
            game.perform("combat.start.slime_2")
            game.engine.state["enemies"]["slime_2"]["health"] = 100
            game.perform("combat.cast.frostbolt")
            self.assertEqual(game.view()["stats"]["health"], 100)
            saved = game.snapshot()
            game = Session.open_save(game.session_dir)
            game.provider = NoAPI()
            self.assertEqual(game.snapshot(), saved)
            self.assertEqual(game.engine.state["enemies"]["slime_2"]["freeze_ready_round"], 3)
            game.perform("combat.cast.frostbolt")
            self.assertEqual(game.view()["stats"]["health"], 94)
            game.perform("combat.cast.frostbolt")
            self.assertEqual(game.view()["stats"]["health"], 94)
            game.perform("combat.flee")
            game.perform("combat.start.slime_3")
            game.engine.state["enemies"]["slime_3"]["health"] = 100
            game.perform("combat.cast.spark")
            expected = game.engine.state["enemies"]["slime_3"]["health"]
            for loss in (8, 8, 5):
                game.perform("combat.attack")
                expected -= loss
                self.assertEqual(game.engine.state["enemies"]["slime_3"]["health"], expected)

    def test_potions_are_consumed_once_combat_use_costs_turn_and_sleep_restores_mana(self):
        with tempfile.TemporaryDirectory() as root:
            game = setup(root)
            attrs = game.engine.state["entities"]["player"]["attributes"]
            attrs["coins"] = 8
            for key in ("health_potion_1", "health_potion_2", "mana_potion_1", "mana_potion_2"):
                game.buy("merchant", key)
            attrs = game.engine.state["entities"]["player"]["attributes"]
            attrs.update({"health": 95, "mana": 45})
            game.use_item("health_potion_1")
            game.use_item("mana_potion_1")
            self.assertEqual((game.view()["stats"]["health"], game.view()["stats"]["mana"]), (100, 50))
            self.assertEqual(game.engine.state["owners"]["mana_potion_1"], "consumed")
            before = game.snapshot()
            with self.assertRaises(ActionError):
                game.use_item("health_potion_2")
            with self.assertRaises(ActionError):
                game.use_item("mana_potion_1")
            self.assertEqual(game.snapshot(), before)
            game.perform("travel.forest")
            game.perform("combat.start.slime_4")
            game.engine.state["entities"]["player"]["attributes"].update({"health": 80, "mana": 0})
            game.use_item("mana_potion_2")
            self.assertEqual((game.view()["stats"]["health"], game.view()["stats"]["mana"]), (74, 25))
            self.assertEqual(game.view()["combat"]["round"], 1)
            game.perform("combat.flee")
            game.perform("travel.village")
            game.perform("travel.home")
            game.perform("wait_evening")
            from test_npc_day import DayModel
            game.provider = DayModel()
            game.perform("sleep")
            self.assertEqual((game.view()["stats"]["health"], game.view()["stats"]["mana"]), (100, 50))
            self.assertEqual(len(game.engine.state["enemies"]), 4)
            self.assertTrue(all(not enemy["defeated"] for enemy in game.engine.state["enemies"].values()))
            self.assertEqual(game.engine.state["owners"]["mana_potion_2"], "consumed")
