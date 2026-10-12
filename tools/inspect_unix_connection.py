#!/usr/bin/env python3
"""Read-only, bounded Linux UNIX socket pairing outside native/render callbacks.

An exact kernel peer relation is a snapshot, not native epoch continuity,
resource namespace, source frame or GPU proof. No automatic process changes.
"""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import stat
import struct
import time


class ConnectionError(RuntimeError):
    pass


def parse_reply(data, sequence, port, inode):
    if len(data) < 32 or len(data) > 4096:
        raise ConnectionError("Invalid bounded netlink response size")
    length, kind, flags, seq, pid = struct.unpack_from("=IHHII", data)
    if length != len(data) or seq != sequence or pid != port or flags:
        raise ConnectionError("Changed netlink envelope or unsupported flags")
    if kind != 20:
        raise ConnectionError("Kernel refused the exact socket query")
    family, typ, state, pad, ino, low, high = struct.unpack_from("=BBBBIII", data, 16)
    if (family, typ, state, pad, ino) != (socket.AF_UNIX, socket.SOCK_STREAM, 1, 0, inode):
        raise ConnectionError("Not the expected established UNIX stream")
    cookie = low | (high << 32)
    if cookie == 0xffffffffffffffff:
        raise ConnectionError("Missing kernel socket generation cookie")
    attributes = {}
    offset = 32
    while offset < length:
        if length - offset < 4:
            raise ConnectionError("Truncated netlink attribute")
        size, tag = struct.unpack_from("=HH", data, offset)
        if size < 4 or size > length - offset or tag in attributes:
            raise ConnectionError("Invalid or duplicate netlink attribute")
        expected = {2: 4, 6: 1, 7: 4}.get(tag)
        if expected is None or size - 4 != expected:
            raise ConnectionError("Unsupported socket attribute")
        attributes[tag] = data[offset + 4:offset + size]
        advance = (size + 3) & ~3
        if advance > length - offset or any(data[offset + size:offset + advance]):
            raise ConnectionError("Invalid attribute padding")
        offset += advance
    if set(attributes) != {2, 6, 7} or attributes[6] != b"\x00":
        raise ConnectionError("Missing peer/owner or shut down socket")
    peer = struct.unpack("=I", attributes[2])[0]
    uid = struct.unpack("=I", attributes[7])[0]
    if not peer or peer == inode:
        raise ConnectionError("Invalid socket peer inode")
    return {"inode": inode, "cookie": cookie, "peer_inode": peer, "uid": uid}


class KernelQuery:
    def __init__(self, deadline=None):
        self.channel = socket.socket(socket.AF_NETLINK, socket.SOCK_RAW, 4)
        self.channel.settimeout(0.5)
        self.channel.bind((0, 0))
        self.port = self.channel.getsockname()[0]
        self.sequence = 0
        self.deadline = time.monotonic() + 2.0 if deadline is None else deadline

    def _timeout(self):
        remaining = self.deadline - time.monotonic()
        if remaining <= 0:
            raise ConnectionError("Socket query deadline exceeded")
        self.channel.settimeout(min(0.5, remaining))

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.channel.close()

    def get(self, inode, cookie=0xffffffffffffffff):
        if not 0 < inode <= 0xffffffff or self.sequence >= 4:
            raise ConnectionError("Exact socket query limit exceeded")
        self.sequence += 1
        # unix_diag_req; request only PEER and UID, never names or packet data.
        request = struct.pack("=BBHIIIII", socket.AF_UNIX, 0, 0, 0xffffffff,
                              inode, 0x44, cookie & 0xffffffff, cookie >> 32)
        envelope = struct.pack("=IHHII", 16 + len(request), 20, 1, self.sequence, self.port)
        self._timeout()
        self.channel.sendto(envelope + request, (0, 0))
        self._timeout()
        data, _, flags, sender = self.channel.recvmsg(4096)
        if flags or sender != (0, 0):
            raise ConnectionError("Truncated or non-kernel netlink response")
        result = parse_reply(data, self.sequence, self.port, inode)
        if cookie != 0xffffffffffffffff and result["cookie"] != cookie:
            raise ConnectionError("Socket lifetime changed")
        return result


def _bounded(path, limit):
    with path.open("rb") as source:
        value = source.read(limit + 1)
    if len(value) > limit:
        raise ConnectionError("Process metadata exceeds bound")
    return value.decode("ascii")


def _process_identity(pid, start_ticks):
    if not 0 < pid <= 0x7fffffff or start_ticks <= 0:
        raise ConnectionError("Invalid exact process identity")
    proc = Path("/proc") / str(pid)
    status = _bounded(proc / "status", 65536)
    uids = next((line.split()[1:] for line in status.splitlines() if line.startswith("Uid:")), [])
    if len(uids) != 4 or any(int(x) != os.geteuid() for x in uids):
        raise ConnectionError("Endpoint is not owned by the current user")
    value = _bounded(proc / "stat", 8192)
    if int(value[value.rindex(")") + 2:].split()[19]) != start_ticks:
        raise ConnectionError("Process lifetime changed")
    namespace = (proc / "ns/net").stat()
    inspector = Path("/proc/self/ns/net").stat()
    if (namespace.st_dev, namespace.st_ino) != (inspector.st_dev, inspector.st_ino):
        raise ConnectionError("Endpoint belongs to another network namespace")
    return {"pid": pid, "process_start_ticks": start_ticks, "uid": os.geteuid(),
            "netns_device": namespace.st_dev, "netns_inode": namespace.st_ino}


