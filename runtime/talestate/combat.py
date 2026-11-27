"""Deterministic equipment and turn-based combat. No model calls."""

from copy import deepcopy
from math import ceil
from .engine import ActionError
from .story import quest_items


def stats(engine):
    base = engine.state["entities"][engine.player]["attributes"]
    result = {"max_health": base.get("max_health", 100), "health": base.get("health", 100),
              "attack": base.get("attack", 5), "defense": base.get("defense", 5),
              "mana": base.get("mana", 50), "max_mana": base.get("max_mana", 50),
              "magic_attack": base.get("magic_attack", 8)}
    for key in engine.state.get("equipment", {}).values():
        if engine.state["owners"].get(key) == engine.player:
            for attribute, amount in engine.items[key].get("bonuses", {}).items():
                if attribute in ("attack", "defense", "magic_attack"):
                    result[attribute] += amount
    return result


def damage(attack, defense):
    return max(1, ceil(attack * 20 / (20 + max(0, defense))))


def inventory(engine):
    quest_goods = quest_items(engine.quests.values())
    use_actions = {action["item"] for action in engine.available_actions()
                   if action["intent"] == "use" and "item" in action}
    rows = []
    for key, owner in engine.state["owners"].items():
        if owner != engine.player:
            continue
        row = deepcopy(engine.items[key])
        row["category"] = "quest" if key in quest_goods or row.get("category") == "quest" else "equipment" if row.get("slot") or row.get("kind") in ("weapon", "equipment") else "misc"
        row["equipped"] = key in engine.state.get("equipment", {}).values()
        from .magic import can_use
        row["usable"] = can_use(engine, key)
        row["errand_usable"] = key in use_actions
        rows.append(row)
    return rows


def equip(engine, item_id, remove=False):
    if engine.state.get("combat"):
        raise ActionError("战斗中不能更换装备。")
    item = engine.items.get(item_id, {})
    if engine.state["owners"].get(item_id) != engine.player or not item.get("slot"):
        raise ActionError("只能装备自己持有的武器或护具。")
    state, events = deepcopy(engine.state), []
    slots = state.setdefault("equipment", {})
    if remove:
        if slots.get(item["slot"]) != item_id:
            raise ActionError("这件物品尚未装备。")
        slots.pop(item["slot"])
    else:
        slots[item["slot"]] = item_id
    engine._event(state, events, "equipment_changed", item=item_id, equipped=not remove)
    state["events"].extend(events)
    engine.state = state
    return events


def reset_enemies(engine, state, day):
    state["enemies"] = {row["id"]: {"health": row["health"], "defeated": False, "spawn_day": day}
                        for row in engine.world.get("encounters", [])}
    state["combat"] = None


def actions(engine):
    if engine.state.get("combat"):
        from .magic import spellbook, can_use
        available = [{"id": "combat." + key, "name": name, "intent": "combat", "actor": engine.player}
                for key, name in (("attack", "攻击"), ("guard", "防御"), ("flee", "脱离战斗"))]
        mana = stats(engine)["mana"]
        available.extend({"id": "combat.cast." + key, "name": row["name"], "intent": "combat", "actor": engine.player}
                         for key, row in spellbook(engine).items() if key in engine.state.get("learned_spells", []) and mana >= row["mana_cost"])
        available.extend({"id": "combat.use." + key, "name": "使用" + engine.name(key), "intent": "combat", "actor": engine.player}
                         for key in engine.items if can_use(engine, key))
        return available
    if engine.clock()["night"]:
        return []
    location = engine.state["entities"][engine.player]["location"]
    return [{"id": "combat.start." + row["id"], "name": "挑战" + row["name"], "intent": "combat", "actor": engine.player}
            for row in engine.world.get("encounters", []) if row["location"] == location
            and not engine.state.get("enemies", {}).get(row["id"], {}).get("defeated", False)]


