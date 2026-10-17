"""Check the actual C++ producer against its Python consumer, with owned facts.

The producer explicitly uses a synthetic lease. Its initial incomplete provider
must refuse. Controlled value mutations test schema/readiness only; neither this
script nor its fake positive receipt establishes a native provider or frame.
"""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


def main():
    assert len(sys.argv) == 2, "explicit owned C++ producer required"
    result = subprocess.run([sys.argv[1], "--provider-status-json"], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=10, check=True)
    assert not result.stderr and len(result.stdout) <= 8192, "bounded real producer output"
    events = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(events) == 3 and events[0]["native_authority"] is False
    assert "synthetic" in events[0]["scope"]
    provider, prepared = events[1:]
    assert provider["event"] == "native_subpass_provider" and prepared["event"] == "prepared"
    required = {"state", "reason", "epoch", "device", "module_base", "inode", "chip", "route_mask",
                "entry_rvas", "provider_path", "provider_path_bytes", "provider_path_truncated",
                "mask_enabled", "native_frame_association_proven"}
    assert required <= provider.keys(), "actual C++/Python provider schema drift"
    assert type(prepared["native_device"]) is int and prepared["native_device"] == provider["device"]
    spec = importlib.util.spec_from_file_location("actual_provider_gate", Path(__file__).parents[1] / "tools/pixel_stream_preflight.py")
    gate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    assert not gate._native_provider(events, [prepared])["ready"], "synthetic default cannot claim a verified native driver"
    # These keys must already exist in the ACTUAL producer. Change values only;
    # inventing a parallel Python schema would miss producer spelling changes.
    control = copy.deepcopy(events)
    control[1].update(module_base=4096, inode=9, chip=7,
                      entry_rvas=[0xb12180, 0xb12180, 0xb11d60, 0xb11d60],
                      provider_path=gate._NATIVE_DRIVER_PATH, provider_path_bytes=len(gate._NATIVE_DRIVER_PATH))
    assert gate._native_provider(control, [prepared])["ready"], "controlled schema-positive receipt"
    for mutation in ("missing_path_bytes", "changed_device", "revoked", "changed_khr_entry"):
        bad = copy.deepcopy(control)
        if mutation == "missing_path_bytes":
            del bad[1]["provider_path_bytes"]
        elif mutation == "changed_device":
            bad[1]["device"] += 1
        elif mutation == "revoked":
            bad[1]["state"] = "revoked"
        else:
            bad[1]["entry_rvas"][1] += 4
        assert not gate._native_provider(bad, [prepared])["ready"], mutation
    print(json.dumps({"status": "PASS", "actual_cpp_producer": True, "causal_refusals": 5,
                      "native_provider_verified": False, "passthrough_authorized": False}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
