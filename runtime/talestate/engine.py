"""One authority for positions, ownership, tasks, time and committed events."""

from copy import deepcopy
from .quest_feedback import reward_feedback
from .travel import TravelMap
from .story import (
    alternatives, change_honor, issue_receipt, matches, quest_items, record_entrusted_loss,
    survey_action, survey_record, use_actions, use_record, visit_actions,
)


class ActionError(ValueError):
    pass


class Engine:
    def __init__(self, definition):
        self.world = definition
        self.locations = {row["id"]: row for row in definition["locations"]}
        self.entities = {row["id"]: row for row in definition["entities"]}
        self.base_items = {row["id"]: row for row in definition["items"]}
        self.actions = {row["id"]: row for row in definition["actions"]}
        self.base_quests = {row["id"]: row for row in definition["quests"]}
        self.player = next(row["id"] for row in definition["entities"] if row["kind"] == "player")
        self.map = TravelMap(definition.get("map", {}))
        self.state = {
            "turn": 0, "minute": 0, "sequence": 0,
            "entities": {key: {"location": row["location"], "attributes": deepcopy(row.get("attributes", {}))}
                         for key, row in self.entities.items()},
            "owners": {key: row["owner"] for key, row in self.base_items.items() if row["owner"] is not None},
            "quests": {key: row.get("initial_status", "active") for key, row in self.base_quests.items()},
            "events": [], "encountered_npcs": [],
        }
        self._discover(self.state)
        attributes = self.state["entities"][self.player]["attributes"]
        for key, value in {"health": 100, "max_health": 100, "attack": 5, "defense": 5,
                           "mana": 50, "max_mana": 50, "magic_attack": 8, "honor": 0}.items():
            attributes.setdefault(key, value)
        from .combat import reset_enemies
        reset_enemies(self, self.state, 1)
        self.state["equipment"] = {}
        self.state["learned_spells"] = []

    @property
    def items(self):
        return {**self.base_items, **self.state.get("generated_items", {})}

    @property
    def quests(self):
        return {**self.base_quests, **self.state.get("generated_quests", {})}

    def name(self, entity):
        return self.entities.get(entity, self.items.get(entity, self.locations.get(entity, {}))).get("name", entity)

    def location_description(self, location, state=None):
        row = self.locations[location]
        for variant in row.get("descriptions", []):
            if all(self.condition(rule, state) for rule in variant.get("conditions", [])):
                return variant["text"]
        return row.get("description", "")

    def condition(self, rule, state=None):
        state = self.state if state is None else state
        op = rule["op"]
        if op == "at":
            return state["entities"][rule["entity"]]["location"] == rule["location"]
        if op == "owned_by":
            return state["owners"].get(rule["item"]) == rule["entity"]
        if op == "item_unissued":
            return rule["item"] not in state["owners"]
        if op == "quest_status":
            return state["quests"][rule["quest"]] == rule["value"]
        if op == "attribute":
            value = state["entities"][rule["entity"]]["attributes"].get(rule["attribute"], 0)
            target = rule["value"]
            comparison = rule.get("compare", "eq")
            return {"eq": value == target, "gte": value >= target, "lte": value <= target}[comparison]
        if op == "day":
            day = (self.world.get("clock", {}).get("start_minute", 480) + state["minute"]) // 1440 + 1
            return day >= rule["value"]
        if op == "evidence":
            return any(matches(event, rule["match"]) for event in state["events"])
        return False

    def clock(self):
        settings = self.world.get("clock", {})
        absolute = settings.get("start_minute", 480) + self.state["minute"]
        minute = absolute % 1440
        night_at = settings.get("night_minute", 1200)
        night = minute >= night_at or minute < settings.get("start_minute", 480)
        home = settings.get("home", "")
        at_home = self.state["entities"][self.player]["location"] == home
        return {"day": absolute // 1440 + 1, "label": f"{minute // 60:02d}:{minute % 60:02d}",
                "day_fraction": minute / 1440, "night": night, "home": home,
                "at_home": at_home,
                "return_minutes": self.map.minutes(self.map.distance(
                    self.state["entities"][self.player]["location"], home))
                    if home and self.map.routes else 0}

    def available_actions(self):
        from .combat import actions as combat_actions
        if self.state.get("combat"):
            return combat_actions(self)
        from .magic import lessons
        actions = [action for action in self.actions.values()
                if action["actor"] == self.player and
                all(self.condition(rule) for rule in action.get("preconditions", []))]
        actions.extend(visit_actions(self))
        present = self.present_npcs()
        location = self.state["entities"][self.player]["location"]
        for key, quest in self.quests.items():
            giver = quest.get("giver")
            if (self.state["quests"][key] == "failed" and giver in present
                    and not self.state.get("disclosed_failures", {}).get(key)):
                actions.append({"id": "disclose." + key, "name": "向" + self.name(giver) + "说明未能完成：" + quest["title"],
                                "intent": "disclose", "actor": self.player, "target": giver, "ai_interaction": True,
                                "duration_minutes": 5, "effects": [{"op": "disclose_failure", "quest": key}]})
        for npc_id in present:
            for course in lessons(self, npc_id):
                if not course["learned"]:
                    actions.append({"id": "learn." + npc_id + "." + course["id"], "name": "学习" + course["name"],
                        "intent": "learn", "actor": self.player, "target": npc_id, "ai_interaction": True,
                        "duration_minutes": 10})
        for key, quest in self.state.get("generated_quests", {}).items():
            status = self.state["quests"][key]
            if status not in ("available", "active"):
                continue
            if "use_at" in quest:
                actions.extend(use_actions(self, quest, status, location, present))
                continue
            if "survey" in quest:
                action = survey_action(self, quest, status, location, present)
                if action:
                    actions.append(action)
                continue
            item, giver, recipient = quest["complete_when"]["item"], quest["giver"], quest["recipient"]
            target = giver if status == "available" else recipient
            source, destination = (giver, self.player) if status == "available" else (self.player, recipient)
            if self.state["owners"].get(item) != source or target not in present:
                continue
            effects = [{"op": "transfer", "item": item, "from": source, "to": destination}]
            if status == "available":
                effects.append({"op": "activate_quest", "quest": key})
            actions.append({"id": key + (".accept" if status == "available" else ".deliver"),
                "name": quest["title"], "intent": "accept" if status == "available" else "deliver",
                "actor": self.player, "target": target, "ai_interaction": True,
                "item": item, "description": f"将{self.name(item)}交给{self.name(recipient)}。",
                "duration_minutes": 5, "effects": effects})
        clock = self.clock()
        for destination, distance in self.map.neighbors(location):
            if clock["night"] and self.world.get("clock"):
                if self.map.distance(destination, clock["home"]) >= self.map.distance(location, clock["home"]):
                    continue
            actions.append({"id": "travel." + destination, "name": "前往" + self.name(destination),
                            "intent": "move", "actor": self.player, "target": destination,
                            "distance_km": distance, "duration_minutes": self.map.minutes(distance),
                            "effects": [{"op": "move", "entity": self.player, "location": destination}]})
        if self.world.get("clock"):
            if clock["night"]:
                actions = [a for a in actions if a["intent"] == "move" and all(
                    self.map.distance(effect["location"], clock["home"]) < self.map.distance(location, clock["home"])
                    for effect in a.get("effects", []) if effect["op"] == "move")]
                if clock["at_home"]:
                    actions = [{"id": "sleep", "name": "睡到明早", "intent": "sleep", "actor": self.player,
                                "duration_minutes": 0, "effects": [{"op": "sleep"}]}]
            else:
                actions.append({"id": "wait", "name": "休息一小时", "intent": "wait", "actor": self.player,
                                "duration_minutes": 60, "effects": [{"op": "wait", "minutes": 60}]})
                if clock["at_home"]:
                    current = (self.world["clock"].get("start_minute", 480) + self.state["minute"]) % 1440
                    duration = self.world["clock"].get("night_minute", 1200) - current
                    actions.append({"id": "wait_evening", "name": "消磨时间到晚上", "intent": "wait", "actor": self.player,
                        "duration_minutes": duration, "effects": [{"op": "wait", "minutes": duration, "until_evening": True}]})
        return actions + combat_actions(self)

    def present_npcs(self):
        if self.state.get("combat"):
            return []
        if self.world.get("clock") and self.clock()["night"]:
            return []
        location = self.state["entities"][self.player]["location"]
        return [key for key, npc in self.entities.items() if npc["kind"] == "npc"
                and self.state["entities"][key]["location"] == location]

    def _event(self, state, events, kind, **data):
        state["sequence"] += 1
        event = {"event_id": "event." + str(state["sequence"]), "type": kind,
                 "turn": state["turn"] + 1, "minute": state["minute"], **data}
        events.append(event)
        return event

    def _effect(self, state, events, effect, action):
        op = effect["op"]
        if op == "move":
            entity = effect["entity"]
            old = state["entities"][entity]["location"]
            state["entities"][entity]["location"] = effect["location"]
            self._event(state, events, "moved", entity=entity, from_location=old,
                        to_location=effect["location"], action=action)
        elif op == "transfer":
            item, source, destination = effect["item"], effect["from"], effect["to"]
            if state["owners"].get(item) != source:
                raise ActionError("物品已不属于交付者。")
            if state["entities"][source]["location"] != state["entities"][destination]["location"]:
                raise ActionError("交付双方不在同一地点。")
            state["owners"][item] = destination
            # Evidence is derived here, never accepted from a model's claim.
            self._event(state, events, "delivered", item=item, action=action,
                        **{"from": source, "to": destination})
        elif op == "consume":
            from .magic import consume_effect
            consume_effect(self, state, events, effect["item"], effect["entity"])
            item = effect["item"]
            if effect["entity"] == self.player and any(
                state["quests"][key] == "active" and item in quest_items((quest,))
                and not any(matches(events[-1], rule) for rule in alternatives(quest["complete_when"]))
                for key, quest in self.quests.items()
            ):
                record_entrusted_loss(self, state, events, [item], events[-1])
        elif op == "add_attribute":
            attributes = state["entities"][effect["entity"]]["attributes"]
            key = effect["attribute"]
            value = attributes.get(key, 0) + effect["amount"]
            attributes[key] = value
            self._event(state, events, "attribute_changed", entity=effect["entity"],
                        attribute=key, amount=effect["amount"], value=value, action=action)
        elif op == "activate_quest":
            quest = effect["quest"]
            if state["quests"][quest] != "available":
                raise ActionError("这项委托已经接取。")
            state["quests"][quest] = "active"
            self._event(state, events, "quest_accepted", quest=quest, action=action)
        elif op == "investigate":
            entity, location = effect["entity"], effect["location"]
            if state["entities"][entity]["location"] != location:
                raise ActionError("须在现场调查。")
            proof = {"entity": entity, "location": location, "clue": effect["clue"], "action": action,
                     "title": effect.get("title", self.actions[action].get("name", effect["clue"]))}
            self._event(state, events, "clue_found", **proof)
        elif op == "npc_decision":
            npc = effect["npc"]
            if npc not in self.present_npcs():
                raise ActionError("该人物当前不在场。")
            self._event(state, events, "npc_decided", npc=npc, decision=effect["decision"], action=action)
        elif op == "issue_item":
            item, npc = effect["item"], effect["npc"]
            if item in state["owners"] or npc not in self.present_npcs():
                raise ActionError("这件物品已经签发，或签发人不在场。")
            state["owners"][item] = npc
            self._event(state, events, "item_created", entity=npc, recipient=self.player, item=item, action=action)
            self._effect(state, events, {"op": "transfer", "item": item, "from": npc, "to": self.player}, action)
        elif op in ("survey_check", "survey_report"):
            quest = self.quests[effect["quest"]]
            event_type = "survey_checked" if op == "survey_check" else "survey_reported"
            observation = (self.location_description(quest["survey"]["location"], state) if op == "survey_check"
                           else survey_record(self, quest["id"])["observation"])
            self._event(state, events, event_type, quest=quest["id"], npc=quest["giver"], action=action,
                        observation=observation, **quest["survey"])
        elif op in ("use_errand_item", "report_errand_use"):
            quest = self.quests[effect["quest"]]
            key, item = quest["id"], quest["item"]
            if op == "use_errand_item":
                location = quest["use_at"]["location"]
                if state["owners"].get(item) != self.player or state["entities"][self.player]["location"] != location:
                    raise ActionError("需要持有这件道具，并在委托指定的地点使用。")
                del state["owners"][item]
                self._event(state, events, "errand_item_used", quest=key, item=item, action=action,
                            location=location, purpose=quest["use_at"]["purpose"])
            else:
                evidence = use_record(self, key)
                self._event(state, events, "errand_reported", quest=key, npc=quest["giver"], action=action,
                            evidence_id=evidence["event_id"], purpose=evidence["purpose"])
        elif op == "disclose_failure":
            from .quest_feedback import feedback
            key = effect["quest"]
            quest = self.quests[key]
            giver = quest["giver"]
            state.setdefault("disclosed_failures", {})[key] = True
            self._event(state, events, "quest_failure_disclosed", quest=key, npc=giver, action=action)
            feedback(self, state, events, quest, giver, "breached")
        elif op == "attend_visit":
            key = effect["visit"]
            row = state["visits"][key]
            row["status"] = "attended"
            self._event(state, events, "appointment_attended", visit=key, npc=row["npc"],
                        location=row["location"], title=row["title"], action=action)
        elif op == "wait":
            self._event(state, events, "waited", action=action, minutes=effect.get("minutes", 60),
                        until_evening=effect.get("until_evening", False))
        elif op == "sleep":
            start = self.world["clock"].get("start_minute", 480)
            absolute = start + state["minute"]
            wake = absolute // 1440 * 1440 + start
            if wake <= absolute:
                wake += 1440
            state["minute"] = wake - start
            from .combat import reset_enemies
            reset_enemies(self, state, wake // 1440 + 1)
            attributes = state["entities"][self.player]["attributes"]
            attributes["health"] = attributes.get("max_health", 100)
            attributes["mana"] = attributes.get("max_mana", 50)
            self._event(state, events, "slept", day=wake // 1440 + 1,
                        wake_time=f"{start // 60:02d}:{start % 60:02d}", action=action)
        else:
            raise ActionError("未实现的效果：" + str(op))

    def execute(self, action_id, *, accept_npc_decision=False):
        if not action_id:
            return []
        if action_id.startswith("learn."):
            from .magic import learn
            _, teacher, spell_id = action_id.split(".", 2)
            return learn(self, teacher, spell_id)
        if action_id.startswith("combat."):
            from .combat import execute
            return execute(self, action_id)
        action = next((a for a in self.available_actions() if a["id"] == action_id), None)
        if action is None or action["actor"] != self.player:
            raise ActionError("当前没有这个玩家行动。")
        if not all(self.condition(rule) for rule in action.get("preconditions", [])):
            raise ActionError("当前地点、物品归属或任务进度不满足行动条件。")
        if not accept_npc_decision and any(e["op"] == "npc_decision" for e in action.get("effects", [])):
            raise ActionError("这个决定需要人物本人同意。")
        candidate = deepcopy(self.state)
        events = []
        candidate["minute"] += action.get("duration_minutes", 0)
        for effect in action.get("effects", []):
            self._effect(candidate, events, effect, action_id)
        self._complete_quests(candidate, events)
        if not events:
            return []
        candidate["turn"] += 1
        candidate["events"].extend(events)
        self._discover(candidate)
        self.state = candidate
        return events

    def _complete_quests(self, candidate, events):
        evidence = list(events)
        for key, quest in self.quests.items():
            if candidate["quests"][key] != "active":
                continue
            proof = next((event for event in evidence if any(matches(event, rule)
                         for rule in alternatives(quest["complete_when"]))), None)
            if proof is None:
                continue
            candidate["quests"][key] = "completed"
            closing = {"closing": quest["closing"]} if quest.get("closing") else {}
            self._event(candidate, events, "quest_completed", quest=key, evidence_id=proof["event_id"], **closing)
            if key in candidate.get("generated_quests", {}) or (
                proof["type"] == "delivered" and proof["from"] == self.player and quest.get("giver")
            ):
                change_honor(self, candidate, events, 1, proof)
            reward_feedback(self, candidate, events, quest, proof)
            for reward in quest.get("rewards", []):
                self._effect(candidate, events, reward, proof.get("action", "exchange"))
            if quest.get("return_receipt"):
                issue_receipt(self, candidate, events, quest)

    def say_generated(self, npc_id, text, expression="neutral", advance=True):
        if npc_id not in self.entities or self.entities[npc_id]["kind"] != "npc" or self.state["entities"][npc_id]["location"] != self.state["entities"][self.player]["location"]:
            raise ActionError("现在无法与这个人物交谈。")
        candidate, events = deepcopy(self.state), []
        if advance:
            candidate["minute"] += self.world.get("clock", {}).get("conversation_minutes", 5)
            candidate["turn"] += 1
        self._event(candidate, events, "dialogue", npc=npc_id, text=text,
                    expression=expression, generated=True)
        # _event normally labels events before the turn counter is committed.
        events[-1]["turn"] = candidate["turn"]
        candidate["events"].extend(events)
        self.state = candidate
        return events

    def _discover(self, state):
        location = state["entities"][self.player]["location"]
        for key, entity in self.entities.items():
            if entity["kind"] == "npc" and state["entities"][key]["location"] == location:
                if key not in state["encountered_npcs"]:
                    state["encountered_npcs"].append(key)
