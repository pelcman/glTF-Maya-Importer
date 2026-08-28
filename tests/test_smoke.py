# -*- coding: utf-8 -*-
# Self-contained smoke test: load the importer and import a hand-written
# triangle .gltf (no exporter needed). Run with mayapy:
#   mayapy tests/test_smoke.py            (uses build<MAYA_VER>/Release)
#   MAYA_VER=2026 mayapy tests/test_smoke.py
import base64
import json
import os
import struct
import sys
import tempfile

VER = os.environ.get("MAYA_VER", "2024")
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMPORTER = os.environ.get(
    "GLTF_IMPORTER_MLL",
    os.path.join(REPO, "build" + VER, "Release", "glTFImporter.mll"))
OUT = tempfile.mkdtemp(prefix="gltf_import_smoke_")

import maya.standalone
maya.standalone.initialize()
import maya.cmds as cmds

try:
    cmds.loadPlugin(IMPORTER)

    pos = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
    idx = struct.pack("<3H", 0, 1, 2) + b"\x00\x00"
    buf = pos + idx
    doc = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "tri"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "buffers": [{"uri": "data:application/octet-stream;base64," +
                     base64.b64encode(buf).decode("ascii"),
                     "byteLength": len(buf)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36},
            {"buffer": 0, "byteOffset": 36, "byteLength": 6}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
             "min": [0, 0, 0], "max": [1, 1, 0]},
            {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}],
    }
    path = os.path.join(OUT, "tri.gltf").replace("\\", "/")
    with open(path, "w") as f:
        json.dump(doc, f)

    cmds.file(new=True, force=True)
    cmds.file(path, i=True, type="glTF Import", options="")
    meshes = cmds.ls(type="mesh", noIntermediate=True)
    ok = (len(meshes) == 1 and
          cmds.polyEvaluate(meshes[0], vertex=True) == 3 and
          cmds.polyEvaluate(meshes[0], face=True) == 1)
    print("SMOKE %s: %s (meshes=%s)" % (VER, "OK" if ok else "NG", meshes))
    sys.exit(0 if ok else 1)
except Exception as e:
    print("SMOKE %s: NG (%s)" % (VER, e))
    sys.exit(1)
