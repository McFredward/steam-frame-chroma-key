#!/usr/bin/env python3
"""Check reviewed trace dlopen/discovery without creating a Vulkan instance.

Temporary manifest paths apply only to this subprocess and are removed on exit.
No Vulkan device, shader, surface, swapchain or existing process is touched.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import tempfile


class Layer(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char * 256), ("spec", ctypes.c_uint32),
                ("implementation", ctypes.c_uint32), ("description", ctypes.c_char * 256)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("--sha256", required=True)
    args = parser.parse_args()
    library = args.library.resolve()
    if not library.is_file() or library.stat().st_size > 256 * 1024:
        raise RuntimeError("Unexpected diagnostic library size/type")
    if hashlib.sha256(library.read_bytes()).hexdigest() != args.sha256:
        raise RuntimeError("Diagnostic library hash does not match reviewed build")
    ctypes.CDLL(str(library), mode=os.RTLD_LOCAL | os.RTLD_NOW)
    manifest = {"file_format_version": "1.2.0", "layer": {
        "name": "VK_LAYER_FRAME_MASK_trace", "type": "GLOBAL",
        "library_path": str(library), "api_version": "1.3.0",
        "implementation_version": 1, "description": "Temporary discovery-only probe",
        "enable_environment": {"FRAME_MASK_TRACE": "1"},
        "disable_environment": {"FRAME_MASK_TRACE_DISABLE": "1"}}}
    with tempfile.TemporaryDirectory(prefix="frame-trace-loader-probe-") as temporary:
        folder = Path(temporary)
        implicit = folder / "vulkan/implicit_layer.d"
        implicit.mkdir(parents=True)
        (implicit / "frame_mask_trace.json").write_text(json.dumps(manifest))
        # Portable discovery route. These changes live only in this process.
        os.environ["XDG_DATA_HOME"] = str(folder)
        os.environ["FRAME_MASK_TRACE"] = "1"
        os.environ.pop("FRAME_MASK_TRACE_DISABLE", None)
        loader = ctypes.CDLL("libvulkan.so.1")
        enumerate_layers = loader.vkEnumerateInstanceLayerProperties
        enumerate_layers.argtypes = [ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(Layer)]
        enumerate_layers.restype = ctypes.c_int32
        count = ctypes.c_uint32()
        result = enumerate_layers(ctypes.byref(count), None)
        if result != 0 or count.value > 128:
            raise RuntimeError(f"Unusable bounded layer enumeration: {result}, {count.value}")
        layers = (Layer * count.value)()
        result = enumerate_layers(ctypes.byref(count), layers)
        if result != 0:
            raise RuntimeError(f"Layer enumeration incomplete: {result}")
        names = [item.name.decode("utf-8", "replace") for item in layers[:count.value]]
        print(json.dumps({"dlopen_ok": True, "trace_discovered": "VK_LAYER_FRAME_MASK_trace" in names,
                          "layer_names": names, "instance_created": False, "device_created": False}))


if __name__ == "__main__":
    main()
