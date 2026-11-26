"""Stage the canonical runtime and worlds into the Ren'Py project."""

from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / ".tools" / "renpy-8.6.0-sdk"
CLIENT = ROOT / "clients" / "renpy"


def stage():
    game = CLIENT / "game"
    shutil.copytree(ROOT / "runtime" / "talestate", game / "python-packages" / "talestate",
                    dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    shutil.copytree(ROOT / "worlds", game / "worlds", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("__pycache__", "*.tmp"))
    fonts = game / "fonts"
    fonts.mkdir(exist_ok=True)
    font = SDK / "sdk-fonts" / "SourceHanSansLite.ttf"
    if font.exists() and not (fonts / font.name).exists():
        shutil.copy2(font, fonts / font.name)
    if (SDK / "LICENSE.txt").exists() and not (fonts / "SDK-LICENSE.txt").exists():
        shutil.copy2(SDK / "LICENSE.txt", fonts / "SDK-LICENSE.txt")
    return CLIENT


if __name__ == "__main__":
    print(stage())
