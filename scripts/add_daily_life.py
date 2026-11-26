"""Add the town merchant to templates or a save, preserving existing progress."""

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

MERCHANT = {"id": "merchant", "kind": "npc", "name": "马丁", "location": "village",
    "attributes": {"coins": 20}, "memory_file": "npcs/merchant.json",
    "profile": "马丁是镇上的行商，早年随商队往返沿海村镇，如今在村庄广场经营小摊。"
               "他卖食品和旅行装备，按标价收钱，也接受有用物件抵价，愿意收下合适的礼物，"
               "不会无故白送货物。他和面包师周穗、杂货铺林岚打过照面，不知道她们与旅人的私下谈话。",
    "shop": {"prices": {"shop_bread": 1, "shop_cheese": 2, "shop_sword": 4, "shop_shield": 3}}}
GOODS = [{"id": "shop_bread", "name": "燕麦面包", "owner": "merchant", "kind": "food", "barter_value": 1,
          "description": "一只适合旅途携带的燕麦面包。"},
         {"id": "shop_cheese", "name": "羊乳酪", "owner": "merchant", "kind": "food", "barter_value": 1,
          "description": "用布包着的一小块羊乳酪。"},
         {"id": "shop_sword", "name": "旧铁剑", "owner": "merchant", "kind": "weapon", "barter_value": 2,
          "description": "朴素的单手铁剑，已磨好刃，尚未接入战斗数值。"},
         {"id": "shop_shield", "name": "木圆盾", "owner": "merchant", "kind": "equipment", "barter_value": 2,
          "description": "木板拼成、包有皮革的圆盾。"}]
SEED = {"npc_id": "merchant", "profile": MERCHANT["profile"], "initial_memories": [
    {"id": "stall", "text": "我在村庄广场摆摊，面包1金币、乳酪2金币、旧铁剑4金币、木盾3金币。"}]}


def extend(world):
    world = deepcopy(world)
    if not any(row["id"] == "merchant" for row in world["entities"]):
        world["entities"].append(deepcopy(MERCHANT))
        world["items"].extend(deepcopy(GOODS))
        world["revision"] = max(world["revision"] + 1, 4)
    errors = validate_world(world)
    if errors:
        raise ValueError(errors)
    return world


def upgrade_save(folder):
    folder = Path(folder).resolve()
    world_path = folder / "world/world.json"
    old = json.loads(world_path.read_text(encoding="utf-8"))
    updated = extend(old)
    if updated == old:
        return
    snapshot_paths = [folder / "session.json", *sorted((folder / "snapshots").glob("*.json"))]
    snapshots = [json.loads(path.read_text(encoding="utf-8")) for path in snapshot_paths]
    if any(save["content_hash"] != content_hash(old) for save in snapshots):
        raise ValueError("存档快照世界版本不一致，请先单独核对。")
    backup = folder.parent.parent / "backups" / (folder.name + "-before-daily-life.zip")
    backup.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(backup, "w", ZIP_DEFLATED) as archive:
        for path in folder.rglob("*"):
            if path.is_file() and "media" not in path.relative_to(folder).parts:
                archive.write(path, path.relative_to(folder).as_posix())
    write_json(folder / "world/npcs/merchant.json", SEED)
    write_json(world_path, updated)
    defaults = Engine(updated).state
    memory = MemoryStore(updated, folder / "world", folder)
    for path, save in zip(snapshot_paths, snapshots):
        save.update({"revision": updated["revision"], "content_hash": content_hash(updated)})
        for key, entity in defaults["entities"].items():
            save["state"]["entities"].setdefault(key, deepcopy(entity))
        for key, owner in defaults["owners"].items():
            save["state"]["owners"].setdefault(key, owner)
        save["npc_memories"].setdefault("merchant", deepcopy(memory.records["merchant"]))
        if save["state"]["entities"]["player"]["location"] == "village":
            if "merchant" not in save["state"]["encountered_npcs"]:
                save["state"]["encountered_npcs"].append("merchant")
        write_json(path, save)
    from talestate import Session
    Session.open_save(folder)
    print("Upgraded save; backup:", backup)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--save", type=Path)
    args = parser.parse_args()
    world_path = ROOT / "worlds/lighthouse/world.json"
    world = extend(json.loads(world_path.read_text(encoding="utf-8")))
    write_json(world_path, world)
    write_json(world_path.parent / "npcs/merchant.json", SEED)
    write_json(ROOT / "tools/world_builder/lighthouse.example.json", world)
    seeds = ROOT / "tools/world_builder/lighthouse.npcs.json"
    records = json.loads(seeds.read_text(encoding="utf-8"))
    records["merchant"] = SEED
    write_json(seeds, records)
    if args.save:
        upgrade_save(args.save)


if __name__ == "__main__":
    main()
