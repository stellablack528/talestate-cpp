"""Runtime-owned evidence, short visits and generated correspondence."""

from collections.abc import Mapping

from .provider import ModelError
from .quest_feedback import timing_issues


# Each alternative must name the object that makes it meaningful.
EVIDENCE_KEYS = {
    "delivered": ("item", "from", "to"),
    "combat_won": ("enemy",),
    "spell_learned": ("spell",),
    "clue_found": ("clue",),
    "appointment_attended": ("visit",),
    "npc_decided": ("npc", "decision"),
}


def alternatives(rule):
    return rule if isinstance(rule, list) else [rule]


def quest_items(quests):
    return {rule["item"] for quest in quests for rule in alternatives(quest["complete_when"])
            if rule.get("type") == "delivered"}


def valid_match(rule):
    if not isinstance(rule, Mapping):
        return False
    kind = rule.get("type")
    return isinstance(kind, str) and kind in EVIDENCE_KEYS and all(isinstance(rule.get(key), str) and rule[key]
                                         for key in EVIDENCE_KEYS[kind])


def matches(event, rule):
    return all(event.get(key) == value for key, value in rule.items())


def visit_actions(engine):
    state = engine.state
    location = state["entities"][engine.player]["location"]
    return [{"id": "visit." + key, "name": row["title"], "actor": engine.player,
             "target": row["npc"], "intent": "visit", "duration_minutes": 0,
             "effects": [{"op": "attend_visit", "visit": key}]}
            for key, row in state.get("visits", {}).items()
            if row["status"] == "open" and state["minute"] < row["expires"]
            and row["location"] == location and row["npc"] in engine.present_npcs()]


def schedule_visit(engine, opportunity, day):
    state, events = engine.state, []
    npc, location = opportunity["npc"], opportunity["location"]
    key = opportunity["id"]
    old = state["entities"][npc]["location"]
    state.setdefault("visits", {})[key] = {
        "npc": npc, "location": location, "origin": old, "title": opportunity["title"],
        "day": day, "expires": state["minute"] + opportunity["minutes"], "status": "open",
    }
    if old != location:
        engine._effect(state, events, {"op": "move", "entity": npc, "location": location}, "night.visit")
    engine._event(state, events, "visit_scheduled", visit=key, npc=npc, location=location,
                  title=opportunity["title"], day=day)
    state["events"].extend(events)
    return events


def expire_visits(engine):
    state, events = engine.state, []
    for key, row in state.get("visits", {}).items():
        if row["status"] in ("open", "attended") and state["minute"] >= row["expires"]:
            if row["status"] == "open":
                engine._event(state, events, "visit_missed", visit=key, npc=row["npc"], title=row["title"])
            row["status"] = "closed" if row["status"] == "attended" else "missed"
            if state["entities"][row["npc"]]["location"] == row["location"] and row["origin"] != row["location"]:
                engine._effect(state, events, {"op": "move", "entity": row["npc"], "location": row["origin"]}, "visit.return")
    state["events"].extend(events)
    return events


def expire_errands(engine, day):
    state, events = engine.state, []
    for key, quest in state.get("generated_quests", {}).items():
        if state["quests"][key] == "available" and day >= quest.get("expires_day", float("inf")):
            state["quests"][key] = "expired"
            engine._event(state, events, "quest_expired", quest=key, npc=quest["giver"])
    state["events"].extend(events)
    return events


def create_delivery_item(engine, state, events, author, recipient, name, description, kind="letter"):
    event = engine._event(state, events, "item_created", entity=author, recipient=recipient)
    key = kind + "." + event["event_id"]
    event["item"] = key
    state.setdefault("generated_items", {})[key] = {
        "id": key, "name": name, "description": description, "owner": author,
        "author": author, "recipient": recipient, "source_event_id": event["event_id"], "category": "quest"}
    state["owners"][key] = author
    return key


def offer_dialogue_quest(engine, giver, request, options, accept, follows=None):
    if not isinstance(request, dict):
        raise ModelError("新委托必须是对象或 null。")
    timing = {field: request[field] for field in ("deadline_minutes", "early_minutes") if field in request}
    for field, message in timing_issues(timing):
        raise ModelError(f"新委托的 {field} {message}。")
    kind = request.get("kind")
    if kind == "survey":
        return offer_survey(engine, giver, request, options["locations"], accept, follows, timing)
    if kind not in ("letter", "parcel"):
        raise ModelError("新委托类型需要是信件、包裹或实地查看。")
    recipient = request.get("recipient")
    if not isinstance(recipient, str) or recipient not in options["recipients"]:
        raise ModelError("新委托的接收人必须是该人物可以指认的 NPC。")
    title = quest_text(request, "title", 80)
    name = quest_text(request, "name", 36)
    description = quest_text(request, "description", 500)
    state = engine.state
    if any(quest.get("recipient") == recipient and engine.name(quest["complete_when"]["item"]).casefold() == name.casefold()
           for quest in pending_quests(engine, giver)):
        raise ModelError("已有同名道具的未结委托，请接取或继续原委托。")
    events = []
    item = create_delivery_item(engine, state, events, giver, recipient, name, description, kind)
    key = "conversation." + item
    create_errand(state, key, giver, recipient, engine.player, item, title, engine.clock()["day"])
    state["generated_quests"][key].update(timing)
    return finish_offer(engine, key, events, accept, follows)


