"""Render facts from engine events. Models cannot supply authoritative prose."""


def render_event(engine, event):
    kind = event["type"]
    if kind == "moved":
        prefix = "" if event["entity"] == engine.player else engine.name(event["entity"])
        return f"{prefix}来到{engine.name(event['to_location'])}。"
    if kind == "clue_found":
        return "获得线索：" + event.get("title", event["clue"])
    if kind == "npc_decided":
        return engine.name(event["npc"]) + "同意了这项请求。"
    if kind == "visit_scheduled":
        return f"今日约见：{event['title']}。地点：{engine.name(event['location'])}。"
    if kind == "appointment_attended":
        return "已赴约：" + event["title"] + "。"
    if kind == "visit_missed":
        return "错过约见：" + event["title"] + "。"
    if kind == "quest_expired":
        return "委托已撤回：" + engine.quests[event["quest"]]["title"] + "。"
    if kind == "item_created":
        return engine.name(event["entity"]) + "写好了「" + engine.name(event["item"]) + "」。"
    if kind == "survey_checked":
        observed = event.get("observation")
        return f"在{engine.name(event['location'])}查看了「{event['focus']}」。" + (f"\n现场：{observed}" if observed else "")
    if kind == "survey_reported":
        return f"向{engine.name(event['npc'])}汇报了查看结果。"
    if kind == "presence":
        return engine.name(event["entity"]) + ("来到了这里。" if event["arrived"] else "离开了这里。")
    if kind == "delivered":
        return f"{engine.name(event['from'])}将{engine.name(event['item'])}交给了{engine.name(event['to'])}。"
    if kind == "quest_completed":
        return "完成任务：" + engine.quests[event["quest"]]["title"]
    if kind == "quest_accepted":
        return "接取委托：" + engine.quests[event["quest"]]["title"]
    if kind == "waited":
        return "在家消磨了一段时间，天色渐暗。" if event.get("until_evening") else "休息了一小时。"
    if kind == "slept":
        return f"在家睡了一觉，现在是第 {event['day']} 天 {event['wake_time']}。"
    if kind == "attribute_changed":
        labels = {"coins": "金币", "health": "生命", "stamina": "体力"}
        attribute = labels.get(event["attribute"], event["attribute"])
        amount = event["amount"]
        return f"{attribute} {'+' if amount >= 0 else ''}{amount}，现为 {event['value']}。"
    if kind == "dialogue":
        return event["text"]
    if kind == "npc_encounter":
        return "与" + "、".join(engine.name(key) for key in event["participants"]) + "聊了聊：" + event["topic"]
    if kind == "equipment_changed":
        return ("装备了" if event["equipped"] else "卸下了") + engine.name(event["item"]) + "。"
    if kind == "spell_learned":
        name = next(row["name"] for row in engine.world["spells"] if row["id"] == event["spell"])
        return "跟随" + engine.name(event["teacher"]) + "学会了" + name + "。"
    if kind == "consumed":
        resource = "生命" if event["resource"] == "health" else "法力"
        return "使用" + engine.name(event["item"]) + "，" + resource + "恢复" + str(event["amount"]) + "，现为" + str(event["value"]) + "。"
    if kind == "elemental_reaction":
        return "触发" + event["reaction"] + ("，本次伤害×" + str(event["multiplier"]) if event["multiplier"] != 1 else "") + ("，额外伤害+" + str(event["bonus"]) if event["bonus"] else "") + "。"
    if kind.startswith("combat_"):
        enemy = next((row["name"] for row in engine.world.get("encounters", []) if row["id"] == event["enemy"]), event["enemy"])
        if kind == "combat_started":
            return "遭遇" + enemy + "，战斗开始。"
        if kind == "combat_hit":
            spell = next((row["name"] for row in engine.world.get("spells", []) if row["id"] == event.get("spell")), "")
            prefix = "你施放" + spell + "，对" + enemy if spell else (("你对" + enemy) if event["attacker"] == "player" else (enemy + "对你"))
            return prefix + "造成了" + str(event["damage"]) + "点伤害。"
        if kind == "combat_frozen":
            return enemy + "被冻结，跳过本次反击。"
        if kind == "combat_guarded":
            return "你举起防备，减轻这一回合的伤害。"
        if kind == "combat_won":
            return "击败了" + enemy + "。"
        if kind == "combat_fled":
            return "你退开几步，脱离了战斗。"
        if kind == "combat_lost":
            return "你败下阵来，带着伤勉强回到了家。休息一晚可以恢复。"
    return ""


def presentation(engine, events):
    return [{"event_id": event["event_id"], "kind": "dialogue" if event["type"] == "dialogue" else "fact",
             "speaker": engine.name(event["npc"]) if event["type"] == "dialogue" else "旅途",
             "text": render_event(engine, event), "npc_id": event.get("npc"),
             "expression": event.get("expression", "neutral"), "voice": event.get("voice")}
            for event in events if event["type"] not in ("item_created", "survey_offered") and
            (event["type"] != "moved" or event["entity"] == engine.player)]
