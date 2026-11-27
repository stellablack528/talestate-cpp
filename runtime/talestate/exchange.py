"""Atomic item exchanges; models propose and consent, the engine settles."""

from copy import deepcopy
from .engine import ActionError
from .story import alternatives, quest_items, record_entrusted_loss


def prices(engine, npc_id):
    return {**engine.entities[npc_id].get("shop", {}).get("prices", {}),
            **engine.state.get("shop_prices", {}).get(npc_id, {})}


def update_prices(engine, npc_id, decision):
    if not decision or decision.get("decision") != "accept" or not decision.get("shop_prices"):
        return {}
    changes = decision["shop_prices"]
    catalog = engine.entities[npc_id].get("shop", {}).get("prices", {})
    if not isinstance(changes, dict) or any(key not in catalog or type(value) is not int or value <= 0
                                           for key, value in changes.items()):
        raise ActionError("售价修改只能包含本人商店的商品 ID 和正整数金币。")
    engine.state.setdefault("shop_prices", {}).setdefault(npc_id, {}).update(changes)
    return deepcopy(changes)


def reserved_items(engine):
    return (quest_items(q for key, q in engine.quests.items() if engine.state["quests"].get(key) in ("available", "active"))
            | issued_quest_items(engine))


def issued_quest_items(engine):
    return {key for key, item in engine.base_items.items() if item["owner"] is None and item.get("category") == "quest"}


def quote(engine, npc_id, proposal):
    if npc_id not in engine.present_npcs():
        raise ActionError("只能与在场的人交换物品。")
    if not isinstance(proposal, dict):
        raise ActionError("交换提案需要 give、receive 和 coins 对象。")
    give, receive, coins = proposal.get("give", []), proposal.get("receive", []), proposal.get("coins", 0)
    if not isinstance(give, list) or not isinstance(receive, list) or type(coins) is not int:
        raise ActionError("交换需要物品 ID 列表与整数金币。")
    if not all(isinstance(key, str) for key in give + receive) or len(set(give + receive)) != len(give + receive):
        raise ActionError("交换物品不能重复。")
    if not give and not receive and not coins:
        raise ActionError("没有提出交换的物品或金币。")
    for keys, owner in ((give, engine.player), (receive, npc_id)):
        if any(engine.state["owners"].get(key) != owner for key in keys):
            raise ActionError("交换物品不属于当前持有人。")
    if set(receive) & reserved_items(engine):
        raise ActionError("对方的委托或约定物品需要按原约定取得。")
    affected = {key: set(give) & quest_items((quest,)) for key, quest in engine.quests.items()
                if engine.state["quests"][key] == "active"}
    affected = {key: items for key, items in affected.items() if items}
    restricted = set(give) & issued_quest_items(engine)
    if affected or restricted:
        if coins >= 0 or receive:
            raise ActionError("受托物品请按委托使用或交付。若要擅自卖掉，需要明确以金币单独成交。")
        for key, items in affected.items():
            quest = engine.quests[key]
            if any(rule.get("type") == "delivered" and rule["item"] in items and rule["to"] == npc_id
                   for rule in alternatives(quest["complete_when"])):
                raise ActionError("对方是约定的接收人，请按原委托交付。")
    for entity, cost in ((engine.player, coins), (npc_id, -coins)):
        if engine.state["entities"][entity]["attributes"].get("coins", 0) < cost:
            raise ActionError("金币不足，交换尚未发生。")
    shop = engine.entities[npc_id].get("shop", {})
    if shop and receive:
        catalog = prices(engine, npc_id)
        if any(key not in catalog for key in receive):
            raise ActionError("这件物品没有列入商店出售清单。")
        value = sum(engine.items[key].get("barter_value", 0) for key in give)
        if coins + value < sum(catalog[key] for key in receive):
            raise ActionError("提出的金币与物品价值不足以购买这些货物。")
    return {"give": give, "receive": receive, "coins": coins,
            "entrusted": sorted(restricted.union(*affected.values()))}


def execute_exchange(engine, npc_id, proposal):
    offer = quote(engine, npc_id, proposal)
    state, events = deepcopy(engine.state), []
    state["minute"] += engine.world.get("clock", {}).get("conversation_minutes", 5)
    action = "exchange." + npc_id
    for keys, source, destination in ((offer["give"], engine.player, npc_id), (offer["receive"], npc_id, engine.player)):
        for key in keys:
            engine._effect(state, events, {"op": "transfer", "item": key, "from": source, "to": destination}, action)
    if offer["coins"]:
        for entity, amount in ((engine.player, -offer["coins"]), (npc_id, offer["coins"])):
            engine._effect(state, events, {"op": "add_attribute", "entity": entity,
                           "attribute": "coins", "amount": amount}, action)
    if offer["entrusted"]:
        sold = offer["entrusted"]
        proof = engine._event(state, events, "entrusted_items_sold", npc=npc_id, items=sold, action=action)
        record_entrusted_loss(engine, state, events, sold, proof)
    engine._complete_quests(state, events)
    state["turn"] += 1
    state["events"].extend(events)
    engine.state = state
    return events


def shop_stock(engine, npc_id):
    catalog = prices(engine, npc_id)
    reserved = reserved_items(engine)
    return [{"id": key, "name": engine.name(key), "price": price,
             "description": engine.items[key].get("description", "")}
            for key, price in catalog.items() if engine.state["owners"].get(key) == npc_id and key not in reserved]
