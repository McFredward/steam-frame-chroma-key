#!/usr/bin/env python3
"""Durable user-only native passthrough setup; never restarts Steam or SteamVR.

Prepare is a fail-closed optional ExecCondition, not a readiness claim. Every
compositor launch gets a fresh unchanged one-shot GHVR session. Emergency-off
unpublishes our injection before attempting any independent camera restoration.
"""
from __future__ import annotations
import argparse
import contextlib
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import stat
import subprocess
import sys

sys.dont_write_bytecode = True
NAME = 'SteamFrameNativePassthrough'
SCHEMA = 'sfnp-user-install-v1'
CONTROL = 'sfnp-user-control-v1'
CAMERA = 'sfnp-camera-key-v1'
MANIFEST = 'VkLayer_SteamFrameNativePassthrough.json'
DROPIN = '80-steam-frame-native-passthrough.conf'
LIBRARY = 'libVkLayer_frame_pixel_snapshot.so'
MAX = 2 * 1024 * 1024
KEY = 'forceSceneAlphaBlending'
REVOCATION = b'SFNP_EMERGENCY_OFF/1\n'


class SafetyError(ValueError):
    pass


def encoded(value):
    return (json.dumps(value, sort_keys=True, indent=2, allow_nan=False) + '\n').encode()


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def absolute(path):
    path = Path(path)
    if not path.is_absolute() or '..' in path.parts or len(str(path)) > 1024 or any(c in str(path) for c in '\r\n\0'):
        raise SafetyError('bounded absolute path required')
    return path


def directory(path, create=False, private=False):
    path = absolute(path)
    fd = os.open('/', os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for part in path.parts[1:]:
            try:
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=fd)
            except FileNotFoundError:
                if not create:
                    raise
                os.mkdir(part, 0o700, dir_fd=fd)
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=fd)
            os.close(fd)
            fd = child
            info = os.fstat(fd)
            if info.st_mode & 0o022 and not (info.st_uid == 0 and info.st_mode & stat.S_ISVTX):
                raise SafetyError('writable parent directory refused')
        info = os.fstat(fd)
        if info.st_uid != os.getuid() or private and stat.S_IMODE(info.st_mode) != 0o700:
            raise SafetyError('owned directory required; private directories must be 0700')
        return fd
    except BaseException:
        os.close(fd)
        raise


def read(path, limit=MAX, owned=True, *, writable_data=False, with_stat=False):
    path = absolute(path)
    # SteamVR creates this mutable DATA file with mode 0666 on the Frame.
    # This is never authority to import executable code or relax private state.
    if writable_data and (not owned or path.name != 'steamvr.vrsettings'):
        raise SafetyError('permissive data policy is restricted to owned SteamVR settings')
    parent = directory(path.parent)
    try:
        fd = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK, dir_fd=parent)
    finally:
        os.close(parent)
    try:
        before = os.fstat(fd)
        if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1 or (not writable_data and before.st_mode & 0o022) or not 0 <= before.st_size <= limit or owned and before.st_uid != os.getuid():
            raise SafetyError('bounded unchanged regular file required: ' + str(path))
        chunks, total = [], 0
        while total <= limit:
            chunk = os.read(fd, min(65536, limit + 1 - total))
            if not chunk:
                break
            chunks.append(chunk)
            total += len(chunk)
        raw = b''.join(chunks)
        after = os.fstat(fd)
        identity = lambda item: (item.st_dev, item.st_ino, item.st_size, item.st_mtime_ns, item.st_ctime_ns)
        if len(raw) != before.st_size or identity(after) != identity(before):
            raise SafetyError('file changed while reading')
        return (raw, before) if with_stat else raw
    finally:
        os.close(fd)


def exists(path):
    return os.path.lexists(path)


def json_bytes(raw):
    def pairs(items):
        value = {}
        for key, item in items:
            if key in value:
                raise SafetyError('duplicate JSON key')
            value[key] = item
        return value
    value = json.loads(raw, object_pairs_hook=pairs, parse_constant=lambda _: (_ for _ in ()).throw(SafetyError('nonfinite JSON')))
    if type(value) is not dict:
        raise SafetyError('JSON object required')
    return value


def load(path):
    return json_bytes(read(path))


