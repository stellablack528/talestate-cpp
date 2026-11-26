screen tale_scene(view):
    $ scene_key = view["location"].get("background", view["location"]["id"])
    $ art = tale.resource("backgrounds", scene_key)
    $ art_file = tale.resource_file("backgrounds", scene_key)
    if art_file:
        add art_file fit "cover"
    elif scene_key == "forest":
        add Solid("#25382c")
        add Solid("#3c4c32") ypos 370 ysize 530
        for x in range(50, 1450, 170):
            add Solid("#253023") xpos x ypos 70 xsize 140 ysize 440
            add Solid("#443e2d") xpos (x + 55) ypos 320 xsize 28 ysize 240
    else:
        # Code-native placeholders: real image files can replace these in the manifest.
        add Solid(art.get("sky", "#14333f"))
        add Solid(art.get("sea", "#244a55")) ypos 360 ysize 540
        add Solid("#9da89322") ypos 325 ysize 4
        for y in range(430, 900, 90):
            add Solid("#d0ded40b") xpos 440 ypos y xsize 1000 ysize 2
        if art.get("layout") == "lighthouse":
            add Solid("#e8dfb9") xpos 980 ypos 160 xsize 86 ysize 280
            add Solid("#b9a772") xpos 970 ypos 145 xsize 106 ysize 30
            add Solid("#eaca7e") xpos 990 ypos 170 xsize 66 ysize 40
            add Solid("#f5e5ac15") xpos 730 ypos 170 xsize 600 ysize 70
        elif art.get("layout") == "home":
            add Solid("#4c3d35") xpos 790 ypos 250 xsize 430 ysize 350
            add Solid("#bda87b") xpos 750 ypos 240 xsize 510 ysize 25
            add Solid("#d8b66a") xpos 840 ypos 320 xsize 110 ysize 110
            add Solid("#292a2b") xpos 1040 ypos 380 xsize 100 ysize 220
        elif art.get("layout") == "harbor":
            add Solid("#2a2629") xpos 490 ypos 320 xsize 160 ysize 180
            add Solid("#3c3635") xpos 660 ypos 360 xsize 190 ysize 140
            add Solid("#8a7b61") xpos 440 ypos 490 xsize 880 ysize 13
            for x in range(480, 1360, 130):
                add Solid("#514b43") xpos x ypos 495 xsize 12 ysize 110
    add Solid("#07171da8" if view["clock"]["night"] else "#07171d55")

