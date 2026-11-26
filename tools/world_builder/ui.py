"""Small tkinter authoring window. Start with python -m tools.world_builder.ui."""
from __future__ import annotations

import json
import os
from pathlib import Path
from queue import Empty, Queue
from threading import Thread
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText

from .builder import DEFAULT_MODEL, ROOT, compile, export, generate, preview, read_json, write_json


class WorldBuilderWindow(ttk.Frame):
    def __init__(self, master: tk.Tk):
        super().__init__(master, padding=12)
        self.pack(fill="both", expand=True)
        master.title("世界创作器")
        master.geometry("1050x780")
        master.minsize(760, 560)
        self.results: Queue = Queue()
        self.busy = False
        self.buttons: list[ttk.Button] = []
        self.base_url = tk.StringVar(value=os.environ.get("OPENAI_BASE_URL", "https://api.openai.com/v1"))
        self.api_key = tk.StringVar(value=os.environ.get("OPENAI_API_KEY", ""))
        self.model = tk.StringVar(value=os.environ.get("OPENAI_MODEL") or DEFAULT_MODEL)
        self.world_id = tk.StringVar(value="my-world")
        self.status = tk.StringVar(value="导入设定后生成，或预览无需 Key 的示例。Key 仅保留在内存中。")

        settings = ttk.LabelFrame(self, text="生成设置", padding=8)
        settings.pack(fill="x")
        settings.columnconfigure(1, weight=1)
        settings.columnconfigure(3, weight=1)
        for label, variable, row, column, secret in (
            ("API 地址（含 /v1）", self.base_url, 0, 0, False),
            ("模型", self.model, 0, 2, False),
            ("API Key", self.api_key, 1, 0, True),
            ("世界 ID", self.world_id, 1, 2, False),
        ):
            ttk.Label(settings, text=label).grid(row=row, column=column, sticky="w", padx=(0, 8), pady=4)
            ttk.Entry(settings, textvariable=variable, show="*" if secret else "").grid(
                row=row, column=column + 1, sticky="ew", padx=(0, 12), pady=4)

        toolbar = ttk.Frame(self)
        toolbar.pack(fill="x", pady=8)
        for text, command in (
            ("导入 UTF-8 设定", self.import_sources),
            ("预览示例", self.load_example),
            ("调用模型生成", self.generate_draft),
            ("打开草案", self.open_draft),
            ("保存草案", self.save_draft),
        ):
            self._button(toolbar, text, command)

        self.tabs = ttk.Notebook(self)
        self.tabs.pack(fill="both", expand=True)
        source_tab, draft_tab = ttk.Frame(self.tabs), ttk.Frame(self.tabs)
        self.tabs.add(source_tab, text="世界设定原文")
        self.tabs.add(draft_tab, text="可编辑 draft JSON")
        self.source = ScrolledText(source_tab, wrap="word", undo=True, font=("Microsoft YaHei UI", 11))
        self.source.pack(fill="both", expand=True)
        self.draft = ScrolledText(draft_tab, wrap="none", undo=True, font=("Consolas", 11))
        self.draft.pack(fill="both", expand=True)
        bottom = ttk.Frame(self)
        bottom.pack(fill="x", pady=(8, 0))
        self._button(bottom, "检查草案", self.check_draft)
        self._button(bottom, "编译并导出到 worlds/<id>", self.export_draft)
        ttk.Label(self, textvariable=self.status, wraplength=980).pack(fill="x", pady=(8, 0))
        self.after(100, self._poll)

    def _button(self, parent, label, command):
        button = ttk.Button(parent, text=label, command=lambda: self._guard(command))
        button.pack(side="left", padx=(0, 6))
        self.buttons.append(button)
        return button

    def _guard(self, operation):
        try:
            operation()
        except Exception as exc:
            self._error(exc)

    def _error(self, exc):
        self.status.set("失败：" + str(exc))
        messagebox.showerror("世界创作器", str(exc), parent=self.winfo_toplevel())

    def _read_draft(self):
        value = json.loads(self.draft.get("1.0", "end-1c"))
        if not isinstance(value, dict):
            raise ValueError("草案 JSON 需要是一个对象。")
        return value

    def _show_draft(self, value):
        self.draft.delete("1.0", "end")
        self.draft.insert("1.0", json.dumps(value, ensure_ascii=False, indent=2))
        self.world_id.set(value.get("definition", {}).get("world_id", self.world_id.get()))
        self.tabs.select(1)

    def _may_replace_draft(self):
        return not self.draft.get("1.0", "end-1c").strip() or messagebox.askyesno(
            "替换草案", "此操作会替换窗口中的草案。未保存的编辑会丢失，继续吗？", parent=self.winfo_toplevel())

    def import_sources(self):
        paths = filedialog.askopenfilenames(parent=self.winfo_toplevel(), title="导入 UTF-8 设定",
                                             filetypes=[("文本与 Markdown", "*.txt *.md"), ("所有文件", "*.*")])
        if not paths:
            return
        # Read before changing the editor so a decoding failure keeps its content.
        text = "\n\n".join(f"【{Path(p).name}】\n{Path(p).read_text(encoding='utf-8-sig')}" for p in paths)
        if self.source.get("1.0", "end-1c").strip():
            self.source.insert("end", "\n\n")
        self.source.insert("end", text)
        self.tabs.select(0)
        self.status.set(f"已导入 {len(paths)} 份设定，可继续编辑原文。")

    def load_example(self):
        if not self._may_replace_draft():
            return
        value = preview()
        # Never point a preview at the shared lighthouse world by default.
        value["definition"]["world_id"] = "lighthouse-preview"
        self._show_draft(value)
        self.status.set("已载入离线示例，默认导出到 worlds/lighthouse-preview；不会覆盖共享灯塔。")

    def open_draft(self):
        path = filedialog.askopenfilename(parent=self.winfo_toplevel(), title="打开草案", filetypes=[("JSON", "*.json")])
        if not path or not self._may_replace_draft():
            return
        value = read_json(path)
        if not isinstance(value, dict) or value.get("format") != "world-builder-draft/v1":
            raise ValueError("请选择 world-builder-draft/v1 草案，而非导出的 world.json。")
        self._show_draft(value)
        self.source.delete("1.0", "end")
        self.source.insert("1.0", "\n\n".join(s["text"] for s in value.get("sources", [])))
        self.status.set("已打开：" + path)

    def save_draft(self):
        value = self._read_draft()
        path = filedialog.asksaveasfilename(parent=self.winfo_toplevel(), title="保存可编辑草案",
                                          defaultextension=".json", initialfile="world-draft.json",
                                          filetypes=[("JSON", "*.json")])
        if path:
            write_json(path, value)
            self.status.set("已保存草案：" + path)

    def _set_busy(self, value):
        self.busy = value
        for button in self.buttons:
            button.configure(state="disabled" if value else "normal")
        self.draft.configure(state="disabled" if value else "normal")

    def generate_draft(self):
        source = self.source.get("1.0", "end-1c").strip()
        if not source:
            raise ValueError("请先导入或填写世界设定。")
        if not self.model.get().strip():
            raise ValueError("请填写模型名称。")
        if not self._may_replace_draft():
            return
        # Capture all Tk values on the main thread; the worker only calls the API.
        kwargs = dict(base_url=self.base_url.get().strip(), model=self.model.get().strip(),
                      api_key=self.api_key.get(), world_id=self.world_id.get().strip() or None)
        self._set_busy(True)
        self.status.set("模型生成中……窗口保持响应；请求最多等待 120 秒。")

        def worker():
            try:
                value = generate([{"id": "editor", "name": "创作窗口设定", "text": source}], **kwargs)
                self.results.put((True, value))
            except Exception as exc:
                self.results.put((False, exc))

        Thread(target=worker, daemon=True).start()

    def _poll(self):
        try:
            succeeded, value = self.results.get_nowait()
        except Empty:
            pass
        else:
            self._set_busy(False)
            if succeeded:
                self._show_draft(value)
                usage = value.get("generation", {}).get("usage", {})
                self.status.set("生成完成，请编辑、检查并导出。用量：" + json.dumps(usage, ensure_ascii=False))
            else:
                self._error(value)
        self.after(100, self._poll)

    def check_draft(self):
        bundle = compile(self._read_draft())
        self.status.set(f"检查通过：{len(bundle['definition']['locations'])} 个地点，"
                        f"{len(bundle['npc_seeds'])} 个 NPC，{len(bundle['definition']['quests'])} 项任务。")

    def export_draft(self):
        bundle = compile(self._read_draft())
        # Export uses the ID in the edited JSON, never an unrelated settings value.
        world_id = bundle["definition"]["world_id"]
        target = ROOT / "worlds" / world_id
        if target.exists() and not messagebox.askyesno(
            "确认覆盖已有世界",
            f"目标：{target}\n\n将覆盖 world.json 和本次导出的 NPC 初始文件。\n"
            "media 目录及其他已有文件会保留；world.json 中的媒体引用以当前草案为准。\n\n确定导出吗？",
            parent=self.winfo_toplevel()
        ):
            self.status.set("已取消导出，已有世界未修改。")
            return
        result = export(bundle, target)
        self.status.set("导出成功：" + result["world"])


def main():
    root = tk.Tk()
    WorldBuilderWindow(root)
    root.mainloop()


if __name__ == "__main__":
    main()
