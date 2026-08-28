# -*- coding: utf-8 -*-
# Round-trip test: draco-compressed GLB (the exporter's default output).
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

    # ---- draco cube (default exporter options = draco on) ----
    cmds.file(new=True, force=True)
    cube = cmds.polyCube(w=2, h=2, d=2, name="dracoCube")[0]
    cmds.setAttr(cube + ".translate", 1, 2, 3, type="double3")
    glb = os.path.join(OUT, "draco_cube.glb").replace("\\", "/")
    cmds.file(rename=os.path.join(OUT, "draco_scene.ma"))
    cmds.file(glb, force=True, options="", type="GLB Export", pr=True, ea=True)
    check(os.path.isfile(glb), "draco GLB exported")

    cmds.file(new=True, force=True)
    cmds.file(glb, i=True, type="glTF Import", options="")
    meshes = cmds.ls(type="mesh", noIntermediate=True)
    check(len(meshes) == 1, "draco mesh imported (got %s)" % meshes)
    if meshes:
        nf = cmds.polyEvaluate(meshes[0], face=True)
        check(nf == 12, "12 triangles (got %s)" % nf)
        tr = cmds.listRelatives(meshes[0], parent=True, fullPath=True)[0]
        bb = cmds.exactWorldBoundingBox(tr)
        size = (bb[3] - bb[0], bb[4] - bb[1], bb[5] - bb[2])
        center = ((bb[0] + bb[3]) / 2, (bb[1] + bb[4]) / 2, (bb[2] + bb[5]) / 2)
        # draco quantizes positions; allow small tolerance
        check(all(abs(s - 2.0) < 0.02 for s in size), "cube size 2 (got %s)" % (size,))
        check(all(abs(c - e) < 0.02 for c, e in zip(center, (1, 2, 3))),
              "cube position (got %s)" % (center,))
        # UVs survived draco?
        uvs = cmds.polyEvaluate(meshes[0], uvcoord=True)
        check(uvs > 0, "UVs decoded from draco (got %s)" % uvs)

    # ---- draco skinned cylinder ----
    cmds.file(new=True, force=True)
    cmds.select(clear=True)
    j1 = cmds.joint(p=(0, 0, 0), name="rootJoint")
    j2 = cmds.joint(p=(0, 2, 0), name="midJoint")
    cyl = cmds.polyCylinder(r=0.5, h=4, sy=8, name="dracoSkinCyl")[0]
    cmds.setAttr(cyl + ".translateY", 2)
    cmds.makeIdentity(cyl, apply=True, t=True, r=True, s=True)
    cmds.skinCluster(j1, j2, cyl, toSelectedBones=True)
    cmds.setKeyframe(j2, attribute="rotateZ", t=1, value=0)
    cmds.setKeyframe(j2, attribute="rotateZ", t=24, value=45)
    glb2 = os.path.join(OUT, "draco_skin.glb").replace("\\", "/")
    cmds.file(rename=os.path.join(OUT, "draco_skin_scene.ma"))
    cmds.file(glb2, force=True, options="output_animations=1;", type="GLB Export", pr=True, ea=True)

    cmds.currentTime(24)
    src_top = None
    for i in range(cmds.polyEvaluate(cyl, vertex=True)):
        p = cmds.pointPosition(cyl + ".vtx[%d]" % i, world=True)
        if src_top is None or p[1] > src_top[1]:
            src_top = p

    cmds.file(new=True, force=True)
    cmds.file(glb2, i=True, type="glTF Import", options="")
    joints = cmds.ls(type="joint")
    check(len(joints) == 2, "2 joints imported from draco skin (got %s)" % joints)
    scs = cmds.ls(type="skinCluster")
    check(len(scs) == 1, "1 skinCluster from draco skin (got %s)" % scs)
    meshes = cmds.ls(type="mesh", noIntermediate=True)
    check(len(meshes) == 1, "1 draco skinned mesh (got %s)" % meshes)
    if meshes:
        cmds.currentTime(24)
        tr = cmds.listRelatives(meshes[0], parent=True, fullPath=True)[0]
        dst_top = None
        for i in range(cmds.polyEvaluate(meshes[0], vertex=True)):
            p = cmds.pointPosition(tr + ".vtx[%d]" % i, world=True)
            if dst_top is None or p[1] > dst_top[1]:
                dst_top = p
        print("src_top=%s dst_top=%s" % (src_top, dst_top))
        ok = all(abs(a - b) < 0.05 for a, b in zip(src_top, dst_top))
        check(ok, "draco skinned deformation matches at frame 24")

except Exception:
    traceback.print_exc()
    failures.append("exception")

print("=" * 40)
print("RESULT: %s (%d failures)" % ("OK" if not failures else "NG", len(failures)))
for f in failures:
    print("  - " + f)
sys.exit(1 if failures else 0)