screen tale_game():
    tag game
    $ v = tale.view()
    $ world_lines = [line for line in v["history"] if line["kind"] in ("fact", "narration")]
    default history_scroll = ui.adjustment(ranged=tale_history_ranged)
    default world_input = FieldInputValue(tale, "input", default=False)
    timer 0.2 repeat True action Function(tale_tick)
    key "game_menu" action Show("tale_settings")
    add Solid("#171b18")
    use tale_scene(v)

    frame:
        xpos 28 ypos 24 xsize 1384 ysize 80
        background Solid("#171b19ed") padding (24,15)
        add Solid("#ac8e5355") ypos 61 ysize 1
        hbox:
            spacing 24 yalign 0.5
            text "TALESTATE" color "#c9ae78" size 27 kerning 2
            text v["title"] size 21 color "#ddd3bb" substitute False
        vbox:
            xpos 752 yalign 0.5 spacing 8
            text ("第 " + str(v["clock"]["day"]) + " 天   " + v["clock"]["label"] + ("  夜" if v["clock"]["night"] else "  昼")) size 19 color "#d6c49a"
            bar value StaticValue(v["clock"]["day_fraction"], 1.0) xsize 365 ysize 5 thumb None
        textbutton "设置" xalign 1.0 yalign 0.5 action Show("tale_settings")

    frame:
        xpos 28 ypos 124 xsize 312 ysize 748
        background Solid("#1b211eeb") padding (24,24)
        vbox:
            spacing 18
            text "旅人手记" style "section_title"
            text v["player"]["name"] size 33 substitute False
            text ("生命  " + str(v["stats"]["health"]) + " / " + str(v["stats"]["max_health"])) size 20 color "#d2c6a9"
            bar value StaticValue(v["stats"]["health"], v["stats"]["max_health"]) xsize 245 ysize 8
            text ("法力  " + str(v["stats"]["mana"]) + " / " + str(v["stats"]["max_mana"])) size 20 color "#b7cce1"
            bar value StaticValue(v["stats"]["mana"], v["stats"]["max_mana"]) xsize 245 ysize 8 left_bar Solid("#759abc")
            text ("攻击  " + str(v["stats"]["attack"]) + "     防御  " + str(v["stats"]["defense"])) size 20 color "#d2c6a9"
            text ("法术强度  " + str(v["stats"]["magic_attack"])) size 20 color "#b7cce1"
            text ("金币  " + str(v["player"]["attributes"].get("coins", 0))) size 20 color "#d2c6a9"
            add Solid("#a68c5144") ysize 1
            text v["location"]["name"] size 21 substitute False
            if not v["clock"]["at_home"]:
                text ("归途约 " + str(v["clock"]["return_minutes"]) + " 分钟") style "small_note"
            null height 2
            vbox:
                spacing 16
                textbutton "背包" xfill True action Show("tale_inventory")
                textbutton "任务" xfill True action Show("tale_quests")
                textbutton "地图" xfill True action Show("tale_map")

    vbox:
        xpos 380 ypos 145 spacing 14
        text v["location"]["name"] size 40 color "#efe2c5" substitute False
        add Solid("#b99b62") xsize 68 ysize 2
    vbox:
        xpos 1170 ypos 145 spacing 12
        for npc_id in v["present_npcs"]:
            hbox:
                spacing 8
                textbutton tale.session.engine.name(npc_id) text_size 22 action [Function(tale.open_chat, npc_id), Show("tale_chat", npc_id=npc_id)] sensitive not tale.busy
                if tale.session.engine.entities[npc_id].get("shop"):
                    textbutton "交易" text_size 19 padding (12,12) action Show("tale_shop", npc_id=npc_id) sensitive not tale.busy
                if tale.session.engine.entities[npc_id].get("lessons"):
                    textbutton "学习" text_size 19 padding (10,12) action Show("tale_lessons", npc_id=npc_id) sensitive not tale.busy

    if v["enemies"]:
        hbox:
            xpos 380 ypos 250 spacing 15
            for enemy in v["enemies"]:
                if not v["combat"] or enemy["id"] == v["combat"]["enemy"]:
                    frame:
                        xsize (520 if v["combat"] else 245) ysize 203 padding (12,14) background Solid("#19251fea")
                        hbox:
                            spacing 10
                            $ slime_art = tale.resource_file("monsters", enemy.get("sprite", "slime"))
                            if slime_art:
                                add slime_art xsize (200 if v["combat"] else 60) ysize 165 fit "contain"
                            else:
                                text "●" size 58 color {"fire":"#d9825b","water":"#6faecf","ice":"#b7dbe1","lightning":"#a88bc8"}.get(enemy.get("element"), "#8ac66c")
                            vbox:
                                spacing 8
                                text enemy["name"] size 24 substitute False
                                text ("生命 " + str(enemy["health"]) + " / " + str(enemy["max_health"])) size 18
                                text ("攻击 " + str(enemy["attack"]) + "  防御 " + str(enemy["defense"])) size 16
                                if enemy.get("defeated"):
                                    text "已击败" color "#a8bd85" size 20
                                elif v["combat"]:
                                    text ("第 " + str(v["combat"]["round"] + 1) + " 回合") size 20 color "#d4b87d"
                                elif not v["clock"]["night"]:
                                    textbutton "挑战" action Function(tale_action, "combat.start." + enemy["id"]) sensitive not tale.busy

    if v["clock"]["night"]:
        frame:
            xpos 380 ypos 250 xsize 690 padding (20,15) background Solid("#1b211ee8")
            text ("夜深了，回家歇息吧。" if not v["clock"]["at_home"] else "灯火渐暗，可以歇息了。") color "#d7bd87" size 22

    frame:
        xpos 376 ypos 470 xsize 1036 ysize 204 padding (24,18)
        background Solid("#19201ce8")
        viewport:
            mousewheel True draggable True scrollbars "vertical" yadjustment history_scroll yinitial 1.0
            vbox:
                spacing 13
                for line in world_lines:
                    text line["text"] size 21 xmaximum 950 color "#dfd7c3" substitute False

    frame:
        xpos 376 ypos 694 xsize 1036 ysize 178 padding (22,18)
        background Solid("#171d1aed")
        vbox:
            spacing 15
            hbox:
                spacing 14
                button:
                    xsize 800 ysize 54 padding (14,10) background Solid("#30352c")
                    action world_input.Enable()
                    input value world_input copypaste True length 2000 xmaximum 770 action Function(tale.submit)
                textbutton "行动" action Function(tale.submit) sensitive not tale.busy
            hbox:
                spacing 10
                for action in v["actions"]:
                    if not action.get("ai_interaction") and not action["id"].startswith(("combat.start.", "combat.cast.", "combat.use.")):
                        $ label = action["name"]
                        if action["intent"] == "move":
                            $ label += "  " + str(action["distance_km"]) + "km / " + str(action["duration_minutes"]) + "分钟"
                        textbutton label text_size 17 padding (13,9) action Function(tale_action, action["id"]) sensitive not tale.busy
                if v["combat"]:
                    textbutton "法术" text_size 17 action Show("tale_magic")
                    textbutton "药水" text_size 17 action Show("tale_inventory", initial_category="misc")
    if tale.error:
        text tale.error xpos 388 ypos 420 size 17 color "#e4b38b" xmaximum 1000 substitute False

