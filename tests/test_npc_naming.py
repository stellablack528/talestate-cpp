"""Author naming policy is preserved in generation and enforced at export."""

from io import BytesIO
import json
from unittest import TestCase
from unittest.mock import patch

from tools.world_builder.builder import BuilderError, MEDIEVAL_NAME_REFERENCE, compile, generate, preview


class NPCNaming(TestCase):
    def test_passes_naming_references_and_rejects_renamed_or_wrong_script_npcs(self):
        sample = preview()
        policy = {"style": "沿海村庄用中文称呼", "script": "han", "fixed_names": {"keeper": "陈伯"},
                  "forbidden_names": ["王爷"]}
        response = {"choices": [{"message": {"content": json.dumps(sample)}}]}
        with patch("tools.world_builder.builder.urlopen", return_value=BytesIO(json.dumps(response).encode())) as api:
            draft = generate(sample["sources"], npc_naming=policy, naming_references=MEDIEVAL_NAME_REFERENCE)
        wire = json.loads(api.call_args.args[0].data)
        context = json.loads(wire["messages"][-1]["content"])
        self.assertEqual(context["npc_naming"], policy)
        self.assertIn("坎特伯雷故事集", context["naming_references"])
        compiled = compile(draft)
        self.assertEqual(compiled["definition"]["npc_naming"], policy)
        self.assertNotIn("naming_references", compiled["definition"])

        keeper = next(npc for npc in draft["definition"]["entities"] if npc["id"] == "keeper")
        for unsuitable in ("Jason", "王爷", "老陈"):
            with self.subTest(name=unsuitable):
                keeper["name"] = unsuitable
                with self.assertRaises(BuilderError):
                    compile(draft)
