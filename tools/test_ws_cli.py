from __future__ import annotations

import struct
import unittest
import zlib

import ws_cli


class WsCliTest(unittest.TestCase):
    def test_small_client_frame_is_masked(self) -> None:
        frame = ws_cli.encode_client_frame(1, b"hello", b"\x01\x02\x03\x04")
        self.assertEqual(frame[:6], b"\x81\x85\x01\x02\x03\x04")
        self.assertEqual(
            bytes(value ^ b"\x01\x02\x03\x04"[i & 3] for i, value in enumerate(frame[6:])),
            b"hello",
        )

    def test_extended_client_frame_length(self) -> None:
        payload = bytes(range(126))
        frame = ws_cli.encode_client_frame(2, payload, b"mask")
        self.assertEqual(frame[:4], b"\x82\xfe\x00\x7e")
        self.assertEqual(len(frame), 4 + 4 + 126)

    def test_png_encoder_preserves_rgb_rows(self) -> None:
        rgb = bytes((255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255))
        png = ws_cli.png_bytes(2, 2, rgb)
        self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))
        pos = 8
        compressed = bytearray()
        while pos < len(png):
            size = struct.unpack(">I", png[pos : pos + 4])[0]
            kind = png[pos + 4 : pos + 8]
            payload = png[pos + 8 : pos + 8 + size]
            if kind == b"IDAT":
                compressed.extend(payload)
            pos += 12 + size
        self.assertEqual(zlib.decompress(compressed), b"\0" + rgb[:6] + b"\0" + rgb[6:])

    def test_screenshot_header_validation(self) -> None:
        with self.assertRaises(ws_cli.WsError):
            ws_cli.save_screenshot(b"not a screenshot", self.id())

    def test_battle_command_defaults_to_manual_control(self) -> None:
        args = ws_cli.build_parser().parse_args(["battle", "7"])
        self.assertEqual(args.team, 7)
        self.assertFalse(args.auto)
        self.assertIsNone(args.battlefield)

    def test_battle_command_accepts_background_resource(self) -> None:
        args = ws_cli.build_parser().parse_args(
            ["battle", "7", "--battlefield", "2"]
        )
        self.assertEqual(args.battlefield, 2)

    def test_load_command_accepts_save_slots(self) -> None:
        args = ws_cli.build_parser().parse_args(["load", "3"])
        self.assertEqual(args.slot, 3)

    def test_script_command_defaults_to_global_event(self) -> None:
        args = ws_cli.build_parser().parse_args(["script", "1435"])
        self.assertEqual(args.entry, 1435)
        self.assertEqual(args.event, 0)

    def test_event_command_selects_event_object(self) -> None:
        args = ws_cli.build_parser().parse_args(["event", "200"])
        self.assertEqual(args.event, 200)

    def test_shop_command_selects_store(self) -> None:
        args = ws_cli.build_parser().parse_args(["shop", "3"])
        self.assertEqual(args.store, 3)

    def test_input_tap_has_a_bounded_explicit_hold(self) -> None:
        args = ws_cli.build_parser().parse_args(["input", "down"])
        self.assertEqual(args.action, "tap")
        self.assertEqual(args.hold_ms, 50)

    def test_record_command_is_bounded_by_default(self) -> None:
        args = ws_cli.build_parser().parse_args(["record", "tmp_ui/ws"])
        self.assertEqual(args.count, 20)
        self.assertEqual(args.interval_ms, 100)

    def test_record_can_start_battle_on_same_connection(self) -> None:
        args = ws_cli.build_parser().parse_args(
            [
                "record",
                "tmp_ui/ws",
                "--battle",
                "4",
                "--battlefield",
                "2",
                "--battle-auto",
            ]
        )
        self.assertEqual(args.battle, 4)
        self.assertEqual(args.battlefield, 2)
        self.assertTrue(args.battle_auto)


if __name__ == "__main__":
    unittest.main()