screen tale_settings():
    modal True
    zorder 50
    default address_input = FieldInputValue(tale.settings, "base_url", default=False)
    default key_input = FieldInputValue(tale.settings, "api_key", default=False)
    default model_input = FieldInputValue(tale.settings, "model", default=False)
    default archive_input = FieldInputValue(tale, "archive_path", default=False)
    if os.environ.get("TALESTATE_UI_CHECK"):
        timer 0.6 action [Function(renpy.screenshot, os.environ["TALESTATE_UI_CHECK"] + ".settings.png"), Quit(confirm=False)]
    add Solid("#030b12cc")
    frame:
        xalign 0.5 yalign 0.5 xsize 900 ysize 730 padding (36,28)
        vbox:
            spacing 17
            hbox:
                xfill True
                text "设置 / 模型与世界" size 30 color "#d4b87d"
                textbutton "关闭" xalign 1.0 action Hide("tale_settings")
            viewport:
                ysize 570 mousewheel True scrollbars "vertical"
                vbox:
                    spacing 14
                    text "模型 API" style "section_title"
                    text "API 地址" style "small_note"
                    button:
                        action address_input.Enable()
                        xfill True padding (12,8) background Solid("#203943")
                        input value address_input copypaste True
                    hbox:
                        spacing 20
                        vbox:
                            spacing 8
                            text "API Key（仅本次内存）" style "small_note"
                            button:
                                action key_input.Enable()
                                xsize 395 padding (12,8) background Solid("#203943")
                                input value key_input mask "*" copypaste True
                        vbox:
                            spacing 8
                            text "模型名称" style "small_note"
                            button:
                                action model_input.Enable()
                                xsize 395 padding (12,8) background Solid("#203943")
                                input value model_input copypaste True
                    hbox:
                        spacing 12
                        textbutton "应用配置" action Function(tale.apply_settings) sensitive not tale.busy
                        text "人物对话、接取和交付需要 AI；步行可直接点选。" style "small_note" yalign 0.5
                    add Solid("#58716b55") ysize 1
                    text "世界与进度" style "section_title"
                    hbox:
                        spacing 12
                        textbutton "保存进度" action Function(tale.save) sensitive not tale.busy
                        textbutton "继续游戏" action Function(tale_load) sensitive not tale.busy
                        textbutton "退出游戏" action Quit(confirm=False) sensitive not tale.busy
                    text "独立存档（含世界、人物记忆和专属素材）" style "small_note"
                    for folder, info in tale.saved_games():
                        textbutton (info["title"] + " / " + info["save_id"]) action [Function(tale.open_save, folder), Function(tale_sync_audio)] sensitive not tale.busy
                    hbox:
                        spacing 12
                        textbutton "导出当前存档 ZIP" action Function(tale.export_archive) sensitive not tale.busy
                        textbutton "打开存档文件夹" action OpenDirectory(str(tale.session.session_dir)) sensitive not tale.busy
                    text "导入 ZIP：粘贴压缩包完整路径" style "small_note"
                    button:
                        action archive_input.Enable()
                        xfill True padding (12,8) background Solid("#203943")
                        input value archive_input copypaste True
                    textbutton "导入并打开存档" action [Function(tale.import_archive), Function(tale_sync_audio)] sensitive not tale.busy
                    text "切换世界会开始新旅途，请先保存当前进度。" style "small_note"
                    hbox:
                        spacing 10
                        for path in tale.worlds[:4]:
                            textbutton path.parent.name action [Function(tale.start, str(path)), Function(tale_sync_audio), Hide("tale_settings")] sensitive not tale.busy
                    text "音乐音量" style "small_note"
                    bar value Preference("music volume") style "bar" xmaximum 780
                    if tale.session:
                        text "[tale.status!q]" size 18 color "#d4b87d" xmaximum 810

