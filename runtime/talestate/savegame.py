"""Portable playthrough folders: content, progress, memories and media together."""

import json
from pathlib import Path, PurePosixPath
import shutil
import tempfile
from uuid import uuid4
from zipfile import ZipFile, ZIP_DEFLATED

from .session import Session


def list_saves(data_dir, world_id=None):
    folders = []
    for metadata in (Path(data_dir) / "saves").glob("*/save.json"):
        info = json.loads(metadata.read_text(encoding="utf-8"))
        if world_id is None or info["world_id"] == world_id:
            folders.append(metadata.parent)
    return sorted(folders, key=lambda p: (p / "session.json").stat().st_mtime_ns, reverse=True)


def export_save(folder, destination):
    folder, destination = Path(folder).resolve(), Path(destination).resolve()
    if destination == folder or folder in destination.parents:
        raise ValueError("压缩包请保存到存档目录之外。")
    if not (folder / "save.json").is_file():
        raise ValueError("请选择包含 save.json 的存档文件夹。")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(destination, "w", ZIP_DEFLATED) as archive:
        for path in sorted(folder.rglob("*")):
            if path.is_file() and path.suffix != ".tmp":
                archive.write(path, path.relative_to(folder).as_posix())
    return destination


def import_save(archive_path, data_dir):
    saves = Path(data_dir) / "saves"
    saves.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=saves) as temporary:
        stage = Path(temporary)
        with ZipFile(archive_path) as archive:
            for entry in archive.infolist():
                name = entry.filename.replace("\\", "/")
                relative = PurePosixPath(name)
                # Imported ZIPs are external files; never let paths escape the save folder.
                if relative.is_absolute() or ".." in relative.parts or ":" in name:
                    raise ValueError("压缩包包含无效路径。")
                target = stage.joinpath(*relative.parts)
                if entry.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.open(entry) as source, target.open("wb") as output:
                        shutil.copyfileobj(source, output)
        metadata = json.loads((stage / "save.json").read_text(encoding="utf-8"))
        if metadata.get("format") != "talestate.save/v1":
            raise ValueError("不支持的存档格式。")
        # Verify world/state coherence before admitting it to the playable save list.
        Session.open_save(stage, data_dir)
        destination = saves / uuid4().hex[:12]
        shutil.move(str(stage), destination)
    # Update save identity after the import; each import is an independent playthrough.
    Session.open_save(destination, data_dir)
    return destination


def main():
    import argparse
    parser = argparse.ArgumentParser(description="TaleState 文件夹存档导入/导出")
    commands = parser.add_subparsers(dest="command", required=True)
    export = commands.add_parser("export")
    export.add_argument("folder")
    export.add_argument("zip")
    load = commands.add_parser("import")
    load.add_argument("zip")
    load.add_argument("--data-dir", default="userdata")
    args = parser.parse_args()
    print(export_save(args.folder, args.zip) if args.command == "export"
          else import_save(args.zip, args.data_dir))


if __name__ == "__main__":
    main()
