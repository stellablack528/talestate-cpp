"""Add magic content and migrate an explicitly selected save without resetting it."""

import argparse
from copy import deepcopy
import json
from pathlib import Path
import sys
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "runtime"))
from talestate.content import content_hash, validate_world
from talestate.engine import Engine
from talestate.memory import MemoryStore, write_json

SPELLS = [
    {"id": "firebolt", "name": "火花术", "element": "fire", "mana_cost": 6, "power": 10},
    {"id": "waterbolt", "name": "水箭术", "element": "water", "mana_cost": 6, "power": 10},
    {"id": "frostbolt", "name": "冰霜术", "element": "ice", "mana_cost": 8, "power": 9},
    {"id": "spark", "name": "雷光术", "element": "lightning", "mana_cost": 8, "power": 10},
]


def seed():
    return json.loads((ROOT / "tools/world_builder/mage.seed.json").read_text(encoding="utf-8"))


def extend(world):
    world = deepcopy(world)
    if any(row["id"] == "mage" for row in world["entities"]):
        return world
    world["revision"] += 1
    world["spells"] = deepcopy(SPELLS)
    world["entities"].append({"id": "mage", "kind": "npc", "name": "伊莲娜", "location": "village",
        "attributes": {"coins": 10}, "profile": seed()["profile"], "portrait": "mage", "memory_file": "npcs/mage.json",
        "lessons": {row["id"]: 0 for row in SPELLS}, "shop": {"prices": {"apprentice_staff": 4}}})
    world["items"].append({"id": "apprentice_staff", "name": "学徒木法杖", "owner": "mage", "kind": "weapon",
        "slot": "weapon", "bonuses": {"attack": 1, "magic_attack": 5}, "barter_value": 2,
        "description": "朴素的橡木法杖，装备后攻击+1、法术强度+5，与其他武器占用同一槽。"})
    merchant = next(row for row in world["entities"] if row["id"] == "merchant")
    for resource, title, amount in (("health", "回血药水", 40), ("mana", "回蓝药水", 25)):
        for i in range(1, 3):
            key = resource + "_potion_" + str(i)
            world["items"].append({"id": key, "name": title, "owner": "merchant", "kind": "consumable",
                "consumable": {"resource": resource, "amount": amount}, "barter_value": 1,
                "description": "恢复" + str(amount) + ("点生命。" if resource == "health" else "点法力。") + "战斗中使用占用一回合。"})
            merchant["shop"]["prices"][key] = 2
    player = next(row for row in world["entities"] if row["kind"] == "player")
    player["attributes"].update({"mana": 50, "max_mana": 50, "magic_attack": 8})
    names = {"fire": "火", "water": "水", "ice": "冰", "lightning": "雷"}
    for i, element in enumerate(names, 1):
        key = "slime_" + str(i)
        enemy = next((row for row in world["encounters"] if row["id"] == key), None)
        if enemy is None:
            enemy = {"id": key, "location": "forest", "health": 20, "attack": 7, "defense": 2, "reward_coins": 2}
            world["encounters"].append(enemy)
        enemy.update({"name": names[element] + "史莱姆", "element": element, "sprite": "slime." + element})
    errors = validate_world(world)
    if errors:
        raise ValueError(errors)
    return world


def upgrade_save(folder):
    folder = Path(folder).resolve()
    old = json.loads((folder / "world/world.json").read_text(encoding="utf-8"))
    updated = extend(old)
    if old == updated:
        return
    paths = [folder / "session.json", *sorted((folder / "snapshots").glob("*.json"))]
    saves = [json.loads(path.read_text(encoding="utf-8")) for path in paths]
    if any(save["content_hash"] != content_hash(old) for save in saves):
        raise ValueError("旧快照与世界版本不一致。")
    backup = folder.parent.parent / "backups" / (folder.name + "-before-magic.zip")
    backup.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(backup, "w", ZIP_DEFLATED) as archive:
        for path in folder.rglob("*"):
            if path.is_file() and "media" not in path.relative_to(folder).parts:
                archive.write(path, path.relative_to(folder).as_posix())
    write_json(folder / "world/world.json", updated)
    write_json(folder / "world/npcs/mage.json", seed())
    defaults = Engine(updated).state
    memory = MemoryStore(updated, folder / "world", folder)
    for path, save in zip(paths, saves):
        save.update({"revision": updated["revision"], "content_hash": content_hash(updated)})
        for key, owner in defaults["owners"].items():
            save["state"]["owners"].setdefault(key, owner)
        save["state"]["entities"].setdefault("mage", deepcopy(defaults["entities"]["mage"]))
        for key, value in {"mana": 50, "max_mana": 50, "magic_attack": 8}.items():
            save["state"]["entities"]["player"]["attributes"].setdefault(key, value)
        save["state"].setdefault("learned_spells", [])
        for key, enemy in defaults["enemies"].items():
            save["state"].setdefault("enemies", {}).setdefault(key, deepcopy(enemy))
        save["npc_memories"].setdefault("mage", deepcopy(memory.records["mage"]))
        if save["state"]["entities"]["player"]["location"] == "village" and "mage" not in save["state"]["encountered_npcs"]:
            save["state"]["encountered_npcs"].append("mage")
        write_json(path, save)
    from talestate import Session
    Session.open_save(folder)
    print("Magic upgrade complete; backup:", backup)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--save", type=Path)
    args = parser.parse_args()
    path = ROOT / "worlds/lighthouse/world.json"
    world = extend(json.loads(path.read_text(encoding="utf-8")))
    write_json(path, world)
    write_json(path.parent / "npcs/mage.json", seed())
    write_json(ROOT / "tools/world_builder/lighthouse.example.json", world)
    seeds_path = ROOT / "tools/world_builder/lighthouse.npcs.json"
    seeds = json.loads(seeds_path.read_text(encoding="utf-8"))
    seeds["mage"] = seed()
    write_json(seeds_path, seeds)
    if args.save:
        upgrade_save(args.save)


if __name__ == "__main__":
    main()
