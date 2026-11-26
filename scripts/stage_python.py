"""Copy an installed Windows Python's standard runtime for the portable editor."""

from pathlib import Path
import shutil
import sys


def main():
    source = Path(sys.base_prefix)
    destination = Path(__file__).resolve().parents[1] / ".tools" / "python"
    destination.mkdir(parents=True, exist_ok=True)
    for pattern in ("python*.exe", "python*.dll", "vcruntime*.dll", "LICENSE.txt"):
        for file in source.glob(pattern):
            shutil.copy2(file, destination / file.name)
    for directory in ("Lib", "DLLs", "tcl"):
        shutil.copytree(source / directory, destination / directory, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("site-packages", "__pycache__", "*.pyc", "test", "tests"))
    print(destination)


if __name__ == "__main__":
    main()