def put(path, raw, expected=None, mode=0o600, *, writable_data=False):
    """Atomic replace only a verified exact predecessor; never adopt foreign bytes."""
    if writable_data and (expected is None or path.name != 'steamvr.vrsettings'):
        raise SafetyError('settings data replacement requires an exact existing predecessor')
    parent = directory(path.parent, create=True)
    temporary = '.' + path.name + '.' + secrets.token_hex(8)
    try:
        if expected is None:
            fd = os.open(path.name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, mode, dir_fd=parent)
            try:
                with os.fdopen(fd, 'wb', closefd=False) as stream:
                    stream.write(raw)
                    stream.flush()
                os.fsync(fd)
            finally:
                os.close(fd)
        else:
            observed, before = read(path, writable_data=writable_data, with_stat=True)
            if observed != expected:
                raise SafetyError('foreign change preserved: ' + str(path))
            # Bind the expected bytes to the same original inode. Do not adopt
            # a different file swapped in between the read and the final stat.
            fingerprint = lambda item: (item.st_dev, item.st_ino, item.st_size, item.st_mtime_ns,
                                        item.st_ctime_ns, item.st_mode, item.st_uid, item.st_nlink)
            identity = before
            if fingerprint(os.stat(path.name, dir_fd=parent, follow_symlinks=False)) != fingerprint(identity):
                raise SafetyError('file changed before replacement')
            fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW | os.O_CLOEXEC, mode, dir_fd=parent)
            try:
                if writable_data:
                    # Preserve the native mode on the NEW temporary inode;
                    # never chmod SteamVR's existing settings file to hide it.
                    os.fchmod(fd, stat.S_IMODE(identity.st_mode))
                with os.fdopen(fd, 'wb', closefd=False) as stream:
                    stream.write(raw)
                    stream.flush()
                os.fsync(fd)
            finally:
                os.close(fd)
            if fingerprint(os.stat(path.name, dir_fd=parent, follow_symlinks=False)) != fingerprint(identity):
                raise SafetyError('file changed before replacement')
            os.replace(temporary, path.name, src_dir_fd=parent, dst_dir_fd=parent)
        os.fsync(parent)
    finally:
        try:
            os.unlink(temporary, dir_fd=parent)
        except FileNotFoundError:
            pass
        os.close(parent)


def remove(path, sha):
    if not exists(path):
        return
    raw = read(path, 16 * 1024 * 1024)
    if digest(raw) != sha:
        raise SafetyError('changed/foreign file preserved: ' + str(path))
    parent = directory(path.parent)
    try:
        observed = os.stat(path.name, dir_fd=parent, follow_symlinks=False)
        if read(path, 16 * 1024 * 1024) != raw or os.stat(path.name, dir_fd=parent, follow_symlinks=False) != observed:
            raise SafetyError('file changed before removal')
        os.unlink(path.name, dir_fd=parent)
        os.fsync(parent)
    finally:
        os.close(parent)


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result


class UserSystem:
    """No elevation, service restart, network request or process termination."""
    def reload(self):
        process = subprocess.run(['/usr/bin/systemctl', '--user', 'daemon-reload'], stdin=subprocess.DEVNULL,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10, check=False)
        return process.returncode == 0

    def running(self):
        # Runtime presence, not service-manager text: a shared vrserver can outlive
        # the compositor. An uncertain scan prohibits editing its settings file.
        entries = list(Path('/proc').iterdir())
        if len(entries) > 8192:
            raise SafetyError('process census exceeded bound')
        for item in entries:
            if not item.name.isdecimal():
                continue
            try:
                if item.stat().st_uid != os.getuid():
                    continue
                name = (item / 'comm').read_text()[:64].strip()
                if name in ('vrserver', 'vrcompositor'):
                    return True
            except FileNotFoundError:
                continue
            except OSError as error:
                raise SafetyError('runtime process state unavailable') from error
        return False

    @contextlib.contextmanager
    def camera(self, package, metadata, runtime):
        # Package/runtime verification must precede this call; the existing
        # helper independently pins the actual public native provider inode.
        sys.path.insert(0, str(package / 'tools'))
        try:
            helper = module(package / 'tools/steamvr_camera_alpha.py', 'sfnp_owned_camera')
            pin = load(package / 'compatibility.json')['runtime_files']['openvr']['sha256']
            with helper.NativeCamera(runtime['openvr'], pin) as api:
                yield api
        finally:
            sys.path.pop(0)