screen tale_memory(npc_id):
    modal True
    zorder 80
    $ memory = tale.memory(npc_id)
    add Solid("#030b12bb")
    frame:
        xalign 0.5 yalign 0.5 xsize 780 ysize 640
        vbox:
            spacing 17
            text "[tale.session.engine.name(npc_id)!q] / 记忆" size 28 color "#d4b87d"
            text "这名人物亲历的事件与交谈记录，随本局进度保存。" style "small_note"
            viewport:
                ysize 440 mousewheel True draggable True scrollbars "vertical"
                vbox:
                    spacing 12
                    text memory["profile"] substitute False
                    if memory.get("daily_state", {}).get("activity"):
                        text "近况" style "section_title"
                        text memory["daily_state"]["activity"] size 20 substitute False
                    for plan in memory.get("plans", []):
                        text (("已办妥：" if plan["status"] == "completed" else "计划：") + plan["text"]) size 18 substitute False
                    if memory.get("notes"):
                        text "记在心里的事" style "section_title"
                        for note in memory["notes"]:
                            text note["text"] size 18 substitute False
                    for field, value in memory.get("settings", {}).items():
                        if field != "profile":
                            text (field + "：" + value) size 18 substitute False
                    text "设定调整记录" style "section_title"
                    for decision in memory.get("setting_decisions", [])[-6:]:
                        text (("接受" if decision["decision"] == "accept" else "拒绝") + "：" + decision["reason"]) size 18 substitute False
                    text "初始认知（旅途开始时）" style "section_title"
                    for row in memory["known"]:
                        text tale.known_text(row) size 19 substitute False
                    text ("亲历事件 " + str(len(memory["observations"])) + " 条") style "small_note"
                    for event in memory["observations"][-12:]:
                        text (event["event_id"] + " / " + str(event["minute"]) + " 分钟") style "small_note"
                        text tale.memory_event_text(event) size 19 substitute False
            textbutton "返回旅途" action Hide("tale_memory") xalign 1.0


