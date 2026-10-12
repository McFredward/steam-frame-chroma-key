#!/usr/bin/env python3
"""Protocol refusals plus real local kernel peer snapshots; no Frame access."""
import importlib.util
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time
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


@unittest.skipUnless(os.name=="posix" and hasattr(socket,"AF_NETLINK"),"Linux socket diagnostics required")
class PeerDiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.left,self.right=socket.socketpair()
        self.ticks=own_ticks()
        self.a=(os.getpid(),self.left.fileno(),self.ticks)
        self.peer=(os.getpid(),self.ticks)

    def tearDown(self):
        self.left.close();self.right.close()

    def test_real_socketpair_four_cookie_queries_and_false_claims(self):
        queries=[]
        original=connection.KernelQuery.get
        def observe(query,inode,cookie=0xffffffffffffffff):
            result=original(query,inode,cookie)
            queries.append((inode,cookie,result["cookie"]))
            return result
        with patch.object(connection.KernelQuery,"get",observe):
            result=connection.inspect_peer(self.a,self.peer)
        self.assertEqual(result["kind"],"unix_peer_discovery_snapshot")
        self.assertEqual(result["endpoints"][1]["fd"],self.right.fileno())
        self.assertTrue(result["kernel_peer_pair_snapshot"])
        self.assertLessEqual(result["right_process_fd_entries_examined"],256)
        self.assertEqual(len(queries),4)
        self.assertEqual([x[0] for x in queries[:2]],[x[0] for x in queries[2:]])
        self.assertEqual([x[2] for x in queries[:2]],[x[1] for x in queries[2:]])
        for flag in ("socket_epoch_continuity","native_cross_process_namespace",
                     "source_frame_associated","accepted_submit","gpu_consumed_coherent",
                     "historical_captured_descriptor_authenticated","native_registry_lease_authenticated"):
            self.assertFalse(result[flag],flag)

    def test_separate_real_client_server_process_without_known_peer_fd(self):
        # The server's independently reported descriptor is only an assertion
        # oracle; discovery receives just its PID/start ticks, never its FD/name.
        server_code="""
import json, os, socket, sys
from pathlib import Path
listener=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
address='\\0frame-peer-test-'+str(os.getpid())
listener.bind(address);listener.listen(1)
value=Path('/proc/self/stat').read_text()
ticks=int(value[value.rindex(')')+2:].split()[19])
print(json.dumps({'pid':os.getpid(),'ticks':ticks,'address':address}),flush=True)
accepted,_=listener.accept()
os.dup2(accepted.fileno(),61);accepted.close()
print(61,flush=True)
sys.stdin.readline()
os.close(61);listener.close()
"""
        process=subprocess.Popen([sys.executable,"-u","-c",server_code],
                                 stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE,text=True)
        client=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
        try:
            server=json.loads(process.stdout.readline())
            client.connect(server["address"])
            actual_fd=int(process.stdout.readline())
            result=connection.inspect_peer((os.getpid(),client.fileno(),self.ticks),
                                           (server["pid"],server["ticks"]))
            self.assertEqual(result["endpoints"][1]["fd"],actual_fd)
            self.assertNotEqual(actual_fd,8)
            self.assertNotEqual(result["endpoints"][0]["pid"],result["endpoints"][1]["pid"])
            with self.assertRaisesRegex(connection.ConnectionError,"specified process"):
                connection.inspect_peer((os.getpid(),client.fileno(),self.ticks),self.peer)
        finally:
            client.close()
            try:
                process.communicate("\n",timeout=2)
            except subprocess.TimeoutExpired:
                process.kill();process.communicate()

    def test_wrong_peer_start_ticks_rejected_before_query(self):
        with patch.object(connection.KernelQuery,"get",side_effect=AssertionError("must not query")):
            with self.assertRaisesRegex(connection.ConnectionError,"lifetime"):
                connection.inspect_peer(self.a,(os.getpid(),self.ticks+1))

    def test_peer_alias_ambiguity_fails_after_only_left_query(self):
        duplicate=os.dup(self.right.fileno())
        try:
            calls=[]
            original=connection.KernelQuery.get
            def observe(query,*args):
                calls.append(args);return original(query,*args)
            with patch.object(connection.KernelQuery,"get",observe):
                with self.assertRaisesRegex(connection.ConnectionError,"Ambiguous"):
                    connection.inspect_peer(self.a,self.peer)
            self.assertEqual(len(calls),1)
        finally:os.close(duplicate)

    def test_scan_hard_cap_even_with_matching_peer_found(self):
        class Entry:
            def __init__(self,name):
                self.name=str(name);self.path="/fake/"+self.name
        class Entries:
            def __enter__(self):return iter([Entry(self.right_fd)]+[Entry(999)]*256)
            def __exit__(self,*_):pass
        found=connection.endpoint(os.getpid(),self.right.fileno(),self.ticks)
        entries=Entries();entries.right_fd=self.right.fileno()
        def link(path):return f"socket:[{found['inode']}]" if path=="/fake/"+str(self.right.fileno()) else "/dev/null"
        with patch.object(connection.os,"scandir",return_value=entries), \
                patch.object(connection.os,"readlink",side_effect=link), \
                patch.object(connection,"endpoint",return_value=found):
            with self.assertRaisesRegex(connection.ConnectionError,"descriptor limit"):
                connection._peer_descriptor(self.peer,found["inode"],time.monotonic()+2)

    def test_real_process_descriptor_cap(self):
        descriptors=[]
        try:
            for _ in range(257):descriptors.append(os.open("/dev/null",os.O_RDONLY))
            with self.assertRaisesRegex(connection.ConnectionError,"descriptor limit"):
                connection.inspect_peer(self.a,self.peer)
        finally:
            for fd in descriptors:os.close(fd)

    def test_selected_peer_descriptor_rebound_before_final_validation(self):
        original=connection.KernelQuery.get
        calls=0
        replacement,other=socket.socketpair()
        def rebind(query,*args):
            nonlocal calls
            calls+=1;result=original(query,*args)
            if calls==4:os.dup2(replacement.fileno(),self.right.fileno())
            return result
        try:
            with patch.object(connection.KernelQuery,"get",rebind):
                with self.assertRaisesRegex(connection.ConnectionError,"metadata changed"):
                    connection.inspect_peer(self.a,self.peer)
        finally:replacement.close();other.close()

    def test_peer_identity_changes_during_discovery(self):
        original=connection._process_identity
        calls=0
        def changed(*args):
            nonlocal calls
            calls+=1;result=original(*args)
            if calls==3:result["netns_inode"]+=1
            return result
        with patch.object(connection,"_process_identity",changed):
            with self.assertRaisesRegex(connection.ConnectionError,"process changed"):
                connection.inspect_peer(self.a,self.peer)

    def test_left_descriptor_identity_changes_after_queries(self):
        original=connection.endpoint
        calls=0
        def changed(*args):
            nonlocal calls
            calls+=1;result=original(*args)
            if calls==3:result["process_start_ticks"]+=1
            return result
        with patch.object(connection,"endpoint",changed):
            with self.assertRaisesRegex(connection.ConnectionError,"metadata changed"):
                connection.inspect_peer(self.a,self.peer)

    def test_deadline_includes_metadata_and_directory_scan(self):
        original=connection.endpoint
        def elapsed(*args):
            result=original(*args)
            clock[0]+=2.1
            return result
        clock=[100.0]
        with patch.object(connection.time,"monotonic",side_effect=lambda:clock[0]), \
                patch.object(connection,"endpoint",elapsed), \
                patch.object(connection.KernelQuery,"get",side_effect=AssertionError("must not query")):
            with self.assertRaisesRegex(connection.ConnectionError,"deadline"):
                connection.inspect_peer(self.a,self.peer)
        with self.assertRaisesRegex(connection.ConnectionError,"deadline"):
            connection._peer_descriptor(self.peer,1,time.monotonic()-1)

    def test_owner_change_is_refused_before_kernel_queries(self):
        original=connection._bounded
        def changed(path,limit):
            if path.name=="status":return "Uid:\t"+"\t".join([str(os.geteuid()+1)]*4)+"\n"
            return original(path,limit)
        with patch.object(connection,"_bounded",changed), \
                patch.object(connection.KernelQuery,"get",side_effect=AssertionError("must not query")):
            with self.assertRaisesRegex(connection.ConnectionError,"owned"):
                connection.inspect_peer(self.a,self.peer)

    def test_total_deadline_refuses_after_four_real_queries(self):
        original=connection.KernelQuery.get
        calls=0
        clock=[100.0]
        def elapsed(query,*args):
            nonlocal calls
            calls+=1;result=original(query,*args)
            if calls==4:clock[0]+=2.1
            return result
        with patch.object(connection.time,"monotonic",side_effect=lambda:clock[0]), \
                patch.object(connection.KernelQuery,"get",elapsed):
            with self.assertRaisesRegex(connection.ConnectionError,"deadline"):
                connection.inspect_peer(self.a,self.peer)
        self.assertEqual(calls,4)

    def test_cli_discovers_real_peer_without_right_fd_argument(self):
        result=subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/"tools/inspect_client_connection.py"),
                               "--left-pid",str(os.getpid()),"--left-fd",str(self.left.fileno()),
                               "--left-start-ticks",str(self.ticks),"--right-pid",str(os.getpid()),
                               "--right-start-ticks",str(self.ticks)],
                              capture_output=True,text=True,timeout=3)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        receipt=json.loads(result.stdout)
        self.assertEqual(receipt["endpoints"][1]["fd"],self.right.fileno())
        self.assertTrue(receipt["kernel_peer_pair_snapshot"])
        self.assertFalse(receipt["historical_captured_descriptor_authenticated"])

    def test_cli_refusal_has_no_positive_claims(self):
        result=subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/"tools/inspect_client_connection.py"),
                               "--left-pid","0","--left-fd","0","--left-start-ticks","1",
                               "--right-pid",str(os.getpid()),"--right-start-ticks",str(self.ticks)],
                              capture_output=True,text=True,timeout=3)
        self.assertEqual(result.returncode,1)
        receipt=json.loads(result.stdout)
        self.assertEqual(receipt["kind"],"unix_peer_discovery_refusal")
        for key,value in receipt.items():
            if isinstance(value,bool):self.assertFalse(value,key)


if __name__=="__main__":unittest.main()
