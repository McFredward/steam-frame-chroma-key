#!/usr/bin/env python3
"""Find an exact UNIX socket peer in a supplied process without guessing its FD.

Read-only kernel/process metadata only: at most 4096 descriptor entries, four
exact socket queries and two seconds. A snapshot authenticates no past captured
descriptor, native resource namespace, Registry lease, source frame or GPU use.
"""
import argparse
import json

from inspect_unix_connection import ConnectionError, inspect_peer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("left-pid", "left-fd", "left-start-ticks", "right-pid", "right-start-ticks"):
        parser.add_argument(f"--{name}", type=int, required=True)
    parser.add_argument("--max-peer-fds", type=int, default=4096,
                        help="Maximum inspected entries in the exact peer process (1..4096; default: 4096)")
    args = parser.parse_args()
    try:
        result = inspect_peer((args.left_pid, args.left_fd, args.left_start_ticks),
                              (args.right_pid, args.right_start_ticks), max_peer_fds=args.max_peer_fds)
    except (OSError, ValueError, IndexError, ConnectionError) as error:
        result = {"kind": "unix_peer_discovery_refusal", "reason": str(error),
                  "right_process_fd_scan_limit": args.max_peer_fds,
                  "right_process_fd_entries_examined": getattr(error, "right_process_fd_entries_examined", 0),
                  "kernel_peer_pair_snapshot": False, "socket_epoch_continuity": False,
                  "native_cross_process_namespace": False, "source_frame_associated": False,
                  "accepted_submit": False, "gpu_consumed_coherent": False,
                  "historical_captured_descriptor_authenticated": False,
                  "native_registry_lease_authenticated": False}
        print(json.dumps(result, sort_keys=True))
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