screen tale_chat(npc_id):
    tag npc_chat
    modal True
    zorder 60
    default chat_scroll = ui.adjustment(ranged=partial(tale_chat_ranged, npc_id))
    default chat_input = DictInputValue(tale.chat_inputs, npc_id, default=False)
    $ messages = tale.chat_history(npc_id)
    $ current = npc_id in tale.view()["present_npcs"]
    $ portrait_key = npc_id + "." + tale.portrait_expression(npc_id)
    $ portrait_file = tale.resource_file("portraits", portrait_key) or tale.resource_file("portraits", npc_id + ".neutral")
    on "show" action [chat_input.Enable(), Function(chat_scroll.change, chat_scroll.range)]
    on "hide" action chat_input.Disable()
    timer 0.2 repeat True action Function(tale_tick)
    key "game_menu" action [chat_input.Disable(), Hide("tale_chat")]
    add Solid("#090c0bc9")
    frame:
        xalign 0.5 yalign 0.5 xsize 1260 ysize 804 padding (0,0)
        background Solid("#20251ff5")
        add Solid("#ab8f5644") ypos 77 ysize 1
        text tale.session.engine.name(npc_id) xpos 32 ypos 25 size 31 color "#e7d7af" substitute False
        if tale.npc_activity(npc_id):
            text tale.npc_activity(npc_id) xpos 340 ypos 28 size 17 color "#b7b19a" xmaximum 650 substitute False
        textbutton "手记" xpos 1040 ypos 20 action Show("tale_memory", npc_id=npc_id)
        if tale.session.engine.entities[npc_id].get("shop"):
            textbutton "商店" xpos 935 ypos 20 action Show("tale_shop", npc_id=npc_id) sensitive not tale.busy
        textbutton "离开" xpos 1140 ypos 20 action [chat_input.Disable(), Hide("tale_chat")]

        frame:
            xpos 24 ypos 105 xsize 294 ysize 654 padding (10,10)
            background Solid("#151b1844")
            if portrait_file:
                add portrait_file xalign 0.5 yalign 1.0 xsize 274 ysize 610 fit "contain"
            elif tale.session.engine.entities[npc_id].get("shop"):
                vbox:
                    spacing 20
                    text "在售货物" size 24 color "#d4b87d"
                    for goods in tale.shop_stock(npc_id):
                        text (goods["name"] + " · " + str(goods["price"]) + " 金币") size 21 substitute False
        frame:
            xpos 340 ypos 102 xsize 888 ysize 457 padding (22,18)
            background Solid("#131a1666")
            viewport:
                ysize 421 mousewheel True draggable True scrollbars "vertical" yadjustment chat_scroll
                vbox:
                    spacing 12
                    for line in messages:
                        text line["speaker"] size 17 color "#bda671" substitute False
                        text line["text"] size 22 xmaximum 825 substitute False
        if tale.busy:
            text "…" xpos 362 ypos 572 size 22 color "#c4b27f"
        elif tale.error:
            text tale.error xpos 354 ypos 570 size 17 xmaximum 850 color "#e4b38b" substitute False
        elif not current:
            text "对方已经离开了。" xpos 354 ypos 574 style "small_note"

        button:
            xpos 340 ypos 614 xsize 756 ysize 132 padding (16,12)
            action chat_input.Enable()
            background Solid("#343a2e")
            hover_background Solid("#3a4133")
            viewport:
                ysize 106 mousewheel True
                input id "npc_editor" value chat_input copypaste True multiline True length 4000 xmaximum 710 size 22 action Function(tale.chat, npc_id)
        textbutton "发送" xpos 1110 ypos 650 xsize 118 action [Function(tale.chat, npc_id), chat_input.Enable()] sensitive (current and not tale.busy)
        text "Enter 发送   Shift+Enter 换行" xpos 348 ypos 762 size 15 color "#969883"

screen tale_quests():
    modal True
    zorder 75
    default status = "active"
    $ quests = [row for row in tale.view()["quests"] if row["status"] == status]
    timer 0.2 repeat True action Function(tale_tick)
    key "game_menu" action Hide("tale_quests")
    add Solid("#080c0bce")
    frame:
        xalign 0.5 yalign 0.5 xsize 1140 ysize 740 padding (30,24) background Solid("#20271ff9")
        vbox:
            spacing 22
            hbox:
                xfill True
                text "任务" size 32 color "#e7d7af"
                textbutton "关闭" xalign 1.0 action Hide("tale_quests")
            hbox:
                spacing 16
                textbutton "未完成" action SetScreenVariable("status", "active") selected status == "active"
                textbutton "已完成" action SetScreenVariable("status", "completed") selected status == "completed"
            viewport:
                ysize 540 mousewheel True draggable True scrollbars "vertical"
                vbox:
                    spacing 18
                    for giver in dict.fromkeys(row["giver_name"] for row in quests):
                        text ("委托人：" + giver) size 23 color "#c9ae78" substitute False
                        for quest in quests:
                            if quest["giver_name"] == giver:
                                frame:
                                    xsize 1040 padding (20,18) background Solid("#30392de8")
                                    vbox:
                                        spacing 7
                                        text quest["title"] size 24 xmaximum 985 substitute False
                                        if status == "active" and quest["description"] and quest["description"] != quest["title"]:
                                            text quest["description"] size 19 color "#d7d5c7" xmaximum 985 substitute False

