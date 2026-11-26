"""Separate persistent NPC records, scoped to one playthrough."""

from copy import deepcopy
import json
from pathlib import Path
from collections.abc import Mapping
import re


def knowledge_text(value):
    """Normalize outside Ren'Py's rebound dict type (RevertableDict)."""
    return str(value.get("text", "")) if isinstance(value, Mapping) else str(value)


def _terms(text):
    terms = set(re.findall(r"[a-z0-9]+", text.lower()))
    for run in re.findall(r"[\u3400-\u9fff]+", text):
        terms.update(run[i:i + 2] for i in range(len(run) - 1))
    return terms


def select_notes(notes, question="", limit=16):
    recent = list(reversed(notes))
    terms = _terms(question)
    relevant = sorted(recent, key=lambda row: len(terms & _terms(row.get("text", ""))), reverse=True)
    chosen = {}
    pinned = [row for row in recent if row.get("kind") in ("agreement", "important")]
    for row in recent[:4] + pinned[:4] + relevant + recent:
        chosen.setdefault(id(row), row)
        if len(chosen) == limit:
            break
    return [{**row, "what": row.get("what", "")[:400], "text": row.get("text", "")[:480]}
            for row in notes if id(row) in chosen]


def witnessed_events(engine, npc_id, events, previous=()):
    """Project events into what one person actually saw; conversations are private."""
    result = []
    location = engine.state["entities"][npc_id]["location"]
    proofs = {e["event_id"] for e in previous}
    for event in events:
        kind = event["type"]
        visible = False
        if kind == "dialogue":
            visible = event["npc"] == npc_id
        elif kind == "delivered":
            visible = npc_id in (event["from"], event["to"])
            if visible:
                proofs.add(event["event_id"])
        elif kind == "quest_completed":
            visible = event.get("evidence_id") in proofs
        elif kind == "quest_accepted":
            quest = engine.quests.get(event["quest"], {})
            visible = quest.get("giver") == npc_id and ("survey" in quest or any(
                e["type"] == "delivered" and npc_id in (e["from"], e["to"]) for e in events))
        elif kind == "npc_encounter":
            visible = npc_id in event["participants"]
        elif kind == "spell_learned":
            visible = npc_id == event["teacher"]
        elif kind in ("appointment_attended", "visit_scheduled", "visit_missed", "npc_decided",
                     "quest_expired", "survey_offered", "survey_reported"):
            visible = npc_id == event["npc"]
        elif kind == "item_created":
            visible = npc_id == event["entity"]
        elif kind == "consumed":
            visible = npc_id == event["entity"]
        elif kind == "moved":
            if event["entity"] == npc_id:
                visible = True
            elif location in (event["from_location"], event["to_location"]):
                result.append({"event_id": event["event_id"], "type": "presence",
                               "entity": event["entity"], "location": location,
                               "arrived": location == event["to_location"],
                               "turn": event["turn"], "minute": event["minute"]})
        if visible:
            result.append(deepcopy(event))
            proofs.add(event["event_id"])
    return result


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_suffix(path.suffix + ".tmp")
    pending.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")
    pending.replace(path)


