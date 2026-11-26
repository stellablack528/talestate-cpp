"""Add combat content to templates and explicitly selected saves, without resets."""

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
from talestate.memory import write_json


def extend(world):
    world = deepcopy(world)
    if any(row["id"] == "forest" for row in world["locations"]):
        return world
    world["revision"] += 1
    world["locations"].append({"id": "forest", "name": "森林", "background": "forest", "bgm": "sea_evening",
        "position_km": [4, 4], "description": "村庄旁的林间空地，树根与落叶间有史莱姆出没。"})
    world["map"]["routes"].append({"from": "village", "to": "forest", "distance_km": 1})
    for item in world["items"]:
        if item["id"] == "shop_sword":
            item.update({"slot": "weapon", "bonuses": {"attack": 5}, "description": "保养过的旧铁剑，装备后攻击+5。"})
        elif item["id"] == "shop_shield":
            item.update({"slot": "offhand", "bonuses": {"defense": 5}, "description": "皮革包边的木圆盾，装备后防御+5。"})
    world["items"].extend([
        {"id": "shop_dagger", "name": "铁匕首", "owner": "merchant", "kind": "weapon", "slot": "weapon",
         "bonuses": {"attack": 2}, "barter_value": 1, "description": "便携的铁匕首，装备后攻击+2。"},
        {"id": "shop_armor", "name": "皮背心", "owner": "merchant", "kind": "equipment", "slot": "body",
         "bonuses": {"defense": 3}, "barter_value": 2, "description": "结实的皮背心，装备后防御+3。"}])
    merchant = next(row for row in world["entities"] if row["id"] == "merchant")
    merchant["shop"]["prices"].update({"shop_dagger": 2, "shop_armor": 4})
    player = next(row for row in world["entities"] if row["kind"] == "player")
    for key, value in {"health": 100, "max_health": 100, "attack": 5, "defense": 5}.items():
        player.setdefault("attributes", {}).setdefault(key, value)
    world["encounters"] = [{"id": "slime_" + str(i), "name": "史莱姆 " + str(i), "location": "forest",
        "health": 20, "attack": 7, "defense": 2, "reward_coins": 2, "sprite": "slime"} for i in range(1, 4)]
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
    snapshots = [json.loads(path.read_text(encoding="utf-8")) for path in paths]
    if any(save["content_hash"] != content_hash(old) for save in snapshots):
        raise ValueError("存档快照版本不一致。")
    backup = folder.parent.parent / "backups" / (folder.name + "-before-combat.zip")
    backup.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(backup, "w", ZIP_DEFLATED) as archive:
        for path in folder.rglob("*"):
            if path.is_file() and "media" not in path.relative_to(folder).parts:
                archive.write(path, path.relative_to(folder).as_posix())
    write_json(folder / "world/world.json", updated)
    defaults = Engine(updated).state
    for path, save in zip(paths, snapshots):
        save.update({"revision": updated["revision"], "content_hash": content_hash(updated)})
        for key, owner in defaults["owners"].items():
            save["state"]["owners"].setdefault(key, owner)
        attributes = save["state"]["entities"]["player"]["attributes"]
        for key, value in {"health": 100, "max_health": 100, "attack": 5, "defense": 5}.items():
            attributes.setdefault(key, value)
        for key in ("combat", "equipment", "enemies"):
            save["state"].setdefault(key, deepcopy(defaults[key]))
        for key, enemy in defaults["enemies"].items():
            save["state"]["enemies"].setdefault(key, deepcopy(enemy))
        day = (updated["clock"]["start_minute"] + save["state"]["minute"]) // 1440 + 1
        for enemy in save["state"]["enemies"].values():
            enemy["spawn_day"] = day
        write_json(path, save)
    from talestate import Session
    Session.open_save(folder)
    print("Combat content added; previous progress backed up:", backup)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--save", type=Path)
    args = parser.parse_args()
    path = ROOT / "worlds/lighthouse/world.json"
    world = extend(json.loads(path.read_text(encoding="utf-8")))
    write_json(path, world)
    write_json(ROOT / "tools/world_builder/lighthouse.example.json", world)
    if args.save:
        upgrade_save(args.save)


if __name__ == "__main__":
    main()