screen tale_map():
    modal True
    zorder 75
    default selected = tale.view()["location"]["id"]
    $ v = tale.view()
    $ points = tale.map_points()
    $ map_art = tale.resource_file("maps", "region")
    $ destination = next((row for row in points if row["id"] == selected), points[0])
    $ travel = next((a for a in v["actions"] if a["id"] == "travel." + selected), None)
    timer 0.2 repeat True action Function(tale_tick)
    key "game_menu" action Hide("tale_map")
    add Solid("#080c0bce")
    frame:
        xalign 0.5 yalign 0.5 xsize 1280 ysize 760 padding (28,24) background Solid("#20271ff9")
        vbox:
            spacing 24
            hbox:
                xfill True
                text "海岸地图" size 32 color "#e7d7af"
                textbutton "关闭" xalign 1.0 action Hide("tale_map")
            hbox:
                spacing 22
                fixed:
                    xsize 900 ysize 506
                    if map_art:
                        add map_art xsize 900 ysize 506 fit "cover"
                    else:
                        add Solid("#b1a27b")
                    for point in points:
                        textbutton point["name"]:
                            xpos int(point["x"] * 900) ypos int(point["y"] * 506) anchor (0.5,0.5)
                            text_size 22 text_color ("#fff2c7" if point["id"] == v["location"]["id"] else "#f0e6ca")
                            background Solid("#72643ced" if point["id"] == v["location"]["id"] else "#28362df2")
                            padding (14,10)
                            selected selected == point["id"]
                            action SetScreenVariable("selected", point["id"])
                vbox:
                    xsize 290 spacing 18
                    text ("当前位置：" + v["location"]["name"]) size 23 color "#d4b87d" substitute False
                    text destination["name"] size 28 substitute False
                    if selected == v["location"]["id"]:
                        text "你在这里。" size 20 color "#b9bda8"
                    elif travel:
                        text (str(travel["distance_km"]) + " 公里 / " + str(travel["duration_minutes"]) + " 分钟") size 20
                        textbutton "前往这里" action [Function(tale_action, travel["id"]), Hide("tale_map")] sensitive not tale.busy
                    elif v["combat"]:
                        text "战斗中无法移动。" size 19 color "#b9bda8"
                    elif v["clock"]["night"]:
                        text "夜深了，只能沿路返家。" size 19 color "#b9bda8" xmaximum 270
                    else:
                        text "沿相连的道路逐站前往。" size 19 color "#b9bda8" xmaximum 270
                    null height 5
                    text "道路" size 22 color "#d4b87d"
                    for route in v["map"]["routes"]:
                        text (tale.session.engine.name(route["from"]) + " — " + tale.session.engine.name(route["to"])) size 20 substitute False
                        text (str(route["distance_km"]) + " 公里 / " + str(route["minutes"]) + " 分钟") size 17 color "#b9bda8"
            text "亮色标记是当前位置，点击地点查看路线。" size 18 color "#b9bda8"

screen tale_sleep():
    modal True
    zorder 100
    add Solid("#080b0bea")
    vbox:
        xalign 0.5 yalign 0.5 spacing 24
        text "睡觉中……" size 40 color "#e7d7af" xalign 0.5
        text "夜色笼罩小镇，人们的生活仍在继续。" size 23 color "#b7b19a"

