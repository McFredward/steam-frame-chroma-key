#!/usr/bin/env python3
"""One focused actual-driver EDS3 cold-factory acceptance and ownership case."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("owned_graphics_case", ROOT / "tests/native-fragment-owned-clone-gpu/run.py")
case = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(case)


def main():
    sources = [Path(__file__), Path(__file__).with_name("fixture.cpp"),
               ROOT / "tests/native-fragment-owned-clone-gpu/fixture.cpp",
               ROOT / "layer/pixel_graphics_definition.h"]
    before = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
    # Reuse the established shader-pin/transform/compiler/driver/active-VVL
    # runner. Its fixture selector now points at this one NEW case.
    arguments = sys.argv[1:]
    record = Path(arguments[arguments.index("--record") + 1]) if "--record" in arguments else None
    original_writer = case.tool.recovery.write_owned
    failed = [False]

    def write_receipt(path, data):
        if path == record:
            report = json.loads(data)
            report["schema"] = "owned-extended-dynamic-graphics-proof-v1"
            report["scope"] = "Actual queried/enabled EDS3 device and original native VS/PS pipeline; production capture and cold factory recreate exact dynamic state after original object retirement and input overwrite. No draw/command/queue work."
            report.setdefault("source_sha256", {}).update(before)
            report["extended_source_changed_during_run"] = [name for name, digest in before.items() if hashlib.sha256(Path(name).read_bytes()).hexdigest() != digest]
            for device in report["devices"]:
                data = device["run"].get("result", {})
                if not data.get("eds3_original_and_owned_factory_admitted") or not data.get("extra_original_dynamic_inputs_wiped") or data.get("eds3_dynamic_count", 0) < 3:
                    failed[0] = True
            if report["extended_source_changed_during_run"]:
                failed[0] = True
            if failed[0]:
                report["status"] = "FAILED"
            data = (json.dumps(report, indent=2, sort_keys=True) + "\n").encode()
        # Preserve the original exclusive, private, no-symlink output writer.
        return original_writer(path, data)

    case.__file__ = __file__
    case.tool.recovery.write_owned = write_receipt
    try:
        result = case.main()
    finally:
        case.tool.recovery.write_owned = original_writer
    return result or int(failed[0])


if __name__ == "__main__":
    raise SystemExit(main())
