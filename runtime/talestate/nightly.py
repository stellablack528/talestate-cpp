"""Personal plan settlement and isolated, small daily memory summaries."""

from .provider import ModelError


def dialogue_batches(rows):
    batches, batch, size = [], [], 0
    for row in rows:
        length = len(row["player"]) + len(row["npc"])
        if batch and (len(batch) == 24 or size + length > 12000):
            batches.append(batch)
            batch, size = [], 0
        batch.append(row)
        size += length
    if batch:
        batches.append(batch)
    return batches


def summarize_day(provider, engine, memory, npc_id, day, dialogues, batch=0):
    response = provider.json_reply(
        '整理一个NPC自己当天的对话，返回JSON，示例：'
        '{"facts":[{"who":"旅人","when":"第1天下午","what":"自述名叫罗文","source":"玩家自述"}]}。'
        '只提炼最重要的人物、时间、事件、约定、关系变化。普通闲聊、问候、逐句原文全部省略。'
        '没有重要信息返回 {"facts":[]}。只读给定NPC自己的记录，不推测其他人物私聊。'
        '区分自述、承诺和实际完成，谈话不能证明物品交付或任务完成。'
        '最多8条精简事实，不重复已有长期笔记，不修改游戏状态，不添加其他字段。',
        {"phase": "nightly", "npc_id": npc_id, "day": day,
         "npc": memory.context(npc_id), "dialogues": dialogues})
    facts = response.get("facts")
    if not isinstance(facts, list) or any(not isinstance(fact, dict) or
            not all(isinstance(fact.get(key), str) for key in ("who", "when", "what", "source"))
            for fact in facts):
        raise ModelError("夜间记忆整理 JSON 缺少 facts 数组或 who/when/what/source 字符串；未写入摘要。")
    memory.remember(npc_id, {"facts": facts[:8]}, f"summary.{day}.{batch}", day)


def settle_npcs(engine, memory, previous_day):
    for npc_id, record in memory.records.items():
        daily = record.setdefault("daily_state", {})
        if daily.get("last_settled_day", 0) >= previous_day:
            continue
        completed = []
        for plan in record.get("plans", []):
            if plan["status"] != "planned" or plan["requires_player"] or plan.get("due_day", plan["day"]) > previous_day:
                continue
            plan.update({"status": "completed", "completed_day": previous_day, "result": plan["text"] + "（已完成）"})
            completed.append(plan)
        if completed:
            summary = "已办妥：" + "；".join(p["text"] for p in completed)
            daily["activity"] = summary + "。眼下正歇息片刻。"
            record.setdefault("daily_log", []).append({"day": previous_day, "summary": summary,
                "completed_plans": [p["id"] for p in completed],
                "source_event_ids": [p["source_event_id"] for p in completed]})
            memory.remember(npc_id, {"facts": [{"who": engine.name(npc_id), "when": "第" + str(previous_day) + "天",
                "what": summary, "source": "daily_progress"}]}, "night." + str(previous_day), previous_day)
        if daily or completed:
            daily.update({"day": engine.clock()["day"], "last_settled_day": previous_day})