def quest_text(request, field, limit):
    value = request.get(field)
    if not isinstance(value, str) or not value.strip() or len(value.strip()) > limit:
        raise ModelError(f"新委托的 {field} 需要是 1 至 {limit} 字的内容。")
    return value.strip()


def pending_quests(engine, giver):
    return (quest for key, quest in engine.state.get("generated_quests", {}).items()
            if quest["giver"] == giver and engine.state["quests"][key] in ("available", "active"))


def offer_survey(engine, giver, request, locations, accept, follows, timing):
    location = request.get("location")
    if not isinstance(location, str) or location not in locations:
        raise ModelError("查看地点必须是当前可指认的真实地点。")
    title = quest_text(request, "title", 80)
    focus = quest_text(request, "focus", 100)
    if any(q.get("survey") == {"location": location, "focus": focus} for q in pending_quests(engine, giver)):
        raise ModelError("这项查看已经有一条未结委托。")
    state, events = engine.state, []
    event = engine._event(state, events, "survey_offered", npc=giver, location=location, focus=focus)
    key = "survey." + event["event_id"]
    event["quest"] = key
    description = f"去{engine.name(location)}查看：{focus}。之后回来告诉{engine.name(giver)}。"
    state.setdefault("generated_quests", {})[key] = {
        "id": key, "title": title, "description": description, "giver": giver,
        "survey": {"location": location, "focus": focus}, "initial_status": "available",
        "generated_day": engine.clock()["day"], "complete_when": {"type": "survey_reported", "quest": key},
        "rewards": [], **timing}
    state["quests"][key] = "available"
    return finish_offer(engine, key, events, accept, follows)


def finish_offer(engine, key, events, accept, follows):
    state = engine.state
    if follows:
        state["generated_quests"][key]["follows"] = follows
        for event in events:
            event["turn"] = state["turn"]
    state["events"].extend(events)
    return events + engine.execute(key + ".accept") if accept else events


def survey_record(engine, key):
    return next((event for event in reversed(engine.state["events"])
                 if event["type"] == "survey_checked" and event["quest"] == key), None)


def survey_action(engine, quest, status, location, present):
    key, giver, survey = quest["id"], quest["giver"], quest["survey"]
    location_id, focus = survey["location"], survey["focus"]
    action = {"actor": engine.player, "duration_minutes": 5, "description": quest["description"]}
    if status == "available" and giver in present:
        return {**action, "id": key + ".accept", "name": "接下：" + quest["title"],
                "intent": "accept", "target": giver, "ai_interaction": True,
                "effects": [{"op": "activate_quest", "quest": key}]}
    if status == "active" and survey_record(engine, key):
        if giver in present:
            return {**action, "id": key + ".report", "name": "向" + engine.name(giver) + "汇报：" + focus,
                    "intent": "report", "target": giver, "ai_interaction": True,
                    "effects": [{"op": "survey_report", "quest": key}]}
    elif status == "active" and location == location_id:
        return {**action, "id": key + ".check", "name": "查看：" + focus, "intent": "investigate", "target": location_id,
                "effects": [{"op": "survey_check", "quest": key}]}
    return None


def create_errand(state, key, giver, recipient, player, item, title, day, receipt=False, expires_day=None):
    state.setdefault("generated_quests", {})[key] = {
        "id": key, "title": title, "description": title, "giver": giver, "recipient": recipient,
        "initial_status": "available", "generated_day": day, "return_receipt": receipt,
        "complete_when": {"type": "delivered", "item": item, "from": player, "to": recipient}, "rewards": []}
    state["quests"][key] = "available"
    if expires_day is not None:
        state["generated_quests"][key]["expires_day"] = expires_day


def issue_receipt(engine, state, events, quest):
    sender, addressee = quest["recipient"], quest["giver"]
    item = create_delivery_item(engine, state, events, sender, addressee, "回执",
                                f"{engine.name(sender)}确认已收到「{quest['title']}」对应的物品。")
    create_errand(state, "receipt." + quest["id"], sender, addressee, engine.player, item,
                  "把回执交给" + engine.name(addressee), engine.clock()["day"])
