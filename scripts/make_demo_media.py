"""Code-native scene placeholders and an original, quiet ambient audio loop."""

import json
import math
from pathlib import Path
import struct
import wave

ROOT = Path(__file__).resolve().parents[1]
WORLD = ROOT / "worlds" / "lighthouse"
MEDIA = WORLD / "media"


def main():
    for folder in ("backgrounds", "portraits/keeper", "audio/bgm", "audio/sfx", "audio/voice"):
        path = MEDIA / folder
        path.mkdir(parents=True, exist_ok=True)
        (path / "README.md").write_text("在这里存放对应素材，并在 media/manifest.json 登记相对文件路径。\n", encoding="utf-8")
    manifest = {
        "backgrounds": {"harbor": {"layout": "harbor", "sky": "#173743", "sea": "#29505a"},
                        "lighthouse": {"layout": "lighthouse", "sky": "#172c3e", "sea": "#2a4857"}},
        "portraits": {"keeper.neutral": {"placeholder": True}, "keeper.happy": {"placeholder": True}},
        "bgm": {"sea_evening": {"file": "media/audio/bgm/sea_evening.wav"}},
        "sfx": {"delivery": {"file": "media/audio/sfx/delivery.wav"}}, "voice": {},
        "event_sounds": {"delivered": "delivery"},
    }
    (MEDIA / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    sample_rate, seconds = 22050, 12
    frames = bytearray()
    # Integer cycles over the whole buffer make the repeating boundary continuous.
    for index in range(sample_rate * seconds):
        t = index / sample_rate
        envelope = 0.5 - 0.5 * math.cos(2 * math.pi * t / seconds)
        signal = sum(math.sin(2 * math.pi * hz * t) / (i + 2)
                     for i, hz in enumerate((110, 165, 220, 275)))
        frames.extend(struct.pack("<h", int(1800 * envelope * signal)))
    with wave.open(str(MEDIA / "audio/bgm/sea_evening.wav"), "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(sample_rate)
        audio.writeframes(frames)
    effect = bytearray()
    for index in range(sample_rate // 3):
        t = index / sample_rate
        envelope = math.sin(math.pi * t * 3) ** 2
        value = sum(math.sin(2 * math.pi * frequency * t) for frequency in (660, 880))
        effect.extend(struct.pack("<h", int(2600 * envelope * value)))
    with wave.open(str(MEDIA / "audio/sfx/delivery.wav"), "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(sample_rate)
        audio.writeframes(effect)
    world_path = WORLD / "world.json"
    world = json.loads(world_path.read_text(encoding="utf-8"))
    for location in world["locations"]:
        location["background"] = location["id"]
        location["bgm"] = "sea_evening"
        if location["id"] == "lighthouse":
            location["descriptions"] = [{"conditions": [{"op": "quest_status", "quest": "deliver_medicine", "value": "completed"}],
                                        "text": "守塔人已经收好了药品。你可以与他交谈，或沿海岸返回港口。"}]
    for npc in world["entities"]:
        if npc["kind"] == "npc":
            npc["portrait"] = npc["id"]
            for line in npc.get("dialogues", []):
                line["expression"] = "happy" if line["id"] == "thanks" else "neutral"
    world_path.write_text(json.dumps(world, ensure_ascii=False, indent=2), encoding="utf-8")
    (MEDIA / "CREDITS.md").write_text(
        "# 示例素材\n\n场景和人物占位由 Ren'Py 界面代码绘制，可在清单中替换成 PNG/WebP 立绘和背景。\n"
        "sea_evening.wav 为本项目用正弦合成的原创示范音频，无第三方录音。\n"
        "素材路径相对于世界目录，bgm、sfx 和 voice 分开管理。\n", encoding="utf-8")


if __name__ == "__main__":
    main()
