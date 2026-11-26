"""Small headless client for the shared session interface."""

import argparse
from pathlib import Path

from .session import Session


def main():
    parser = argparse.ArgumentParser(description="TaleState unified runtime")
    parser.add_argument("world", help="world.json or exported world directory")
    parser.add_argument("--data", default="userdata")
    args = parser.parse_args()
    session = Session(Path(args.world), args.data)
    print(session.view()["title"])
    print("Enter an action ID or describe an action; /save, /load, /quit.")
    while True:
        view = session.view()
        print(view["location"]["name"], " | ".join(a.get("name", a["id"]) + " (" + a["id"] + ")" for a in view["actions"]))
        text = input("> ")
        if text == "/quit":
            break
        try:
            if text == "/save":
                print(session.save())
            elif text == "/load":
                session.load()
            else:
                for line in session.take_turn(text):
                    print(line["speaker"] + ": " + line["text"])
        except (ValueError, OSError) as error:
            print(error)


if __name__ == "__main__":
    main()
