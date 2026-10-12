#!/usr/bin/env python3
"""Actual host preload controls; never manufactures a production Ready capability."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PROBE = r'''
#include "import/import_startup_audit.h"
#include <initializer_list>
#include <cerrno>
#include <cstdio>
#include <dlfcn.h>
#include <pthread.h>
using namespace frame_import_startup_audit;
using Callback=BridgeResponse(*)(BridgeRequest);
int main() {
    pthread_mutex_t mutex{};
    errno=E2BIG;const int created=pthread_mutex_init(&mutex,nullptr);const int init_errno=errno;
    errno=ERANGE;const int destroyed=created==0?pthread_mutex_destroy(&mutex):-1;const int destroy_errno=errno;
    void* raw=dlsym(RTLD_DEFAULT,"frame_client_startup_bridge");
    const auto bridge=reinterpret_cast<Callback>(raw);
    bool inert=true;
    if(bridge) {
        for(const auto stage:{Stage::Prepare,Stage::Bind,Stage::Retire}) {
            BridgeRequest request{};request.stage=stage;
            errno=ENOTTY;const auto answer=bridge(request);
            inert=inert&&!answer.ready&&answer.hidden_entry==0&&errno==ENOTTY;
        }
    }
    std::printf("{\"created\":%d,\"init_errno\":%d,\"destroyed\":%d,\"destroy_errno\":%d,\"bridge_inert\":%s,\"bridge_present\":%s}\n",
        created,init_errno,destroyed,destroy_errno,inert?"true":"false",bridge?"true":"false");
    return !inert||created!=0||destroyed!=0;
}
'''


def checked(command, **kw):
    result = subprocess.run(command, capture_output=True, text=True, timeout=20, **kw)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {result.stderr}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--observer', required=True, type=Path)
    parser.add_argument('--envelope', type=Path,
                        help='Optional real private prepared envelope; wrong host process remains inert')
    parser.add_argument('--workdir', type=Path)
    parser.add_argument('--compiler', default='c++')
    parser.add_argument('--trace-io', action='store_true',
                        help='Check actual strace read/open refusal boundaries (unsanitized run)')
    parser.add_argument('--compiler-flag', action='append', default=[])
    parser.add_argument('--preload-prefix', type=Path,
                        help='Optional sanitizer runtime loaded before actual observer')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    observer = args.observer.resolve(strict=True)
    if args.workdir:
        args.workdir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='observer-controls-', dir=args.workdir))
    work.chmod(0o700)
    receipt = {'schema': 1, 'scope': 'actual host preload refusal/inert forwarding only',
               'production_ready_proven': False, 'native_startup_proven': False,
               'frame_or_gpu_proven': False, 'cases': []}
    try:
        source, probe = work / 'probe.cpp', work / 'probe'
        source.write_text(PROBE)
        checked([args.compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                 '-I', str(root), *args.compiler_flag, str(source), '-pthread',
                 '-ldl', '-o', str(probe)])
        env = dict(os.environ)
        for name in ('LD_PRELOAD', 'GHVR_FRAME_CLIENT_STARTUP_ENVELOPE',
                     'GHVR_FRAME_CLIENT_STARTUP_OUTPUT'):
            env.pop(name, None)
        baseline = json.loads(checked([str(probe)], env=env).stdout)
        assert baseline['created'] == baseline['destroyed'] == 0
        assert baseline['bridge_inert']
        assert baseline.pop('bridge_present') is False
        symbols = checked(['nm', '-D', '--defined-only', str(observer)]).stdout
        names = [line.split()[-1].split('@')[0] for line in symbols.splitlines()]
        assert 'frame_client_startup_bridge' in names
        assert not any(n.startswith(('VR_Init', 'VR_Shutdown')) for n in names)
        # Public pthread aliases belong to the shared Root ARM entry assembler,
        # not to C wrappers in this source. They may exist in the final artifact.
        receipt['bridge_export'] = True
        receipt['public_vr_exports'] = False
        preload = ':'.join(str(p) for p in (args.preload_prefix, observer) if p)
        envelope, output = work / 'envelope', work / 'capture.jsonl'
        def case(name, profile=None, out=None, expect_read=None):
            test_env = dict(env, LD_PRELOAD=preload)
            if profile is not None:
                test_env['GHVR_FRAME_CLIENT_STARTUP_ENVELOPE'] = str(profile)
            if out is not None:
                test_env['GHVR_FRAME_CLIENT_STARTUP_OUTPUT'] = str(out)
            before = output.read_bytes() if output.exists() else None
            command = [str(probe)]
            trace = work / 'syscalls.txt'
            if args.trace_io and expect_read is not None:
                command = ['strace', '-f', '-yy', '-e', 'trace=openat,read,newfstatat',
                           '-o', str(trace), *command]
            process = checked(command, env=test_env)
            assert not process.stderr, (name, process.stderr)
            result = json.loads(process.stdout)
            assert result.pop('bridge_present') is True, (name, 'observer did not load')
            if args.trace_io and expect_read is not None:
                text = trace.read_text()
                actual_read = any('read(' in line and f'<{profile}>' in line
                                  for line in text.splitlines())
                assert actual_read == expect_read, (name, 'read boundary')
                if name == 'nonblocking_fifo':
                    assert any(str(profile) in line and 'openat(' in line and
                               'O_NONBLOCK' in line for line in text.splitlines())
                receipt.setdefault('io_boundary_checked', []).append(name)
                trace.unlink()
            assert result == baseline, (name, result, baseline)
            assert (output.read_bytes() if output.exists() else None) == before, name
            receipt['cases'].append(name)
        def payload(data, mode=0o600):
            envelope.write_bytes(data)
            envelope.chmod(mode)
        case('no_environment')
        case('missing_profile', work / 'missing', output)
        case('relative_profile', 'relative', output)
        payload(bytes(11800))
        case('relative_output', envelope, 'relative')
        case('bad_magic', envelope, output, True)
        payload(b'FCSTART1' + (1).to_bytes(4,'little') + (11800).to_bytes(4,'little') + bytes(11784))
        case('untrusted_zero_native_profiles', envelope, output, True)
        for mode in (0o400, 0o700, 0o644, 0o601, 0o4600):
            envelope.chmod(mode)
            case('wrong_mode_' + oct(mode), envelope, output, False)
        envelope.chmod(0o600)
        alias = work / 'symlink'; alias.symlink_to(envelope)
        case('nofollow_symlink', alias, output, False)
        alias.unlink()
        hardlink = work / 'hardlink'; os.link(envelope, hardlink)
        case('multiple_link_count', envelope, output, False)
        hardlink.unlink()
        fifo = work / 'fifo'; os.mkfifo(fifo, 0o600)
        # O_NONBLOCK must refuse before any attempt to consume a FIFO. Timeout
        # alone is a failure, never a pass or a timeout-based Ready assertion.
        case('nonblocking_fifo', fifo, output, False)
        case('not_regular_directory', work, output, False)
        for size in (0, 11799, 11801):
            payload(bytes(size))
            case('wrong_size_' + str(size), envelope, output, False)
        output.write_bytes(b'owned existing output must remain untouched\n')
        case('existing_output_unchanged', envelope, output)
        if args.envelope:
            payload(args.envelope.resolve(strict=True).read_bytes())
            assert envelope.stat().st_size == 11800
            case('actual_prepared_envelope_wrong_executable', envelope, output)
        receipt['passed'] = len(receipt['cases'])
        target = (args.workdir or Path.cwd()) / 'client-startup-observer-controls.json'
        target.write_text(json.dumps(receipt, indent=2) + '\n')
        print(json.dumps(receipt, sort_keys=True))
    finally:
        # All child processes were reaped by subprocess.run; retain only the
        # small receipt. These files are generated, never private native inputs.
        shutil.rmtree(work)


if __name__ == '__main__':
    main()
