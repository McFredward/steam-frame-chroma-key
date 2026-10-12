#!/usr/bin/env python3
"""Protocol refusals plus real local kernel peer snapshots; no Frame access."""
import importlib.util
import os
from pathlib import Path
import socket
import struct
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("unix_connection", Path(__file__).resolve().parents[1] / "tools/inspect_unix_connection.py")
connection = importlib.util.module_from_spec(spec)
spec.loader.exec_module(connection)


def reply(sequence=1, port=100, inode=123):
    # Independently constructed UAPI reply with peer, shutdown and UID attributes.
    body = struct.pack("=BBBBIII", 1, 1, 1, 0, inode, 0x12345678, 0xabcdef00)
    body += struct.pack("=HHI", 8, 2, 456)
    body += struct.pack("=HHB3x", 5, 6, 0)
    body += struct.pack("=HHI", 8, 7, os.geteuid())
    return struct.pack("=IHHII", 16 + len(body), 20, 0, sequence, port) + body


def own_ticks():
    text = Path("/proc/self/stat").read_text()
    return int(text[text.rindex(")") + 2:].split()[19])


class ReplyTests(unittest.TestCase):
    def test_literal_reply(self):
        result = connection.parse_reply(reply(), 1, 100, 123)
        self.assertEqual(result, {"inode": 123, "cookie": 0xabcdef0012345678,
                                  "peer_inode": 456, "uid": os.geteuid()})

    def test_envelope_and_scalar_mutations(self):
        cases = [(0,"=I",55), (4,"=H",2), (6,"=H",2), (8,"=I",2),
                 (12,"=I",101), (16,"=B",2), (17,"=B",2), (18,"=B",10),
                 (19,"=B",1), (20,"=I",124), (24,"=Q",0xffffffffffffffff),
                 (32,"=H",3), (34,"=H",3), (36,"=I",0), (36,"=I",123),
                 (40,"=H",8), (42,"=H",2), (44,"=B",1), (45,"=B",1),
                 (48,"=H",7), (50,"=H",0x8007)]
        for offset, fmt, value in cases:
            with self.subTest(offset=offset, value=value):
                data=bytearray(reply()); struct.pack_into(fmt,data,offset,value)
                with self.assertRaises(connection.ConnectionError):
                    connection.parse_reply(data,1,100,123)

    def test_truncation_trailer_and_limits(self):
        for length in range(len(reply())):
            with self.subTest(length=length), self.assertRaises(connection.ConnectionError):
                connection.parse_reply(reply()[:length],1,100,123)
        for data in (reply()+b"\x00", b"\x00"*4097):
            with self.assertRaises(connection.ConnectionError):
                connection.parse_reply(data,1,100,123)

    def test_missing_or_duplicate_attributes(self):
        for data in (reply()[:-8],reply()+reply()[32:40]):
            mutated=bytearray(data);struct.pack_into("=I",mutated,0,len(mutated))
            with self.assertRaises(connection.ConnectionError):
                connection.parse_reply(mutated,1,100,123)

    def test_endpoint_parameters(self):
        for args in ((0,0,1),(1,-1,1),(1,0,0),(1<<31,0,1)):
            with self.assertRaises(connection.ConnectionError):connection.endpoint(*args)


@unittest.skipUnless(os.name=="posix" and hasattr(socket,"AF_NETLINK"),"Linux socket diagnostics required")
class LiveKernelTests(unittest.TestCase):
    def setUp(self):
        self.left,self.right=socket.socketpair()
        self.ticks=own_ticks()
        self.a=(os.getpid(),self.left.fileno(),self.ticks)
        self.b=(os.getpid(),self.right.fileno(),self.ticks)

    def tearDown(self):
        self.left.close();self.right.close()

    def test_real_peer_pair_and_false_unproved_flags(self):
        result=connection.inspect_pair(self.a,self.b)
        self.assertTrue(result["kernel_peer_pair_snapshot"])
        for flag in ("socket_epoch_continuity","native_cross_process_namespace","source_frame_associated","accepted_submit","gpu_consumed_coherent"):
            self.assertFalse(result[flag])
        self.assertNotEqual(result["kernel_sockets"][0]["cookie"],result["kernel_sockets"][1]["cookie"])

    def test_different_pair_rejected(self):
        extra,other=socket.socketpair()
        try:
            with self.assertRaises(connection.ConnectionError):
                connection.inspect_pair(self.a,(os.getpid(),extra.fileno(),self.ticks))
        finally:extra.close();other.close()

    def test_fd_alias_is_not_peer(self):
        duplicate=os.dup(self.left.fileno())
        try:
            with self.assertRaises(connection.ConnectionError):
                connection.inspect_pair(self.a,(os.getpid(),duplicate,self.ticks))
        finally:os.close(duplicate)

    def test_pid_reuse_and_non_socket(self):
        with self.assertRaises(connection.ConnectionError):
            connection.endpoint(os.getpid(),self.left.fileno(),self.ticks+1)
        fd=os.open("/dev/null",os.O_RDONLY)
        try:
            with self.assertRaises(connection.ConnectionError):connection.endpoint(os.getpid(),fd,self.ticks)
        finally:os.close(fd)

    def test_query_cap_and_cookie_reuse_refusal(self):
        inode=connection.endpoint(*self.a)["inode"]
        with connection.KernelQuery() as query:
            first=query.get(inode)
            query.get(inode,first["cookie"])
            query.get(inode,first["cookie"])
            query.get(inode,first["cookie"])
            with self.assertRaises(connection.ConnectionError):query.get(inode)
        with connection.KernelQuery() as query:
            with self.assertRaises(connection.ConnectionError):query.get(inode,first["cookie"]+1)

    def test_before_after_lifetime_change(self):
        original=connection.endpoint
        calls=0
        def changed(*args):
            nonlocal calls
            calls+=1;data=original(*args)
            if calls==3:data["fd"]+=1
            return data
        with patch.object(connection,"endpoint",changed):
            with self.assertRaises(connection.ConnectionError):connection.inspect_pair(self.a,self.b)


if __name__=="__main__":unittest.main()
