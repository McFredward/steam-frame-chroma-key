#!/usr/bin/env python3
"""Check local artifacts and write a proposed receiver diagnostic scope.

Never install, load, attach, restart, inspect process memory or enable a mask.
The output is a reviewable plan, not runtime authorization or frame evidence.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct

from collect_mask_bridge import CollectionError, run_bounded

RECEIVER_SHA256 = "0000009e5c7124bb03bfd444f7200304e0bb3ec1ec0cf80f0086a19ec048132b"
RECEIVER_SERVICE = "steam.service"
MAX_INPUT_BYTES = 16 * 1024 * 1024
MAX_LIBRARY_BYTES = 256 * 1024
PROVIDER_SYMBOLS = (
    "pthread_mutex_lock@@GLIBC_2.17",
    "pthread_mutex_unlock@@GLIBC_2.17",
    "__pthread_mutex_lock@GLIBC_2.17",
    "__pthread_mutex_unlock@GLIBC_2.17",
)


def artifact(path: Path, limit: int) -> tuple[dict, bytes]:
    with os.fdopen(os.open(path, os.O_RDONLY | os.O_NONBLOCK), "rb") as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise CollectionError("Artifact input must be a regular file.")
        raw = stream.read(limit + 1)
    if len(raw) > limit:
        raise CollectionError("Artifact input exceeds its size limit.")
    if len(raw) < 64 or raw[:7] != b"\x7fELF\x02\x01\x01":
        raise CollectionError("Expected a little-endian ELF64 artifact.")
    kind, machine = struct.unpack_from("<HH", raw, 16)
    if kind != 3 or machine != 183:
        raise CollectionError("Expected an AArch64 position-independent ELF artifact.")
    return {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
            "elf_machine": machine, "elf_type": kind}, raw


def dynamic_symbols(path: Path) -> list[tuple[str, str, str]]:
    code, output = run_bounded(["readelf", "--dyn-syms", "--wide", str(path)],
                              output_limit=2 * 1024 * 1024)
    if code != 0:
        raise CollectionError("Could not verify the artifact's dynamic symbols.")
    symbols = []
    for line in output.decode("ascii", errors="strict").splitlines():
        cols = line.split()
        if len(cols) >= 8 and re.fullmatch(r"\d+:", cols[0]):
            symbols.append((cols[7], cols[4], cols[6]))
    return symbols


def prepare(receiver: Path, provider: Path, observer: Path,
            observer_sha256: str, user_home: Path, *, loader: Path | None = None) -> dict:
    receiver_info, _ = artifact(receiver, MAX_INPUT_BYTES)
    if receiver_info["sha256"] != RECEIVER_SHA256:
        raise CollectionError("Unsupported receiver fingerprint; no plan prepared.")
    provider_info, _ = artifact(provider, MAX_INPUT_BYTES)
    provider_symbols = dynamic_symbols(provider)
    for symbol in PROVIDER_SYMBOLS:
        if not any(name == symbol and binding == "GLOBAL" and index != "UND"
                   for name, binding, index in provider_symbols):
            raise CollectionError("Required versioned mutex provider alias is unavailable.")
    observer_info, _ = artifact(observer, MAX_LIBRARY_BYTES)
    if not re.fullmatch(r"[0-9a-f]{64}", observer_sha256):
        raise CollectionError("Expected an explicit lowercase observer SHA-256.")
    if observer_info["sha256"] != observer_sha256:
        raise CollectionError("Observer artifact does not match its reviewed fingerprint.")
    symbols = dynamic_symbols(observer)
    loader_info = None
    loader_symbols = []
    if loader is not None:
        loader_info, _ = artifact(loader, MAX_INPUT_BYTES)
        loader_symbols = dynamic_symbols(loader)
    provider_definitions = {name.replace("@@", "@") for name, binding, index in provider_symbols
                            if binding in ("GLOBAL", "WEAK") and index != "UND"}
    provider_definitions.update(name.replace("@@", "@") for name, binding, index in loader_symbols
                                if binding in ("GLOBAL", "WEAK") and index != "UND")
    required_libc = sorted({name for name, binding, index in symbols
                            if binding == "GLOBAL" and index == "UND" and "@GLIBC_" in name})
    if any(name not in provider_definitions for name in required_libc):
        raise CollectionError("Provider lacks a required versioned observer libc symbol.")
    for function in ("pthread_mutex_lock", "pthread_mutex_unlock"):
        if not any(name.split("@", 1)[0] == function and binding == "GLOBAL"
                   and index != "UND" for name, binding, index in symbols):
            raise CollectionError("Observer lacks a required forwarding wrapper.")
        alias = f"__{function}@GLIBC_2.17"
        if not any(name == alias and index == "UND" for name, _, index in symbols):
            raise CollectionError("Observer does not use the verified provider alias ABI.")
    if any(name.split("@", 1)[0] == "__tls_get_addr" and index == "UND"
           for name, _, index in symbols):
        raise CollectionError("Observer uses unsupported dynamic TLS in its callback path.")
    if not user_home.is_absolute() or ".." in user_home.parts:
        raise CollectionError("Expected an absolute, normalized native user home.")
    owned = user_home / ".local/share/frame-receiver-observer"
    return {
        "schema": "frame-receiver-observer-proposal-v2",
        "receiver_service": RECEIVER_SERVICE,
        "offline_files_supported": True,
        "installed": False,
        "runtime_authorized": False,
        "receiver": receiver_info,
        "provider": {**provider_info, "required_aliases": list(PROVIDER_SYMBOLS),
                     "verified_observer_libc_imports": required_libc},
        "loader": loader_info,
        "observer": observer_info,
        "proposed_owned_directory": str(owned),
        "proposed_runtime_dropin_basename": "91-frame-receiver-observer.conf",
        "activation": "temporary Steam user-service LD_PRELOAD; exact receiver only samples",
        "callbacks": ["pthread_mutex_lock", "pthread_mutex_unlock"],
        "selection_caller_returns": ["0x34a50", "0x34d74"],
        "metadata_caller_returns": ["0x5200c", "0x5203c"],
        "protected_read_fields": ["shared_token_u64", "stored_key_u32",
                                  "content_generation_u64", "metadata_valid_u8",
                                  "metadata_stored_key_u32", "presentation_word_u32"],
        "limits": {"selections": 32, "bytes": 1048576, "seconds": 120},
        "image_or_mapped_uniform_reads": False,
        "native_binary_patch": False,
        "kernel_or_system_image_change": False,
        "shader_or_mask_activation": False,
        "automatic_restart": False,
        "before_installation": [
            "Fresh native receiver/provider fingerprints and executable path.",
            "Actual receiver belongs to steam.service, not merely steamvr.service.",
            "Known original service environment; refuse foreign preload/drop-in/session.",
            "Explicit permission for private scalar copies and temporary preload.",
            "Restart readiness; verified manual rollback and owned-file inventory.",
        ],
        "evidence_limit": "Protected selection attempt; not accepted submit, source session or GPU proof.",
    }


def write_new_private(path: Path, report: dict) -> None:
    if not path.is_absolute() or not path.parent.is_dir() or path.parent.is_symlink():
        raise CollectionError("Plan output needs an existing absolute private directory.")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    with os.fdopen(os.open(path, flags, stat.S_IRUSR | stat.S_IWUSR), "w") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--receiver", type=Path, required=True)
    parser.add_argument("--provider-libc", type=Path, required=True)
    parser.add_argument("--provider-loader", type=Path, required=True)
    parser.add_argument("--observer", type=Path, required=True)
    parser.add_argument("--observer-sha256", required=True)
    parser.add_argument("--native-user-home", type=Path, default=Path("/home/steamos"))
    parser.add_argument("--output-plan", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = prepare(args.receiver, args.provider_libc, args.observer,
                         args.observer_sha256, args.native_user_home, loader=args.provider_loader)
        write_new_private(args.output_plan, report)
    except (CollectionError, OSError, UnicodeError, struct.error) as exc:
        parser.exit(1, f"Receiver observer preparation refused: {type(exc).__name__}: {exc}\n")
    print("Offline artifacts checked; proposed scope written. Nothing installed or activated.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
