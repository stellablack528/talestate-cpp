"""Start the current Ren'Py game without touching the old C++ build."""

import os
import subprocess
import sys

from stage_game import CLIENT, ROOT, SDK, stage


def main():
    executable = SDK / "lib" / "py3-windows-x86_64" / "python.exe"
    if not executable.exists():
        print("Ren'Py SDK is missing. See docs/DEVELOPMENT.md.")
        return 1
    stage()
    env = os.environ.copy()
    env["TALESTATE_DATA_DIR"] = str(ROOT / "userdata")
    args = [str(executable), str(SDK / "renpy.py"), str(CLIENT)]
    if "--check" in sys.argv or "--check-save" in sys.argv:
        env["TALESTATE_DATA_DIR"] = str(ROOT / ".deployment" / "renpy-check-data")
        env["TALESTATE_UI_CHECK"] = str(ROOT / ".deployment" / "renpy-game.png")
        if "--check-save" in sys.argv:
            from pathlib import Path
            env["TALESTATE_CHECK_SAVE"] = str(Path(sys.argv[sys.argv.index("--check-save") + 1]).resolve())
        process = subprocess.run(args, cwd=ROOT, env=env, timeout=35)
        return process.returncode
    if "--lint" in sys.argv:
        return subprocess.run(args + ["lint"], cwd=ROOT, env=env).returncode
    subprocess.Popen(args, cwd=ROOT, env=env, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