class Setup:
    def __init__(self, home, *, system=None, verifier=None, runtime_factory=None, runtime_dir=None):
        self.home = absolute(home)
        self.data = self.home / '.local/share' / NAME
        self.config = self.home / '.config' / NAME
        self.package = self.data / 'package'
        self.tools = self.data / 'tools'
        self.runtime = absolute(runtime_dir) if runtime_dir is not None else Path('/run/user') / str(os.getuid()) / 'steam-frame-native-passthrough'
        self.runs = self.runtime / 'runs'
        self.target = self.runtime / 'receiver.json'
        self.history = self.config / 'launch-history.json'
        self.receipt = self.config / 'install.json'
        self.upgrade = self.config / 'upgrade.json'
        self.control = self.config / 'control.json'
        self.revocation = self.config / 'emergency-off.marker'
        self.camera_receipt = self.config / 'camera-before.json'
        self.inflight = self.config / 'inflight.json'
        self.status_file = self.config / 'status.json'
        self.manifest = self.home / '.local/share/vulkan/implicit_layer.d' / MANIFEST
        self.session = self.data / 'session.conf'
        self.dropin = self.home / '.config/systemd/user/steamvr.service.d' / DROPIN
        self.system = system or UserSystem()
        self.verifier = verifier
        self.runtime_factory = runtime_factory

    @contextlib.contextmanager
    def locked(self):
        os.close(directory(self.config, create=True, private=True))
        fd = os.open(self.config / 'setup.lock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_nlink != 1 or stat.S_IMODE(info.st_mode) != 0o600:
                raise SafetyError('foreign lock preserved')
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            yield
        finally:
            os.close(fd)

    def _engine(self):
        # A source-checkout install/update uses its independently supplied
        # verifier, so a partially upgraded package can be repaired without
        # importing an old/new mixture from that package.
        source = Path(__file__).with_name('prepare_native_release.py')
        if source.is_file():
            return module(source, 'sfnp_release_verifier')
        path = self.package / 'tools/prepare_native_release.py'
        if not exists(path):
            raise SafetyError('installed verifier unavailable; ordinary SteamVR is retained')
        if exists(self.receipt):
            expected = self._state()['files'].get(str(path))
            if expected is None or digest(read(path)) != expected:
                raise SafetyError('changed installed verifier preserved; no executable code imported')
        return module(path, 'sfnp_release_verifier')

    def _verify(self, package=None):
        engine = None if self.verifier and self.runtime_factory else self._engine()
        runtime = self.runtime_factory() if self.runtime_factory else engine.runtime_files(home=self.home)
        metadata = self.verifier(package or self.package, runtime) if self.verifier else engine.verify_package(package or self.package, runtime)
        return metadata, runtime

    def _state(self):
        receipt = load(self.receipt)
        if (set(receipt) != {'schema', 'home', 'data', 'manifest_link', 'package_sha256', 'files'}
                or receipt.get('schema') != SCHEMA or receipt.get('home') != str(self.home)
                or receipt.get('data') != str(self.data) or receipt.get('manifest_link') != str(self.target)):
            raise SafetyError('foreign installation receipt preserved')
        if type(receipt['files']) is not dict or not 1 <= len(receipt['files']) <= 32:
            raise SafetyError('bounded owned install inventory required')
        for name, sha in receipt['files'].items():
            path = absolute(name)
            if not (path == self.dropin or path.is_relative_to(self.data)) or type(sha) is not str or not re.fullmatch('[0-9a-f]{64}', sha):
                raise SafetyError('foreign installation inventory path preserved')
        return receipt

    def _control(self):
        value = load(self.control)
        if set(value) != {'schema', 'enabled'} or value['schema'] != CONTROL or type(value['enabled']) is not bool:
            raise SafetyError('foreign control file preserved')
        if self._revoked():
            value['enabled'] = False
        return value

    def _revoked(self):
        if not exists(self.revocation):
            return False
        if read(self.revocation, 64) != REVOCATION:
            raise SafetyError('foreign emergency marker preserved')
        return True

    def request_emergency(self):
        # No long setup/build lock is required to revoke loader discovery. This
        # marker also makes an in-progress preparation refuse final publication.
        os.close(directory(self.config, create=True, private=True))
        if not self._revoked():
            try:
                put(self.revocation, REVOCATION)
            except FileExistsError:
                self._revoked()
        if exists(self.receipt):
            self._state()
            self._unlink()  # Exact owned link only; no provider/camera call here.
        return {'reason': 'emergency_off_requested', 'enabled': False,
                'camera_restore_pending': True, 'automatic_restart': False,
                'manual_action': 'Future injection is revoked. Stop SteamVR and run disable again to finish any pending camera restoration.'}

    def _enabled(self, enabled):
        value = {'schema': CONTROL, 'enabled': enabled}
        put(self.control, encoded(value), read(self.control) if exists(self.control) else None)

    def _status(self, reason, **extra):
        value = {'schema': 'sfnp-user-status-v1', 'reason': reason, 'automatic_restart': False, **extra}
        put(self.status_file, encoded(value), read(self.status_file) if exists(self.status_file) else None)
        return value

    def _collision(self):
        old = self.home / '.local/share/GloomhavenVR/SteamLinkMaskProbe/session.conf'
        old_manifest = self.manifest.with_name('VkLayer_GHVR_pixel_snapshot.json')
        legacy_manifest = self.manifest.with_name('mrchroma.json')
        if exists(old) or exists(old_manifest) or exists(legacy_manifest):
            raise SafetyError('Existing temporary GloomhavenVR experiment or legacy mrchroma layer preserved. Roll it back with its original operator/receipt (including camera baseline), then install again; no automatic migration.')
        if not exists(self.receipt) and exists(self.manifest):
            raise SafetyError('Unowned native passthrough manifest preserved')

    def _unit(self):
        script = str(self.tools / 'native_passthrough_setup.py')
        def quote(value):
            return '"' + value.replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%').replace('$', '$$') + '"'
        # Valve starts vrstartup from its own ExecStartPre. An additive condition
        # runs before every vendor pre-command; '-' ignores optional helper errors.
        return ('# Owned by SteamFrameNativePassthrough; existing directory lists are not reset.\n[Service]\nRuntimeDirectory=steam-frame-native-passthrough\nRuntimeDirectoryMode=0700\nRuntimeDirectoryPreserve=no\n'
                'ExecCondition=-/usr/bin/python3 ' + quote(script) + ' prepare-launch --home ' + quote(str(self.home)) + '\n'
                'ExecStopPost=-/usr/bin/python3 ' + quote(script) + ' finish-launch --home ' + quote(str(self.home)) + '\n').encode()

    def install(self, package):
        self._collision()
        metadata, _ = self._verify(package)
        source = Path(__file__).resolve()
        files = {self.tools / source.name: source.read_bytes()}
        # The verifier owns the finite complete package allowlist and every pin.
        for name in metadata['files']:
            if '..' in Path(name).parts or Path(name).is_absolute():
                raise SafetyError('package relative path refused')
            files[self.package / name] = read(package / name, 16 * 1024 * 1024)
        files[self.package / 'package.json'] = read(package / 'package.json')
        # The installed verifier imports no private original shader at startup.
        files[self.dropin] = self._unit()
        planned = {'schema': SCHEMA, 'home': str(self.home), 'data': str(self.data),
                   'manifest_link': str(self.target), 'package_sha256': digest(files[self.package / 'package.json']),
                   'files': {str(p): digest(b) for p, b in files.items()}}
        previous = self._state() if exists(self.receipt) else None
        upgrading = previous is not None and encoded(previous) != encoded(planned)
        upgrade = {'schema': 'sfnp-upgrade-v1', 'previous': previous, 'next': planned}
        if upgrading:
            if self.system.running():
                raise SafetyError('Stop SteamVR before updating installed code; no restart or mapped-library overwrite was attempted')
            self._unpublish(unlink=True)
            if not self._restore()['restored']:
                raise SafetyError('Camera restoration is pending; current installation retained')
            if exists(self.upgrade):
                if encoded(load(self.upgrade)) != encoded(upgrade):
                    raise SafetyError('foreign/different interrupted upgrade preserved')
            else:
                put(self.upgrade, encoded(upgrade))
        elif previous is None:
            # Durable write-ahead ownership; install can safely resume after interruption.
            put(self.receipt, encoded(planned))
        elif exists(self.upgrade):
            pending = load(self.upgrade)
            if pending.get('schema') != 'sfnp-upgrade-v1' or encoded(pending.get('next')) != encoded(planned):
                raise SafetyError('foreign interrupted upgrade preserved')
        for path, raw in files.items():
            os.close(directory(path.parent, create=True))
            if exists(path):
                current = read(path, 16 * 1024 * 1024)
                if current == raw:
                    continue
                old = previous['files'].get(str(path)) if upgrading else None
                if old is None or digest(current) != old:
                    raise SafetyError('changed installed file preserved: ' + str(path))
                put(path, raw, current)
            else:
                put(path, raw)
        if upgrading:
            for name, sha in previous['files'].items():
                if name not in planned['files']:
                    remove(Path(name), sha)
            put(self.receipt, encoded(planned), encoded(previous))
        if exists(self.upgrade):
            remove(self.upgrade, digest(read(self.upgrade)))
        if not exists(self.control):
            self._enabled(True)
        else:
            self._control()  # Reinstallation preserves explicit disabled state.
        if self._control()['enabled']:
            self._link()
            if self._revoked():
                self._unlink()
        self.system.reload()
        return self._status('installed', enabled=self._control()['enabled'], manual_action='Start SteamVR normally; setup did not restart it.')

    def install_automatic(self):
        self._collision()
        engine = self._engine()
        # Exact repeats reuse the immutable already checked package. They do not
        # compile again or grow preparation records on every invocation.
        if exists(self.receipt) and not exists(self.upgrade):
            try:
                engine.verify_package(self.package, engine.runtime_files(home=self.home))
                return self.install(self.package)
            except Exception:
                pass  # Changed source/runtime must go through the full cold gate.
        os.close(directory(self.data, create=True, private=True))
        attempts = self.data / 'preparation'
        os.close(directory(attempts, create=True, private=True))
        staging = attempts / ('attempt-' + secrets.token_hex(16))
        os.close(directory(staging, create=True, private=True))
        package = staging / 'package'
        # The helper retains bounded logs/source/result evidence next to output,
        # even on failure. An outer temporary context must not erase that proof.
        engine.prepare(package)
        result = self.install(package)
        metadata = load(package / 'package.json')
        for name, record in metadata['files'].items():
            remove(package / name, record['sha256'])
        remove(package / 'package.json', digest(encoded(metadata)))
        # Only empty directories after exact-file removal qualify for cleanup.
        for path in sorted(package.rglob('*'), key=lambda p: len(p.parts), reverse=True):
            if path.is_dir() and not path.is_symlink():
                try:
                    path.rmdir()
                except OSError:
                    pass
        try:
            package.rmdir()
        except OSError:
            pass
        return result

    def _link(self):
        self._state()
        if exists(self.target) and not exists(self.inflight):
            raise SafetyError('unowned volatile manifest preserved')
        os.close(directory(self.manifest.parent, create=True))
        if exists(self.manifest):
            if not self.manifest.is_symlink() or os.readlink(self.manifest) != str(self.target):
                raise SafetyError('foreign Vulkan manifest/link preserved')
        else:
            os.symlink(str(self.target), self.manifest)

    def _unlink(self):
        if not exists(self.manifest):
            return
        if not self.manifest.is_symlink() or os.readlink(self.manifest) != str(self.target):
            raise SafetyError('foreign Vulkan manifest/link preserved')
        parent = directory(self.manifest.parent)
        try:
            identity = os.stat(self.manifest.name, dir_fd=parent, follow_symlinks=False)
            if identity.st_uid != os.getuid() or identity.st_nlink != 1 or os.readlink(self.manifest.name, dir_fd=parent) != str(self.target):
                raise SafetyError('foreign symlink preserved')
            os.unlink(self.manifest.name, dir_fd=parent)
            os.fsync(parent)
        finally:
            os.close(parent)

    def _unpublish(self, unlink=False):
        if not exists(self.receipt):
            if exists(self.manifest):
                raise SafetyError('unowned Vulkan manifest preserved')
            return
        self._state()
        # Removing a verified owned link is independent of config/provider health.
        link_error = None
        if unlink:
            try:
                self._unlink()
            except Exception as error:
                link_error = error
        if exists(self.inflight):
            run = load(self.inflight)
            if run.get('schema') != 'sfnp-launch-v1' or Path(run.get('directory', '')).parent != self.runs:
                raise SafetyError('foreign launch ownership preserved')
            remove(self.target, run['manifest_sha256'])
            remove(self.session, run['config_sha256'])
        elif exists(self.target) or exists(self.session):
            raise SafetyError('unowned launch files preserved')
        if link_error is not None:
            raise link_error

    def _remember(self, launch, outcome):
        records = load(self.history).get('launches', []) if exists(self.history) else []
        if type(records) is not list or len(records) > 3:
            raise SafetyError('foreign launch history preserved')
        records.append({'capture_id': launch['capture_id'], 'outcome': outcome[:64]})
        put(self.history, encoded({'schema': 'sfnp-launch-history-v1', 'launches': records[-3:]}),
            read(self.history) if exists(self.history) else None)

    def _settings(self):
        # Valve CVRPathRegistry_Public::GetPaths gives nonempty VR_CONFIG_PATH
        # priority; GetConfigPath returns config.front(), not a unique/deduped
        # path. Later entries are history, never a fallback target to edit.
        # Public source: openvr/src/vrcore/vrpathregistry_public.cpp (v1.26.7).
        selected = os.environ.get('VR_CONFIG_PATH', '')
        if not selected:
            registry_override = os.environ.get('VR_PATHREG_OVERRIDE', '')
            config_home = os.environ.get('XDG_CONFIG_HOME', '')
            registry = (absolute(registry_override) if registry_override else
                        (absolute(config_home) if config_home else self.home / '.config') / 'openvr/openvrpaths.vrpath')
            value = load(registry)  # A missing registry cannot authorize a guessed Steam path.
            configs = value.get('config')
            if (type(configs) is not list or not 1 <= len(configs) <= 32
                    or any(type(item) is not str or not item or len(item) > 1024 for item in configs)):
                raise SafetyError('bounded nonempty OpenVR config path history required')
            selected = configs[0]
        root = absolute(selected)
        os.close(directory(root))  # Pin owned, non-writable, no-follow ancestors.
        return root / 'steamvr.vrsettings'

    def _camera_state(self):
        path = self._settings()
        raw = read(path, writable_data=True)  # Mutable Steam DATA; never invent a missing file.
        value = json_bytes(raw)
        section = value.get('camera', {})
        if type(section) is not dict or KEY in section and type(section[KEY]) is not bool:
            raise SafetyError('foreign nonboolean camera setting preserved')
        return path, raw, value, {'present': KEY in section, 'value': section.get(KEY)}

    def _camera(self, enable, verified=None):
        path, raw, value, current = self._camera_state()
        if not exists(self.camera_receipt):
            if not enable:
                return {'restored': True, 'changed': False}
            before = {'schema': CAMERA, 'settings': str(path), 'previous': current,
                      'write_intent': current != {'present': True, 'value': True}}
            put(self.camera_receipt, encoded(before))  # BEFORE file or API modification.
        previous = load(self.camera_receipt)
        if set(previous) != {'schema', 'settings', 'previous', 'write_intent'} or previous['schema'] != CAMERA or previous['settings'] != str(path):
            raise SafetyError('foreign camera receipt preserved')
        baseline = previous['previous']
        if type(baseline) is not dict or set(baseline) != {'present', 'value'} or type(baseline['present']) is not bool or (type(baseline['value']) is not bool if baseline['present'] else baseline['value'] is not None) or type(previous['write_intent']) is not bool:
            raise SafetyError('invalid camera baseline preserved')
        owned = {'present': True, 'value': True}
        if current != baseline and (not previous['write_intent'] or current != owned):
            raise SafetyError('unexpected foreign camera value preserved')
        wanted = owned if enable else baseline
        if current == wanted:
            return {'restored': not enable, 'changed': False}
        if self.system.running():
            if verified is None:
                verified = self._verify()  # Unknown/updated providers never receive native calls.
            metadata, runtime = verified
            with self.system.camera(self.package, metadata, runtime) as api:
                if wanted['present']:
                    api.write(wanted['value'])
                else:
                    api.remove_override()
            _, _, _, observed = self._camera_state()
            return {'restored': not enable and observed == baseline, 'persistence_pending': observed != wanted,
                    'native_api_acknowledged': True}
        section = value.setdefault('camera', {})
        if wanted['present']:
            section[KEY] = wanted['value']
        else:
            section.pop(KEY, None)
        mode = stat.S_IMODE(path.stat().st_mode)
        put(path, encoded(value), raw, mode=mode, writable_data=True)
        return {'restored': not enable, 'changed': True}

    def _restore(self):
        try:
            return self._camera(False) if exists(self.camera_receipt) else {'restored': True, 'changed': False}
        except Exception as error:
            return {'restored': False, 'reason': str(error)[:300]}

    def _prune(self):
        os.close(directory(self.runtime, private=True))
        os.close(directory(self.runs, create=True, private=True))
        runs = sorted(self.runs.iterdir(), key=lambda p: p.name)
        if len(runs) > 8:
            raise SafetyError('run inventory bound exceeded; foreign files preserved')
        for run in runs[:-2]:
            if not re.fullmatch(r'run-[0-9a-f]{32}', run.name):
                raise SafetyError('foreign run preserved')
            receipt = load(run / 'run-receipt.json')
            if receipt.get('schema') != 'sfnp-run-files-v1' or receipt.get('directory') != str(run):
                raise SafetyError('foreign run receipt preserved')
            allowed = set(receipt['files']) | {'run-receipt.json', 'launch.claim', 'scene-presenter-status.json'}
            if {p.name for p in run.iterdir()} - allowed:
                raise SafetyError('unexpected run artifact preserved; no captures are collected')
            for name, sha in receipt['files'].items():
                remove(run / name, sha)
            claim = run / 'launch.claim'
            if exists(claim):
                remove(claim, digest(b'GHVR_PIXEL_CLAIM/1\n'))
            status = run / 'scene-presenter-status.json'
            if exists(status):
                item = load(status)
                if item.get('capture_id') != receipt['capture_id'] or item.get('schema') != 'frame-scene-presenter-receiver-v1':
                    raise SafetyError('foreign native status preserved')
                remove(status, digest(read(status)))
            remove(run / 'run-receipt.json', digest(encoded(receipt)))
            run.rmdir()

    def enable(self):
        self._state()
        self._collision()
        revoked_before = self._revoked()
        self._verify()
        if exists(self.upgrade):
            raise SafetyError('Interrupted upgrade must be completed before enable')
        if exists(self.inflight):
            raise SafetyError('Previous launch still marked inflight; disable first, then enable explicitly')
        if self._revoked():
            if not revoked_before:
                raise SafetyError('Emergency-off arrived during enable; enable again explicitly when ready')
            remove(self.revocation, digest(REVOCATION))
        self._enabled(True)
        self._link()
        if self._revoked():
            self._unlink()
            return self._status('emergency_off', enabled=False, camera=self._restore())
        return self._status('enabled', enabled=True, manual_action='Start SteamVR normally; no restart was performed.')

    def prepare(self):
        try:
            self._state()
            self._collision()
            if exists(self.upgrade):
                self._unpublish(unlink=True)
                return self._status('interrupted_upgrade', enabled=False, camera=self._restore())
            if exists(self.inflight):
                self._unpublish()
                self._enabled(False)
                return self._status('unclean_previous_launch', enabled=False, camera=self._restore())
            if not self._control()['enabled']:
                self._unpublish()
                return self._status('disabled', enabled=False, camera=self._restore())
            verified = self._verify()
            self._prune()
            run = self.runs / ('run-' + secrets.token_hex(16))
            os.close(directory(run, create=True, private=True))
            capture = secrets.token_hex(16)
            config = b'GHVR_PIXEL_SNAPSHOT/1\n' + capture.encode() + b'\n' + str(run).encode() + b'\n'
            manifest = encoded({'file_format_version': '1.2.0', 'layer': {
                'name': 'VK_LAYER_GHVR_pixel_snapshot', 'type': 'GLOBAL', 'library_path': str(self.package / LIBRARY),
                'api_version': '1.3.0', 'implementation_version': '1', 'description': 'Steam Frame Native Passthrough; locally verified per-launch opt-in',
                'functions': {'vkGetInstanceProcAddr': 'frame_pixel_vkGetInstanceProcAddr', 'vkGetDeviceProcAddr': 'frame_pixel_vkGetDeviceProcAddr',
                              'vkNegotiateLoaderLayerInterfaceVersion': 'frame_pixel_vkNegotiateLoaderLayerInterfaceVersion'},
                'disable_environment': {'GHVR_PIXEL_SNAPSHOT_DISABLE': '1'}}})
            launch = {'schema': 'sfnp-launch-v1', 'directory': str(run), 'capture_id': capture,
                      'manifest_sha256': digest(manifest), 'config_sha256': digest(config)}
            put(self.inflight, encoded(launch))  # Dirty before any opt-in/camera change.
            artifact = read(self.package / 'scene-presenter.spv')
            files = {'scene-presenter.spv': artifact, 'scene-presenter.enable': b'GHVR_SCENE_PRESENTER/1\n'}
            put(run / 'run-receipt.json', encoded({'schema': 'sfnp-run-files-v1', 'directory': str(run), 'capture_id': capture,
                                                 'files': {n: digest(b) for n, b in files.items()}}))
            for name, raw in files.items():
                put(run / name, raw)
            put(self.session, config)
            camera = self._camera(True, verified)
            if camera.get('persistence_pending'):
                raise SafetyError('camera API persistence is pending; ordinary SteamVR launch retained')
            # The last publication is the only loader-visible opt-in. Emergency
            # revocation can arrive without waiting for the cold setup lock.
            if self._revoked():
                raise SafetyError('Emergency-off revoked this preparation')
            put(self.target, manifest)
            self._link()
            if self._revoked():
                self._unpublish(unlink=True)
                raise SafetyError('Emergency-off revoked final publication')
            return self._status('prepared', enabled=True, capture_id=capture, camera=camera)
        except Exception as error:
            cleanup = None
            try:
                self._unpublish()
            except Exception as cleanup_error:
                cleanup = str(cleanup_error)[:300]
            return self._status('stock_fallback', reason_detail=str(error)[:300], cleanup_error=cleanup, camera=self._restore())

    def finish(self, service_result='success'):
        self._state()
        self._unpublish()  # Always revoke injection independently of camera restoration.
        if service_result != 'success':
            self._enabled(False)
        camera = self._restore()
        if exists(self.inflight):
            self._remember(load(self.inflight), service_result)
            remove(self.inflight, digest(read(self.inflight)))
        return self._status('stopped' if service_result == 'success' else 'abnormal_stop_disabled',
                            enabled=self._control()['enabled'], service_result=service_result[:64], camera=camera)

    def disable(self, emergency=False):
        if not exists(self.receipt):
            # Stock shell/Python remains useful if a provider or installed helper failed.
            if exists(self.manifest):
                raise SafetyError('unowned manifest preserved; inspect it manually')
            return {'reason': 'not_installed', 'automatic_restart': False}
        self._state()
        self._enabled(False)
        self._unpublish(unlink=True)
        camera = self._restore()
        if exists(self.inflight) and not self.system.running():
            remove(self.inflight, digest(read(self.inflight)))
        return self._status('emergency_off' if emergency else 'disabled', enabled=False, camera=camera,
                            current_process_may_remain_loaded=self.system.running(), manual_action='Restart SteamVR manually to unload an already active receiver.')

    def uninstall(self):
        if not exists(self.receipt):
            return {'reason': 'not_installed', 'automatic_restart': False}
        result = self.disable()
        receipt = self._state()
        if self.system.running() or not result['camera']['restored']:
            # Keep the disabled StopPost helper installed until the running daemon
            # can persist the per-key restoration. Injection is already revoked.
            return self._status('uninstall_pending', enabled=False, camera=result['camera'], manual_action='Stop SteamVR and run uninstall again; disabled stop helper and receipts are retained for camera restoration.')
        # Remove only our unchanged drop-in after restoration. Do not reset a unit.
        if str(self.dropin) in receipt['files']:
            remove(self.dropin, receipt['files'][str(self.dropin)])
        self.system.reload()
        for name, sha in receipt['files'].items():
            remove(Path(name), sha)
        for file in (self.control, self.camera_receipt, self.inflight):
            if exists(file):
                remove(file, digest(read(file)))
        remove(self.receipt, digest(encoded(receipt)))
        return self._status('uninstalled', enabled=False)

    def status(self):
        result = {'installed': exists(self.receipt), 'manifest_link_installed': exists(self.manifest), 'manifest_published': exists(self.target), 'automatic_restart': False}
        if result['installed']:
            self._state()
            result['enabled'] = self._control()['enabled']
            try:
                self._verify()
                result['compatible'] = True
            except Exception as error:
                result.update(compatible=False, compatibility_reason=str(error)[:300])
            if exists(self.status_file):
                result['last_launch'] = load(self.status_file)
        return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('install', 'status', 'enable', 'disable', 'uninstall', 'emergency-off', 'prepare-launch', 'finish-launch'))
    parser.add_argument('--home', type=Path, default=Path.home())
    parser.add_argument('--package', type=Path)
    args = parser.parse_args(argv)
    setup = Setup(args.home)
    try:
        if args.command == 'emergency-off':
            requested = setup.request_emergency()
            try:
                with setup.locked():
                    result = setup.disable(True)
            except BlockingIOError:
                result = requested
            print(json.dumps(result, sort_keys=True))
            return 0
        with setup.locked():
            if args.command == 'install':
                setup._collision()
                package = args.package
                if package is None:
                    result = setup.install_automatic()
                else:
                    result = setup.install(absolute(package))
            elif args.command == 'prepare-launch':
                result = setup.prepare()
            elif args.command == 'finish-launch':
                result = setup.finish(os.environ.get('SERVICE_RESULT', 'success'))
            else:
                command = {'emergency-off': lambda: setup.disable(True)}.get(args.command, getattr(setup, args.command, None))
                result = command()
        print(json.dumps(result, sort_keys=True))
        return 0
    except Exception as error:
        print('Steam Frame Native Passthrough: ' + str(error)[:400], file=sys.stderr)
        # Optional hooks must never prevent the original SteamVR service starting.
        return 0 if args.command in ('prepare-launch', 'finish-launch') else 2


if __name__ == '__main__':
    raise SystemExit(main())
