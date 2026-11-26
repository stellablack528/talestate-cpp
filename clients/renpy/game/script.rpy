init python:
    import os
    from functools import partial
    from talestate_client import Client
    tale = Client(config.basedir)
    tale_history_key = None
    tale_history_range = None
    tale_chat_scroll_state = {}

    def tale_chat_ranged(npc_id, adjustment):
        key = (id(tale.session), id(adjustment), len(tale.session.dialogues.get(npc_id, [])), adjustment.range)
        if tale_chat_scroll_state.get(npc_id) != key:
            adjustment.change(adjustment.range)
            tale_chat_scroll_state[npc_id] = key
    def tale_history_ranged(adjustment):
        global tale_history_key, tale_history_range
        history = tale.session.history if tale.session else []
        key = (id(tale.session), len(history), history[-1]["event_id"] if history else None)
        if key != tale_history_key or adjustment.range != tale_history_range:
            adjustment.change(adjustment.range)
            tale_history_key, tale_history_range = key, adjustment.range

    def tale_sync_audio():
        if not tale.session:
            return
        view = tale.view()
        path = tale.resource_file("bgm", view["location"].get("bgm", ""))
        if path and renpy.music.get_playing(channel="music") != path:
            renpy.music.play(path, channel="music", loop=True, fadein=1.2)
        elif not path:
            renpy.music.stop(channel="music", fadeout=1.0)
        last_sequence = getattr(tale, "sound_sequence", 0)
        for event in tale.session.engine.state["events"][last_sequence:]:
            sound_key = tale.media.get("event_sounds", {}).get(event["type"])
            sound_file = tale.resource_file("sfx", sound_key) if sound_key else None
            if sound_file:
                renpy.music.play(sound_file, channel="sound", loop=False)
        tale.sound_sequence = len(tale.session.engine.state["events"])
        if view["history"]:
            last = view["history"][-1]
            voice = tale.resource_file("voice", last.get("voice", ""))
            if voice and last["event_id"] != getattr(tale, "last_voice_event", None):
                renpy.music.play(voice, channel="voice", loop=False)
                tale.last_voice_event = last["event_id"]

    def tale_tick():
        tale.poll()
        if tale.sleeping:
            if not renpy.get_screen("tale_sleep"):
                renpy.show_screen("tale_sleep")
        else:
            renpy.hide_screen("tale_sleep")
        tale_sync_audio()

    def tale_action(action=None, npc=None):
        tale.perform(action, npc)
        if tale.sleeping:
            renpy.show_screen("tale_sleep")
        tale_sync_audio()

    def tale_load():
        tale.load()
        tale.sound_sequence = len(tale.session.engine.state["events"])
        tale_sync_audio()

label main_menu:
    jump start

label start:
    if not tale.worlds:
        "还没有世界。请先用世界生成器导出世界，然后运行打包／启动脚本。"
        return
    if os.environ.get("TALESTATE_CHECK_SAVE"):
        $ tale.open_save(os.environ["TALESTATE_CHECK_SAVE"])
    elif os.environ.get("TALESTATE_UI_CHECK"):
        $ tale.start()
    else:
        $ tale.start_latest()
    $ tale_sync_audio()
    if os.environ.get("TALESTATE_UI_CHECK"):
        jump tale_check
    jump tale_loop

label tale_loop:
    call screen tale_game
    jump tale_loop

