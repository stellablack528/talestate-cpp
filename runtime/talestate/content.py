"""Load author-owned world definitions; keep content out of UI code."""

import hashlib
import json
from .travel import TravelMap
from .story import alternatives, valid_match
from pathlib import Path


def validate_world(world):
    errors = []

    def error(path, message):
        errors.append({"code": "INVALID_DATA", "path": path, "message": message})

    if not isinstance(world, dict) or world.get("format") != "world-definition/v1":
        error("/format", "需要 world-definition/v1 世界定义")
        return errors
    if not world.get("world_id") or not isinstance(world.get("revision"), int):
        error("/world_id", "世界需要稳定 ID 和整数 revision")
    tables = {}
    for section in ("locations", "entities", "items", "actions", "quests", "spells"):
        rows = world.get(section, [])
        if not isinstance(rows, list):
            error("/" + section, "需要列表")
            rows = []
        tables[section] = {}
        for index, row in enumerate(rows):
            if not isinstance(row, dict) or not row.get("id"):
                error(f"/{section}/{index}", "需要 ID")
            elif row["id"] in tables[section]:
                error(f"/{section}/{index}/id", "同类内容 ID 重复")
            else:
                tables[section][row["id"]] = row
    if len([e for e in tables["entities"].values() if e.get("kind") == "player"]) != 1:
        error("/entities", "首版世界需要一个玩家实体")
    for entity in tables["entities"].values():
        if entity.get("location") not in tables["locations"]:
            error("/entities/" + entity["id"], "实体所在地点不存在")
        shop = entity.get("shop")
        if shop:
            for key, price in shop.get("prices", {}).items():
                if key not in tables["items"] or type(price) is not int or price <= 0:
                    error("/entities/" + entity["id"] + "/shop", "货物需引用已有物品，价格需为正整数金币")
        for key, price in entity.get("lessons", {}).items():
            if key not in tables["spells"] or type(price) is not int or price < 0:
                error("/entities/" + entity["id"] + "/lessons", "课程需引用已有法术，学费需为非负整数")
    for item in tables["items"].values():
        if "slot" in item and item["slot"] not in ("weapon", "offhand", "body"):
            error("/items/" + item["id"], "装备槽需为weapon/offhand/body")
        if any(key not in ("attack", "defense", "magic_attack") or type(value) is not int or value < 0
               for key, value in item.get("bonuses", {}).items()):
            error("/items/" + item["id"], "装备加成为非负整数attack/defense/magic_attack")
        consumable = item.get("consumable")
        if consumable and (consumable.get("resource") not in ("health", "mana") or
                           type(consumable.get("amount")) is not int or consumable["amount"] <= 0):
            error("/items/" + item["id"], "药水需指定health/mana与正整数回复量")
        if type(item.get("barter_value", 0)) is not int or item.get("barter_value", 0) < 0:
            error("/items/" + item["id"], "抵价需为非负整数金币")
    for item in tables["items"].values():
        if "owner" not in item or (item["owner"] is not None and item["owner"] not in tables["entities"]):
            error("/items/" + item["id"], "物品所有者不存在")
    for action in tables["actions"].values():
        path = "/actions/" + action["id"]
        if action.get("actor") not in tables["entities"]:
            error(path, "动作执行者不存在")
        if not isinstance(action.get("duration_minutes", 0), int) or action.get("duration_minutes", 0) < 0:
            error(path, "动作耗时需要非负整数")
        for condition in action.get("preconditions", []):
            _check_operation(condition, tables, path, error, condition=True)
        for effect in action.get("effects", []):
            _check_operation(effect, tables, path, error)
            if effect.get("op") == "issue_item":
                item, npc = effect.get("item"), effect.get("npc")
                if (tables["items"].get(item, {}).get("owner", False) is not None
                        or action.get("target") != npc
                        or [e.get("npc") for e in action["effects"] if e.get("op") == "npc_decision"] != [npc]
                        or not any(p.get("op") == "item_unissued" and p.get("item") == item
                                   for p in action.get("preconditions", []))
                        or tables["entities"].get(action.get("actor"), {}).get("kind") != "player"):
                    error(path, "签发需使用尚未制作的物品，且须由同一动作的目标 NPC 同意")
            if effect.get("op") == "npc_decision" and (
                    not action.get("ai_interaction") or action.get("target") != effect.get("npc")
                    or tables["entities"].get(effect.get("npc"), {}).get("kind") != "npc"
                    or not isinstance(effect.get("decision"), str) or not effect["decision"]):
                error(path, "人物决定需要该 NPC 本人确认，并提供决定 ID")
            if effect.get("op") == "investigate" and (
                    effect.get("entity") not in tables["entities"] or effect.get("location") not in tables["locations"]
                    or not isinstance(effect.get("clue"), str) or not effect["clue"]):
                error(path, "调查需要有效调查者、地点和线索 ID")
    for quest in tables["quests"].values():
        if quest.get("initial_status", "active") not in ("available", "active", "completed"):
            error("/quests/" + quest["id"], "任务初始状态需要 available、active 或 completed")
        rules = alternatives(quest.get("complete_when"))
        if not rules or any(not valid_match(rule) for rule in rules):
            error("/quests/" + quest["id"], "任务需要有效的事件证据")
        for match in rules:
            if valid_match(match) and match.get("action") and match["action"] not in tables["actions"]:
                error("/quests/" + quest["id"], "任务引用的动作不存在")
        for effect in quest.get("rewards", []):
            _check_operation(effect, tables, "/quests/" + quest["id"], error)
            if effect.get("op") == "issue_item":
                error("/quests/" + quest["id"], "物品签发只能用于需要 NPC 同意的动作")
    map_data = world.get("map")
    if map_data:
        route_errors = len(errors)
        size = map_data.get("size_km", {})
        if any(not isinstance(size.get(key), (int, float)) or size[key] <= 0 for key in ("width", "height")):
            error("/map/size_km", "地图宽高需要正数公里")
        if not isinstance(map_data.get("walking_speed_kmh", 4), (int, float)) or map_data.get("walking_speed_kmh", 4) <= 0:
            error("/map/walking_speed_kmh", "步行速度需要正数")
        for route in map_data.get("routes", []):
            if route.get("from") not in tables["locations"] or route.get("to") not in tables["locations"]:
                error("/map/routes", "道路端点必须是已有地点")
            if not isinstance(route.get("distance_km"), (int, float)) or route["distance_km"] <= 0:
                error("/map/routes", "道路距离需要正数公里")
        if len(errors) == route_errors:
            travel = TravelMap(map_data)
            start = next(iter(tables["locations"]), None)
            if any(travel.distance(start, key) == float("inf") for key in tables["locations"]):
                error("/map/routes", "地点之间需要有连通的道路")
    clock = world.get("clock")
    if clock:
        if clock.get("home") not in tables["locations"] or not map_data:
            error("/clock/home", "昼夜机制需要地图和有效的家地点")
        if not (0 <= clock.get("start_minute", 480) < clock.get("night_minute", 1200) < 1440):
            error("/clock", "开始时间与入夜时间需要是当天依次递增的分钟数")
    visit_ids = set()
    for row in world.get("night_opportunities", []):
        key = row.get("id")
        if (not clock or not key or key in visit_ids or row.get("location") not in tables["locations"]
                or tables["entities"].get(row.get("npc"), {}).get("kind") != "npc"
                or not row.get("title") or type(row.get("minutes")) is not int or row["minutes"] <= 0):
            error("/night_opportunities", "约见需要唯一 ID、有效人物、地点和时长")
        visit_ids.add(key)
        for rule in row.get("preconditions", []):
            _check_operation(rule, tables, "/night_opportunities", error, condition=True)
    for spell in tables["spells"].values():
        if spell.get("element") not in ("fire", "water", "ice", "lightning") or any(
                type(spell.get(key)) is not int or spell[key] <= 0 for key in ("mana_cost", "power")):
            error("/spells/" + spell["id"], "法术需要四系元素及正整数mana_cost/power")
    seen = set()
    for row in world.get("encounters", []):
        key = row.get("id")
        if not key or key in seen or row.get("location") not in tables["locations"]:
            error("/encounters", "怪物需要唯一ID和有效地点")
        seen.add(key)
        if row.get("element") not in (None, "fire", "water", "ice", "lightning"):
            error("/encounters", "怪物元素需为fire/water/ice/lightning")
        if type(row.get("health")) is not int or row["health"] <= 0 or any(
                type(row.get(field)) is not int or row[field] < 0 for field in ("attack", "defense")):
            error("/encounters", "怪物血量需为正整数，攻防需为非负整数")
        if type(row.get("reward_coins", 0)) is not int or row.get("reward_coins", 0) < 0:
            error("/encounters", "怪物金币奖励需为非负整数")
    return errors


