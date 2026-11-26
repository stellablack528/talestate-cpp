"""One NPC's knowledge projection; no player inventory or global quest context."""

from .memory import witnessed_events


def delivery_recipients(engine, npc_id, context, player_input):
    known = {row["id"] for row in context["visible_people"]}
    npc = context["npc"]
    for event in npc["observations"]:
        known.update(event.get("participants", ()))
        if event["type"] in ("presence", "delivered"):
            known.update(event.get(field) for field in ("entity", "from", "to"))
    mentioned = (player_input, npc["profile"], *(row["text"] for row in npc["notes"]))
    return {key: engine.name(key) for key, entity in engine.entities.items()
            if entity["kind"] == "npc" and key != npc_id
            and (key in known or any(engine.name(key) in text for text in mentioned))}


def survey_locations(engine, context, player_input):
    scene = context["scene"]["id"]
    known = {scene, *(location for location, _ in engine.map.neighbors(scene))}
    npc = context["npc"]
    known.update(event.get("to_location") for event in npc["observations"] if event["type"] == "moved")
    return {key: engine.name(key) for key in engine.locations
            if key in known or engine.name(key) in player_input or engine.name(key) in npc["profile"]}


def role_context(engine, memory, npc_id, events, player_input):
    remembered = memory.records[npc_id]["observations"]
    current = witnessed_events(engine, npc_id, events, remembered)
    evidence = remembered + current
    quests = []
    for quest in engine.quests.values():
        key = quest["id"]
        if engine.state["quests"].get(key) == "expired":
            continue
        completed = any(e["type"] == "quest_completed" and e["quest"] == key for e in evidence)
        accepted = any(e["type"] == "quest_accepted" and e["quest"] == key for e in evidence)
        offered = quest.get("giver") == npc_id and not quest.get("hidden_until_active")
        if offered:
            accepting_actions = [a for a in engine.actions.values() if a.get("target") == npc_id and any(
                e["op"] == "activate_quest" and e["quest"] == key for e in a.get("effects", []))]
            if accepting_actions and not any(all(engine.condition(c) for c in a.get("preconditions", []) if c["op"] == "day")
                                            for a in accepting_actions):
                offered = False
        if not (completed or accepted or offered):
            continue
        quests.append({"id": key, "title": quest["title"], "description": quest.get("description", ""),
                       "known_status": "completed" if completed else "active" if accepted else "offered",
                       "giver": quest.get("giver"), "recipient": quest.get("recipient")})
    items = engine.items
    own_items = [{"id": key, "name": items[key]["name"], "description": items[key].get("description", ""),
                  **({"author": engine.name(items[key]["author"])} if "author" in items[key] else {})}
                 for key, owner in engine.state["owners"].items() if owner == npc_id]
    # Names are public. Other people's profiles, author_notes and memories are not.
    location = engine.state["entities"][npc_id]["location"]
    visible_people = [{"id": key, "name": "来访者" if key == engine.player else entity["name"]} for key, entity in engine.entities.items()
                      if engine.state["entities"][key]["location"] == location]
    context = memory.context(npc_id, player_input)
    clock = engine.clock()
    from .exchange import shop_stock
    return {"player_input": player_input, "npc": context,
            "clock": {key: clock[key] for key in ("day", "label", "night")},
            "scene": {"id": location, "name": engine.name(location)}, "visible_people": visible_people,
            "own_items": own_items, "known_quests": quests, "witnessed_action": current,
            "own_attributes": dict(engine.state["entities"][npc_id]["attributes"]),
            "shop_stock": shop_stock(engine, npc_id),
            "lessons": [{**row, "price": engine.entities[npc_id]["lessons"][row["id"]]}
                        for row in engine.world.get("spells", []) if row["id"] in engine.entities[npc_id].get("lessons", {})],
            "knowledge_policy": "私聊不共享；玩家自述未经证实；来访不证明来意；仅亲历事件可确认交付。"}