class MemoryStore:
    def __init__(self, world, world_dir, session_dir):
        self.path = Path(session_dir) / "npcs"
        self.records = {}
        for npc in world["entities"]:
            if npc["kind"] != "npc":
                continue
            seed = {}
            if npc.get("memory_file"):
                seed_path = Path(world_dir) / npc["memory_file"]
                if seed_path.exists():
                    seed = json.loads(seed_path.read_text(encoding="utf-8-sig"))
            self.records[npc["id"]] = {
                "npc_id": npc["id"], "profile": seed.get("profile", npc.get("profile", "")),
                "voice": seed.get("voice") or npc.get("voice") or "按自身经历、性格和当下心情自然决定措辞和长短。",
                "known": deepcopy(seed.get("initial_memories", [])), "observations": [], "expression": "neutral",
                "knowledge_version": 2, "settings": {}, "setting_decisions": [],
                "notes": [], "plans": [], "daily_state": {}, "daily_log": [], "task_openings": [],
            }
        self.initial_records = deepcopy(self.records)

    def observe(self, engine, events, player_input):
        for npc_id, memory in self.records.items():
            for event in witnessed_events(engine, npc_id, events, memory["observations"]):
                if event["type"] == "dialogue" and event["npc"] == npc_id:
                    memory["expression"] = event.get("expression", "neutral")
                else:
                    memory["observations"].append(deepcopy(event))

    def context(self, npc_id, question=""):
        memory = self.records[npc_id]
        plans = memory.get("plans", [])
        open_plans = [row for row in plans if row["status"] == "planned"]
        terms = _terms(question)
        relevant_plans = sorted(reversed(open_plans), key=lambda row: len(terms & _terms(row["text"])), reverse=True)
        chosen_plans = relevant_plans[:12] + [row for row in plans if row["status"] != "planned"][-4:]
        return {"npc_id": npc_id, "profile": memory["profile"], "initial_memories": memory["known"],
                "voice": memory["voice"],
                "settings": deepcopy(memory.get("settings", {})),
                "notes": select_notes(memory.get("notes", []), question),
                "plans": [{**row, "text": row["text"][:400], **({"result": row["result"][:440]} if "result" in row else {})}
                          for row in plans if row in chosen_plans],
                "daily_state": deepcopy(memory.get("daily_state", {})),
                "shop_prices": deepcopy(memory.get("shop_prices", {})),
                "recent_days": deepcopy(memory.get("daily_log", [])[-3:]),
                "observations": memory["observations"][-12:]}

    def remember(self, npc_id, update, event_id, day, minute=None):
        if not update:
            return
        record = self.records[npc_id]
        for fact in update.get("facts", []):
            if not isinstance(fact, Mapping) or not isinstance(fact.get("what"), str) or not fact["what"].strip():
                continue
            who, what = str(fact.get("who", npc_id)), fact["what"].strip()
            if any(row.get("who") == who and row.get("what") == what for row in record.setdefault("notes", [])):
                continue
            when = str(fact.get("when", "第" + str(day) + "天"))
            kind = fact.get("kind", "fact")
            record["notes"].append({"who": who, "when": when, "what": what, "text": who + "，" + when + "，" + what,
                "event_id": event_id, "day": day, "minute": minute, "source": "personal_conversation",
                "reported_source": str(fact.get("source", "statement")),
                "kind": kind if kind in ("agreement", "important") else "fact"})
        for proposed in update.get("plans", []):
            key, text = proposed.get("id"), proposed.get("text")
            if not isinstance(key, str) or not isinstance(text, str) or not text.strip():
                continue
            existing = next((p for p in record.setdefault("plans", []) if p["id"] == key), None)
            if existing:
                # Mentioning a finished plan again must not silently reopen it.
                continue
            record["plans"].append({"id": key, "text": text, "status": "planned", "day": day,
                                    "due_day": proposed.get("due_day", day) if isinstance(proposed.get("due_day", day), int) else day,
                                    "requires_player": bool(proposed.get("requires_player", False)),
                                    "source_event_id": event_id})
        activity = update.get("activity")
        if isinstance(activity, str) and activity.strip():
            record.setdefault("daily_state", {}).update({"activity": activity, "day": day})

    def restore(self, records, engine):
        self.records = deepcopy(self.initial_records)
        for npc_id, seed in self.records.items():
            if npc_id not in records:
                continue
            saved = deepcopy(records[npc_id])
            saved.pop("conversations", None)
            dialogue_events = [e for e in saved.get("observations", []) if e["type"] == "dialogue"]
            saved.setdefault("expression", dialogue_events[-1].get("expression", "neutral") if dialogue_events else "neutral")
            saved["observations"] = [e for e in saved.get("observations", []) if e["type"] != "dialogue"]
            for decision in saved.get("setting_decisions", []):
                decision.pop("player_input", None)
            for key in ("voice", "notes", "plans", "daily_state", "daily_log", "task_openings"):
                saved.setdefault(key, deepcopy(seed.get(key, {} if key == "daily_state" else [])))
            if saved.get("knowledge_version") != 2:
                # Old broadcasts are not retained as character knowledge.
                saved["observations"] = witnessed_events(engine, npc_id, saved.get("observations", []))
                saved["profile"] = seed["profile"]
                saved["known"] = seed["known"]
                saved["knowledge_version"] = 2
                saved.setdefault("settings", {})
                saved.setdefault("setting_decisions", [])
            self.records[npc_id] = saved

    def apply_setting_decision(self, npc_id, decision, player_input, event_id):
        if decision is not None and not isinstance(decision, Mapping):
            raise ValueError("模型返回的角色设定决策格式不正确，进度未改变。")
        if not decision or decision.get("decision", "none") == "none":
            return None
        outcome = decision.get("decision")
        if outcome not in ("accept", "reject"):
            raise ValueError("模型返回的角色设定决策无法识别，进度未改变。")
        allowed = {"profile", "personality", "attitude", "goals", "beliefs"}
        changes = decision.get("changes", {}) if outcome == "accept" else {}
        if not isinstance(changes, Mapping) or any(key not in allowed or not isinstance(value, str)
                                                  for key, value in changes.items()):
            raise ValueError("角色设定修改只能包含背景、性格、态度、目标和个人认知文本。")
        record = self.records[npc_id]
        if outcome == "accept":
            record.setdefault("settings", {}).update(deepcopy(changes))
            if "profile" in changes:
                record["profile"] = changes["profile"]
        audit = {"decision": outcome, "reason": str(decision.get("reason", "")),
                 "changes": deepcopy(changes), "event_id": event_id}
        record.setdefault("setting_decisions", []).append(audit)
        return audit

    def flush(self):
        for npc_id, record in self.records.items():
            write_json(self.path / (npc_id + ".json"), record)
            write_json(self.path.parent / "characters" / (npc_id + ".json"), {
                "npc_id": npc_id, "profile": record["profile"], "voice": record["voice"], "settings": record.get("settings", {}),
                "decisions": record.get("setting_decisions", []), "notes": record.get("notes", []),
                "plans": record.get("plans", []), "daily_state": record.get("daily_state", {}),
                "daily_log": record.get("daily_log", []), "shop_prices": record.get("shop_prices", {})})
