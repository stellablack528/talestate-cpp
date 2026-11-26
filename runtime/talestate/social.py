"""Night director chooses meetings; participants retain independent knowledge."""

from copy import deepcopy
from .exchange import reserved_items
from .provider import ModelError
from .memory import select_notes
from .story import create_delivery_item, create_errand, schedule_visit

TOPICS = {"daily": "收工后的闲谈", "supplies": "生意和日常物资", "help": "互相照应",
          "tomorrow": "明天的安排", "news": "近日见闻"}


def _errand(engine, memory, giver, request, day, events):
    if not request:
        return
    if not isinstance(request, dict):
        raise ModelError("夜间新委托必须是对象或null。")
    item, recipient, title = request.get("item"), request.get("recipient"), request.get("title")
    if (not isinstance(recipient, str) or
            recipient not in memory.records or recipient == giver or not isinstance(title, str) or not title.strip()):
        raise ModelError("夜间新委托需要有效收件人和标题。")
    letter = request.get("letter")
    if letter is not None:
        if (item is not None or not isinstance(letter, dict) or
                any(not isinstance(letter.get(k), str) or not letter[k].strip() or len(letter[k]) > size
                    for k, size in (("name", 36), ("text", 500)))):
            raise ModelError("新信件需要简短的名称和正文，且不能同时指定已有物品。")
        item = create_delivery_item(engine, engine.state, events, giver, recipient,
                                    letter["name"].strip(), letter["text"].strip())
    elif not isinstance(item, str) or engine.state["owners"].get(item) != giver or item in reserved_items(engine):
        raise ModelError("夜间委托需要本人持有且未被占用的物品。")
    key = f"daily.{day}.{giver}.{item}"
    create_errand(engine.state, key, giver, recipient, engine.player, item, title.strip()[:80],
                  day, receipt=request.get("return_receipt") is True,
                  expires_day=day + 3 if letter is not None else None)


