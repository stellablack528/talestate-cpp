"""Ren'Py adapter. The engine is the only owner of game state."""

import json
import os
from pathlib import Path
import threading

from talestate import Session
from talestate.narration import render_event
from talestate.savegame import list_saves, export_save, import_save
from talestate.memory import knowledge_text


class Settings:
    base_url = "https://api.deepseek.com/v1"
    api_key = ""
    model = "deepseek-flash"


class Client:
    def __init__(self, basedir):
        self.basedir = Path(basedir)
        self.worlds_dir = self.basedir / "game" / "worlds"
        self.data_dir = Path(os.environ.get("TALESTATE_DATA_DIR", self.basedir / "userdata"))
        self.settings = Settings()
        self.session = None
        self.worlds = sorted(path for path in self.worlds_dir.glob("*/world.json"))
        self.status = "选择一个世界，开始旅途。"
        self.busy = False
        self.sleeping = False
        self.pending = None
        self.thread = None
        self.media = {}
        self.selected_npc = None
        self.sound_sequence = 0
        self.input = ""
        self.archive_path = ""
        self.chat_inputs = {}
        self.error = ""

    def start(self, world_path=None):
        path = Path(world_path) if world_path else self.worlds[0]
        self.session = Session(path, self.data_dir)
        self.apply_settings()
        self.refresh_media()
        self.status = "世界已载入。可以输入行动，或选择下方的行动按钮。"
        self.selected_npc = None
        self.sound_sequence = 0
        self.last_voice_event = None

    def refresh_media(self):
        media = self.session.world_dir / "media" / "manifest.json"
        self.media = json.loads(media.read_text(encoding="utf-8")) if media.exists() else {}

    def start_latest(self):
        folders = list_saves(self.data_dir)
        if folders:
            self.open_save(folders[0])
        else:
            self.start()
            legacy = self.data_dir / "saves" / self.session.world["world_id"] / "quick.json"
            if legacy.exists():
                self.session.restore(legacy)
                self.session.save()

    def saved_games(self):
        return [(str(path), json.loads((path / "save.json").read_text(encoding="utf-8")))
                for path in list_saves(self.data_dir)]

    def open_save(self, folder):
        if self.busy:
            return
        self.session = Session.open_save(folder, self.data_dir)
        self.apply_settings()
        self.refresh_media()
        self.selected_npc = None
        self.sound_sequence = len(self.session.engine.state["events"])
        self.last_voice_event = None
        self.status = "已打开独立存档，世界、素材和人物记忆一起恢复。"

    def export_archive(self):
        if self.busy:
            return
        try:
            self.session.save()
            destination = self.data_dir / "exports" / (self.session.session_id + ".zip")
            destination.parent.mkdir(parents=True, exist_ok=True)
            export_save(self.session.session_dir, destination)
            self.archive_path = str(destination)
            self.status = "已导出：" + str(destination)
        except (ValueError, OSError) as error:
            self.status = str(error)

    def import_archive(self):
        if self.busy:
            return
        try:
            folder = import_save(self.archive_path.strip().strip('"'), self.data_dir)
            self.open_save(folder)
        except (ValueError, OSError) as error:
            self.status = str(error)

    def apply_settings(self):
        if self.session:
            self.session.configure(self.settings.base_url, self.settings.api_key, self.settings.model)
            self.status = ("模型配置已应用。" if self.session.provider.configured and self.session.provider.api_key
                           else "请填写 API Key 并应用配置，才能处理整晚事务。")

    def view(self):
        return self.session.view() if self.session else None

    def map_points(self):
        view = self.session.view()
        size = view["map"]["size_km"]
        return [{**row, "x": 0.08 + 0.84 * row["position_km"][0] / size.get("width", 12),
                 "y": 0.12 + 0.76 * row["position_km"][1] / size.get("height", 8)}
                for row in view["map"]["locations"]]

    def perform(self, action=None, npc=None):
        if self.busy:
            return
        self.sleeping = action == "sleep"
        self._background(lambda: self.session.perform(action, npc_id=npc,
                         player_input="交谈" if npc else ""))

    def submit(self):
        if self.busy or not self.input.strip():
            return
        text = self.input
        def send():
            result = self.session.take_turn(text)
            if self.input == text:
                self.input = ""
            return result
        self._background(send)

    def chat(self, npc_id):
        if self.busy:
            return
        text = self.chat_inputs.get(npc_id, "").strip()
        if not text:
            return
        def send():
            result = self.session.take_turn(text, npc_id=npc_id)
            if self.chat_inputs.get(npc_id, "").strip() == text:
                self.chat_inputs[npc_id] = ""
            return result
        self._background(send)

    def open_chat(self, npc_id):
        self.selected_npc = npc_id
        self.chat_inputs.setdefault(npc_id, "")
        self.error = ""
        if self.busy or not self.session.provider.configured or npc_id not in self.session.engine.present_npcs():
            return
        if not self.session.has_task_opening(npc_id):
            return
        self._background(lambda: self.session.greet(npc_id))

    def character_definition(self, npc_id):
        record = self.memory(npc_id)
        return {"profile": record["profile"], **record.get("settings", {})}

    def known_text(self, row):
        return knowledge_text(row)

    def chat_history(self, npc_id):
        lines = []
        for row in self.session.dialogues.get(npc_id, []):
            if row["player"]:
                lines.append({"id": row["event_id"] + ".player", "speaker": "你", "text": str(row["player"])})
            lines.append({"id": row["event_id"], "speaker": self.session.engine.name(npc_id), "text": str(row["npc"])})
        return lines

    def shop_stock(self, npc_id):
        from talestate.exchange import shop_stock
        return shop_stock(self.session.engine, npc_id)

    def buy(self, npc_id, item_id):
        if not self.busy:
            self._background(lambda: self.session.buy(npc_id, item_id))

    def equip(self, item_id, remove=False):
        if not self.busy:
            self._background(lambda: self.session.equip(item_id, remove))

    def lessons(self, npc_id):
        from talestate.magic import lessons
        return lessons(self.session.engine, npc_id)

    def element_name(self, element):
        from talestate.magic import ELEMENTS
        return ELEMENTS.get(element, "无")

    def learn(self, npc_id, spell_id):
        if not self.busy:
            self._background(lambda: self.session.learn(npc_id, spell_id))

    def use_item(self, item_id):
        if not self.busy:
            self._background(lambda: self.session.use_item(item_id))

    def portrait_expression(self, npc_id):
        return self.memory(npc_id).get("expression", "neutral")

    def npc_activity(self, npc_id):
        return self.memory(npc_id).get("daily_state", {}).get("activity", "")

    def _background(self, operation):
        self.busy = True
        self.status = "…"
        self.error = ""

        def work():
            try:
                lines = operation()
                self.pending = (True, lines)
            except Exception as error:
                self.pending = (False, str(error))

        self.thread = threading.Thread(target=work, daemon=True)
        self.thread.start()

    def poll(self):
        if self.pending is None:
            return
        ok, result = self.pending
        self.pending = None
        self.busy = False
        self.sleeping = False
        self.status = "" if ok else result
        self.error = "" if ok else result
        if ok:
            for line in result:
                if line.get("npc_id"):
                    self.selected_npc = line["npc_id"]

    def save(self):
        if self.busy:
            return
        try:
            self.session.save()
            self.status = "进度已保存，NPC 记忆也已一起保存。"
        except OSError:
            self.status = "保存失败，请确认游戏目录可以写入。"

    def load(self):
        if self.busy:
            return
        try:
            # The current fresh playthrough is excluded when continuing another save.
            current = self.session.session_dir / "snapshots" / "quick.json"
            folders = list_saves(self.data_dir, self.session.world["world_id"])
            if current.exists():
                self.session.load()
            elif folders and folders[0] != self.session.session_dir:
                self.open_save(folders[0])
            else:
                self.session.load()
            self.status = "进度与 NPC 记忆已恢复。"
        except FileNotFoundError:
            self.status = "这个世界还没有快速存档。"
        except ValueError as error:
            self.status = str(error)

    def resource(self, kind, key):
        value = self.media.get(kind, {}).get(key, {})
        return value if isinstance(value, dict) else {"file": value}

    def memory_event_text(self, event):
        return render_event(self.session.engine, event)

    def resource_file(self, kind, key):
        value = self.resource(kind, key).get("file")
        if not value or not self.session:
            return None
        path = self.session.world_dir / value
        if not path.is_file():
            return None
        return path.resolve().as_posix()

    def memory(self, npc_id):
        if not npc_id or npc_id not in self.session.memory.records:
            return None
        return self.session.memory.records[npc_id]