screen tale_inventory(initial_category="quest"):
    modal True
    zorder 75
    default category = initial_category
    $ v = tale.view()
    timer 0.2 repeat True action Function(tale_tick)
    key "game_menu" action Hide("tale_inventory")
    add Solid("#080c0bce")
    frame:
        xalign 0.5 yalign 0.5 xsize 1180 ysize 760 padding (30,24) background Solid("#20271ff9")
        vbox:
            spacing 22
            hbox:
                xfill True
                text "背包与装备" size 32 color "#e7d7af"
                textbutton "关闭" xalign 1.0 action Hide("tale_inventory")
            hbox:
                spacing 14
                for key, title in [("quest", "任务物品"), ("misc", "日常杂物"), ("equipment", "装备物品")]:
                    textbutton title action SetScreenVariable("category", key) selected category == key
            hbox:
                spacing 32
                viewport:
                    xsize 755 ysize 515 mousewheel True draggable True scrollbars "vertical"
                    vbox:
                        spacing 14
                        for item in v["inventory"]:
                            if item["category"] == category:
                                frame:
                                    xsize 730 padding (18,14) background Solid("#30392de8")
                                    vbox:
                                        spacing 10
                                        text (item["name"] + ("（已装备）" if item["equipped"] else "")) size 23 substitute False
                                        if item.get("description"):
                                            text item["description"] size 18 xmaximum 685 color "#b9bda8" substitute False
                                        if item.get("slot"):
                                            hbox:
                                                spacing 16
                                                text ("攻击 +" + str(item.get("bonuses", {}).get("attack", 0)) + "  防御 +" + str(item.get("bonuses", {}).get("defense", 0)) + "  法术 +" + str(item.get("bonuses", {}).get("magic_attack", 0))) size 18 yalign 0.5
                                                textbutton ("卸下" if item["equipped"] else "装备") action Function(tale.equip, item["id"], item["equipped"]) sensitive not tale.busy and not v["combat"]
                                        if item.get("consumable"):
                                            textbutton "使用" action Function(tale.use_item, item["id"]) sensitive item["usable"] and not tale.busy
                vbox:
                    xsize 300 spacing 20
                    text "角色属性" size 25 color "#d4b87d"
                    text ("生命  " + str(v["stats"]["health"]) + " / " + str(v["stats"]["max_health"])) size 22
                    text ("法力  " + str(v["stats"]["mana"]) + " / " + str(v["stats"]["max_mana"])) size 22 color "#b7cce1"
                    text ("攻击  " + str(v["stats"]["attack"])) size 22
                    text ("防御  " + str(v["stats"]["defense"])) size 22
                    text ("法术强度  " + str(v["stats"]["magic_attack"])) size 22 color "#b7cce1"
                    textbutton "已学法术" action Show("tale_magic")
                    null height 8
                    for slot, title in [("weapon", "武器"), ("offhand", "副手"), ("body", "护甲")]:
                        text (title + "：" + tale.session.engine.name(v["equipment"][slot]) if slot in v["equipment"] else title + "：未装备") size 20 substitute False
                    if tale.error:
                        text tale.error size 17 color "#e4b38b" xmaximum 280 substitute False

screen tale_magic():
    modal True
    zorder 85
    $ v = tale.view()
    timer 0.2 repeat True action Function(tale_tick)
    key "game_menu" action Hide("tale_magic")
    add Solid("#080c0bce")
    frame:
        xalign 0.5 yalign 0.5 xsize 1200 ysize 740 padding (30,24) background Solid("#20271ff9")
        vbox:
            spacing 24
            hbox:
                xfill True
                text "法术" size 32 color "#d3dfee"
                textbutton "关闭" xalign 1.0 action Hide("tale_magic")
            text ("法力 " + str(v["stats"]["mana"]) + " / " + str(v["stats"]["max_mana"]) + "     法术强度 " + str(v["stats"]["magic_attack"])) size 22 color "#b7cce1"
            hbox:
                spacing 28
                viewport:
                    xsize 700 ysize 510 mousewheel True scrollbars "vertical"
                    vbox:
                        spacing 16
                        if not v["spells"]:
                            text "尚未掌握法术，可以向村庄的伊莲娜学习。" size 22 xmaximum 660
                        for spell in v["spells"]:
                            frame:
                                xsize 675 padding (18,16) background Solid("#30392de8")
                                hbox:
                                    spacing 24
                                    vbox:
                                        xsize 430 spacing 10
                                        text (spell["name"] + " / " + tale.element_name(spell["element"]) + "系") size 24 substitute False
                                        text ("消耗 " + str(spell["mana_cost"]) + " 点法力") size 20 color "#b7cce1"
                                    textbutton "施放" action [Function(tale.perform, "combat.cast." + spell["id"]), Hide("tale_magic")] sensitive bool(v["combat"]) and v["stats"]["mana"] >= spell["mana_cost"] and not tale.busy
                vbox:
                    xsize 385 spacing 17
                    text "元素反应" size 25 color "#d4b87d"
                    text "水击火：蒸发 ×2\n火击水：蒸发 ×1.5" size 21
                    text "火击冰：融化 ×2\n冰击火：融化 ×1.5" size 21
                    text "水 + 冰：冻结，跳过一次反击；间隔一回合才能再次冻结。" size 20 xmaximum 375
                    text "水 + 雷：感电，额外4伤害\n火 + 雷：超载，额外6伤害" size 20
                    text "冰 + 雷：超导，随后两回合物理伤害提高50%。" size 20 xmaximum 375
                    text "同元素仅造成25%伤害。史莱姆自带元素，不会因反应消失。" size 19 color "#b9bda8" xmaximum 375