def endpoint(pid, fd, start_ticks):
    if not 0 <= fd <= 0x7fffffff:
        raise ConnectionError("Invalid exact endpoint identity")
    identity = _process_identity(pid, start_ticks)
    proc = Path("/proc") / str(pid)
    target = proc / "fd" / str(fd)
    match = re.fullmatch(r"socket:\[(\d+)\]", os.readlink(target))
    metadata = target.stat()
    if not match or not stat.S_ISSOCK(metadata.st_mode) or metadata.st_ino != int(match[1]) \
            or metadata.st_uid != identity["uid"]:
        raise ConnectionError("Changed descriptor or non-socket endpoint")
    return {**identity, "fd": fd, "inode": metadata.st_ino}


def _check_deadline(deadline):
    if time.monotonic() >= deadline:
        raise ConnectionError("Peer discovery deadline exceeded")


def _peer_descriptor(right, peer_inode, deadline):
    """Inspect at most 256 entries in one specified process, never all PIDs."""
    pid, start_ticks = right
    found = None
    examined = 0
    with os.scandir(Path("/proc") / str(pid) / "fd") as entries:
        for entry in entries:
            _check_deadline(deadline)
            examined += 1
            if examined > 256:
                raise ConnectionError("Known peer process descriptor limit exceeded")
            if not re.fullmatch(r"[0-9]+", entry.name):
                raise ConnectionError("Unexpected process descriptor entry")
            # readlink and fstat contain kernel metadata only, not socket payloads.
            match = re.fullmatch(r"socket:\[(\d+)\]", os.readlink(entry.path))
            if match and int(match[1]) == peer_inode:
                if found is not None:
                    raise ConnectionError("Ambiguous peer descriptor aliases")
                found = endpoint(pid, int(entry.name), start_ticks)
                if found["inode"] != peer_inode:
                    raise ConnectionError("Peer descriptor changed during discovery")
    _check_deadline(deadline)
    if found is None:
        raise ConnectionError("Kernel peer is not in the specified process")
    return found, examined


def _pair_result(before, after, first, again):
    if before != after or first != again:
        raise ConnectionError("Endpoint or kernel socket metadata changed")
    if any(x["uid"] != os.geteuid() for x in first) or \
            first[0]["peer_inode"] != first[1]["inode"] or \
            first[1]["peer_inode"] != first[0]["inode"]:
        raise ConnectionError("Kernel endpoints do not form the exact peer pair")
    return {"kind": "unix_peer_snapshot", "endpoints": before, "kernel_sockets": first,
            "kernel_peer_pair_snapshot": True, "socket_epoch_continuity": False,
            "native_cross_process_namespace": False, "source_frame_associated": False,
            "accepted_submit": False, "gpu_consumed_coherent": False}


def inspect_pair(left, right):
    """Four bounded queries within 2 seconds; no continuous-epoch claim."""
    before = [endpoint(*x) for x in (left, right)]
    if before[0]["inode"] == before[1]["inode"]:
        raise ConnectionError("Aliases of one socket are not a peer pair")
    with KernelQuery() as query:
        first = [query.get(x["inode"]) for x in before]
        again = [query.get(x["inode"], x["cookie"]) for x in first]
    after = [endpoint(*x) for x in (left, right)]
    return _pair_result(before, after, first, again)


def inspect_peer(left, right):
    """Discover a peer FD in one exact process within a single 2-second budget.

    left is (pid, fd, process_start_ticks); right is (pid, process_start_ticks).
    This validates a contemporaneous kernel snapshot only. It does not bind a
    historically captured descriptor, native Registry lease or source frame.
    """
    deadline = time.monotonic() + 2.0
    left_before = endpoint(*left)
    right_identity = _process_identity(*right)
    _check_deadline(deadline)
    with KernelQuery(deadline=deadline) as query:
        left_first = query.get(left_before["inode"])
        if left_first["uid"] != left_before["uid"]:
            raise ConnectionError("Kernel socket owner differs from process owner")
        right_before, examined = _peer_descriptor(right, left_first["peer_inode"], deadline)
        if any(right_before[key] != value for key, value in right_identity.items()):
            raise ConnectionError("Peer process changed during discovery")
        _check_deadline(deadline)
        right_first = query.get(right_before["inode"])
        first = [left_first, right_first]
        again = [query.get(x["inode"], x["cookie"]) for x in first]
    after = [endpoint(*left), endpoint(right[0], right_before["fd"], right[1])]
    _check_deadline(deadline)
    result = _pair_result([left_before, right_before], after, first, again)
    result.update({"kind": "unix_peer_discovery_snapshot",
                   "right_process_fd_entries_examined": examined,
                   "historical_captured_descriptor_authenticated": False,
                   "native_registry_lease_authenticated": False})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for side in ("left", "right"):
        for field in ("pid", "fd", "start-ticks"):
            parser.add_argument(f"--{side}-{field}", type=int, required=True)
    args = parser.parse_args()
    try:
        result = inspect_pair((args.left_pid, args.left_fd, args.left_start_ticks),
                              (args.right_pid, args.right_fd, args.right_start_ticks))
    except (OSError, ValueError, IndexError, ConnectionError) as error:
        print(json.dumps({"kind": "unix_peer_refusal", "reason": str(error),
                          "kernel_peer_pair_snapshot": False, "native_cross_process_namespace": False}))
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
