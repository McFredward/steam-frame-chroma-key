#!/usr/bin/env python3
"""Check reviewed trace dlopen/discovery without creating a Vulkan instance.

Temporary manifest paths apply only to this subprocess and are removed on exit.
No Vulkan device, shader, surface, swapchain or existing process is touched.
"""
import argparse
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile


SPEC = importlib.util.spec_from_file_location(
    "frame_trace_session", Path(__file__).resolve().with_name("frame_trace_session.py"))
SESSION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SESSION)


class Layer(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char * 256), ("spec", ctypes.c_uint32),
                ("implementation", ctypes.c_uint32), ("description", ctypes.c_char * 256)]


def validate_manifest(manifest):
    """Reject the numeric-version bug even when an older loader tolerates it."""
    layer = manifest["layer"]
    if layer.get("implementation_version") != "1" or layer.get("api_version") != "1.3.0":
        raise RuntimeError("Trace manifest version fields must be the reviewed strings")
    if layer.get("name") != "VK_LAYER_FRAME_MASK_trace":
        raise RuntimeError("Unexpected diagnostic layer name")
    return layer


def validate_properties(properties, manifest):
    expected = validate_manifest(manifest)
    matching = [item for item in properties if item.name.decode("utf-8", "replace") == expected["name"]]
    if len(matching) != 1:
        raise RuntimeError("Diagnostic manifest was not uniquely discovered")
    item = matching[0]
    # A loader can expose a partially initialized name after rejecting a field.
    # Name presence alone is not a successful manifest parse.
    if (item.spec != ((1 << 22) | (3 << 12)) or item.implementation != 1
            or item.description.decode("utf-8", "replace") != expected["description"]):
        raise RuntimeError("Discovered trace properties do not match the complete manifest")


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
    with tempfile.TemporaryDirectory(prefix="frame-trace-loader-probe-") as temporary:
        folder = Path(temporary)
        # Exercise exactly the temporary installation helper's serialized bytes,
        # rather than a separately authored discovery-only manifest.
        session = SESSION.Session(folder, folder / "runtime")
        session.folder.mkdir(parents=True)
        shutil.copy2(library, session.folder / "libVkLayer_frame_mask_trace.so")
        manifest_bytes = session.contents()[session.manifest]
        manifest = json.loads(manifest_bytes)
        validate_manifest(manifest)
        implicit = folder / "vulkan/implicit_layer.d"
        implicit.mkdir(parents=True)
        (implicit / "frame_mask_trace.json").write_bytes(manifest_bytes)
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
        validate_properties(layers[:count.value], manifest)
        names = [item.name.decode("utf-8", "replace") for item in layers[:count.value]]
        print(json.dumps({"dlopen_ok": True, "trace_discovered": True,
                          "manifest_properties_match": True,
                          "manifest_source": "frame_trace_session.Session.contents",
                          "layer_names": names, "instance_created": False, "device_created": False,
                          "instance_chain_verified": False}))


if __name__ == "__main__":
    main()
