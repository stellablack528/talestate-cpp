"""A small BYOK Chat Completions adapter. No credentials in snapshots."""

import json
from copy import deepcopy
from pathlib import Path
from time import monotonic
from uuid import uuid4
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


class ModelError(ValueError):
    pass


class Provider:
    def __init__(self, base_url="", api_key="", model="", timeout=40, diagnostics_dir=None):
        self.base_url = base_url.rstrip("/")
        self.api_key = api_key
        self.model = model
        self.timeout = timeout
        self.diagnostics_dir = Path(diagnostics_dir) if diagnostics_dir else None
        self.usage = {"input_tokens": 0, "output_tokens": 0}
        self.phase_usage = {}

    @property
    def configured(self):
        return bool(self.base_url and self.model and (self.api_key or "api.deepseek.com" not in self.base_url))

    def json_reply(self, instructions, context):
        payload = deepcopy(context)
        history = payload.pop("conversation_history", [])
        for name in ("npc", "npc_context"):
            if history and isinstance(payload.get(name), dict):
                payload[name].pop("conversations", None)
        messages = [{"role": "system", "content": instructions}]
        for row in history:
            messages.append({"role": "user", "content": row["player"] or "（人物主动开场）"})
            messages.append({"role": "assistant", "content": json.dumps({"text": row["npc"]}, ensure_ascii=False)})
        messages.append({"role": "user", "content": json.dumps(payload, ensure_ascii=False)})
        body = {"model": self.model, "messages": messages, "max_tokens": 4096, "temperature": 0.5,
                "response_format": {"type": "json_object"}}
        phase = context.get("phase", "interaction")
        metrics = self.phase_usage.setdefault(phase, {"requests": 0, "failures": 0, "seconds": 0.0})
        for attempt in range(2):
            metrics["requests"] += 1
            started = monotonic()
            try:
                return self._request(body, context)
            except ModelError as error:
                metrics["failures"] += 1
                if attempt or getattr(error, "reason", "") not in ("length", "empty_content"):
                    raise
                if error.reason == "length":
                    body["max_tokens"] = 8192
                else:
                    body["messages"] = messages + [{"role": "user", "content":
                        'Return one complete JSON object matching the requested example. Do not return whitespace or Markdown.'}]
            finally:
                metrics["seconds"] += monotonic() - started

    def _failure(self, reason, message, body, context, response):
        path = None
        if self.diagnostics_dir:
            self.diagnostics_dir.mkdir(parents=True, exist_ok=True)
            path = self.diagnostics_dir / (uuid4().hex + ".json")
            # No credentials, request headers or full player/NPC input are recorded.
            report = {"reason": reason, "model": self.model, "max_tokens": body["max_tokens"],
                      "phase": context.get("phase", "interaction"), "npc_id": context.get("npc_id"),
                      "response": response}
            path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
        error = ModelError(message + (" 诊断文件：" + str(path) if path else ""))
        error.reason = reason
        return error

    def _request(self, body, context):
        url = self.base_url if self.base_url.endswith("/chat/completions") else self.base_url + "/chat/completions"
        headers = {"Content-Type": "application/json"}
        if self.api_key:
            headers["Authorization"] = "Bearer " + self.api_key
        request = Request(url, json.dumps(body).encode("utf-8"), headers, method="POST")
        try:
            with urlopen(request, timeout=self.timeout) as response:
                raw = response.read().decode("utf-8")
        except HTTPError as error:
            raise ModelError(f"模型服务返回 HTTP {error.code}，请检查地址、Key 和模型名。") from None
        except (URLError, TimeoutError, OSError):
            raise ModelError("模型连接失败或超时，请检查设置。") from None
        except UnicodeError:
            raise ModelError("模型服务响应不是 UTF-8 文本。") from None
        try:
            data = json.loads(raw)
        except ValueError:
            raise self._failure("invalid_envelope", "模型服务的 HTTP 响应体不是 JSON。", body, context, {"body": raw}) from None
        try:
            usage = data.get("usage", {})
            self.usage["input_tokens"] += usage.get("prompt_tokens", 0)
            self.usage["output_tokens"] += usage.get("completion_tokens", 0)
            choice = data["choices"][0]
            if choice.get("finish_reason") == "length":
                raise self._failure("length", "模型输出达到 token 上限，JSON 未完整生成。", body, context, data)
            text = choice["message"]["content"]
            if not isinstance(text, str) or not text.strip():
                raise self._failure("empty_content", "模型返回空白 content，没有生成 JSON。", body, context, data)
            result, _ = json.JSONDecoder().raw_decode(text[text.index("{"):])
            if not isinstance(result, dict):
                raise ValueError("Expected object")
            return result
        except ModelError:
            raise
        except json.JSONDecodeError as error:
            raise self._failure("invalid_content", f"模型 content 中的 JSON 语法错误：第 {error.lineno} 行第 {error.colno} 列，{error.msg}。", body, context, data) from None
        except (KeyError, IndexError, TypeError, AttributeError):
            raise self._failure("invalid_schema", "模型服务响应缺少有效 choices/message/content 字段。", body, context, data) from None
        except ValueError:
            raise self._failure("no_object", "模型 content 没有返回 JSON 对象。", body, context, data) from None
