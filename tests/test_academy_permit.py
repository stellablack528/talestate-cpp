"""A signed permit must enter the bag and reach the archivist before copying."""

from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from talestate import Session
from talestate.engine import ActionError


WORLD = Path(__file__).resolve().parents[1] / "worlds" / "star_academy"
ITEM = "transcription_permit"
ISSUE = "issue_transcription_permit"
REGISTER = "register_transcription_permit"
COPY = "read_calibration_ledger"


class PermitModel:
    configured = True
    usage = {}

    def __init__(self):
        self.action = None
        self.accept = False
        self.role = {}

    def json_reply(self, instructions, context):
        if "actions" in context:
            if self.action:
                assert self.action in {action["id"] for action in context["actions"]}
            return {"action_id": self.action}
        self.role = context
        return {"text": "好。", "activity": "整理手边的册页。",
                "decision_outcome": "accept" if self.accept else "reject"}


class AcademyPermit(unittest.TestCase):
    def test_dialogue_issues_and_registers_real_proof(self):
        with TemporaryDirectory() as data:
            game = Session(WORLD, data)
            model = game.provider = PermitModel()
            game.perform("travel.court")
            with self.assertRaises(ActionError):
                game.engine.execute(ISSUE)
            model.action = ISSUE
            game.take_turn("请你给我一份当场抄校准簿的证明。", npc_id="tutor")
            self.assertNotIn(ITEM, game.engine.state["owners"])
            self.assertTrue(model.role["proposed_decision"]["items_to_issue"])
            model.accept = True
            game.take_turn("原簿会留在档案室，请签个许可。", npc_id="tutor")
            self.assertEqual(game.engine.state["owners"][ITEM], "player")
            self.assertIn(ITEM, {item["id"] for item in game.view()["inventory"]})
            self.assertNotIn(ISSUE, {action["id"] for action in game.view()["actions"]})

            game = Session.open_save(game.session_dir)
            game.provider = model
            game.perform("travel.archive")
            self.assertNotIn(COPY, {action["id"] for action in game.view()["actions"]})
            model.action = None
            game.take_turn("我拿到许可了。", npc_id="archivist")
            self.assertFalse(any(event["type"] == "delivered" for event in model.role["witnessed_action"]))
            self.assertNotIn(COPY, {action["id"] for action in game.view()["actions"]})

            model.action = REGISTER
            game.take_turn("把艾琳给我的证明交给你登记。", npc_id="archivist")
            self.assertEqual(game.engine.state["owners"][ITEM], "archivist")
            self.assertTrue(any(event["type"] == "delivered" and event["item"] == ITEM
                                for event in model.role["witnessed_action"]))
            game.perform(COPY)
            self.assertEqual(game.engine.state["quests"]["read_the_ring"], "completed")
            self.assertEqual(game.engine.state["quests"]["align_the_tower"], "active")
