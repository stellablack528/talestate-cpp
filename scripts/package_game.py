"""Build the distributable with Ren'Py's own packaging command."""

import os
from pathlib import Path
import subprocess
import zipfile

from stage_game import CLIENT, ROOT, SDK, stage


def main():
    stage()
    env = os.environ.copy()
    env["APPDATA"] = str(ROOT / ".tools" / "appdata")
    destination = ROOT / "dist"
    destination.mkdir(exist_ok=True)
    args = [str(SDK / "lib/py3-windows-x86_64/python.exe"), str(SDK / "renpy.py"),
            "launcher", "distribute", str(CLIENT), "--destination", str(destination),
            "--package", "pc", "--no-update"]
    log = ROOT / ".deployment" / "renpy-package.log"
    log.parent.mkdir(exist_ok=True)
    with log.open("w", encoding="utf-8") as stream:
        result = subprocess.run(args, cwd=SDK, env=env, stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(encoding="utf-8", errors="replace")[-5000:])
        raise SystemExit(result.returncode)
    archive = destination / "TaleState-0.3.0-pc.zip"
    with zipfile.ZipFile(archive) as package:
        package.extractall(destination)
    print("Packaged and extracted:", destination / "TaleState-0.3.0-pc")


if __name__ == "__main__":
    main()
