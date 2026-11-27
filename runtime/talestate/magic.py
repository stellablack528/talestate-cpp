"""Typed spells, lessons, consumables and a compact elemental reaction table."""

from copy import deepcopy
from .engine import ActionError

ELEMENTS = {"fire": "火", "water": "水", "ice": "冰", "lightning": "雷"}
REACTIONS = {
    ("water", "fire"): ("蒸发", 2.0, 0), ("fire", "water"): ("蒸发", 1.5, 0),
    ("fire", "ice"): ("融化", 2.0, 0), ("ice", "fire"): ("融化", 1.5, 0),
    ("water", "ice"): ("冻结", 1.0, 0), ("ice", "water"): ("冻结", 1.0, 0),
    ("lightning", "water"): ("感电", 1.0, 4), ("water", "lightning"): ("感电", 1.0, 4),
    ("lightning", "fire"): ("超载", 1.0, 6), ("fire", "lightning"): ("超载", 1.0, 6),
    ("lightning", "ice"): ("超导", 1.0, 0), ("ice", "lightning"): ("超导", 1.0, 0),
}


def spellbook(engine):
    return {row["id"]: row for row in engine.world.get("spells", [])}


def lessons(engine, npc_id):
    book = spellbook(engine)
    known = engine.state.get("learned_spells", [])
    return [{**deepcopy(book[key]), "price": price, "learned": key in known}
            for key, price in engine.entities[npc_id].get("lessons", {}).items() if key in book]


def learn(engine, npc_id, spell_id):
    if npc_id not in engine.present_npcs():
        raise ActionError("请到老师面前学习法术。")
    lesson = next((row for row in lessons(engine, npc_id) if row["id"] == spell_id), None)
    if not lesson or lesson["learned"]:
        raise ActionError("这门法术不能重复学习，或老师尚未开设。")
    if engine.state["entities"][engine.player]["attributes"].get("coins", 0) < lesson["price"]:
        raise ActionError("学习费用不足。")
    state, events = deepcopy(engine.state), []
    state["minute"] += 10
    if lesson["price"]:
        engine._effect(state, events, {"op": "add_attribute", "entity": engine.player,
                       "attribute": "coins", "amount": -lesson["price"]}, "learn." + spell_id)
        engine._effect(state, events, {"op": "add_attribute", "entity": npc_id,
                       "attribute": "coins", "amount": lesson["price"]}, "learn." + spell_id)
    state.setdefault("learned_spells", []).append(spell_id)
    engine._event(state, events, "spell_learned", teacher=npc_id, spell=spell_id)
    engine._complete_quests(state, events)
    state["turn"] += 1
    state["events"].extend(events)
    engine.state = state
    return events


def can_use(engine, item_id):
    item = engine.items.get(item_id, {})
    effect = item.get("consumable", {})
    if engine.state["owners"].get(item_id) != engine.player or effect.get("resource") not in ("health", "mana"):
        return False
    attrs = engine.state["entities"][engine.player]["attributes"]
    resource = effect["resource"]
    maximum = attrs.get("max_" + resource, 100 if resource == "health" else 50)
    return attrs.get(resource, maximum) < maximum


def consume_effect(engine, state, events, item_id, entity):
    effect = engine.items.get(item_id, {}).get("consumable", {})
    if state["owners"].get(item_id) != entity or effect.get("resource") not in ("health", "mana"):
        raise ActionError("只能使用自己持有的可用药水。")
    attrs = state["entities"][entity]["attributes"]
    resource = effect["resource"]
    maximum = attrs.get("max_" + resource, 100 if resource == "health" else 50)
    old = attrs.get(resource, maximum)
    if old >= maximum:
        raise ActionError("已经是满血或满蓝，不会消耗药水。")
    attrs[resource] = min(maximum, old + effect["amount"])
    state["owners"][item_id] = "consumed"
    engine._event(state, events, "consumed", entity=entity, item=item_id, resource=resource,
                  amount=attrs[resource] - old, value=attrs[resource])


def use_item(engine, item_id):
    if engine.state.get("combat"):
        return engine.execute("combat.use." + item_id)
    state, events = deepcopy(engine.state), []
    state["minute"] += 1
    engine._effect(state, events, {"op": "consume", "entity": engine.player, "item": item_id}, "use." + item_id)
    engine._complete_quests(state, events)
    state["turn"] += 1
    state["events"].extend(events)
    engine.state = state
    return events
