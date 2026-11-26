"""Reproduce JSON failures through the actual HTTP adapter."""

import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import tempfile
import threading
import unittest

from talestate.provider import ModelError, Provider


class JSONFailures(unittest.TestCase):
    def test_real_wire_failures_are_identified_and_retries_are_bounded(self):
        replies, requests = [], []

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                requests.append(json.loads(self.rfile.read(int(self.headers["Content-Length"]))))
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps(replies.pop(0)).encode())

            def log_message(self, *_):
                pass

        def response(content, finish="stop"):
            return {"choices": [{"finish_reason": finish, "message": {"content": content}}],
                    "usage": {"prompt_tokens": 10, "completion_tokens": 20}}

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            with tempfile.TemporaryDirectory() as directory:
                provider = Provider(f"http://127.0.0.1:{server.server_port}/v1", "secret-header-key", "model",
                                    diagnostics_dir=directory)
                context = {"phase": "nightly", "npc_id": "baker"}
                replies.extend([response('{"facts": [', "length"), response('{"facts": []}')])
                self.assertEqual(provider.json_reply("Return JSON", context), {"facts": []})
                self.assertEqual([r["max_tokens"] for r in requests], [4096, 8192])
                replies.extend([response("   "), response('{"facts": []}')])
                self.assertEqual(provider.json_reply("Return JSON", context), {"facts": []})
                for payload, reason in [(response('{"facts": [}'), "invalid_content"),
                                        ({"choices": []}, "invalid_schema"),
                                        (response("not json"), "no_object")]:
                    replies.append(payload)
                    with self.assertRaises(ModelError) as caught:
                        provider.json_reply("Return JSON", context)
                    self.assertEqual(caught.exception.reason, reason)
                before = len(requests)
                replies.extend([response(" "), response(" ")])
                with self.assertRaises(ModelError) as caught:
                    provider.json_reply("Return JSON", context)
                self.assertEqual(caught.exception.reason, "empty_content")
                self.assertEqual(len(requests) - before, 2)
                logs = [path.read_text(encoding="utf-8") for path in Path(directory).glob("*.json")]
                self.assertTrue(all("secret-header-key" not in log for log in logs))
                reasons = {json.loads(log)["reason"] for log in logs}
                self.assertTrue({"length", "empty_content", "invalid_content", "invalid_schema", "no_object"} <= reasons)
        finally:
            server.shutdown()
            server.server_close()
