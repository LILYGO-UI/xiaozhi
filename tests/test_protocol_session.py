"""Exercise the real session helper against a local WebSocket peer and SDL dummy audio."""

import base64
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest


HELPER = str(Path(sys.argv.pop(1)).resolve())


def read_exact(connection, size):
    data = bytearray()
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise EOFError("helper closed the WebSocket")
        data.extend(chunk)
    return bytes(data)


def read_frame(connection):
    header = read_exact(connection, 2)
    length = header[1] & 0x7F
    if length == 126:
        length = struct.unpack("!H", read_exact(connection, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", read_exact(connection, 8))[0]
    if length > 1024 * 1024:
        raise AssertionError("unexpectedly large client frame")
    mask = read_exact(connection, 4) if header[1] & 0x80 else None
    payload = read_exact(connection, length)
    if mask is not None:
        payload = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
    return header[0] & 0x0F, payload


def send_frame(connection, opcode, payload):
    if isinstance(payload, str):
        payload = payload.encode()
    if len(payload) < 126:
        header = bytes((0x80 | opcode, len(payload)))
    else:
        header = bytes((0x80 | opcode, 126)) + struct.pack("!H", len(payload))
    connection.sendall(header + payload)


class ProtocolSessionTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="xiaozhi-protocol-")
        self.addCleanup(self.temporary.cleanup)
        self.control = str(Path(self.temporary.name) / "control")
        self.server = socket.socket()
        self.addCleanup(self.server.close)
        self.server.bind(("127.0.0.1", 0))
        self.server.listen(1)
        self.server.settimeout(5)

    def start_session(self, audio_driver="dummy"):
        environment = dict(os.environ, SDL_AUDIODRIVER=audio_driver)
        environment["XIAOZHI_TOKEN"] = "local-test-token"
        port = self.server.getsockname()[1]
        self.process = subprocess.Popen(
            [
                HELPER, "session", "--url", f"ws://127.0.0.1:{port}/",
                "--control", self.control, "--timeout-ms", "1000", "--seconds", "3",
                "--capture", "default", "--playback", "default",
                "--device-id", "02:00:00:00:00:01", "--client-id", "local-session-test",
            ],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self.addCleanup(self.stop_process)
        self.connection, _ = self.server.accept()
        self.addCleanup(self.connection.close)
        self.connection.settimeout(5)
        request = bytearray()
        while not request.endswith(b"\r\n\r\n"):
            request.extend(read_exact(self.connection, 1))
            self.assertLess(len(request), 8192)
        headers = {}
        for line in bytes(request).split(b"\r\n")[1:]:
            if b":" in line:
                name, value = line.split(b":", 1)
                headers[name.lower()] = value.strip()
        accept = base64.b64encode(hashlib.sha1(
            headers[b"sec-websocket-key"] + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
        ).digest())
        self.connection.sendall(
            b"HTTP/1.1 101 Switching Protocols\r\n"
            b"Upgrade: websocket\r\nConnection: Upgrade\r\n"
            b"Sec-WebSocket-Accept: " + accept + b"\r\n\r\n"
        )
        opcode, payload = read_frame(self.connection)
        self.assertEqual(opcode, 1)
        self.assertEqual(json.loads(payload)["type"], "hello")
        send_frame(self.connection, 1, json.dumps({
            "type": "hello", "session_id": "local-session",
            "audio_params": {"sample_rate": 24000},
        }))

    def stop_process(self):
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.communicate(timeout=3)

    def command(self, command):
        descriptor = os.open(self.control, os.O_WRONLY | os.O_NONBLOCK)
        try:
            os.write(descriptor, (command + "\n").encode())
        finally:
            os.close(descriptor)

    def finish(self, expected_code):
        _, output = self.process.communicate(timeout=5)
        self.assertEqual(self.process.returncode, expected_code, output)
        return output

    def test_idle_ping_receives_pong(self):
        self.start_session()
        send_frame(self.connection, 9, b"still-connected")
        self.assertEqual(read_frame(self.connection), (10, b"still-connected"))
        self.command("QUIT")
        self.finish(0)

    def test_idle_close_exits_for_controller_reconnect(self):
        self.start_session()
        send_frame(self.connection, 8, struct.pack("!H", 1000))
        self.assertIn("idle session server close", self.finish(1))

    def test_idle_eof_exits_for_controller_reconnect(self):
        self.start_session()
        self.connection.shutdown(socket.SHUT_RDWR)
        self.connection.close()
        self.assertIn("idle session connection ended", self.finish(1))

    def test_capture_failure_exits_for_controller_reconnect(self):
        self.start_session(audio_driver="xiaozhi-nonexistent-audio-driver")
        self.command("PTT_BEGIN")
        output = self.finish(1)
        self.assertIn("SDL audio init:", output)
        self.assertIn("ptt turn done rc=1", output)

    def send_manual_turn(self):
        self.command("PTT_BEGIN")
        opcode, payload = read_frame(self.connection)
        self.assertEqual(opcode, 1)
        self.assertEqual(json.loads(payload)["state"], "start")
        opcode, payload = read_frame(self.connection)
        self.assertEqual(opcode, 2)
        self.assertGreater(len(payload), 0)
        audio_frame = payload
        self.command("PTT_END")
        for _ in range(100):
            opcode, payload = read_frame(self.connection)
            if opcode == 1:
                self.assertEqual(json.loads(payload)["state"], "stop")
                break
            self.assertEqual(opcode, 2)
        else:
            self.fail("helper did not stop recording")
        return audio_frame

    def assert_abort_exits_promptly(self, command="ABORT"):
        started = time.monotonic()
        self.command(command)
        for _ in range(100):
            opcode, payload = read_frame(self.connection)
            if opcode == 1 and json.loads(payload).get("type") == "abort":
                self.assertEqual(json.loads(payload)["session_id"], "local-session")
                break
        else:
            self.fail("helper did not send protocol abort")
        output = self.finish(0)
        self.assertLess(time.monotonic() - started, 2, output)
        self.assertIn("turn_aborted", output)
        self.assertNotIn("response_done", output)

    def test_abort_during_recording_stops_upload_and_exits(self):
        self.start_session()
        self.command("PTT_BEGIN")
        self.assertEqual(json.loads(read_frame(self.connection)[1])["state"], "start")
        self.assertEqual(read_frame(self.connection)[0], 2)
        self.assert_abort_exits_promptly()

    def test_abort_while_waiting_for_silent_server(self):
        self.start_session()
        self.send_manual_turn()
        self.assert_abort_exits_promptly()

    def test_abort_while_receiving_partial_frame(self):
        self.start_session()
        self.send_manual_turn()
        self.connection.sendall(b"\x81\x64{\"")  # Peer stalls mid-frame.
        time.sleep(0.15)
        self.assert_abort_exits_promptly()

    def test_quit_while_waiting_for_response_is_prompt(self):
        self.start_session()
        self.send_manual_turn()
        self.assert_abort_exits_promptly("QUIT")

    def test_press_during_response_aborts_instead_of_disappearing(self):
        self.start_session()
        self.send_manual_turn()
        self.assert_abort_exits_promptly("PTT_BEGIN")

    def test_abort_during_audio_playback_discards_pending_audio(self):
        self.start_session()
        packet = self.send_manual_turn()
        send_frame(self.connection, 1, '{"type":"tts","state":"start"}')
        for _ in range(10):
            send_frame(self.connection, 2, packet)
        self.assert_abort_exits_promptly()

    def test_response_audio_drains_before_session_becomes_idle(self):
        self.start_session()
        packet = self.send_manual_turn()
        send_frame(self.connection, 1, '{"type":"tts","state":"start"}')
        started = time.monotonic()
        for _ in range(6):
            send_frame(self.connection, 2, packet)
        send_frame(self.connection, 1, '{"type":"tts","state":"stop"}')
        send_frame(self.connection, 9, b"audio-finished")
        self.assertEqual(read_frame(self.connection), (10, b"audio-finished"))
        self.assertGreater(time.monotonic() - started, 0.15)
        self.command("QUIT")
        output = self.finish(0)
        self.assertEqual(output.count("EVENT response_done"), 1)

    def test_response_eof_exits_for_controller_reconnect(self):
        self.start_session()
        self.send_manual_turn()
        self.connection.shutdown(socket.SHUT_RDWR)
        self.connection.close()
        output = self.finish(1)
        self.assertIn("request_sent", output)
        self.assertIn("read frame ended", output)
        self.assertIn("ptt turn done rc=1", output)
        self.assertNotIn("ptt turn done rc=0", output)

    def test_manual_turn_sends_audio_and_completes(self):
        self.start_session()
        self.send_manual_turn()
        send_frame(self.connection, 1, '{"type":"stt","text":"你好，小智"}')
        send_frame(self.connection, 1, '{"type":"llm","emotion":"happy"}')
        send_frame(self.connection, 1, '{"type":"tts","state":"start"}')
        send_frame(self.connection, 1, '{"type":"tts","state":"sentence_start","text":"你好，很高兴见到你。"}')
        send_frame(self.connection, 1, '{"type":"tts","state":"stop"}')
        send_frame(self.connection, 9, b"turn-complete")
        self.assertEqual(read_frame(self.connection), (10, b"turn-complete"))
        self.command("QUIT")
        output = self.finish(0)
        self.assertIn("response_done", output)
        self.assertIn("ptt turn done rc=0", output)
        self.assertRegex(output, r"record sent_frames=[1-9][0-9]*")
        self.assertIn("CHAT  user: 你好，小智", output)
        self.assertIn("CHAT  assistant: 你好，很高兴见到你。", output)
        self.assertIn("EMOTION happy", output)

    def test_json_escaped_subtitles_remain_single_utf8_log_lines(self):
        self.start_session()
        self.send_manual_turn()
        question = '你好，小智 😀\nEVENT capture_error stage=open'
        reply = '你好，路径 C:\\tmp，"开始" 😊'
        send_frame(self.connection, 1, json.dumps({"type": "stt", "text": question}))
        send_frame(self.connection, 1, '{"type":"llm","emotion":"\\u0068appy"}')
        send_frame(self.connection, 1, json.dumps({
            "type": "tts", "state": "sentence_start", "text": reply,
        }))
        # The 512-byte subtitle buffer must stop before a partial character,
        # whether the server uses raw UTF-8 or JSON Unicode escapes.
        long_text = "智" * 170 + "😀尾"
        for escaped in (False, True):
            send_frame(self.connection, 1, json.dumps({"type": "stt", "text": long_text}, ensure_ascii=escaped))
        send_frame(self.connection, 1, '{"type":"tts","state":"stop"}')
        send_frame(self.connection, 9, b"unicode-complete")
        self.assertEqual(read_frame(self.connection), (10, b"unicode-complete"))
        self.command("QUIT")
        output = self.finish(0)
        user_lines = [line.split("CHAT  user: ", 1)[1] for line in output.splitlines() if "CHAT  user: " in line]
        self.assertEqual(user_lines, [question.replace("\n", " "), "智" * 170, "智" * 170])
        self.assertIn("CHAT  assistant: " + reply, output)
        self.assertIn("EMOTION happy", output)
        self.assertFalse(any(line.startswith("EVENT capture_error") for line in output.splitlines()))

    def test_invalid_json_strings_do_not_publish_partial_subtitles(self):
        self.start_session()
        self.send_manual_turn()
        for value in ('"\\ud800"', '"\\udc00"', '"\\u12xz"', '"\\q"', '"hello\\u0000world"'):
            send_frame(self.connection, 1, '{"type":"stt","text":' + value + '}')
        send_frame(self.connection, 1, '{"type":"tts","state":"stop"}')
        send_frame(self.connection, 9, b"invalid-complete")
        self.assertEqual(read_frame(self.connection), (10, b"invalid-complete"))
        self.command("QUIT")
        _, output = self.process.communicate(timeout=5)
        self.assertEqual(self.process.returncode, 0)
        self.assertNotIn("CHAT  user:", output)


if __name__ == "__main__":
    unittest.main()