def evolve_day(request_reply, engine, memory, day, overnight_minutes=720):
    """A meeting is real; sharing a statement never proves its contents true."""
    if engine.state.get("social_days", {}).get(str(day), {}).get("status") == "completed":
        return []
    groups = {}
    active_locations = set()
    start = engine.world.get("clock", {}).get("start_minute", 480)
    for key, record in memory.records.items():
        location = engine.state["entities"][key]["location"]
        groups.setdefault(location, []).append(key)
        if (record.get("daily_state", {}).get("day") == day or
                any(row.get("day") == day for row in record.get("notes", [])) or
                any((start + event["minute"]) // 1440 + 1 == day for event in record["observations"][-12:])):
            active_locations.add(location)
    hubs = sorted((loc for loc, group in groups.items() if len(group) >= 2),
                  key=lambda loc: (loc not in active_locations, -len(groups[loc])))
    people = []
    for location in hubs[:2]:
        for key in groups[location][:8]:
            record = memory.records[key]
            people.append({"id": key, "name": engine.name(key), "location": location,
                           "activity": record.get("daily_state", {}).get("activity", "")[:400],
                           "experiences": select_notes([note for note in record.get("notes", []) if note.get("day") == day], limit=4),
                           "plans": memory.context(key)["plans"][:4]})
    opportunities = {row["id"]: row for row in engine.world.get("night_opportunities", [])
                     if row["id"] not in engine.state.get("visits", {})
                     and all(engine.condition(rule) for rule in row.get("preconditions", []))
                     and engine.map.minutes(engine.map.distance(engine.state["entities"][row["npc"]]["location"],
                                                                 row["location"])) <= overnight_minutes
                     and not any(visit["npc"] == row["npc"] and visit["status"] in ("open", "attended")
                                 for visit in engine.state.get("visits", {}).values())}
    result = request_reply(
        '你是中世纪小镇的日常生活统筹。返回JSON示例：'
        '{"meetings":[{"participants":["baker","villager"],"topic":"supplies"}]}。'
        '根据各人当前动作选择零到两次自然的日常见面，可以闲聊、互相照应、商量明天。'
        '每次恰好两名在同一地点的人，每人每晚最多参加一次；没有合理缘由就返回空meetings。'
        'topic只能为daily/supplies/help/tomorrow/news，不得把私密事实编码进话题。'
        'experiences是你统筹时可读的个人经历，不代表其他人物已经知道；具体透露哪些事实由本人独立决定。'
        '从opportunities中最多选择一个可发生的次日约见，输出visit为该ID或null。不要编造新地点。'
        '不必每天制造任务，不发奖励。具体交流由参与者自己决定。',
        {"phase": "night_director", "day": day, "people": people,
         "opportunities": [{k: row[k] for k in ("id", "npc", "location", "title")} for row in opportunities.values()]})
    meetings = result.get("meetings")
    if not isinstance(meetings, list) or len(meetings) > 2:
        raise ModelError("夜间统筹需要至多两次 meetings。")
    visit = result.get("visit")
    if visit is not None and (not isinstance(visit, str) or visit not in opportunities):
        raise ModelError("夜间统筹选择了当前不可用的约见。")
    candidate, records = deepcopy(engine), deepcopy(memory)
    public_events = []
    used = set()
    for meeting in meetings:
        if not isinstance(meeting, dict):
            raise ModelError("夜间见面需要人物和话题对象。")
        actors, topic_key = meeting.get("participants"), meeting.get("topic")
        topic = TOPICS.get(topic_key) if isinstance(topic_key, str) else None
        if (not isinstance(actors, list) or len(actors) != 2 or
                any(not isinstance(key, str) or key not in {row["id"] for row in people} for key in actors) or
                len(set(actors)) != 2 or used.intersection(actors) or not isinstance(topic, str) or not topic.strip()):
            raise ModelError("夜间见面的人物或话题无效。")
        location = candidate.state["entities"][actors[0]]["location"]
        if candidate.state["entities"][actors[1]]["location"] != location:
            raise ModelError("夜间见面的人物不在同一地点。")
        used.update(actors)
        shared, replies = {}, {}
        for actor in actors:
            partner = next(key for key in actors if key != actor)
            context = records.context(actor, topic + " " + candidate.name(partner))
            facts = {"fact." + str(i): fact for i, fact in enumerate(context.pop("notes"))}
            own_items = [{"id": key, "name": candidate.name(key)} for key, owner in candidate.state["owners"].items()
                         if owner == actor and key not in reserved_items(candidate)]
            reply = request_reply(
                '你只扮演此NPC，正在与指定人物短暂交流。返回JSON示例：'
                '{"activity":"整理好摊位，准备明天开门","share_facts":[],"request":null}。'
                '只能知道自己的经历和对方本次明确告诉你的heard内容，不知玩家与别人私聊。'
                'share_facts可填写自己facts中你愿意主动透露的ID，最多三条，不必分享私事。'
                'activity写本次日常之后的近况，不编造奖励、交付、其他人承诺或远处事件。'
                '若自然地想请玩家帮忙转交自己持有的空闲物品，可写request：'
                '{"item":"own_items里的ID","recipient":"某已知NPC的ID","title":"自然的委托标题"}。'
                '也可用letter:{name:简短标题,text:不超过500字的内容}替代item，请玩家送自己写的信。'
                '需要正式回执可设return_receipt:true。'
                '普通日常优先，不强制生成委托，不默认玩家答应。不返回脚本或修改别人的设定。',
                {"phase": "night_participant", "day": day, "npc_id": actor, "npc": context,
                 "partner": {"id": partner, "name": candidate.name(partner)}, "topic": topic,
                 "facts": facts, "heard": shared.get(partner, []), "own_items": own_items,
                 "known_people": [{"id": key, "name": candidate.name(key)} for key in records.records
                                  if key == partner or candidate.name(key) in records.records[actor]["profile"]]})
            ids = reply.get("share_facts", [])
            if not isinstance(reply.get("activity"), str) or not reply["activity"].strip():
                raise ModelError(f"夜间角色 {actor} 的 activity 缺失或无效。")
            if (not isinstance(ids, list) or len(ids) > 3 or
                    any(not isinstance(key, str) or key not in facts for key in ids)):
                raise ModelError(f"夜间角色 {actor} 的分享事实 ID 无效。")
            shared[actor] = [facts[key] for key in ids]
            replies[actor] = reply
        events = []
        candidate._event(candidate.state, events, "npc_encounter", participants=actors, location=location,
                         topic=topic.strip(), day=day)
        candidate.state["events"].extend(events)
        event = events[0]
        for actor in actors:
            partner = next(key for key in actors if key != actor)
            records.records[actor]["observations"].append(deepcopy(event))
            facts = [{"who": candidate.name(partner), "when": f"第{day}天收工时",
                      "what": "主动告知：" + fact["text"], "source": "NPC自述，非独立证实"}
                     for fact in shared[partner]]
            records.remember(actor, {"facts": facts, "activity": replies[actor]["activity"]},
                             event["event_id"], day + 1)
            records.records[actor].setdefault("daily_log", []).append({"day": day,
                "summary": "与" + candidate.name(partner) + "见面，" + topic, "source_event_ids": [event["event_id"]]})
            additions = []
            _errand(candidate, records, actor, replies[actor].get("request"), day, additions)
            candidate.state["events"].extend(additions)
            records.observe(candidate, additions, "")
    if visit is not None:
        public_events.extend(schedule_visit(candidate, opportunities[visit], day + 1))
    candidate.state.setdefault("social_days", {})[str(day)] = {"status": "completed"}
    engine.state, memory.records = candidate.state, records.records
    return public_events