screen tale_lessons(npc_id):
    modal True
    zorder 80
    $ v = tale.view()
    $ courses = tale.lessons(npc_id)
    timer 0.2 repeat True action Function(tale_tick)
    key "game_menu" action Hide("tale_lessons")
    add Solid("#080c0bce")
    frame:
        xalign 0.5 yalign 0.5 xsize 1080 ysize 740 padding (30,24) background Solid("#20271ff9")
        vbox:
            spacing 24
            hbox:
                xfill True
                text (tale.session.engine.name(npc_id) + "的课堂") size 30 color "#d3dfee" substitute False
                textbutton "关闭" xalign 1.0 action Hide("tale_lessons")
            text "学习基础法术后，可在战斗中消耗法力施放。每门课约十分钟。" size 21 color "#b9bda8"
            viewport:
                ysize 530 mousewheel True scrollbars "vertical"
                vbox:
                    spacing 16
                    for course in courses:
                        frame:
                            xsize 980 padding (20,18) background Solid("#30392de8")
                            hbox:
                                spacing 30
                                vbox:
                                    xsize 690 spacing 10
                                    text (course["name"] + " / " + tale.element_name(course["element"]) + "系") size 24 substitute False
                                    text ("消耗法力 " + str(course["mana_cost"]) + ("；基础课程免费" if course["price"] == 0 else "；学费 " + str(course["price"]) + " 金币")) size 20 color "#b7cce1"
                                textbutton ("已掌握" if course["learned"] else "学习") action Function(tale.learn, npc_id, course["id"]) sensitive not course["learned"] and npc_id in v["present_npcs"] and not tale.busy and v["player"]["attributes"].get("coins", 0) >= course["price"]
            if tale.error:
                text tale.error size 18 color "#e4b38b" substitute False

screen tale_shop(npc_id):
    modal True
    zorder 80
    $ v = tale.view()
    $ stock = tale.shop_stock(npc_id)
    $ coins = v["player"]["attributes"].get("coins", 0)
    timer 0.2 repeat True action Function(tale_tick)
    key "game_menu" action Hide("tale_shop")
    add Solid("#080c0bce")
    frame:
        xalign 0.5 yalign 0.5 xsize 1120 ysize 750 padding (30,24) background Solid("#20271ff9")
        vbox:
            spacing 22
            hbox:
                xfill True
                text (tale.session.engine.name(npc_id) + "的商店") size 32 color "#e7d7af" substitute False
                textbutton "关闭" xalign 1.0 action Hide("tale_shop")
            text ("持有金币  " + str(coins)) size 23 color "#d4b87d"
            viewport:
                ysize 490 mousewheel True draggable True scrollbars "vertical"
                vbox:
                    spacing 13
                    for goods in stock:
                        frame:
                            xsize 1030 padding (18,14) background Solid("#30392de8")
                            hbox:
                                spacing 24
                                vbox:
                                    xsize 720 spacing 8
                                    text goods["name"] size 25 substitute False
                                    text goods["description"] size 18 color "#b9bda8" xmaximum 700 substitute False
                                vbox:
                                    spacing 8
                                    text (str(goods["price"]) + " 金币") size 23 color "#d4b87d"
                                    textbutton "购买" action Function(tale.buy, npc_id, goods["id"]) sensitive (not tale.busy and coins >= goods["price"] and npc_id in v["present_npcs"])
            if tale.error:
                text tale.error size 18 color "#e4b38b" xmaximum 1020 substitute False
