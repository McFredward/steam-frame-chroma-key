#!/usr/bin/env python3
"""Execute the two real startup veneers with explicitly supplied private providers.

Reuses the original executed register probe and process cleanup helper. No
downloads, package installation, Frame access or observer activation occurs.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True
import run_import_native_forwarding as common


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", required=True, type=Path)
    parser.add_argument("--providers", required=True, type=Path)
    parser.add_argument("--compiler", default="aarch64-linux-gnu-g++")
    parser.add_argument("--support-libraries", type=Path, default=Path("/usr/aarch64-linux-gnu/lib"))
    parser.add_argument("--record", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    receipt = {"status": "failed", "logs": {}, "providers": {}, "frame_access": False,
               "native_observer_activation": False, "source_frame_associated": False,
               "gpu_consumed_coherent": False}
    try:
        qemu = args.qemu.resolve(strict=True)
        prefix = args.providers.resolve(strict=True)
        support = args.support_libraries.resolve(strict=True)
        receipt["qemu_sha256"] = common.digest(qemu)
        for name, expected in common.PINNED.items():
            source = prefix / "lib" / name
            actual = common.digest(source)
            if actual != expected:
                raise SystemExit("Refusing unreviewed provider: " + name)
            receipt["providers"][name] = actual
        compiler = shlex.split(args.compiler)
        if not compiler or not shutil.which(compiler[0]):
            raise SystemExit("Explicit ARM64 compiler unavailable")
        sources = [root / "tests/test_startup_native_execution.cpp",
                   root / "import/native_startup_entry.S", root / "import/import_startup_forwarding.cpp"]
        dependencies = [root / "import/native_import_entry.S", root / "import/import_startup_forwarding.h",
                        root / "import/import_startup_audit.h", root / "import/native_import_route.h",
                        root / "tests/test_import_native_forwarding.cpp", Path(__file__).resolve(),
                        root / "tools/run_import_native_forwarding.py"]
        receipt["source_sha256"] = {str(p.relative_to(root)): common.digest(p) for p in sources + dependencies}
        original = dependencies[4].read_text()
        probes = re.findall(r'__asm__\(R"\((.*?)\)"\);', original, re.S)
        if len(probes) != 1 or "fixture_abi_probe:" not in probes[0]:
            raise SystemExit("Original executed own-source register probe is not unique")
        with tempfile.TemporaryDirectory(prefix="frame-startup-native-") as temporary:
            temporary = Path(temporary)
            temporary.chmod(0o700)
            (temporary / "native_probe_assembly.h").write_text('__asm__(R"(' + probes[0] + ')");\n')
            executable = temporary / "startup-native"
            built = common.recorded_run(compiler + ["-std=c++17", "-O2", "-g", "-fno-omit-frame-pointer",
                "-pthread", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I", str(root), "-I",
                str(temporary)] + [str(p) for p in sources] + ["-o", str(executable)],
                temporary, receipt["logs"], "compile", 60)
            if built.returncode:
                raise SystemExit("Startup ARM64 build failed: " + built.stderr[-3000:])
            env = os.environ.copy()
            for name in ("LD_PRELOAD", "LD_AUDIT", "LD_LIBRARY_PATH", "QEMU_LD_PREFIX"):
                env.pop(name, None)
            command = [str(qemu), "-L", str(prefix), "-E",
                       "LD_LIBRARY_PATH=" + str(prefix / "lib") + ":" + str(support), str(executable)]
            executed = common.recorded_run(command, temporary, receipt["logs"], "execution", 20, env)
            if executed.returncode:
                raise SystemExit("Startup ARM64 execution failed: " + executed.stderr[-3000:])
            results = [json.loads(line[7:]) for line in executed.stdout.splitlines() if line.startswith("RESULT=")]
            if len(results) != 1 or results[0].get("veneers") != 2 or not results[0].get("exception_and_forced_unwind"):
                raise SystemExit("Incomplete actual startup ABI execution receipt")
            receipt["result"] = results[0]
            receipt["fixture_elf_sha256"] = common.digest(executable)
            receipt["status"] = "passed"
            print(executed.stdout, end="")
    finally:
        receipt["cleanup"] = "Owned compiler/QEMU groups reaped; temporary sources/ELF removed; only bounded receipt retained"
        if args.record:
            args.record.parent.mkdir(parents=True, exist_ok=True)
            args.record.write_text(json.dumps(receipt, indent=2) + "\n")
            args.record.chmod(0o600)


if __name__ == "__main__":
    main()
