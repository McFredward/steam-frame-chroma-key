#!/usr/bin/env python3
"""Real temp-filesystem installer controls; fake runtime/unit manager, no device."""
import contextlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('owned_setup', ROOT / 'tools/native_passthrough_setup.py')
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)


class System:
    def __init__(self):
        self.live = False
        self.reloads = 0
        self.native_calls = []
        self.settings = None
        self.persist = True

    def reload(self):
        self.reloads += 1
        return True

    def running(self):
        return self.live

    @contextlib.contextmanager
    def camera(self, package, metadata, runtime):
        self.native_calls.append('open-pinned')
        owner = self
        class Api:
            def write(self, value):
                owner.native_calls.append(('set', value))
                if owner.persist:
                    data = s.load(owner.settings)
                    data.setdefault('camera', {})[s.KEY] = value
                    s.put(owner.settings, s.encoded(data), s.read(owner.settings))
            def remove_override(self):
                owner.native_calls.append('remove-key')
                if owner.persist:
                    data = s.load(owner.settings)
                    data.setdefault('camera', {}).pop(s.KEY, None)
                    s.put(owner.settings, s.encoded(data), s.read(owner.settings))
        yield Api()


class SetupTests(unittest.TestCase):
    def setUp(self):
        environment = mock.patch.dict(os.environ, {'VR_CONFIG_PATH': '', 'VR_PATHREG_OVERRIDE': '', 'XDG_CONFIG_HOME': ''})
        environment.start()
        self.addCleanup(environment.stop)
        self.temp = tempfile.TemporaryDirectory(prefix='sfnp-setup-owned-')
        self.root = Path(self.temp.name)
        self.home = self.root / 'home'
        self.home.mkdir(mode=0o700)
        self.runtime = self.root / 'runtime'
        self.runtime.mkdir(mode=0o700)
        self.package = self.root / 'input'
        self.package.mkdir(mode=0o700)
        artifacts = {s.LIBRARY: b'OWNED-LIBRARY-NOT-A-NATIVE-PROOF', 'scene-presenter.spv': b'OWNED-SPV',
                     'tools/prepare_native_release.py': b'# owned fake verifier, never executed\n',
                     'tools/steamvr_camera_alpha.py': b'# owned fake camera, never executed\n',
                     'tools/pixel_snapshot_session.py': b'# owned fixture\n', 'pc/vrsettings_win.py': b'# fixture\n',
                     'compatibility.json': s.encoded({'runtime_files': {'openvr': {'sha256': '0' * 64}}})}
        self.metadata = {'schema': 'sfnp-native-package-v1', 'files': {n: {'bytes': len(b), 'sha256': s.digest(b)} for n, b in artifacts.items()}}
        for name, raw in {**artifacts, 'package.json': s.encoded(self.metadata)}.items():
            (self.package / name).parent.mkdir(parents=True, exist_ok=True, mode=0o700)
            s.put(self.package / name, raw)
        self.unit = System()
        self.compatible = True
        self.checks = 0
        def verify(package, runtime):
            self.checks += 1
            if not self.compatible:
                raise s.SafetyError('unsupported runtime fingerprint: explicit owned fixture')
            manifest = s.load(package / 'package.json')
            for name, record in manifest['files'].items():
                raw = s.read(package / name)
                if s.digest(raw) != record['sha256']:
                    raise s.SafetyError('artifact hash mismatch')
            return manifest
        self.setup = s.Setup(self.home, system=self.unit, verifier=verify, runtime_factory=lambda: {'openvr': self.root / 'pinned'}, runtime_dir=self.runtime)
        self.settings = self.home / '.config/openvr/config/steamvr.vrsettings'
        os.close(s.directory(self.settings.parent, create=True))
        self.unit.settings = self.settings
        s.put(self.settings, s.encoded({'unrelated': {'keep': 42}, 'camera': {'other': 'keep'}}))
        self.secondary_settings = self.home / '.local/share/Steam/config/steamvr.vrsettings'
        os.close(s.directory(self.secondary_settings.parent, create=True))
        s.put(self.secondary_settings, s.encoded({'second_root_untouched': True, 'camera': {s.KEY: False}}))
        self.registry = self.home / '.config/openvr/openvrpaths.vrpath'
        s.put(self.registry, s.encoded({'config': [str(self.settings.parent), str(self.secondary_settings.parent)], 'runtime': ['/opt/steamvr']}))
        self.registry.chmod(0o755)
        os.close(s.directory(self.setup.config, create=True, private=True))

    def tearDown(self):
        self.temp.cleanup()

    def install(self):
        return self.setup.install(self.package)

    def test_repeat_install_preserves_disabled_and_other_dropin(self):
        foreign = self.setup.dropin.with_name('10-other.conf')
        os.close(s.directory(foreign.parent, create=True))
        foreign.write_text('[Service]\nEnvironment=OTHER=1\n')
        self.install()
        self.assertTrue(self.setup.manifest.is_symlink())
        self.assertEqual(os.readlink(self.setup.manifest), str(self.setup.target))
        self.assertFalse(self.setup.manifest.exists())
        self.setup.disable()
        self.install()
        self.assertFalse(self.setup.status()['enabled'])
        self.assertIn('OTHER=1', foreign.read_text())
        unit = self.setup.dropin.read_text()
        for key in ('RuntimeDirectory=steam-frame-native-passthrough', 'RuntimeDirectoryMode=0700', 'RuntimeDirectoryPreserve=no', 'ExecCondition=-/usr/bin/python3', 'ExecStopPost=-/usr/bin/python3'):
            self.assertIn(key, unit)
        self.assertNotIn('Restart=', unit)
        self.assertNotIn('ExecStart=\n', unit)

    def test_launch_then_normal_stop_then_fresh_launch(self):
        self.install()
        self.assertEqual(self.setup.prepare()['reason'], 'prepared')
        launch = s.load(self.setup.inflight)
        self.assertTrue(self.setup.target.exists())
        self.assertEqual(self.setup.session.read_bytes(), b'GHVR_PIXEL_SNAPSHOT/1\n' + launch['capture_id'].encode() + b'\n' + launch['directory'].encode() + b'\n')
        run = Path(launch['directory'])
        self.assertEqual(run.parent, self.runtime / 'runs')
        self.assertEqual(run.stat().st_mode & 0o777, 0o700)
        self.assertEqual((run / 'scene-presenter.enable').read_bytes(), b'GHVR_SCENE_PRESENTER/1\n')
        self.assertTrue(s.load(self.settings)['camera'][s.KEY])
        self.assertEqual(self.setup.finish()['reason'], 'stopped')
        self.assertFalse(self.setup.target.exists())
        self.assertTrue(self.setup.manifest.is_symlink())
        self.assertNotIn(s.KEY, s.load(self.settings)['camera'])
        self.assertEqual(self.setup.prepare()['reason'], 'prepared')
        self.assertNotEqual(s.load(self.setup.inflight)['capture_id'], launch['capture_id'])

    def test_runtime_update_retains_install_but_removes_injection(self):
        self.install()
        self.compatible = False
        result = self.setup.prepare()
        self.assertEqual(result['reason'], 'stock_fallback')
        self.assertIn('unsupported runtime', result['reason_detail'])
        self.assertFalse(self.setup.target.exists())
        self.assertFalse(self.setup.inflight.exists())
        self.assertTrue(self.setup.status()['installed'])
        self.assertFalse(self.setup.status()['compatible'])
        self.assertEqual(self.unit.native_calls, [])

    def test_powerloss_dangling_link_and_persistent_crash_latch(self):
        self.install()
        self.setup.prepare()
        shutil.rmtree(self.runtime)  # Models RuntimeDirectory teardown/reboot, not production removal.
        self.assertFalse(self.setup.manifest.exists())
        self.assertTrue(self.setup.manifest.is_symlink())
        self.runtime.mkdir(mode=0o700)
        result = self.setup.prepare()
        self.assertEqual(result['reason'], 'unclean_previous_launch')
        self.assertFalse(self.setup.status()['enabled'])
        self.setup.finish()
        self.assertEqual(self.setup.prepare()['reason'], 'disabled')
        self.setup.enable()
        self.assertEqual(self.setup.prepare()['reason'], 'prepared')

    def test_abnormal_stop_requires_explicit_enable(self):
        self.install()
        self.setup.prepare()
        self.assertEqual(self.setup.finish('signal')['reason'], 'abnormal_stop_disabled')
        self.assertFalse(self.setup.status()['enabled'])
        self.assertEqual(self.setup.prepare()['reason'], 'disabled')
        self.setup.enable()
        self.assertEqual(self.setup.prepare()['reason'], 'prepared')

    def test_each_camera_baseline_and_unrelated_fields(self):
        for baseline in (None, False, True):
            with self.subTest(baseline=baseline):
                # A new receipt is required per original baseline, never overwrite one.
                if self.setup.camera_receipt.exists():
                    self.setup.camera_receipt.unlink()
                data = s.load(self.settings)
                if baseline is None:
                    data['camera'].pop(s.KEY, None)
                else:
                    data['camera'][s.KEY] = baseline
                s.put(self.settings, s.encoded(data), s.read(self.settings))
                self.setup._camera(True)
                before = s.load(self.setup.camera_receipt)
                self.assertEqual(before['previous'], {'present': baseline is not None, 'value': baseline})
                self.assertEqual(self.setup._camera(False)['restored'], True)
                after = s.load(self.settings)
                self.assertEqual(after['camera'].get(s.KEY), baseline)
                self.assertEqual(after['camera']['other'], 'keep')
                self.assertEqual(after['unrelated'], {'keep': 42})

    def test_camera_receipt_precedes_write(self):
        original = s.put
        writes = []
        def observed(path, raw, *args, **kwargs):
            if path == self.settings:
                self.assertTrue(self.setup.camera_receipt.exists())
                self.assertTrue(s.load(self.setup.camera_receipt)['write_intent'])
            writes.append(path)
            return original(path, raw, *args, **kwargs)
        s.put = observed
        try:
            self.setup._camera(True)
        finally:
            s.put = original
        self.assertLess(writes.index(self.setup.camera_receipt), writes.index(self.settings))

    def test_changed_camera_value_preserved_without_api(self):
        self.install()
        self.setup.prepare()
        data = s.load(self.settings)
        data['camera'][s.KEY] = False  # Original absent; false is a foreign explicit edit.
        s.put(self.settings, s.encoded(data), s.read(self.settings))
        result = self.setup.disable(True)
        self.assertFalse(result['camera']['restored'])
        self.assertFalse(self.setup.target.exists())
        self.assertFalse(self.setup.manifest.is_symlink())
        self.assertFalse(s.load(self.settings)['camera'][s.KEY])
        self.assertEqual(self.unit.native_calls, [])

    def test_original_true_no_write_does_not_adopt_foreign_false(self):
        data = s.load(self.settings)
        data['camera'][s.KEY] = True
        s.put(self.settings, s.encoded(data), s.read(self.settings))
        self.setup._camera(True)
        data['camera'][s.KEY] = False
        s.put(self.settings, s.encoded(data), s.read(self.settings))
        with self.assertRaises(s.SafetyError):
            self.setup._camera(False)
        self.assertFalse(s.load(self.settings)['camera'][s.KEY])

    def test_live_restore_pending_retains_stop_helper(self):
        self.install()
        self.setup.prepare()
        self.unit.live = True
        self.unit.persist = False
        result = self.setup.uninstall()
        self.assertEqual(result['reason'], 'uninstall_pending')
        self.assertFalse(result['camera']['restored'])
        self.assertTrue(result['camera']['persistence_pending'])
        self.assertTrue(self.setup.dropin.exists())
        self.assertFalse(self.setup.target.exists())
        self.assertIn('remove-key', self.unit.native_calls)
        self.unit.live = False
        result = self.setup.uninstall()
        self.assertEqual(result['reason'], 'uninstalled')
        self.assertEqual(self.setup.uninstall()['reason'], 'not_installed')
        self.assertNotIn(s.KEY, s.load(self.settings)['camera'])

    def test_unknown_live_provider_never_called_emergency_still_revokes(self):
        self.install()
        self.setup.prepare()
        self.unit.live = True
        self.compatible = False
        result = self.setup.disable(True)
        self.assertFalse(result['camera']['restored'])
        self.assertFalse(self.setup.manifest.is_symlink())
        self.assertFalse(self.setup.target.exists())
        self.assertEqual(self.unit.native_calls, [])
        self.assertFalse(self.setup.status()['enabled'])

    def test_foreign_dropin_install_uninstall_preserve(self):
        os.close(s.directory(self.setup.dropin.parent, create=True))
        self.setup.dropin.write_text('foreign')
        with self.assertRaises(s.SafetyError):
            self.install()
        self.assertEqual(self.setup.dropin.read_text(), 'foreign')
        self.setup.dropin.unlink()
        self.install()
        self.setup.dropin.write_text('later foreign')
        with self.assertRaises(s.SafetyError):
            self.setup.uninstall()
        self.assertEqual(self.setup.dropin.read_text(), 'later foreign')

    def test_legacy_collision_before_any_install_write(self):
        old = self.home / '.local/share/GloomhavenVR/SteamLinkMaskProbe/session.conf'
        os.close(s.directory(old.parent, create=True))
        old.write_text('old experiment')
        with self.assertRaisesRegex(s.SafetyError, 'original operator/receipt'):
            self.install()
        self.assertFalse(self.setup.receipt.exists())
        self.assertEqual(old.read_text(), 'old experiment')

    def test_missing_settings_keeps_stock_and_no_synthetic_baseline(self):
        self.install()
        self.settings.unlink()
        result = self.setup.prepare()
        self.assertEqual(result['reason'], 'stock_fallback')
        self.assertFalse(self.setup.target.exists())
        self.assertFalse(self.setup.camera_receipt.exists())

    def test_foreign_manifest_preserved_on_disable_but_owned_target_revoked(self):
        self.install()
        self.setup.prepare()
        self.setup.manifest.unlink()
        self.setup.manifest.write_text('FOREIGN MANIFEST')
        with self.assertRaises(s.SafetyError):
            self.setup.disable(True)
        self.assertEqual(self.setup.manifest.read_text(), 'FOREIGN MANIFEST')
        self.assertFalse(self.setup.target.exists())

    def test_bounded_history_and_owned_runs(self):
        self.install()
        captures = []
        for _ in range(6):
            self.assertEqual(self.setup.prepare()['reason'], 'prepared')
            captures.append(s.load(self.setup.inflight)['capture_id'])
            self.setup.finish()
        self.assertEqual(len(set(captures)), 6)
        self.assertEqual(len(s.load(self.setup.history)['launches']), 3)
        self.assertLessEqual(len(list(self.setup.runs.iterdir())), 3)

    def test_changed_package_or_nonprivate_runtime_refuses_before_publication(self):
        self.install()
        (self.setup.package / s.LIBRARY).write_bytes(b'changed')
        self.assertEqual(self.setup.prepare()['reason'], 'stock_fallback')
        self.assertFalse(self.setup.target.exists())
        self.assertFalse(self.setup.camera_receipt.exists())

    def test_no_runtime_directory_no_injection(self):
        self.install()
        self.runtime.rmdir()
        self.assertEqual(self.setup.prepare()['reason'], 'stock_fallback')
        self.assertFalse(self.setup.target.exists())
        self.assertFalse(self.setup.camera_receipt.exists())

    def test_stopped_package_upgrade_and_repeat_preserve_preference(self):
        self.install()
        self.setup.disable()
        new = b'OWNED UPDATED LIBRARY'
        (self.package / s.LIBRARY).write_bytes(new)
        self.metadata['files'][s.LIBRARY] = {'bytes': len(new), 'sha256': s.digest(new)}
        s.put(self.package / 'package.json', s.encoded(self.metadata), s.read(self.package / 'package.json'))
        self.assertEqual(self.install()['reason'], 'installed')
        self.assertFalse(self.setup.status()['enabled'])
        self.assertEqual((self.setup.package / s.LIBRARY).read_bytes(), new)
        self.assertFalse(self.setup.upgrade.exists())
        self.assertEqual(self.install()['reason'], 'installed')

    def test_live_update_refused_and_interrupted_upgrade_resumes_without_injection(self):
        self.install()
        new = b'OWNED NEW VERSION'
        (self.package / s.LIBRARY).write_bytes(new)
        self.metadata['files'][s.LIBRARY] = {'bytes': len(new), 'sha256': s.digest(new)}
        s.put(self.package / 'package.json', s.encoded(self.metadata), s.read(self.package / 'package.json'))
        self.unit.live = True
        with self.assertRaisesRegex(s.SafetyError, 'Stop SteamVR'):
            self.install()
        self.unit.live = False
        original = s.put
        def interrupt(path, raw, *args, **kwargs):
            if path == self.setup.package / s.LIBRARY:
                raise OSError('injected interruption before version write')
            return original(path, raw, *args, **kwargs)
        s.put = interrupt
        try:
            with self.assertRaises(OSError):
                self.install()
        finally:
            s.put = original
        self.assertTrue(self.setup.upgrade.exists())
        self.assertEqual(self.setup.prepare()['reason'], 'interrupted_upgrade')
        self.assertFalse(self.setup.target.exists())
        self.assertEqual(self.install()['reason'], 'installed')
        self.assertFalse(self.setup.upgrade.exists())
        self.assertEqual((self.setup.package / s.LIBRARY).read_bytes(), new)

    def test_foreign_runtime_permissions_and_nonboolean_camera_refuse(self):
        self.install()
        self.runtime.chmod(0o775)
        self.assertEqual(self.setup.prepare()['reason'], 'stock_fallback')
        self.assertFalse(self.setup.target.exists())
        self.assertFalse(self.setup.camera_receipt.exists())
        self.runtime.chmod(0o700)
        data = s.load(self.settings)
        data['camera'][s.KEY] = 1
        s.put(self.settings, s.encoded(data), s.read(self.settings))
        self.assertEqual(self.setup.prepare()['reason'], 'stock_fallback')
        self.assertEqual(s.load(self.settings)['camera'][s.KEY], 1)
        self.assertFalse(self.setup.target.exists())

    def test_installed_entrypoint_emergency_off_does_not_import_verifier(self):
        self.install()
        self.setup.prepare()
        (self.setup.package / 'tools/prepare_native_release.py').write_text('raise Exception("MUST NOT IMPORT")')
        # Real installed CLI, no fake runtime verifier. The temporary injected
        # runtime namespace cannot be selected by a production CLI, so only the
        # dangling-link baseline can be used here; camera is already restored.
        self.setup.finish()
        self.setup.manifest.unlink()
        receipt = s.load(self.setup.receipt)
        receipt['manifest_link'] = str(Path('/run/user') / str(os.getuid()) / 'steam-frame-native-passthrough/receiver.json')
        s.put(self.setup.receipt, s.encoded(receipt), s.read(self.setup.receipt))
        script = self.setup.tools / 'native_passthrough_setup.py'
        result = subprocess.run([sys.executable, '-B', str(script), 'emergency-off', '--home', str(self.home)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['reason'], 'emergency_off')
        self.assertFalse(s.load(self.setup.control)['enabled'])
        self.assertNotIn('MUST NOT IMPORT', result.stderr)

    def test_cross_process_emergency_while_long_build_lock_held(self):
        self.install()
        self.setup.prepare()
        # Clear only the volatile staged session so the production CLI need not
        # select this test namespace. A real existing owned symlink uses the
        # exact production target; no /run content is touched by this fixture.
        self.setup.finish()
        self.setup.manifest.unlink()
        receipt = s.load(self.setup.receipt)
        production_target = Path('/run/user') / str(os.getuid()) / 'steam-frame-native-passthrough/receiver.json'
        receipt['manifest_link'] = str(production_target)
        s.put(self.setup.receipt, s.encoded(receipt), s.read(self.setup.receipt))
        self.setup.manifest.symlink_to(production_target)
        with self.setup.locked():
            child = subprocess.run([sys.executable, '-B', str(self.setup.tools / 'native_passthrough_setup.py'), 'emergency-off', '--home', str(self.home)], capture_output=True, text=True, timeout=5)
            self.assertEqual(child.returncode, 0, child.stderr)
            result = json.loads(child.stdout)
            self.assertEqual(result['reason'], 'emergency_off_requested')
            self.assertTrue(result['camera_restore_pending'])
            self.assertFalse(self.setup.manifest.is_symlink())
            self.assertEqual(self.setup.revocation.read_bytes(), s.REVOCATION)
        # The still-running parent installer must not re-enable/recreate a link.
        receipt['manifest_link'] = str(self.setup.target)
        s.put(self.setup.receipt, s.encoded(receipt), s.read(self.setup.receipt))
        self.assertFalse(self.install()['enabled'])
        self.assertFalse(self.setup.manifest.is_symlink())
        self.assertEqual(self.setup.prepare()['reason'], 'disabled')
        self.setup.enable()
        self.assertFalse(self.setup.revocation.exists())
        self.assertEqual(self.setup.prepare()['reason'], 'prepared')

    def test_emergency_during_final_publication_revokes_before_pre_returns(self):
        self.install()
        original = s.put
        def emergency_after_publication(path, raw, *args, **kwargs):
            result = original(path, raw, *args, **kwargs)
            if path == self.setup.target:
                self.setup.request_emergency()
            return result
        s.put = emergency_after_publication
        try:
            result = self.setup.prepare()
        finally:
            s.put = original
        self.assertEqual(result['reason'], 'stock_fallback')
        self.assertFalse(self.setup.manifest.is_symlink())
        self.assertFalse(self.setup.target.exists())
        self.assertFalse(self.setup._control()['enabled'])
        self.assertNotIn(s.KEY, s.load(self.settings)['camera'])

    def test_foreign_emergency_marker_not_removed_or_adopted(self):
        self.install()
        self.setup.revocation.write_bytes(b'FOREIGN')
        with self.assertRaises(s.SafetyError):
            self.setup.request_emergency()
        self.assertEqual(self.setup.revocation.read_bytes(), b'FOREIGN')
        self.assertEqual(self.setup.prepare()['reason'], 'stock_fallback')
        self.assertFalse(self.setup.target.exists())

    def test_automatic_install_reuses_package_and_preserves_failed_preparation_evidence(self):
        owner = self
        class Engine:
            calls = 0
            fail = False
            def runtime_files(self, home):
                return {'openvr': owner.root / 'pinned'}
            def verify_package(self, package, runtime):
                return owner.setup.verifier(package, runtime)
            def prepare(self, package):
                self.calls += 1
                evidence = package.parent / 'preparation-result.json'
                s.put(evidence, s.encoded({'result': 'failure' if self.fail else 'success'}))
                if self.fail:
                    raise s.SafetyError('injected local build failure; evidence ' + str(evidence))
                shutil.copytree(owner.package, package)
        engine = Engine()
        self.setup._engine = lambda: engine
        self.assertEqual(self.setup.install_automatic()['reason'], 'installed')
        self.assertEqual(engine.calls, 1)
        attempts = list((self.setup.data / 'preparation').iterdir())
        self.assertEqual(len(attempts), 1)
        self.assertTrue((attempts[0] / 'preparation-result.json').exists())
        self.assertFalse((attempts[0] / 'package').exists())
        self.setup.disable()
        self.assertFalse(self.setup.install_automatic()['enabled'])
        self.assertEqual(engine.calls, 1)
        self.assertEqual(len(list((self.setup.data / 'preparation').iterdir())), 1)
        engine.fail = True
        self.compatible = False
        with self.assertRaisesRegex(s.SafetyError, 'injected local build failure'):
            self.setup.install_automatic()
        failures = [p for p in (self.setup.data / 'preparation').iterdir() if s.load(p / 'preparation-result.json')['result'] == 'failure']
        self.assertEqual(len(failures), 1)
        self.assertFalse(self.setup.target.exists())

    def test_legacy_chroma_manifest_refused_but_disabled_library_backup_preserved(self):
        legacy = self.setup.manifest.with_name('mrchroma.json')
        os.close(s.directory(legacy.parent, create=True))
        legacy.write_bytes(b'FOREIGN LEGACY MANIFEST')
        backup = self.home / '.local/share/mrchroma/libVkLayer_mrchroma.so'
        os.close(s.directory(backup.parent, create=True))
        backup.write_bytes(b'DISABLED BACKUP')
        with self.assertRaisesRegex(s.SafetyError, 'legacy mrchroma'):
            self.install()
        self.assertEqual(legacy.read_bytes(), b'FOREIGN LEGACY MANIFEST')
        self.assertEqual(backup.read_bytes(), b'DISABLED BACKUP')
        self.assertFalse(self.setup.receipt.exists())
        legacy.unlink()  # Explicit fixture removal, never a production migration.
        self.assertEqual(self.install()['reason'], 'installed')
        self.assertEqual(backup.read_bytes(), b'DISABLED BACKUP')

    def test_steam_mode666_roundtrip_preserves_each_baseline_and_mode(self):
        for baseline in (None, False, True):
            with self.subTest(baseline=baseline):
                if self.setup.camera_receipt.exists():
                    self.setup.camera_receipt.unlink()
                data = {'unrelated': {'keep': 42}, 'camera': {'other': 'keep'}}
                if baseline is not None:
                    data['camera'][s.KEY] = baseline
                self.settings.write_bytes(s.encoded(data))
                self.settings.chmod(0o666)
                self.setup._camera(True)
                self.assertTrue(s.json_bytes(self.settings.read_bytes())['camera'][s.KEY])
                self.assertEqual(self.settings.stat().st_mode & 0o777, 0o666)
                self.assertTrue(self.setup._camera(False)['restored'])
                self.assertEqual(s.json_bytes(self.settings.read_bytes()), data)
                self.assertEqual(self.settings.stat().st_mode & 0o777, 0o666)
                self.assertEqual(self.setup.camera_receipt.stat().st_mode & 0o777, 0o600)

    def test_steam_mode666_complete_prepare_finish_with_registered0755_path(self):
        registry = self.home / '.config/openvr/openvrpaths.vrpath'
        os.close(s.directory(registry.parent, create=True))
        s.put(registry, s.encoded({'config': [str(self.settings.parent)]}), s.read(registry))
        registry.chmod(0o755)
        self.settings.chmod(0o666)
        self.install()
        self.assertEqual(self.setup.prepare()['reason'], 'prepared')
        self.assertTrue(self.setup.target.exists())
        self.assertTrue(self.setup.finish()['camera']['restored'])
        self.assertNotIn(s.KEY, s.json_bytes(self.settings.read_bytes())['camera'])
        self.assertEqual(self.settings.stat().st_mode & 0o777, 0o666)
        self.assertEqual(registry.stat().st_mode & 0o777, 0o755)
        self.assertEqual(self.unit.native_calls, [])

    def test_mode666_private_artifacts_stay_refused(self):
        self.install()
        for path in (self.setup.receipt, self.setup.control, self.setup.package / s.LIBRARY):
            with self.subTest(path=path.name):
                raw = path.read_bytes()
                path.chmod(0o666)
                with self.assertRaises(s.SafetyError):
                    s.read(path)
                with self.assertRaises(s.SafetyError):
                    s.put(path, b'NOT ALLOWED', raw)
                with self.assertRaises(s.SafetyError):
                    s.read(path, writable_data=True)
                self.assertEqual(path.read_bytes(), raw)
                path.chmod(0o600)

    def test_mode666_expected_mismatch_and_foreign_camera_edit_preserved(self):
        self.settings.chmod(0o666)
        raw = self.settings.read_bytes()
        with self.assertRaises(s.SafetyError):
            s.put(self.settings, b'NOT ALLOWED', raw + b'changed', writable_data=True)
        self.assertEqual(self.settings.read_bytes(), raw)
        self.setup._camera(True)
        changed = s.json_bytes(self.settings.read_bytes())
        changed['camera'][s.KEY] = False  # Original absent; this is a foreign override.
        self.settings.write_bytes(s.encoded(changed))
        with self.assertRaises(s.SafetyError):
            self.setup._camera(False)
        self.assertEqual(s.json_bytes(self.settings.read_bytes()), changed)
        self.assertEqual(self.settings.stat().st_mode & 0o777, 0o666)

    def test_mode666_changed_inode_after_read_is_not_adopted(self):
        self.settings.chmod(0o666)
        expected = self.settings.read_bytes()
        foreign = s.encoded({'camera': {'other': 'FOREIGN'}})
        actual_read = s.read
        swapped = False
        def replace_after_read(path, *args, **kwargs):
            nonlocal swapped
            result = actual_read(path, *args, **kwargs)
            if path == self.settings and kwargs.get('with_stat') and not swapped:
                swapped = True
                replacement = path.with_name('foreign.vrsettings')
                replacement.write_bytes(foreign)
                replacement.chmod(0o666)
                os.replace(replacement, path)
            return result
        s.read = replace_after_read
        try:
            with self.assertRaises(s.SafetyError):
                s.put(self.settings, b'NOT ALLOWED', expected, writable_data=True)
        finally:
            s.read = actual_read
        self.assertTrue(swapped)
        self.assertEqual(self.settings.read_bytes(), foreign)

    def test_mode666_settings_symlink_hardlink_and_nonboolean_stay_refused(self):
        raw = self.settings.read_bytes()
        original = self.settings.with_name('original.vrsettings')
        self.settings.rename(original)
        self.settings.symlink_to(original)
        with self.assertRaises(OSError):
            self.setup._camera_state()
        self.settings.unlink()
        os.link(original, self.settings)
        with self.assertRaises(s.SafetyError):
            self.setup._camera_state()
        self.settings.unlink()
        original.rename(self.settings)
        self.settings.write_bytes(s.encoded({'camera': {s.KEY: 1}}))
        self.settings.chmod(0o666)
        with self.assertRaises(s.SafetyError):
            self.setup._camera_state()
        self.assertNotEqual(self.settings.read_bytes(), raw)
        self.assertFalse(self.setup.camera_receipt.exists())

    def test_condition_precedes_vendor_startup_and_ignores_optional_failures(self):
        self.install()
        consumer = self.root / 'native-startup-consumer.py'
        consumer.write_text('import json,pathlib,sys\nprint(json.dumps({"saw_layer_at_startup":pathlib.Path(sys.argv[1]).exists()}))\n')
        native_argv = [sys.executable, str(consumer), str(self.setup.target)]
        # This small scheduler models the documented public service phase order;
        # the vendor pre-command is a REAL child consuming the published target.
        # No systemd service is started and no native driver authority is claimed.
        def run_phase_order(dropin, helper_exit=None):
            commands = [('ExecStartPre', native_argv, False)]
            for line in dropin.splitlines():
                key, _, value = line.partition('=')
                if key in ('ExecCondition', 'ExecStartPre'):
                    ignore = value.startswith('-')
                    argv = shlex.split(value.lstrip('-'))
                    self.assertEqual(argv[0], '/usr/bin/python3')
                    self.assertEqual(argv[2:], ['prepare-launch', '--home', str(self.home)])
                    commands.append((key, argv, ignore))
            observed = []
            for phase in ('ExecCondition', 'ExecStartPre'):
                for key, argv, ignore in commands:
                    if key != phase:
                        continue
                    if argv == native_argv:
                        child = subprocess.run(argv, capture_output=True, text=True, timeout=5)
                        self.assertEqual(child.returncode, 0, child.stderr)
                        observed.append(json.loads(child.stdout)['saw_layer_at_startup'])
                    else:
                        if helper_exit is None:
                            self.assertEqual(self.setup.prepare()['reason'], 'prepared')
                            code = 0
                        else:
                            child = subprocess.run([sys.executable, '-c', 'raise SystemExit(' + str(helper_exit) + ')'], capture_output=True, timeout=5)
                            code = child.returncode
                        if code and not ignore:
                            return observed
            return observed
        dropin = self.setup._unit().decode()
        self.assertNotIn('ExecStartPre=', dropin)  # No reset/override of Valve argv.
        self.assertEqual(run_phase_order(dropin), [True])
        self.setup.finish()
        # Causal old-hook mutant: late appended StartPre misses native startup.
        self.assertEqual(run_phase_order(dropin.replace('ExecCondition=', 'ExecStartPre=')), [False])
        self.setup.finish()
        for code in (2, 203, 255):
            with self.subTest(optional_exit=code):
                self.assertEqual(run_phase_order(dropin, code), [False])
        # The ignore prefix is essential: a failing ordinary condition skips VR.
        self.assertEqual(run_phase_order(dropin.replace('ExecCondition=-', 'ExecCondition='), 2), [])

    def test_condition_unit_passes_offline_systemd_analyze_verify(self):
        binary = shutil.which('systemd-analyze')
        if binary is None:
            self.skipTest('systemd-analyze not installed; primary-source ordering controls remain')
        self.install()
        unit = self.root / 'sfnp-ordering-fixture.service'
        unit.write_text('[Unit]\nDescription=Owned offline native passthrough syntax fixture\n'
                        '[Service]\nType=oneshot\nExecStartPre=/usr/bin/true\nExecStart=/usr/bin/true\n'
                        + self.setup._unit().decode())
        child = subprocess.run([binary, 'verify', '--man=no', '--generators=no', str(unit)], capture_output=True, text=True, timeout=20)
        self.assertEqual(child.returncode, 0, child.stdout + child.stderr)
        self.assertIn('ExecCondition=-/usr/bin/python3', unit.read_text())

    def test_actual_frame_two_config_history_selects_first_and_never_touches_second(self):
        self.settings.chmod(0o666)
        self.secondary_settings.chmod(0o666)
        second = self.secondary_settings.read_bytes()
        self.assertEqual(s.load(self.registry)['config'], [str(self.home / '.config/openvr/config'), str(self.home / '.local/share/Steam/config')])
        self.assertEqual(self.setup._settings(), self.settings)
        self.install()
        self.assertEqual(self.setup.prepare()['reason'], 'prepared')
        self.assertEqual(self.setup.finish()['reason'], 'stopped')
        self.assertNotIn(s.KEY, s.json_bytes(self.settings.read_bytes())['camera'])
        self.assertEqual(self.secondary_settings.read_bytes(), second)
        self.assertEqual(self.secondary_settings.stat().st_mode & 0o777, 0o666)

    def test_first_history_missing_data_does_not_fall_back_to_second(self):
        second = self.secondary_settings.read_bytes()
        self.settings.unlink()
        self.assertEqual(self.setup._settings(), self.settings)
        with self.assertRaises(FileNotFoundError):
            self.setup._camera_state()
        self.assertEqual(self.secondary_settings.read_bytes(), second)
        self.assertFalse(self.setup.camera_receipt.exists())

    def test_config_environment_has_exact_nonempty_priority(self):
        alternate = self.root / 'owned-config'
        alternate.mkdir(mode=0o700)
        target = alternate / 'steamvr.vrsettings'
        s.put(target, s.encoded({'camera': {'other': 'alternate'}}))
        target.chmod(0o666)
        first, second = self.settings.read_bytes(), self.secondary_settings.read_bytes()
        with mock.patch.dict(os.environ, {'VR_CONFIG_PATH': str(alternate)}):
            self.assertEqual(self.setup._settings(), target)
            self.setup._camera(True)
            self.assertTrue(s.json_bytes(target.read_bytes())['camera'][s.KEY])
            self.assertTrue(self.setup._camera(False)['restored'])
        self.assertEqual(self.setup._settings(), self.settings)  # Empty env is ignored, not trimmed/guessed.
        self.assertEqual(self.settings.read_bytes(), first)
        self.assertEqual(self.secondary_settings.read_bytes(), second)

    def test_registry_environment_selection_matches_public_openvr_priority(self):
        alternate = self.root / 'alternate-config'
        alternate.mkdir(mode=0o700)
        registry = self.root / 'registered.vrpath'
        s.put(registry, s.encoded({'config': [str(alternate), str(self.settings.parent)]}))
        xdg = self.root / 'xdg'
        os.close(s.directory(xdg / 'openvr', create=True))
        s.put(xdg / 'openvr/openvrpaths.vrpath', s.encoded({'config': [str(self.settings.parent)]}))
        with mock.patch.dict(os.environ, {'VR_PATHREG_OVERRIDE': str(registry), 'XDG_CONFIG_HOME': str(xdg)}):
            self.assertEqual(self.setup._settings(), alternate / 'steamvr.vrsettings')
        with mock.patch.dict(os.environ, {'XDG_CONFIG_HOME': str(xdg)}):
            self.assertEqual(self.setup._settings(), self.settings)
        with mock.patch.dict(os.environ, {'VR_PATHREG_OVERRIDE': 'relative.vrpath'}):
            with self.assertRaises(s.SafetyError):
                self.setup._settings()

    def test_missing_malformed_and_foreign_registered_targets_refuse(self):
        original = s.read(self.registry)
        for configs in (None, [], '', [None], [''], ['relative'], ['/tmp/../escape'], [str(self.settings.parent)] * 33):
            with self.subTest(configs=configs):
                s.put(self.registry, s.encoded({'config': configs}), s.read(self.registry))
                with self.assertRaises((s.SafetyError, OSError)):
                    self.setup._settings()
        s.put(self.registry, original, s.read(self.registry))
        self.registry.unlink()
        with self.assertRaises(FileNotFoundError):
            self.setup._settings()  # Never guess the still-existing second Steam config.
        s.put(self.registry, original)
        self.settings.parent.chmod(0o777)
        with self.assertRaises(s.SafetyError):
            self.setup._settings()
        self.settings.parent.chmod(0o700)
        link = self.root / 'foreign-config-link'
        link.symlink_to(self.settings.parent)
        s.put(self.registry, s.encoded({'config': [str(link)]}), s.read(self.registry))
        with self.assertRaises(OSError):
            self.setup._settings()
        self.assertFalse(self.setup.camera_receipt.exists())

    def test_cli_no_install_and_no_implicit_restart(self):
        result = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/native_passthrough_setup.py'), 'status', '--home', str(self.home)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(json.loads(result.stdout)['installed'])
        source = (ROOT / 'tools/native_passthrough_setup.py').read_text()
        self.assertNotIn("'restart'", source)
        self.assertNotIn("'sudo'", source)

    def test_guarded_filesystem_permissions_and_symlinks(self):
        self.install()
        self.assertEqual(self.setup.config.stat().st_mode & 0o777, 0o700)
        self.assertEqual(self.setup.receipt.stat().st_mode & 0o777, 0o600)
        self.setup.control.unlink()
        self.setup.control.symlink_to(self.settings)
        with self.assertRaises(OSError):
            self.setup.enable()
        self.assertNotIn(s.KEY, s.load(self.settings)['camera'])


if __name__ == '__main__':
    unittest.main()