def execute(engine, action_id):
    if action_id not in {row["id"] for row in actions(engine)}:
        raise ActionError("当前不能进行这项战斗操作。")
    state, events = deepcopy(engine.state), []
    health = state["entities"][engine.player]["attributes"]
    health.setdefault("health", 100)
    if action_id.startswith("combat.start."):
        key = action_id.removeprefix("combat.start.")
        for field in ("freeze_ready_round", "physical_weak_until"):
            state["enemies"][key].pop(field, None)
        state["combat"] = {"enemy": key, "round": 0}
        engine._event(state, events, "combat_started", enemy=key)
    else:
        fight = state["combat"]
        key = fight["enemy"]
        enemy = next(row for row in engine.world["encounters"] if row["id"] == key)
        target = state["enemies"][key]
        state["minute"] += 1
        fight["round"] += 1
        hero = stats(engine)
        if action_id == "combat.flee":
            state["combat"] = None
            for field in ("freeze_ready_round", "physical_weak_until"):
                target.pop(field, None)
            engine._event(state, events, "combat_fled", enemy=key)
        else:
            frozen = False
            if action_id == "combat.attack":
                multiplier = 1.5 if fight["round"] <= target.get("physical_weak_until", -1) else 1
                loss = min(target["health"], ceil(damage(hero["attack"], enemy["defense"]) * multiplier))
                target["health"] -= loss
                engine._event(state, events, "combat_hit", enemy=key, attacker="player", damage=loss,
                              health=target["health"], round=fight["round"])
            elif action_id.startswith("combat.cast."):
                from .magic import spellbook, REACTIONS
                spell = spellbook(engine)[action_id.removeprefix("combat.cast.")]
                health["mana"] = hero["mana"] - spell["mana_cost"]
                element = spell["element"]
                aura = enemy.get("element") or target.get("aura")
                reaction, multiplier, bonus = REACTIONS.get((element, aura), ("", 1.0, 0))
                if enemy.get("element") == element:
                    reaction, multiplier = "同元素抗性", 0.25
                if reaction == "冻结":
                    frozen = fight["round"] >= target.get("freeze_ready_round", 0)
                    if frozen:
                        target["freeze_ready_round"] = fight["round"] + 2
                    else:
                        reaction = "冻结冷却"
                if reaction == "超导":
                    target["physical_weak_until"] = fight["round"] + 2
                if not enemy.get("element"):
                    target["aura"] = None if reaction else element
                if reaction:
                    engine._event(state, events, "elemental_reaction", enemy=key, reaction=reaction,
                                  element=element, multiplier=multiplier, bonus=bonus)
                raw = max(1, ceil(damage(hero["magic_attack"] + spell["power"], enemy["defense"]) * multiplier) + bonus)
                loss = min(target["health"], raw)
                target["health"] -= loss
                engine._event(state, events, "combat_hit", enemy=key, attacker="player", damage=loss,
                              health=target["health"], round=fight["round"], spell=spell["id"], element=element,
                              mana_cost=spell["mana_cost"])
            elif action_id.startswith("combat.use."):
                engine._effect(state, events, {"op": "consume", "entity": engine.player,
                               "item": action_id.removeprefix("combat.use.")}, action_id)
            else:
                engine._event(state, events, "combat_guarded", enemy=key)
            if target["health"] == 0:
                target["defeated"] = True
                state["combat"] = None
                engine._event(state, events, "combat_won", enemy=key)
                reward = enemy.get("reward_coins", 0)
                if reward:
                    engine._effect(state, events, {"op": "add_attribute", "entity": engine.player,
                                   "attribute": "coins", "amount": reward}, action_id)
            elif frozen:
                engine._event(state, events, "combat_frozen", enemy=key)
            else:
                incoming = damage(enemy["attack"], hero["defense"])
                if action_id == "combat.guard":
                    incoming = max(1, ceil(incoming / 2))
                loss = min(health["health"], incoming)
                health["health"] -= loss
                engine._event(state, events, "combat_hit", enemy=key, attacker=key, damage=loss,
                              health=health["health"], round=fight["round"])
                if health["health"] == 0:
                    state["combat"] = None
                    health["health"] = 1
                    old = state["entities"][engine.player]["location"]
                    home = engine.world["clock"]["home"]
                    state["entities"][engine.player]["location"] = home
                    engine._event(state, events, "combat_lost", enemy=key)
                    engine._event(state, events, "moved", entity=engine.player, from_location=old, to_location=home, action=action_id)
    engine._complete_quests(state, events)
    state["turn"] += 1
    state["events"].extend(events)
    engine.state = state
    return events
