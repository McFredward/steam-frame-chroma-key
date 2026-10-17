"""New functional-session sandbox only: no services, native loading or device contact."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('synthetic_presenter_session', ROOT / 'tools/synthetic_presenter_session.py')
mod = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(mod)
NONCE = '0123456789abcdef0123456789abcdef'


def elf(marker=mod.BUILD_MARKER, flags=4, load_marker=True):
    length = 120 + len(marker)
    raw = bytearray(length); raw[:7] = b'\x7fELF\x02\x01\x01'
    struct.pack_into('<HHI', raw, 16, 3, 183, 1)
    struct.pack_into('<Q', raw, 32, 64)
    struct.pack_into('<HHH', raw, 52, 64, 56, 1)
    span = length if load_marker else 120
    struct.pack_into('<IIQQQQQQ', raw, 64, 1, flags, 0, 0, 0, span, span, 4096)
    raw[120:] = marker
    return bytes(raw)


class SyntheticSessionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='synthetic-session-'); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name); self.home = self.root / 'home'; self.home.mkdir(mode=0o700)
        self.proc = self.root / 'proc'; self.proc.mkdir(mode=0o700)
        self.run = self.home / '.local/share/GloomhavenVR/SteamLinkMaskProbe/runs/owned-r1'
        self.session = mod.Session(self.home, self.run, proc_root=self.proc, host_fixture=True)
        self.library = self.root / 'source.so'; self.write(self.library, elf())
        self.library_sha = mod.base.digest(self.library.read_bytes())
        self.candidate = self.root / 'candidate.spv'
        self.code = b'\x03\x02\x23\x07' + b'fixture-only' + b'\0' * (mod.CANDIDATE_BYTES - 16)
        self.assertEqual(len(self.code), mod.CANDIDATE_BYTES)
        self.write(self.candidate, self.code)
        # Only sandbox tests replace the hash constant; no production argument,
        # fixture flag or receipt can relax the actual private executable pin.
        patch = mock.patch.object(mod, 'CANDIDATE_SHA256', mod.base.digest(self.code)); patch.start(); self.addCleanup(patch.stop)

    def write(self, path, raw):
        path.write_bytes(raw); path.chmod(0o600)
    def prepare(self):
        return self.session.prepare(self.library, self.library_sha, self.candidate, capture_id=NONCE)
    def process(self, line):
        p = self.proc / '101'; p.mkdir(mode=0o700, exist_ok=True); self.write(p / 'maps', line.encode())
    def status_value(self, **overrides):
        value = {'schema': 'frame-synthetic-presenter-receiver-v1', 'mode': 'synthetic_format_guarded_presenter',
            'capture_id': NONCE, 'source_ready': True, 'source_reason': 'ready', 'candidate_sha256': mod.CANDIDATE_SHA256,
            'candidate_bytes': mod.CANDIDATE_BYTES, 'observed_device': 123, 'observed_api': 4198400, 'active_devices': 1, 'pipeline_attempts': 6, 'pipeline_replacements': 2,
            'last_primitive_reason': 'none', 'definition_history_failed': False, 'revision': 1, 'updates': 1,
            'budget_exhausted': False, 'diagnostic_atomics_not_source_cohort': True, 'native_frame_association_proven': False, 'passthrough_authorized': False}
        value.update(overrides); return value

    def test_explicit_functional_scope_and_enable_is_last(self):
        calls = []; real = mod.base.write_new
        def writer(path, raw): calls.append(path); real(path, raw)
        with mock.patch.object(mod.base, 'write_new', side_effect=writer), mock.patch.object(mod.base.Session, 'arm', side_effect=AssertionError('no image capture')):
            result = self.prepare()
        self.assertEqual(calls[0], self.session.receipt); self.assertEqual(calls[-1], self.session.enable)
        self.assertEqual(self.session.enable.read_bytes(), mod.ENABLE)
        self.assertEqual(self.session.candidate.read_bytes(), self.code)
        self.assertEqual(self.session.receipt.parent, self.run.parent)
        for p in (self.run, self.run.parent): self.assertEqual(stat.S_IMODE(p.stat().st_mode), 0o700)
        for p in (self.session.receipt, self.session.enable, self.session.candidate):
            self.assertEqual(stat.S_IMODE(p.stat().st_mode), 0o600); self.assertEqual(p.stat().st_nlink, 1)
        self.assertEqual(result['scope'], mod.SCOPE); self.assertTrue(result['integrity_valid'])
        self.assertFalse(result['image_acquisition_requested']); self.assertFalse(result['automatic_restart'])
        self.assertFalse(result['passthrough_authorized']); self.assertFalse(self.session.base.arm_file.exists())

    def test_idempotent_exact_stage_preserves_all_inodes(self):
        self.prepare(); paths = (self.session.receipt, self.session.base.receipt, self.session.base.library,
            self.session.base.config, self.session.base.manifest, self.session.candidate, self.session.enable)
        before = [(p.stat().st_ino, p.read_bytes()) for p in paths]
        self.prepare(); self.assertEqual(before, [(p.stat().st_ino, p.read_bytes()) for p in paths])
        with self.assertRaises(mod.SafetyError): self.session.prepare(self.library, self.library_sha, self.candidate, capture_id='f' * 32)

    def test_resumes_own_interruption_before_or_during_base_stage(self):
        real = mod.base.write_new
        for failure in ('session-receipt.json', 'session.conf', mod.CANDIDATE_NAME, mod.ENABLE_NAME):
            with self.subTest(failure=failure):
                def writer(path, raw):
                    if path.name == failure: raise OSError('interrupted fixture stage')
                    real(path, raw)
                with mock.patch.object(mod.base, 'write_new', side_effect=writer), self.assertRaises(OSError): self.prepare()
                self.assertTrue(self.session.receipt.exists()); self.assertFalse(self.session.enable.exists())
                self.prepare(); self.assertTrue(self.session.enable.exists())
                self.assertTrue(self.session.rollback()['complete'])
                # Every subcase is a NEW run, not reuse of its consumed code group.
                self.run = self.run.with_name('owned-' + failure.replace('.', '-'))
                self.session = mod.Session(self.home, self.run, proc_root=self.proc, host_fixture=True)

    def test_passive_or_foreign_run_is_never_adopted(self):
        self.session.base.prepare(self.library, self.library_sha, capture_id=NONCE)
        with self.assertRaises(mod.SafetyError): self.prepare()
        self.assertFalse(self.session.receipt.exists()); self.assertFalse(self.session.enable.exists())
        self.assertTrue(self.session.base.manifest.exists())

    def test_passive_wrong_hash_marker_overlay_and_writable_marker_refuse(self):
        for raw in (elf(b'ordinary-passive\0'), elf(mod.BUILD_MARKER, 6), elf(mod.BUILD_MARKER, load_marker=False), elf(mod.BUILD_MARKER * 2)):
            with self.subTest(raw=raw[-48:]):
                self.write(self.library, raw)
                with self.assertRaises(mod.SafetyError): self.session.prepare(self.library, mod.base.digest(raw), self.candidate, capture_id=NONCE)
                self.assertFalse(self.session.receipt.exists())
        overflow = bytearray(elf()); struct.pack_into('<Q', overflow, 80, 0xfffffffffffffff0)
        self.write(self.library, overflow)
        with self.assertRaises(mod.SafetyError): self.session.prepare(self.library, mod.base.digest(overflow), self.candidate)
        self.assertFalse(self.session.receipt.exists())
        self.write(self.library, elf())
        with self.assertRaises(mod.SafetyError): self.session.prepare(self.library, '0' * 64, self.candidate)

    def test_wrong_candidate_hash_size_or_code_never_creates_receipt(self):
        for raw in (self.code[:-1], self.code + b'x', b'\0' * len(self.code), self.code[:100] + b'x' + self.code[101:]):
            self.write(self.candidate, raw)
            with self.assertRaises(mod.SafetyError): self.prepare()
            self.assertFalse(self.session.receipt.exists())

    def test_wrong_modes_symlinks_and_hardlinks_are_not_repaired(self):
        self.prepare()
        for path in (self.session.enable, self.session.candidate):
            with self.subTest(path=path):
                path.chmod(0o644)
                self.assertFalse(self.session.status()['integrity_valid'])
                with self.assertRaises(mod.SafetyError): self.prepare()
                self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o644)
                path.chmod(0o600)
        self.session.enable.unlink(); self.session.enable.symlink_to(self.candidate)
        self.assertFalse(self.session.status()['integrity_valid'])
        result = self.session.rollback(); self.assertFalse(result['complete']); self.assertTrue(self.session.enable.is_symlink())
        self.assertEqual(self.candidate.read_bytes(), self.code)

    def test_hardlinked_candidate_and_symlinked_inputs_refuse(self):
        alias = self.root / 'linked.spv'; alias.symlink_to(self.candidate)
        with self.assertRaises(OSError): self.session.prepare(self.library, self.library_sha, alias)
        alias.unlink(); os.link(self.candidate, alias)
        with self.assertRaises(mod.SafetyError): self.prepare()
        self.assertFalse(self.session.receipt.exists())

    def test_partial_foreign_candidate_is_preserved(self):
        real = mod.base.write_new
        def writer(path, raw):
            if path == self.session.candidate: raise OSError('pause')
            real(path, raw)
        with mock.patch.object(mod.base, 'write_new', side_effect=writer), self.assertRaises(OSError): self.prepare()
        self.write(self.session.candidate, b'foreign')
        with self.assertRaises(mod.SafetyError): self.prepare()
        self.assertEqual(self.session.candidate.read_bytes(), b'foreign')
        result = self.session.rollback(); self.assertFalse(result['complete'])
        self.assertFalse(self.session.base.manifest.exists()); self.assertFalse(self.session.base.config.exists())

    def test_changed_receipt_or_type_is_never_authority(self):
        self.prepare(); original = self.session.receipt.read_bytes()
        for change in ({'profile_revision': True}, {'scope': 'PASSIVE_DIAGNOSTIC'}, {'extra': True}):
            value = json.loads(original); value.update(change); self.write(self.session.receipt, mod.base.encoded(value))
            with self.assertRaises(mod.SafetyError): self.session.status()
            with self.assertRaises(mod.SafetyError): self.session.rollback()
            self.assertTrue(self.session.enable.exists())
        self.write(self.session.receipt, original)
        self.assertTrue(self.session.rollback()['complete'])

    def test_rollback_disables_first_but_retains_mapped_library_until_manual_restart(self):
        self.prepare(); self.process('1000-2000 r-xp 00000000 00:00 0 ' + str(self.session.base.library) + '\n')
        result = self.session.rollback(); self.assertFalse(result['complete'])
        self.assertFalse(self.session.enable.exists()); self.assertFalse(self.session.base.manifest.exists()); self.assertFalse(self.session.base.config.exists())
        self.assertTrue(self.session.base.library.exists()); self.assertTrue(self.session.candidate.exists()); self.assertTrue(self.session.consumed.exists())
        self.assertFalse(result['running_replacements_revoked']); self.assertFalse(result['automatic_restart'])
        self.process(''); self.assertTrue(self.session.rollback()['complete'])
        self.assertTrue(self.session.rollback()['complete']); self.assertFalse(self.session.base.library.exists())
        with self.assertRaises(mod.SafetyError): self.prepare()

    def test_changed_base_receipt_still_removes_own_enable_but_preserves_unknown_base(self):
        self.prepare(); self.write(self.session.base.receipt, b'{}')
        result = self.session.rollback(); self.assertFalse(result['complete'])
        self.assertFalse(self.session.enable.exists()); self.assertTrue(self.session.base.library.exists())
        self.assertTrue(self.session.base.manifest.exists())

    def test_rollback_own_reservation_before_base_creation_is_terminal(self):
        real = mod.base.write_new
        def writer(path, raw):
            if path == self.session.base.receipt: raise OSError('pause before base')
            real(path, raw)
        with mock.patch.object(mod.base, 'write_new', side_effect=writer), self.assertRaises(OSError): self.prepare()
        self.assertTrue(self.session.rollback()['complete'])
        with self.assertRaises(mod.SafetyError): self.prepare()

    def test_status_is_bounded_session_tied_and_does_not_read_images(self):
        self.prepare(); value = self.status_value(); self.write(self.session.receiver_status, mod.base.encoded(value))
        (self.run / 'snapshot.p010').symlink_to(self.candidate)
        result = self.session.status(); self.assertEqual(result['receiver_status']['state'], 'bounded_diagnostic_only')
        self.assertLess(len(mod.base.encoded(result)), 16 * 1024)
        for change in ({'capture_id': 'f' * 32}, {'candidate_sha256': '0' * 64}, {'source_ready': 1},
                       {'pipeline_replacements': True}, {'native_frame_association_proven': True}, {'observed_api': 0x100000000},
                       {'diagnostic_atomics_not_source_cohort': False}, {'updates': 33}, {'last_primitive_reason': 'made_up'}):
            self.write(self.session.receiver_status, mod.base.encoded(self.status_value(**change)))
            self.assertFalse(self.session.status()['integrity_valid'])
        self.write(self.session.receiver_status, b' ' * (mod.MAX_RECEIVER_STATUS + 1))
        self.assertFalse(self.session.status()['integrity_valid'])

    def test_status_refuses_conflicting_capture_arm_and_uncertain_process_scan(self):
        self.prepare()
        self.write(self.session.base.arm_file, mod.base.ARM_MAGIC)
        self.assertFalse(self.session.status()['integrity_valid'])
        self.session.base.arm_file.unlink()
        self.process('x' * (mod.base.MAX_MAP + 1))
        self.assertFalse(self.session.status()['integrity_valid'])
        self.process('1000-2000 r-xp 00000000 00:00 0 /foreign/' + mod.base.LIBRARY + '\n')
        self.assertFalse(self.session.status()['integrity_valid'])

    def test_used_status_or_launch_claim_cannot_restage_even_when_unmapped(self):
        self.prepare()
        self.write(self.session.receiver_status, mod.base.encoded(self.status_value()))
        with self.assertRaises(mod.SafetyError): self.prepare()
        self.session.receiver_status.unlink(); self.write(self.run / 'launch.claim', mod.base.CLAIM_MAGIC)
        with self.assertRaises(mod.SafetyError): self.prepare()

    def test_foreign_or_unknown_mappings_never_stage_or_delete_loaded_code(self):
        self.process('1000-2000 r-xp 00000000 00:00 0 /foreign/' + mod.base.LIBRARY + '\n')
        with self.assertRaises(mod.SafetyError): self.prepare()
        self.assertFalse(self.session.enable.exists())
        self.process(''); self.prepare()
        self.process('x' * (mod.base.MAX_MAP + 1))
        self.assertFalse(self.session.rollback()['complete']); self.assertTrue(self.session.base.library.exists())

    def test_unsafe_parent_run_names_and_fixture_booleans_refuse(self):
        for run in (self.run / 'nested', self.run.with_name('UPPER'), self.run.with_name('x' * 65)):
            with self.assertRaises(mod.SafetyError): mod.Session(self.home, run, proc_root=self.proc, host_fixture=True)
        with self.assertRaises(mod.SafetyError): mod.Session(self.home, self.run, proc_root=self.proc, host_fixture=1)
        self.run.parent.mkdir(mode=0o700, parents=True); self.run.parent.chmod(0o755)
        with self.assertRaises(mod.SafetyError): self.prepare()
        self.assertEqual(stat.S_IMODE(self.run.parent.stat().st_mode), 0o755)

    def test_cli_is_truthfully_functional_and_never_exposes_capture_arm_command(self):
        common = ['--home', str(self.home), '--run-dir', str(self.run), '--proc-root', str(self.proc), '--host-fixture']
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(mod.main(['prepare', *common, '--library', str(self.library), '--library-sha256', self.library_sha, '--candidate', str(self.candidate)]), 0)
            self.assertEqual(mod.main(['status', *common]), 0)
            self.assertEqual(mod.main(['rollback', *common]), 0)
        self.assertIn('SYNTHETIC_OUTPUT_REPLACEMENT', output.getvalue())
        self.assertNotIn('import socket', Path(mod.__file__).read_text())


if __name__ == '__main__': unittest.main()
