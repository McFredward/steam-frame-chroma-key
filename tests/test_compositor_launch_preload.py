"""Host-only real libc/loader fixture. It does not admit native Frame observations."""
import hashlib
import json
import os
from pathlib import Path
import resource
import signal
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
RECEIPT = Path(os.environ.get("FRAME_LAUNCH_TEST_RECEIPT", ROOT / "build/launch-isolation-receipts/host.json"))


def bounded_run(command, *, env=None, cwd=None, timeout=20):
    def child_limits():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               env=env, cwd=cwd, start_new_session=True, preexec_fn=child_limits)
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except BaseException:
        os.killpg(process.pid, signal.SIGKILL)
        process.communicate()
        raise
    return process.returncode, stdout.decode(errors="replace"), stderr.decode(errors="replace")


class PreloadIsolation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="frame-launch-selector-test-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.receipts = []
        cls.started = time.monotonic()
        def compile_file(output, sources, extra=()):
            command = ["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                       *extra, *map(str, sources), "-o", str(output)]
            code, out, err = bounded_run(command, timeout=30)
            if code:
                raise RuntimeError(out + err)
        cls.compile_file = staticmethod(compile_file)
        cls.launcher = cls.directory / "launcher"
        compile_file(cls.launcher, [ROOT / "tests/test_compositor_launch_child.c"], ["-ldl"])
        cls.target, cls.peer = cls.directory / "vrcompositor", cls.directory / "vrwebhelper"
        # Distinct, exact path identities; never symlinks posing as source proof.
        cls.target.write_bytes(cls.launcher.read_bytes()); cls.target.chmod(0o700)
        cls.peer.write_bytes(cls.launcher.read_bytes()); cls.peer.chmod(0o700)
        cls.payload, cls.audit, cls.selector = (cls.directory / name for name in (
            "fixture_payload.so", "fixture_audit.so", "selector.so"))
        payload_source = cls.directory / "payload.c"
        payload_source.write_text('__thread unsigned char large_tls[512] __attribute__((tls_model("initial-exec")));\n'
                                  'int frame_fixture_tls_value(void) { large_tls[511] = 37; return large_tls[511]; }\n')
        compile_file(cls.payload, [payload_source], ["-fPIC", "-shared"])
        cls.large_tls = cls.directory / "fixture_large_tls.so"
        large_tls_source = cls.directory / "large_tls.c"
        large_tls_source.write_text('__thread unsigned char large_tls[65536] __attribute__((tls_model("initial-exec")));\n'
                                    'int frame_fixture_large_tls_value(void) { large_tls[65535] = 41; return large_tls[65535]; }\n')
        compile_file(cls.large_tls, [large_tls_source], ["-fPIC", "-shared"])
        audit_source = cls.directory / "audit.c"
        audit_source.write_text('#define _GNU_SOURCE 1\n#include <link.h>\n#include <unistd.h>\n'
                                'unsigned la_version(unsigned v) { const char s[]="AUDIT_FIXTURE_LOADED\\n"; '
                                '(void)v; ssize_t wrote = write(2,s,sizeof s-1); (void)wrote; return LAV_CURRENT; }\n')
        compile_file(cls.audit, [audit_source], ["-fPIC", "-shared"])
        cls.config = {"target": str(cls.target), "selector": str(cls.selector), "payload": str(cls.payload),
                      "audit": str(cls.audit), "envelope": str(cls.directory / "envelope"),
                      "output": str(cls.directory / "output"), "journal": str(cls.directory / "journal")}
        config_source = cls.directory / "config.c"
        config_source.write_text('#include "receiver/compositor_launch_selector.h"\n'
                                 'static const struct frame_launch_config c = {1,' +
                                 ','.join(json.dumps(cls.config[key]) for key in (
                                     "target", "selector", "payload", "audit", "envelope", "output", "journal")) + '};\n'
                                 'const struct frame_launch_config *frame_compositor_launch_config_v1(void) { return &c; }\n')
        cls.mapflag = "-Wl,--version-script=" + str(ROOT / "receiver/compositor_launch_selector.map")
        compile_file(cls.selector, [ROOT / "receiver/compositor_launch_selector.c", config_source],
                     ["-fPIC", "-shared", "-fvisibility=hidden", cls.mapflag, "-ldl"])
        cls.inert = cls.directory / "inert.so"
        compile_file(cls.inert, [ROOT / "receiver/compositor_launch_selector.c"],
                     ["-fPIC", "-shared", "-fvisibility=hidden", cls.mapflag, "-ldl"])
        cls.base_env = {key: value for key, value in os.environ.items()
                        if key not in ("LD_PRELOAD", "LD_AUDIT") and
                        not key.startswith(("FRAME_MASK_TRACE", "GHVR_FRAME_COMPOSITOR_RAW_"))}
        cls.base_env.update({"LD_PRELOAD": str(cls.selector), "FOREIGN_UNRELATED": "exact bytes / with spaces"})

    @classmethod
    def tearDownClass(cls):
        # Preserve compact source hashes, controls and ELF evidence before binary cleanup.
        source_paths = [ROOT / "receiver/compositor_launch_selector.c", ROOT / "receiver/compositor_launch_selector.h",
                        ROOT / "receiver/compositor_launch_selector.map", Path(__file__),
                        ROOT / "tests/test_compositor_launch_child.c", ROOT / "tests/test_compositor_launch_selector.c"]
        RECEIPT.parent.mkdir(parents=True, exist_ok=True)
        RECEIPT.write_text(json.dumps({"schema": "host-launch-selector-1", "scope": "host libc/ELF/loader only; no Frame admission or GPU evidence",
                                      "elapsed_s": round(time.monotonic() - cls.started, 3),
                                      "source_sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in source_paths},
                                      "controls": cls.receipts}, indent=2) + "\n")
        cls.temp.cleanup()

    def run_fixture(self, arguments, *, env=None, cwd=None, expected=0):
        code, out, err = bounded_run([str(self.launcher), *map(str, arguments)], env=env or self.base_env, cwd=cwd)
        self.assertEqual(code, expected, (code, out, err))
        rows = [json.loads(line) for line in out.splitlines()]
        self.receipts.append({"test": self.id().split(".")[-1], "rows": rows,
                              "audit_loads": err.count("AUDIT_FIXTURE_LOADED"), "stderr": err[:1024]})
        return rows, err

    def assert_clean(self, row):
        self.assertEqual(row["payload_mapped"], 0)
        self.assertEqual(row["provider_mask"], 15)
        for key in ("audit", "trace", "bindings", "envelope", "output", "journal"):
            self.assertEqual(row[key], "", (key, row))
        self.assertEqual(row["preload"], str(self.selector))
        self.assertEqual(row["foreign"], "exact bytes / with spaces")

    def assert_target(self, row):
        self.assertEqual(row["payload_mapped"], 1)
        self.assertEqual(row["tls_value"], 37)  # Really accesses a 512-byte initial-exec TLS payload.
        self.assertEqual(row["provider_mask"], 15)
        self.assertEqual(row["preload"], str(self.selector) + ":" + str(self.payload))
        self.assertEqual(row["audit"], str(self.audit))
        self.assertEqual(row["trace"], "1"); self.assertEqual(row["bindings"], "1")
        for key in ("envelope", "output", "journal"):
            self.assertEqual(row[key], self.config[key])

    def test_01_exact_elf_surface_and_no_tls(self):
        code, elf, err = bounded_run(["readelf", "-W", "--dyn-syms", str(self.selector)])
        self.assertEqual(code, 0, err)
        exports = {line.split()[-1] for line in elf.splitlines() if "GLOBAL" in line and "DEFAULT" in line and "UND" not in line}
        self.assertEqual(exports, {"execve", "execv", "execvp", "execvpe", "posix_spawn", "posix_spawnp",
                                   "frame_compositor_launch_clean_environment", "frame_compositor_launch_provider_mask"})
        for forbidden in ("__tls_get_addr", "malloc", "calloc", "realloc", "_Z", "pthread"):
            self.assertNotIn(forbidden, elf)
        code, sections, _ = bounded_run(["readelf", "-W", "--sections", str(self.selector)])
        self.assertEqual(code, 0); self.assertNotIn(".tbss", sections); self.assertNotIn(".tdata", sections)
        code, dynamic, _ = bounded_run(["readelf", "-W", "--dynamic", str(self.selector)])
        self.assertEqual(code, 0); self.assertNotIn("libstdc++", dynamic)
        self.receipts.append({"test": self.id().split(".")[-1], "exports": sorted(exports), "module_tls": False,
                              "selector_sha256": hashlib.sha256(self.selector.read_bytes()).hexdigest()})

    def test_02_exec_and_spawn_hierarchy(self):
        for route in ("execve", "execv", "execvpe", "execvp", "spawn", "spawnp"):
            with self.subTest(route=route):
                rows, err = self.run_fixture(["--relay", route, self.target, "--descend", route, self.peer])
                self.assertEqual([r["tag"] for r in rows], ["launcher", "target", "descendant"])
                self.assert_clean(rows[0]); self.assert_target(rows[1]); self.assert_clean(rows[2])
                self.assertEqual(err.count("AUDIT_FIXTURE_LOADED"), 1)

    def test_03_service_wide_negative_control(self):
        dirty = dict(self.base_env, LD_PRELOAD=str(self.selector) + ":" + str(self.payload),
                     LD_AUDIT=str(self.audit), FRAME_MASK_TRACE="1", FRAME_MASK_TRACE_BINDINGS="1")
        # The old activation loads the full initial-exec DSO into an unrelated helper.
        rows, err = self.run_fixture(["--report", "old-service-wide"], env=dirty)
        self.assertEqual(rows[0]["payload_mapped"], 1); self.assertEqual(rows[0]["tls_value"], 37)
        self.assertEqual(err.count("AUDIT_FIXTURE_LOADED"), 1)
        # A repaired selector forwards that process's new helper with its diagnostics scrubbed.
        rows, err = self.run_fixture(["--relay", "spawn", self.peer, "--report", "ui"], env=dirty)
        self.assertEqual(rows[0]["payload_mapped"], 1); self.assert_clean(rows[1])
        self.assertEqual(err.count("AUDIT_FIXTURE_LOADED"), 1)

    def test_03b_real_large_tls_loader_negative(self):
        clean = dict(self.base_env, LD_PRELOAD=str(self.large_tls))
        code, out, err = bounded_run([str(self.peer), "--report", "large-tls-without-audit"], env=clean)
        self.assertEqual(code, 0, (out, err))
        dirty = dict(clean, LD_AUDIT=str(self.audit))
        code, out, err = bounded_run([str(self.peer), "--report", "large-tls-with-audit"], env=dirty)
        self.assertEqual(code, 127, (out, err))
        self.assertIn("cannot allocate memory in static TLS block", err)
        self.receipts.append({"test": self.id().split(".")[-1], "clean_result": 0,
                              "audited_result": code, "stderr": err[:2048], "tls_bytes": 65536})

    def test_04_unknown_path_and_ambiguous_search_stay_clean(self):
        rows, _ = self.run_fixture(["--relay", "execve", self.peer, "--report", "ui"])
        self.assert_clean(rows[1])
        env = dict(self.base_env, PATH=str(self.directory) + ":/bin")
        rows, _ = self.run_fixture(["--relay", "execvp", "vrcompositor", "--report", "ambiguous"], env=env)
        self.assert_clean(rows[1])

    def test_05_known_relative_and_single_search(self):
        rows, err = self.run_fixture(["--relay", "execve", "./vrcompositor", "--report", "relative"], cwd=self.directory)
        self.assert_target(rows[1]); self.assertEqual(err.count("AUDIT_FIXTURE_LOADED"), 1)
        for route in ("execvp", "execvpe", "spawnp"):
            rows, _ = self.run_fixture(["--relay", route, "vrcompositor", "--report", "single-path"],
                                       env=dict(self.base_env, PATH=str(self.directory)))
            self.assert_target(rows[1])

    def test_06_opaque_actions_cannot_change_absolute_identity(self):
        rows, _ = self.run_fixture(["--actions", "spawn", self.target, "--report", "absolute-after-chdir"])
        self.assert_target(rows[1])
        rows, _ = self.run_fixture(["--actions", "spawnp", "vrcompositor", "--report", "relative-actions"],
                                   env=dict(self.base_env, PATH=str(self.directory)))
        self.assert_clean(rows[1])

    def test_07_native_failures_and_errno_preserved(self):
        baseline = dict(self.base_env); baseline.pop("LD_PRELOAD")
        for route in ("execve", "execv", "execvpe", "execvp", "spawn", "spawnp"):
            native, _ = self.run_fixture(["--error", route], env=baseline)
            observed, _ = self.run_fixture(["--error", route])
            self.assertEqual(observed, native)

    def test_08_dirty_overflow_is_refused_without_launch(self):
        rows, _ = self.run_fixture(["--overflow", "exec"])
        self.assertEqual(rows, [{"result": -1, "errno": 7}])
        rows, _ = self.run_fixture(["--overflow", "spawn"])
        self.assertEqual(rows, [{"result": 7, "errno": 77}])

    def test_09_inert_unconfigured_selector_forwards_without_changes(self):
        env = dict(self.base_env, LD_PRELOAD=str(self.inert))
        rows, _ = self.run_fixture(["--relay", "execve", self.peer, "--report", "inert"], env=env)
        self.assertEqual(rows[1]["preload"], str(self.inert)); self.assertEqual(rows[1]["provider_mask"], 15)
        self.assertEqual(rows[1]["payload_mapped"], 0)

    def test_09b_argv_bytes_preserved(self):
        label = 'unchanged "argument" with spaces /literal'
        rows, _ = self.run_fixture(["--relay", "execve", self.target, "--report", label])
        self.assertEqual(rows[1]["tag"], label); self.assert_target(rows[1])

    def test_10_gnu_enoexec_fallback_is_native(self):
        script = self.directory / "no-shebang-script"
        script.write_text('exec "' + str(self.peer) + '" --report fallback\n'); script.chmod(0o700)
        rows, _ = self.run_fixture(["--relay", "execvpe", script, "unused-argument"])
        self.assert_clean(rows[1]); self.assertEqual(rows[1]["tag"], "fallback")


if __name__ == "__main__":
    unittest.main()
