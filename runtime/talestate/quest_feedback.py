"""Quest timing, evidence and NPC reactions derived from committed outcomes."""

from itertools import chain

REACTIONS = {
    "strict": {"overdue": -3, "early": 2, "quality": 3, "on_time": 1, "late": 0, "breached": -3},
    "balanced": {"overdue": -2, "early": 1, "quality": 2, "on_time": 1, "late": 0, "breached": -2},
    "patient": {"overdue": -1, "early": 1, "quality": 2, "on_time": 1, "late": 0, "breached": -1},
}


def timing_issues(quest):
    deadline = quest.get("deadline_minutes")
    early = quest.get("early_minutes")
    if "deadline_minutes" in quest and (type(deadline) is not int or deadline < 1):
        yield "deadline_minutes", "需要正整数分钟"
    if "early_minutes" in quest and (type(early) is not int or early < 1 or
            type(deadline) is not int or early >= deadline):
        yield "early_minutes", "需要小于期限的正整数分钟"


def accepted_minute(state, key, pending=()):
    # A task that starts active has been open since minute zero.
    return next((event["minute"] for event in chain(reversed(pending), reversed(state["events"]))
                 if event["type"] == "quest_accepted" and event["quest"] == key), 0)


def due_minute(quest, state, pending=()):
    duration = quest.get("deadline_minutes")
    return None if duration is None else accepted_minute(state, quest["id"], pending) + duration


def deadline_label(engine, quest):
    due = due_minute(quest, engine.state)
    if due is None:
        return None
    day, minute = divmod(engine.world.get("clock", {}).get("start_minute", 480) + due, 1440)
    return f"第 {day + 1} 天 {minute // 60:02d}:{minute % 60:02d}"


def feedback(engine, state, events, quest, npc, result):
    actor = engine.entities.get(npc)
    if not actor or actor["kind"] != "npc":
        return
    style = actor.get("quest_reaction", "balanced")
    requested = REACTIONS[style][result]
    attributes = state["entities"][npc]["attributes"]
    before = attributes.get("quest_trust", 0)
    attributes["quest_trust"] = max(-100, min(100, before + requested))
    engine._event(state, events, "quest_feedback", quest=quest["id"], npc=npc,
                  result=result, style=style, trust_change=attributes["quest_trust"] - before)


def mark_overdue(engine, state, events, quest, minute):
    due = due_minute(quest, state, events)
    if due is None or minute <= due:
        return False
    late = state.setdefault("overdue_quests", {})
    if quest["id"] not in late:
        late[quest["id"]] = due
        feedback(engine, state, events, quest, quest.get("giver"), "overdue")
    return True


def settle_deadlines(engine):
    state, notices = engine.state, []
    for key, quest in engine.quests.items():
        if state["quests"][key] == "active":
            mark_overdue(engine, state, notices, quest, state["minute"])
    for event in notices:
        event["turn"] = state["turn"]
    state["events"].extend(notices)
    return notices


def reward_feedback(engine, state, events, quest, proof):
    from .story import alternatives, matches

    quality = quest.get("quality_when")
    if "deadline_minutes" not in quest and not quality:
        return
    late = mark_overdue(engine, state, events, quest, proof["minute"])
    early = quest.get("early_minutes")
    evidence = state["events"] + events
    if quality and any(matches(event, rule) for rule in alternatives(quality) for event in evidence):
        result = "quality"
    elif late:
        result = "late"
    elif early is not None and proof["minute"] - accepted_minute(state, quest["id"], events) <= early:
        result = "early"
    else:
        result = "on_time"
    if proof["type"] == "delivered":
        witness = proof["to"] if proof["from"] == engine.player else proof["from"]
    else:
        witness = proof.get("npc") or proof.get("teacher")
        giver = quest.get("giver")
        if witness is None and giver in engine.entities and proof.get("location") == state["entities"][giver]["location"]:
            witness = giver
    feedback(engine, state, events, quest, witness, result)
