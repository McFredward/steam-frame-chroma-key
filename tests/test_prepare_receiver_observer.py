import importlib.util
import json
import os
import pathlib
import stat
import struct
import sys
import tempfile
import unittest
from unittest import mock

TOOLS = pathlib.Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location("prepare_receiver_observer", TOOLS / "prepare_receiver_observer.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PreparationTests(unittest.TestCase):
    def supported_inputs(self):
        fingerprints = [MODULE.RECEIVER_SHA256, "b" * 64, "a" * 64]
        artifacts = [({"sha256": sha, "bytes": 123, "elf_machine": 183,
                       "elf_type": 3}, b"") for sha in fingerprints]
        provider = [(name, "GLOBAL", "12") for name in MODULE.PROVIDER_SYMBOLS]
        observer = [(name, "GLOBAL", "12") for name in
                    ("pthread_mutex_lock", "pthread_mutex_unlock")]
        observer += [(name, "GLOBAL", "UND") for name in MODULE.PROVIDER_SYMBOLS[2:]]
        return artifacts, [provider, observer]

    def prepare_fixture(self):
        return MODULE.prepare(pathlib.Path("receiver"), pathlib.Path("libc"), pathlib.Path("observer"),
                              "a" * 64, pathlib.Path("/home/steamos"))

    def test_supported_files_produce_only_a_nonactivated_proposal(self):
        artifacts, symbols = self.supported_inputs()
        with mock.patch.object(MODULE, "artifact", side_effect=artifacts), \
                mock.patch.object(MODULE, "dynamic_symbols", side_effect=symbols):
            plan = self.prepare_fixture()
        self.assertTrue(plan["offline_files_supported"])
        self.assertEqual(plan["schema"], "frame-receiver-observer-proposal-v2")
        self.assertEqual(plan["receiver_service"], "steam.service")
        self.assertEqual(plan["selection_caller_returns"], ["0x355f8", "0x3591c"])
        self.assertEqual(plan["metadata_caller_returns"], ["0x538dc", "0x5390c"])
        for key in ("installed", "runtime_authorized", "image_or_mapped_uniform_reads",
                    "native_binary_patch", "kernel_or_system_image_change",
                    "shader_or_mask_activation", "automatic_restart"):
            self.assertFalse(plan[key], key)
        self.assertEqual(plan["limits"], {"selections": 32, "bytes": 1048576, "seconds": 120})

    def test_refuses_observer_abi_tls_fingerprint_and_home_changes(self):
        cases = ("lock_export", "unlock_export", "lock_alias", "unlock_alias",
                 "dynamic_tls", "fingerprint", "relative_home", "parent_home", "missing_libc_version")
        for case in cases:
            with self.subTest(case=case):
                artifacts, symbols = self.supported_inputs()
                homes = {"relative_home": pathlib.Path("home/steamos"),
                         "parent_home": pathlib.Path("/home/../steamos")}
                names = {"lock_export": "pthread_mutex_lock", "unlock_export": "pthread_mutex_unlock",
                         "lock_alias": "__pthread_mutex_lock@GLIBC_2.17",
                         "unlock_alias": "__pthread_mutex_unlock@GLIBC_2.17"}
                if case in names:
                    symbols[1] = [s for s in symbols[1] if s[0] != names[case]]
                elif case == "dynamic_tls":
                    symbols[1].append(("__tls_get_addr@GLIBC_2.17", "GLOBAL", "UND"))
                elif case == "fingerprint":
                    artifacts[2][0]["sha256"] = "c" * 64
                elif case == "missing_libc_version":
                    symbols[1].append(("pthread_create@GLIBC_2.34", "GLOBAL", "UND"))
                with mock.patch.object(MODULE, "artifact", side_effect=artifacts), \
                        mock.patch.object(MODULE, "dynamic_symbols", side_effect=symbols):
                    with self.assertRaises(MODULE.CollectionError):
                        MODULE.prepare(pathlib.Path("receiver"), pathlib.Path("libc"), pathlib.Path("observer"),
                                       "a" * 64, homes.get(case, pathlib.Path("/home/steamos")))

    def test_wrong_receiver_fingerprint_is_refused_before_provider(self):
        with mock.patch.object(MODULE, "artifact", return_value=({"sha256": "0" * 64}, b"")), \
                mock.patch.object(MODULE, "dynamic_symbols") as symbols:
            with self.assertRaises(MODULE.CollectionError):
                MODULE.prepare(pathlib.Path("receiver"), pathlib.Path("libc"), pathlib.Path("observer"),
                               "0" * 64, pathlib.Path("/home/steamos"))
            symbols.assert_not_called()

    def test_pre_update_receiver_is_refused_before_provider_or_observer_work(self):
        old = "0000009e5c7124bb03bfd444f7200304e0bb3ec1ec0cf80f0086a19ec048132b"
        with mock.patch.object(MODULE, "artifact", return_value=({"sha256": old}, b"")) as files, \
                mock.patch.object(MODULE, "dynamic_symbols") as symbols:
            with self.assertRaisesRegex(MODULE.CollectionError, "Unsupported receiver"):
                self.prepare_fixture()
            files.assert_called_once_with(pathlib.Path("receiver"), MODULE.MAX_INPUT_BYTES)
            symbols.assert_not_called()

    def test_expected_aarch64_elf_header_is_required(self):
        with tempfile.TemporaryDirectory() as folder:
            file = pathlib.Path(folder) / "artifact"
            raw = bytearray(64)
            raw[:7] = b"\x7fELF\x02\x01\x01"
            struct.pack_into("<HH", raw, 16, 3, 183)
            file.write_bytes(raw)
            self.assertEqual(MODULE.artifact(file, 64)[0]["elf_machine"], 183)
            struct.pack_into("<H", raw, 18, 62)
            file.write_bytes(raw)
            with self.assertRaises(MODULE.CollectionError):
                MODULE.artifact(file, 64)
            file.write_bytes(raw + b"x")
            with self.assertRaises(MODULE.CollectionError):
                MODULE.artifact(file, 64)

    def test_refuses_missing_provider_alias(self):
        info = {"sha256": MODULE.RECEIVER_SHA256}
        with mock.patch.object(MODULE, "artifact", return_value=(info, b"")), \
                mock.patch.object(MODULE, "dynamic_symbols", return_value=[]):
            with self.assertRaises(MODULE.CollectionError):
                MODULE.prepare(pathlib.Path("receiver"), pathlib.Path("libc"), pathlib.Path("observer"),
                               "0" * 64, pathlib.Path("/home/steamos"))

    def test_refuses_each_undefined_or_missing_provider_alias(self):
        for symbol in MODULE.PROVIDER_SYMBOLS:
            for missing in (False, True):
                with self.subTest(symbol=symbol, missing=missing):
                    artifacts, symbols = self.supported_inputs()
                    symbols[0] = [(name, binding, "UND" if name == symbol else index)
                                  for name, binding, index in symbols[0]
                                  if not (missing and name == symbol)]
                    with mock.patch.object(MODULE, "artifact", side_effect=artifacts), \
                            mock.patch.object(MODULE, "dynamic_symbols", side_effect=symbols):
                        with self.assertRaises(MODULE.CollectionError):
                            self.prepare_fixture()

    def test_fifo_is_refused_without_waiting_for_a_writer(self):
        with tempfile.TemporaryDirectory() as folder:
            fifo = pathlib.Path(folder) / "fifo"
            os.mkfifo(fifo)
            with self.assertRaises(MODULE.CollectionError):
                MODULE.artifact(fifo, 64)

    def test_loader_definition_and_weak_libc_definition_are_supported(self):
        artifacts, symbols = self.supported_inputs()
        symbols[0].append(("close@@GLIBC_2.17", "WEAK", "12"))
        symbols[1] += [("close@GLIBC_2.17", "GLOBAL", "UND"),
                       ("__stack_chk_guard@GLIBC_2.17", "GLOBAL", "UND")]
        artifacts.append(({"sha256": "d" * 64, "elf_machine": 183, "elf_type": 3}, b""))
        symbols.append([("__stack_chk_guard@@GLIBC_2.17", "GLOBAL", "15")])
        with mock.patch.object(MODULE, "artifact", side_effect=artifacts), \
                mock.patch.object(MODULE, "dynamic_symbols", side_effect=symbols):
            plan = MODULE.prepare(pathlib.Path("receiver"), pathlib.Path("libc"), pathlib.Path("observer"),
                                  "a" * 64, pathlib.Path("/home/steamos"), loader=pathlib.Path("loader"))
        self.assertEqual(plan["loader"]["sha256"], "d" * 64)
        self.assertIn("__stack_chk_guard@GLIBC_2.17", plan["provider"]["verified_observer_libc_imports"])

    def test_new_plan_does_not_overwrite_or_follow_symlink(self):
        with tempfile.TemporaryDirectory() as folder:
            output = pathlib.Path(folder) / "plan.json"
            MODULE.write_new_private(output, {"installed": False})
            self.assertEqual(json.loads(output.read_text()), {"installed": False})
            self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o600)
            with self.assertRaises(FileExistsError):
                MODULE.write_new_private(output, {})
            alias = pathlib.Path(folder) / "alias.json"
            alias.symlink_to(output)
            with self.assertRaises(FileExistsError):
                MODULE.write_new_private(alias, {})
            self.assertEqual(json.loads(output.read_text()), {"installed": False})


if __name__ == "__main__":
    unittest.main()