# A short engine-owned smoke flow, invoked only by the development check script.
label tale_check:
    if not os.environ.get("TALESTATE_CHECK_SAVE"):
        $ tale.session.perform("travel.village")
        python:
            for check_turn in range(8):
                tale.session.perform("wait")
        $ tale.save()
    show screen tale_game
    $ renpy.pause(0.3, hard=True)
    if not os.environ.get("TALESTATE_CHECK_SAVE"):
        $ renpy.get_screen_variable("history_scroll", screen="tale_game").change(0)
        $ tale.session.perform("wait")
        $ renpy.restart_interaction()
        $ renpy.pause(0.4, hard=True)
        python:
            adjustment = renpy.get_screen_variable("history_scroll", screen="tale_game")
            assert adjustment.range > 0 and abs(adjustment.value - adjustment.range) < 1, "New text did not scroll to bottom"
    $ renpy.pause(0.5, hard=True)
    python:
        adjustment = renpy.get_screen_variable("history_scroll", screen="tale_game")
        assert abs(adjustment.value - adjustment.range) < 1, "New text did not scroll to bottom"
    $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"])
    if not os.environ.get("TALESTATE_CHECK_SAVE"):
        python:
            quest_check_states = dict(tale.session.engine.state["quests"])
            tale.session.engine.state["quests"].update({"deliver_medicine": "completed", "visit_keeper_tomorrow": "active", "deliver_bread": "active"})
            assert {q["giver"] for q in tale.view()["quests"]} == {"villager", "keeper", "baker"}
            assert all(q["giver_name"] for q in tale.view()["quests"])
        show screen tale_quests
        $ renpy.pause(0.25, hard=True, modal=False)
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".quests-active.png")
        $ renpy.set_screen_variable("status", "completed", screen="tale_quests")
        $ renpy.pause(0.25, hard=True, modal=False)
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".quests-completed.png")
        hide screen tale_quests
        $ tale.session.engine.state["quests"] = quest_check_states
    show screen tale_map
    $ renpy.pause(0.25, hard=True, modal=False)
    $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".map.png")
    hide screen tale_map
    show screen tale_memory(npc_id="keeper")
    $ renpy.pause(0.2, hard=True, modal=False)
    $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".memory.png")
    hide screen tale_memory
    if not os.environ.get("TALESTATE_CHECK_SAVE"):
        python:
            tale.session.dialogues["villager"] = [{"event_id": "scroll." + str(i), "minute": 0,
                "player": "这是第" + str(i) + "句测试。", "npc": "海风吹过街角，今天的店铺照常开门。" * 3}
                for i in range(18)]
    if "villager" in tale.session.memory.records:
        $ tale.open_chat("villager")
        show screen tale_chat(npc_id="villager")
        $ renpy.pause(0.2, hard=True, modal=False)
        if not os.environ.get("TALESTATE_CHECK_SAVE"):
            python:
                import pygame_sdl2 as pygame
                editor = renpy.get_displayable("tale_chat", "npc_editor")
                for editor_event in [pygame.event.Event(pygame.TEXTINPUT, text="你好，想听听你的故事。"),
                                     pygame.event.Event(pygame.KEYDOWN, key=pygame.K_RETURN, mod=pygame.KMOD_SHIFT, unicode="", repeat=False),
                                     pygame.event.Event(pygame.TEXTINPUT, text="我会耐心听。")]:
                    try:
                        editor.event(editor_event, 0, 0, 0)
                    except renpy.display.core.IgnoreEvent:
                        pass
                assert tale.chat_inputs["villager"] == "你好，想听听你的故事。\n我会耐心听。", "Editor lost Chinese text or newline"
                try:
                    editor.event(pygame.event.Event(pygame.KEYDOWN, key=pygame.K_RETURN, mod=0, unicode="", repeat=False), 0, 0, 0)
                except renpy.display.core.IgnoreEvent:
                    pass
            $ renpy.pause(0.3, hard=True, modal=False)
            $ tale.poll()
            $ assert tale.error and tale.chat_inputs["villager"], "API error lost the draft"
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".chat.png")
        if not os.environ.get("TALESTATE_CHECK_SAVE"):
            $ renpy.get_screen_variable("chat_scroll", screen="tale_chat").change(0)
        hide screen tale_chat
        $ tale.open_chat("villager")
        show screen tale_chat(npc_id="villager")
        $ renpy.pause(0.3, hard=True, modal=False)
        if not os.environ.get("TALESTATE_CHECK_SAVE"):
            python:
                adjustment = renpy.get_screen_variable("chat_scroll", screen="tale_chat")
                assert adjustment.range > 0 and abs(adjustment.value - adjustment.range) < 1, "Reopened chat is not at the newest text"
                adjustment.change(0)
            $ renpy.pause(0.2, hard=True, modal=False)
            $ assert renpy.get_screen_variable("chat_scroll", screen="tale_chat").value == 0, "Reading older messages was interrupted"
        hide screen tale_chat
    if "baker" in tale.session.memory.records:
        $ tale.open_chat("baker")
        show screen tale_chat(npc_id="baker")
        $ renpy.pause(0.2, hard=True, modal=False)
        $ assert not tale.chat_inputs["baker"], "Different NPC shared an input draft"
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".baker.png")
        hide screen tale_chat
    if not os.environ.get("TALESTATE_CHECK_SAVE"):
        python:
            tale.session.engine.state["entities"]["player"]["attributes"]["coins"] = 20
            tale.session.buy("merchant", "shop_sword")
            tale.session.buy("merchant", "shop_shield")
            tale.session.equip("shop_sword")
            tale.session.equip("shop_shield")
            assert tale.view()["stats"]["attack"] == 10 and tale.view()["stats"]["defense"] == 10
        show screen tale_inventory
        $ renpy.set_screen_variable("category", "equipment", screen="tale_inventory")
        $ renpy.pause(0.3, hard=True, modal=False)
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".inventory.png")
        hide screen tale_inventory
        show screen tale_shop(npc_id="merchant")
        $ renpy.pause(0.3, hard=True, modal=False)
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".shop.png")
        hide screen tale_shop
        python:
            for magic_check_spell in ("firebolt", "waterbolt", "frostbolt", "spark"):
                tale.session.learn("mage", magic_check_spell)
            tale.session.buy("mage", "apprentice_staff")
            tale.session.buy("merchant", "health_potion_1")
            tale.session.buy("merchant", "mana_potion_1")
        show screen tale_lessons(npc_id="mage")
        $ renpy.pause(0.3, hard=True, modal=False)
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".lessons.png")
        hide screen tale_lessons
        $ tale.open_chat("mage")
        show screen tale_chat(npc_id="mage")
        $ renpy.pause(0.3, hard=True, modal=False)
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".mage.png")
        hide screen tale_chat
        $ tale.session.perform("travel.forest")
        $ tale_sync_audio()
        $ renpy.pause(0.3, hard=True, modal=False)
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".forest.png")
        $ tale.session.perform("combat.start.slime_1")
        $ tale.session.perform("combat.attack")
        $ renpy.pause(0.3, hard=True, modal=False)
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".combat.png")
        $ tale.session.perform("combat.flee")
        $ tale.session.perform("combat.start.slime_2")
        show screen tale_magic
        $ renpy.pause(0.3, hard=True, modal=False)
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".magic.png")
        hide screen tale_magic
        $ tale.session.perform("combat.cast.frostbolt")
        $ assert tale.view()["stats"]["mana"] == 42
        $ tale.session.use_item("mana_potion_1")
        $ assert tale.view()["stats"]["mana"] == 50
        $ renpy.pause(0.3, hard=True, modal=False)
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".reaction.png")
        $ tale.session.perform("combat.cast.firebolt")
        $ assert not tale.view()["combat"]
        show screen tale_inventory(initial_category="misc")
        $ renpy.pause(0.3, hard=True, modal=False)
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".potions.png")
        hide screen tale_inventory
        $ tale.session.perform("travel.village")
        $ tale.session.perform("travel.home")
        if not tale.view()["clock"]["night"]:
            $ tale.session.perform("wait_evening")
        $ sleep_check_day = tale.view()["clock"]["day"]
        $ tale_action("sleep")
        $ renpy.pause(0.2, hard=True, modal=False)
        $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".sleep.png")
        $ tale.fast_forward_sleep()
        $ assert tale.view()["clock"]["day"] == sleep_check_day + 1 and not tale.sleeping
        hide screen tale_sleep
        $ tale.session.perform("wait_evening")
        $ tale_action("sleep")
        $ tale.sleep_waiting = True
        $ renpy.pause(2.2, hard=True, modal=False)
        $ assert tale.view()["clock"]["day"] == sleep_check_day + 2 and not tale.sleeping
    show screen tale_settings
    $ renpy.pause(0.3, hard=True, modal=True)
    $ renpy.screenshot(os.environ["TALESTATE_UI_CHECK"] + ".settings.png")
    $ renpy.quit()
