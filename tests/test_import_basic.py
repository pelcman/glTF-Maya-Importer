# -*- coding: utf-8 -*-
# Basic round-trip test: export a cube with glTFExporter, import with glTFImporter.
import os
import struct
import sys
import tempfile
import traceback
import zlib

# Configuration (override with environment variables):
#   MAYA_VER          target Maya version           (default: 2024)
#   GLTF_IMPORTER_MLL path to glTFImporter.mll      (default: build<ver>/Release)
#   GLTF_EXPORTER_MLL path to glTFExporter.mll      (default: sibling
#                     glTF-Maya-Exporter/releases/Maya<ver>)
VER = os.environ.get("MAYA_VER", "2024")
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMPORTER = os.environ.get(
    "GLTF_IMPORTER_MLL",
    os.path.join(REPO, "build" + VER, "Release", "glTFImporter.mll"))
EXPORTER = os.environ.get(
    "GLTF_EXPORTER_MLL",
    os.path.join(os.path.dirname(REPO), "glTF-Maya-Exporter",
                 "releases", "Maya" + VER, "glTFExporter.mll"))
OUT = os.environ.get("GLTF_TEST_OUT", tempfile.mkdtemp(prefix="gltf_import_test_"))

import maya.standalone
maya.standalone.initialize()
import maya.cmds as cmds

failures = []

def check(cond, label):
    print(("PASS: " if cond else "FAIL: ") + label)
    if not cond:
        failures.append(label)

try:
    cmds.loadPlugin(EXPORTER)
    cmds.loadPlugin(IMPORTER)
    check(True, "plugins loaded")

    # ---- build source scene ----
    cmds.file(new=True, force=True)
    cube = cmds.polyCube(w=2, h=2, d=2, name="testCube")[0]
    sh = cmds.shadingNode("standardSurface", asShader=True, name="redMat")
    cmds.setAttr(sh + ".baseColor", 1, 0, 0, type="double3")
    cmds.setAttr(sh + ".metalness", 0.75)
    cmds.setAttr(sh + ".specularRoughness", 0.25)
    sg = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name="redSG")
    cmds.connectAttr(sh + ".outColor", sg + ".surfaceShader", force=True)
    cmds.sets(cube, e=True, forceElement=sg)
    cmds.setAttr(cube + ".translate", 1, 2, 3, type="double3")
    cmds.setAttr(cube + ".rotate", 0, 45, 0, type="double3")

    glb = os.path.join(OUT, "cube.glb").replace("\\", "/")
    cmds.file(rename=os.path.join(OUT, "scene.ma"))
    cmds.file(glb, force=True, options="output_buffer=0;", type="GLB Export", pr=True, ea=True)
    check(os.path.isfile(glb), "GLB exported: " + glb)

    # ---- import back ----
    cmds.file(new=True, force=True)
    cmds.file(glb, i=True, type="glTF Import",
              options="import_animations=1;import_skins=1;import_blendshapes=1;")

    meshes = cmds.ls(type="mesh", long=True)
    check(len(meshes) == 1, "one mesh imported (got %d)" % len(meshes))
    if meshes:
        nv = cmds.polyEvaluate(meshes[0], vertex=True)
        nf = cmds.polyEvaluate(meshes[0], face=True)
        print("verts=%s faces=%s" % (nv, nf))
        check(nf == 12, "12 triangles (got %s)" % nf)

        # transform of the imported node
        tr = cmds.listRelatives(meshes[0], parent=True, fullPath=True)[0]
        t = cmds.getAttr(tr + ".translate")[0]
        r = cmds.getAttr(tr + ".rotate")[0]
        check(all(abs(a - b) < 1e-3 for a, b in zip(t, (1, 2, 3))),
              "translate round-trip %s" % (t,))
        check(abs(r[1] - 45.0) < 1e-3, "rotateY round-trip %s" % (r,))

        # world-space bounding box should be that of a 2x2x2 cube
        bb = cmds.exactWorldBoundingBox(tr)
        size = (bb[3] - bb[0], bb[4] - bb[1], bb[5] - bb[2])
        check(abs(size[1] - 2.0) < 1e-3, "bbox height 2 (got %s)" % (size,))

    mats = [m for m in cmds.ls(type="standardSurface") if m != "standardSurface1"]
    check(len(mats) == 1, "one standardSurface imported (got %s)" % mats)
    if mats:
        bc = cmds.getAttr(mats[0] + ".baseColor")[0]
        mt = cmds.getAttr(mats[0] + ".metalness")
        rg = cmds.getAttr(mats[0] + ".specularRoughness")
        print("baseColor=%s metalness=%s roughness=%s" % (bc, mt, rg))
        check(abs(bc[0] - 1.0) < 0.02 and bc[1] < 0.02 and bc[2] < 0.02, "baseColor red")
        check(abs(mt - 0.75) < 0.02, "metalness 0.75 (got %s)" % mt)
        check(abs(rg - 0.25) < 0.02, "roughness 0.25 (got %s)" % rg)

    # ---- .gltf (text) with embedded base64 buffer: hand-written triangle ----
    import base64, struct, json
    pos = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
    idx = struct.pack("<3H", 0, 1, 2) + b"\x00\x00"
    buf = pos + idx
    gltf_json = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "tri"}],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": 0}, "indices": 1}]}],
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
    tri_path = os.path.join(OUT, "tri.gltf").replace("\\", "/")
    with open(tri_path, "w") as f:
        json.dump(gltf_json, f)

    cmds.file(new=True, force=True)
    cmds.file(tri_path, i=True, type="glTF Import", options="")
    tri_meshes = cmds.ls(type="mesh")
    check(len(tri_meshes) == 1, "triangle .gltf imported")
    if tri_meshes:
        check(cmds.polyEvaluate(tri_meshes[0], vertex=True) == 3, "3 vertices")
        check(cmds.polyEvaluate(tri_meshes[0], face=True) == 1, "1 face")
        parent = cmds.listRelatives(tri_meshes[0], parent=True)[0]
        check(parent == "tri", "node name kept (got %s)" % parent)

except Exception:
    traceback.print_exc()
    failures.append("exception")

print("=" * 40)
print("RESULT: %s (%d failures)" % ("OK" if not failures else "NG", len(failures)))
for f in failures:
    print("  - " + f)
sys.exit(1 if failures else 0)