def _check_operation(operation, tables, path, error, condition=False):
    op = operation.get("op")
    allowed = ({"at", "owned_by", "item_unissued", "quest_status", "attribute", "day", "evidence"} if condition
               else {"move", "transfer", "consume", "add_attribute", "activate_quest", "investigate", "npc_decision", "issue_item"})
    if op not in allowed:
        error(path, "不支持的规则：" + str(op))
        return
    if op == "evidence" and not valid_match(operation.get("match")):
        error(path, "前置证据不完整")
    if op == "item_unissued" and operation.get("item") not in tables["items"]:
        error(path, "签发前置条件须引用物品")
    references = {"entity": "entities", "location": "locations", "item": "items", "quest": "quests", "npc": "entities"}
    if not condition and op == "transfer":
        references.update({"from": "entities", "to": "entities"})
    for key, table in references.items():
        if key in operation and operation[key] not in tables[table]:
            error(path, f"{key} 引用不存在：{operation[key]}")


def content_hash(world):
    raw = json.dumps(world, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(raw.encode("utf-8")).hexdigest()


def load_world(path):
    path = Path(path)
    if path.is_dir():
        path /= "world.json"
    world = json.loads(path.read_text(encoding="utf-8-sig"))
    errors = validate_world(world)
    if errors:
        raise ValueError("；".join(e["path"] + ": " + e["message"] for e in errors))
    return world, path.parent
