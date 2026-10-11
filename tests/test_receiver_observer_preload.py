#!/usr/bin/env python3
"""Exercise real host PLT forwarding; never deploy or claim native frame proof."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    production = Path(sys.argv[1]).resolve()
    fixture = Path(sys.argv[2]).resolve()
    root = Path(__file__).resolve().parents[1]
    runs = 0
    with tempfile.TemporaryDirectory(prefix="receiver-observer-fixture-") as temporary:
        private = Path(temporary)
        private.chmod(0o700)
        testing = private / "fixture-observer.so"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-shared", "-fPIC",
                        "-fvisibility=hidden", "-ftls-model=initial-exec", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                        "-DFRAME_RECEIVER_OBSERVER_TEST_FIXTURE", str(root/"receiver/receiver_observer.cpp"),
                        str(root/"receiver/receiver_observer_core.cpp"), "-pthread", "-ldl", "-o", str(testing)], check=True)
        digest = hashlib.sha256(fixture.read_bytes()).hexdigest()

        def run(mode, library=testing, arm=True, wrong_hash=False, existing=False, relative=False, symlink=False):
            nonlocal runs
            runs += 1
            folder = private / str(runs)
            folder.mkdir(mode=0o700)
            output, marker = folder/"capture.jsonl", folder/"arm"
            if arm:
                marker.touch(mode=0o600)
            if existing:
                output.write_text("keep this evidence\n")
            if symlink:
                output.symlink_to(marker)
            env = os.environ.copy()
            env.update(LD_PRELOAD=str(library), FRAME_RECEIVER_TRACE_OUTPUT="relative.jsonl" if relative else str(output),
                       FRAME_RECEIVER_TRACE_ARM=str(marker), FRAME_RECEIVER_TEST_SHA256="0"*64 if wrong_hash else digest)
            result = subprocess.run([str(fixture), mode], env=env, capture_output=True, text=True, timeout=5)
            assert result.returncode == 0, (mode, result.returncode, result.stderr)
            if existing:
                assert output.read_text() == "keep this evidence\n"
                return []
            if symlink:
                assert output.is_symlink() and marker.read_bytes() == b""
                return []
            if not output.exists():
                return []
            assert output.stat().st_mode & 0o777 == 0o600
            assert output.stat().st_size < 1024*1024
            return [json.loads(line) for line in output.read_text().splitlines()]

        assert run("valid", production) == []  # An unrelated process cannot claim the capture path.
        assert run("valid", wrong_hash=True) == []
        assert run("valid", existing=True) == []
        assert run("valid", relative=True) == []
        assert run("valid", symlink=True) == []
        unarmed = run("valid", arm=False)
        assert unarmed[-1]["armed"] is False and unarmed[-1]["serialized"] == 0
        assert unarmed[-1]["selection_evidence_complete"] is False
        for mode, reason in [("valid", 0), ("missing", 32), ("mismatch", 128), ("invalid", 64),
                             ("changed", 256), ("multiple", 1024), ("lock-failure", 512)]:
            data = run(mode)
            assert data[0]["supported"] is True and data[-1]["kind"] == "footer"
            records = [r for r in data if r["kind"] == "selection_attempt"]
            assert len(records) == 1, (mode, data)
            record = records[0]
            assert record["reason_bits"] & reason == reason, (mode, record)
            assert record["metadata_valid"] is (reason == 0), (mode, record)
            assert data[-1]["selection_evidence_complete"] is (reason == 0), (mode, data[-1])
            assert record["accepted_submit"] is False and record["source_frame_associated"] is False
            assert data[-1]["pending_callbacks"] == 0 and data[-1]["active_scopes"] == 0
            if reason == 0:
                assert record["content_generation"] == 4294967303 and record["native_metadata_word"] == 1234
        cap = run("cap")
        assert cap[-1]["serialized"] == 32 and cap[-1]["queue_loss"] == 0 and cap[-1]["unmatched"] == 0
        assert cap[-1]["selection_evidence_complete"] is True
        incomplete = run("incomplete")
        assert incomplete[-1]["active_scopes"] == 1 and incomplete[-1]["serialized"] == 0
        assert incomplete[-1]["closed_quiescent"] is False
        assert incomplete[-1]["selection_evidence_complete"] is False

        # Force a real pthread_create failure and enter the wrappers recursively
        # during initialization. The receiver fixture itself creates no threads.
        fail_source = private / "failed-writer.cpp"
        fail_source.write_text('''#include <pthread.h>
#include <errno.h>
extern "C" int pthread_create(pthread_t*,const pthread_attr_t*,void*(*)(void*),void*) noexcept {
    pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER;
    int result=pthread_mutex_lock(&m);
    if (!result) result=pthread_mutex_unlock(&m);
    errno=55;
    return result?result:EAGAIN;
}
''')
        fail_library = private / "failed-writer.so"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-shared", "-fPIC", str(fail_source),
                        "-pthread", "-o", str(fail_library)], check=True)
        runs += 1
        folder = private / str(runs)
        folder.mkdir(mode=0o700)
        output, marker = folder/"capture.jsonl", folder/"arm"
        marker.touch(mode=0o600)
        env = os.environ.copy()
        env.update(LD_PRELOAD=f"{testing}:{fail_library}", FRAME_RECEIVER_TRACE_OUTPUT=str(output),
                   FRAME_RECEIVER_TRACE_ARM=str(marker), FRAME_RECEIVER_TEST_SHA256=digest)
        subprocess.run([str(fixture), "valid"], env=env, check=True, capture_output=True, timeout=5)
        refusal = [json.loads(line) for line in output.read_text().splitlines()]
        assert refusal[0]["supported"] is False and refusal[0]["reason"] == "writer_start_failed"
        assert refusal[-1]["serialized"] == 0

        # A real background writer holds the lock before arming and releases it
        # after its footer, independently of the receiver process lifetime.
        runs += 1
        folder = private / str(runs)
        folder.mkdir(mode=0o700)
        output, marker = folder/"capture.jsonl", folder/"arm"
        env = os.environ.copy()
        env.update(LD_PRELOAD=str(testing), FRAME_RECEIVER_TRACE_OUTPUT=str(output),
                   FRAME_RECEIVER_TRACE_ARM=str(marker), FRAME_RECEIVER_TEST_SHA256=digest)
        process = subprocess.Popen([str(fixture), "liveness"], env=env,
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        def records_until(predicate):
            expires = time.monotonic() + 2
            while time.monotonic() < expires:
                if output.exists():
                    lines = output.read_bytes().split(b"\n")[:-1]
                    rows = [json.loads(line) for line in lines]
                    if predicate(rows):
                        return rows
                assert process.poll() is None, "fixture exited before writer liveness check"
                time.sleep(0.005)
            raise AssertionError("writer liveness check timed out")
        try:
            before = records_until(lambda rows: rows and rows[0]["kind"] == "header")
            assert before[0]["writer_liveness"] == "exclusive_flock_v1"
            with output.open("rb") as reader:
                try:
                    fcntl.flock(reader, fcntl.LOCK_SH | fcntl.LOCK_NB)
                except BlockingIOError:
                    pass
                else:
                    raise AssertionError("active writer did not own its exclusive lock")
                marker.touch(mode=0o600)
                process.stdin.write(b"b")
                process.stdin.flush()
                rows = records_until(lambda rows: rows and rows[-1]["kind"] == "footer")
                assert rows[-1]["selection_evidence_complete"] is True
                # Header/footer and lock must agree while process still lives.
                expires = time.monotonic() + 1
                while True:
                    try:
                        fcntl.flock(reader, fcntl.LOCK_SH | fcntl.LOCK_NB)
                        break
                    except BlockingIOError:
                        assert time.monotonic() < expires, "closed writer retained liveness lock"
                        time.sleep(0.005)
                assert process.poll() is None
                process.stdin.write(b"e")
                process.stdin.flush()
                process.communicate(timeout=2)
                assert process.returncode == 0
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=2)
    print(f"receiver preload: {runs} isolated cases passed; actual host PLT forwarding, no hardware proof")


if __name__ == "__main__":
    main()
