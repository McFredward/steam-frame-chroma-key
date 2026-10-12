#!/usr/bin/env python3
"""Run the actual ARM64 entry ABI with explicitly pinned, private providers.

QEMU and the Frame libc/loader must already be available. This tool performs no
network access, package installation, Frame access or native observer activation.
Only public Vulkan headers and cross libstdc++/libgcc support the controlled
Vulkan ABI fixture; it does not execute a Frame kernel, Vulkan GPU or headset.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shlex
import shutil
import signal
import subprocess
import tempfile


PINNED = {
    "libc.so.6": "be7ee393429d493c4d3b996ed25cbb02fabb2896a77efe6d57e5993ead5bbb3b",
    "ld-linux-aarch64.so.1": "2ddc56a02f476765a21e31d69144e68bee933950fe613747d54b87a2369a037e",
}


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def disable_core_dumps():
    # Expected negative-control faults must not leave large host/guest cores.
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def recorded_run(command, directory, logs, label, timeout, environment=None):
    def clipped(value):
        if isinstance(value, bytes):
            value = value.decode("utf-8", errors="replace")
        return (value or "")[-32768:]

    process = subprocess.Popen(command, cwd=directory, env=environment, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, start_new_session=True,
                               preexec_fn=disable_core_dumps)

    def kill_and_reap_owned_group():
        # The compiler driver may own cc1/as/ld descendants. Kill the complete
        # session's process group before reaping the driver and deleting inputs.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass  # The owned group already exited; still reap the direct child.
        return process.communicate()

    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired as error:
        stdout, stderr = kill_and_reap_owned_group()
        logs[label] = {"timeout_seconds": timeout, "exit_status": process.returncode,
                       "owned_group_terminated": True, "direct_child_reaped": True,
                       "stdout": clipped(stdout), "stderr": clipped(stderr)}
        raise SystemExit(f"Bounded {label} run timed out") from error
    except BaseException:
        # Interrupted runs must not let a live tool retain the temporary paths.
        kill_and_reap_owned_group()
        raise
    result = subprocess.CompletedProcess(command, process.returncode, stdout, stderr)
    logs[label] = {"exit_status": result.returncode, "stdout": clipped(result.stdout),
                   "stderr": clipped(result.stderr)}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", required=True, type=Path)
    parser.add_argument("--providers", required=True, type=Path,
                        help="private prefix containing lib/{libc.so.6,ld-linux-aarch64.so.1}")
    parser.add_argument("--compiler", default=os.environ.get("AARCH64_CXX", "aarch64-linux-gnu-g++"))
    parser.add_argument("--support-libraries", type=Path, default=Path("/usr/aarch64-linux-gnu/lib"))
    parser.add_argument("--vulkan-headers", type=Path, default=Path("/usr/include"))
    parser.add_argument("--record", type=Path)
    parser.add_argument("--skip-negative-controls", action="store_true",
                        help="run only the normal fixture; recorded evidence states this limited scope")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    record = {"status": "failed", "providers": {}, "logs": {}, "negative_controls": {},
              "kernel": "host kernel through QEMU user-mode syscall/signal translation",
              "native_libc_provider": "actual private hash-pinned SteamOS 0.4.5 libc, dladdr verified on success",
              "vulkan_provider": "controlled ABI stubs, no GPU/layer device lookup",
              "frame_access": False, "native_observer_activation": False,
              "source_frame_association": False, "gpu_consumed_coherent": False}
    try:
        qemu = args.qemu.resolve(strict=True)
        prefix = args.providers.resolve(strict=True)
        record["qemu_sha256"] = digest(qemu)
        for name, expected in PINNED.items():
            path = prefix / "lib" / name
            actual = digest(path)
            record["providers"][name] = {"sha256": actual, "bytes": path.stat().st_size}
            if actual != expected:
                raise SystemExit(f"Refusing unreviewed provider {name}: SHA-256 mismatch")
        compiler = shlex.split(args.compiler)
        if not compiler or not shutil.which(compiler[0]):
            raise SystemExit("Explicit AArch64 cross compiler unavailable")
        support = args.support_libraries.resolve(strict=True)
        source_paths = [root / "tests/test_import_native_forwarding.cpp", root / "import/native_import_entry.S",
                        root / "import/import_forwarding.cpp", root / "import/import_vulkan_forwarding.cpp"]
        # Include the forwarded interfaces in the provenance, not only TU hashes.
        record["source_sha256"] = {str(path.relative_to(root)): digest(path) for path in source_paths +
            [root / "import/import_forwarding.h", root / "import/import_vulkan_forwarding.h",
             root / "import/native_import_route.h", Path(__file__).resolve()]}
        logs = record["logs"]
        with tempfile.TemporaryDirectory(prefix="frame-import-native-forwarding-") as directory:
            temporary = Path(directory)
            temporary.chmod(0o700)
            headers = temporary / "headers"
            headers.mkdir()
            # An include of the entire host /usr/include would mix host glibc
            # headers into the ARM64 build. Copy only public Vulkan/video headers.
            for name in ("vulkan", "vk_video"):
                source = args.vulkan_headers / name
                if source.is_dir():
                    shutil.copytree(source, headers / name)
            if not (headers / "vulkan/vulkan.h").is_file():
                raise SystemExit("Public Vulkan C headers unavailable")
            build_prefix = compiler + ["-std=c++17", "-O2", "-g", "-fno-omit-frame-pointer", "-pthread",
                "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I", str(root), "-I", str(headers)]
            clean_environment = os.environ.copy()
            for name in ("LD_PRELOAD", "LD_AUDIT", "LD_LIBRARY_PATH", "QEMU_LD_PREFIX"):
                clean_environment.pop(name, None)
            library_path = str(prefix / "lib") + ":" + str(support)
            execute_prefix = [str(qemu), "-L", str(prefix), "-E", "LD_LIBRARY_PATH=" + library_path]

            def build_run(label, entry_source):
                target = temporary / label
                sources = [source_paths[0], entry_source, *source_paths[2:]]
                command = build_prefix + [str(path) for path in sources] + ["-ldl", "-o", str(target)]
                compiled = recorded_run(command, temporary, logs, label + "_compile", 60)
                if compiled.returncode:
                    raise SystemExit("ARM64 build failed:\n" + compiled.stderr)
                executed = recorded_run(execute_prefix + [str(target), str(prefix / "lib/libc.so.6")],
                                        temporary, logs, label + "_execution", 20, clean_environment)
                return executed, digest(target)

            executed, elf_hash = build_run("normal", source_paths[1])
            record["fixture_elf_sha256"] = elf_hash
            if executed.returncode:
                raise SystemExit(f"ARM64 execution failed ({executed.returncode}):\n" + executed.stdout + executed.stderr)
            results = [json.loads(line[7:]) for line in executed.stdout.splitlines() if line.startswith("RESULT=")]
            expected_path = "PROVIDER_LIBC=" + str(prefix / "lib/libc.so.6")
            if len(results) != 1 or expected_path not in executed.stdout.splitlines():
                raise SystemExit("Execution omitted actual provider/result evidence")
            result = results[0]
            if not (result["aarch64_executed"] and result["veneers"] == 11 and result["libc_aliases"] == 8 and
                    result["read_write_close_forced_unwind"] and not result["native_vulkan_gpu"] and
                    not result["frame_kernel"] and not result["headset"]):
                raise SystemExit("Execution did not meet the explicitly bounded fixture scope")
            record["result"] = result
            print(executed.stdout, end="")
            if not args.skip_negative_controls:
                original = source_paths[1].read_text()
                controls = {
                    "restore_x0_instead_of_x1": ("ldr x1, [sp, #8]", "ldr x0, [sp, #8]", None),
                    # Affect only read CFI. Normal reads and the Vulkan exception
                    # must succeed before actual read cancellation reaches it.
                    "read_wrong_unwind_lr": (".cfi_offset 30, -16",
                        ".cfi_offset 30, -16\n    .ifc \\name,frame_import_read_entry\n"
                        "    .cfi_offset 30, -24\n    .endif", "CANCELLATION_BEGIN=read"),
                }
                for label, (old, replacement, reached_stage) in controls.items():
                    if original.count(old) != 1:
                        raise SystemExit("Negative-control source anchor no longer unique: " + label)
                    mutated = temporary / (label + ".S")
                    mutated.write_text(original.replace(old, replacement))
                    failure, mutant_hash = build_run(label, mutated)
                    if failure.returncode == 0 or "RESULT=" in failure.stdout:
                        raise SystemExit("Fixture accepted mutated actual ARM64 entry: " + label)
                    if reached_stage and reached_stage not in failure.stdout:
                        raise SystemExit("Unwind negative control faulted before native cancellation: " + label)
                    if not reached_stage and "native ARM64 check" not in failure.stderr:
                        raise SystemExit("Register negative control did not reach a fixture assertion: " + label)
                    record["negative_controls"][label] = {"rejected": True, "exit_status": failure.returncode,
                        "reached_stage": reached_stage, "mutated_source_sha256": digest(mutated),
                        "mutant_elf_sha256": mutant_hash}
                    print("NEGATIVE_CONTROL=" + label + ":rejected-by-actual-execution")
            record["negative_controls_skipped"] = args.skip_negative_controls
            record["status"] = "passed"
        print("ARM64 execution complete; temporary fixture ELF/header copies removed, no Frame/kernel/headset proof")
    except (SystemExit, OSError, ValueError) as error:
        record["failure"] = str(error)
        raise
    finally:
        if args.record:
            args.record.parent.mkdir(parents=True, exist_ok=True)
            args.record.write_text(json.dumps(record, indent=2) + "\n")
            args.record.chmod(0o600)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
