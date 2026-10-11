#!/usr/bin/env python3
"""Exercise actual UDP endpoints and independently inspect the wire format."""
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ENDPOINT = sys.argv.pop(1) if len(sys.argv) > 1 else "build/mask_endpoint"
SESSION = "0102030405060708090a0b0c0d0e0f10"


class EndpointTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="frame-mask-network-")
        self.root = Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    def mask(self, data, width, height):
        path = self.root / "mask.pgm"
        path.write_bytes(f"P5\n{width} {height}\n255\n".encode() + bytes(data))
        return path

    def send(self, port, path, frame=42, eye=0, session=SESSION):
        return subprocess.run([ENDPOINT, "send", "127.0.0.1", str(port), session,
                               str(frame), str(eye), str(path)], capture_output=True,
                              text=True, timeout=3)

    def receiver(self, width, height, timeout=1000):
        output = self.root / "received.pgm"
        process = subprocess.Popen([ENDPOINT, "receive", "127.0.0.1", "0", SESSION,
                                    "42", "0", str(width), str(height), str(output), str(timeout)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(lambda: process.kill() if process.poll() is None else None)
        self.addCleanup(process.stdout.close)
        self.addCleanup(process.stderr.close)
        self.assertTrue(select.select([process.stdout], [], [], 3)[0], "Receiver failed to become ready")
        line = process.stdout.readline()
        self.assertTrue(line.startswith("READY port="), line)
        return process, int(line.split()[1].split("=")[1]), output

    def test_fragmented_udp_roundtrip_and_binary_whitespace_pixels(self):
        pixels = bytes([32, 10, 13, 0, 64, 128, 255, 0]) * 1024
        source = self.mask(pixels, 128, 64)
        receiver, port, output = self.receiver(128, 64)
        sent = self.send(port, source)
        self.assertEqual(sent.returncode, 0, sent.stderr)
        stdout, stderr = receiver.communicate(timeout=3)
        self.assertEqual(receiver.returncode, 0, stderr)
        self.assertEqual(output.read_bytes(), source.read_bytes())
        self.assertIn("no Steam Link color frame was identified", stdout)

    def test_wrong_frame_eye_session_never_substitute_expected_mask(self):
        source = self.mask([0, 64, 128, 255], 4, 1)
        receiver, port, output = self.receiver(4, 1, timeout=300)
        for arguments in ({"frame": 41}, {"eye": 1}, {"session": "11" * 16}):
            sent = self.send(port, source, **arguments)
            self.assertEqual(sent.returncode, 0, sent.stderr)
        stdout, stderr = receiver.communicate(timeout=3)
        self.assertEqual(receiver.returncode, 3, (stdout, stderr))
        self.assertFalse(output.exists())
        self.assertIn("remain opaque", stderr)

    def test_independent_network_byte_order_and_crc(self):
        data = bytes([0, 64, 128, 255])
        source = self.mask(data, 4, 1)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
            listener.bind(("127.0.0.1", 0))
            listener.settimeout(3)
            sent = self.send(listener.getsockname()[1], source, frame=0x0102030405060708)
            self.assertEqual(sent.returncode, 0, sent.stderr)
            packet = listener.recv(1201)
        self.assertEqual(len(packet), 60)
        self.assertEqual(packet[:8], b"MRA1\x01\x00\x00\x00")
        self.assertEqual(packet[8:24], bytes.fromhex(SESSION))
        self.assertEqual(struct.unpack("!QHHIIHHII", packet[24:56]),
                         (0x0102030405060708, 4, 1, 4, 0, 4, 0,
                          zlib.crc32(data), zlib.crc32(packet[:52] + packet[56:])))
        self.assertEqual(packet[56:], data)

    def test_truncated_pgm_and_invalid_arguments_fail_before_sending(self):
        path = self.mask([255], 4, 1)
        for kwargs in ({}, {"eye": 2}, {"session": "bad"}, {"frame": -1}):
            with self.subTest(kwargs=kwargs):
                sent = self.send(29999, path, **kwargs)
                self.assertEqual(sent.returncode, 1)
        source = self.mask([255], 1, 1)
        with source.open("ab") as stream:
            stream.write(b"extra")
        self.assertEqual(self.send(29999, source).returncode, 1)


if __name__ == "__main__":
    unittest.main()
