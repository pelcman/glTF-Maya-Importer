# -*- coding: utf-8 -*-
# Round-trip test: skinning, animation, blend shapes.
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

EXPORT_OPTS = "output_buffer=0;output_animations=1;"

try:
    cmds.loadPlugin(EXPORTER)
    cmds.loadPlugin(IMPORTER)

    # ================= skinned cylinder with joint animation =================
    cmds.file(new=True, force=True)
    cmds.select(clear=True)
    j1 = cmds.joint(p=(0, 0, 0), name="rootJoint")
    j2 = cmds.joint(p=(0, 2, 0), name="midJoint")
    cyl = cmds.polyCylinder(r=0.5, h=4, sy=8, name="skinCyl")[0]
    cmds.setAttr(cyl + ".translateY", 2)
    cmds.makeIdentity(cyl, apply=True, t=True, r=True, s=True)
    sc = cmds.skinCluster(j1, j2, cyl, toSelectedBones=True)[0]

    cmds.setKeyframe(j2, attribute="rotateZ", t=1, value=0)
    cmds.setKeyframe(j2, attribute="rotateZ", t=24, value=45)

    glb = os.path.join(OUT, "skin.glb").replace("\\", "/")
    cmds.file(rename=os.path.join(OUT, "skin_scene.ma"))
    cmds.file(glb, force=True, options=EXPORT_OPTS, type="GLB Export", pr=True, ea=True)
    check(os.path.isfile(glb), "skinned GLB exported")

    # record source deformation at frame 24 (world pos of top-most vertex)
    cmds.currentTime(24)
    src_top = None
    nv = cmds.polyEvaluate(cyl, vertex=True)
    for i in range(nv):
        p = cmds.pointPosition(cyl + ".vtx[%d]" % i, world=True)
        if src_top is None or p[1] > src_top[1]:
            src_top = p
    print("source top vertex @24:", src_top)

    # ---- import back ----
    cmds.file(new=True, force=True)
    cmds.file(glb, i=True, type="glTF Import",
              options="import_animations=1;import_skins=1;import_blendshapes=1;")

    joints = cmds.ls(type="joint")
    check(len(joints) == 2, "2 joints imported (got %s)" % joints)
    scs = cmds.ls(type="skinCluster")
    check(len(scs) == 1, "1 skinCluster imported (got %s)" % scs)
    meshes = cmds.ls(type="mesh", noIntermediate=True)
    check(len(meshes) == 1, "1 mesh imported (got %s)" % meshes)

    curves = cmds.ls(type="animCurveTA")
    check(len(curves) > 0, "rotation animCurves imported (got %d)" % len(curves))

    if meshes:
        cmds.currentTime(24)
        dst_top = None
        tr = cmds.listRelatives(meshes[0], parent=True, fullPath=True)[0]
        nv = cmds.polyEvaluate(meshes[0], vertex=True)
        for i in range(nv):
            p = cmds.pointPosition(tr + ".vtx[%d]" % i, world=True)
            if dst_top is None or p[1] > dst_top[1]:
                dst_top = p
        print("imported top vertex @24:", dst_top)
        ok = all(abs(a - b) < 0.05 for a, b in zip(src_top, dst_top))
        check(ok, "skinned deformation matches at frame 24 (src=%s dst=%s)" % (src_top, dst_top))

    # playback range
    check(cmds.playbackOptions(q=True, maxTime=True) >= 23.9,
          "playback range extended (max=%s)" % cmds.playbackOptions(q=True, maxTime=True))

    # ================= blend shape with weight animation =================
    cmds.file(new=True, force=True)
    base = cmds.polySphere(r=1, sx=8, sy=8, name="baseSphere")[0]
    tgt = cmds.polySphere(r=1, sx=8, sy=8, name="spike")[0]
    cmds.setAttr(tgt + ".scaleY", 2.0)
    cmds.makeIdentity(tgt, apply=True, s=True)
    cmds.setAttr(tgt + ".translateX", 5)  # keep target geometry alive for the exporter
    bs = cmds.blendShape(tgt, base, name="testBS")[0]
    cmds.setKeyframe(bs + ".w[0]", t=1, value=0)
    cmds.setKeyframe(bs + ".w[0]", t=24, value=1)

    glb2 = os.path.join(OUT, "morph.glb").replace("\\", "/")
    cmds.file(rename=os.path.join(OUT, "morph_scene.ma"))
    cmds.file(glb2, force=True, options=EXPORT_OPTS, type="GLB Export", pr=True, ea=True)
    check(os.path.isfile(glb2), "morph GLB exported")

    cmds.currentTime(24)
    src_bb = cmds.exactWorldBoundingBox(base)
    src_h = src_bb[4] - src_bb[1]
    print("source morph height @24:", src_h)

    cmds.file(new=True, force=True)
    cmds.file(glb2, i=True, type="glTF Import",
              options="import_animations=1;import_skins=1;import_blendshapes=1;")

    bss = cmds.ls(type="blendShape")
    check(len(bss) == 1, "1 blendShape imported (got %s)" % bss)
    meshes = [m for m in cmds.ls(type="mesh", noIntermediate=True, long=True)
              if "baseSphere" in m]
    check(len(meshes) == 1, "morph base mesh imported (got %s)" % meshes)
    if bss and meshes:
        cmds.currentTime(24)
        w = cmds.getAttr(bss[0] + ".w[0]")
        check(abs(w - 1.0) < 1e-3, "blend weight 1.0 at frame 24 (got %s)" % w)
        tr = cmds.listRelatives(meshes[0], parent=True, fullPath=True)[0]
        bb = cmds.exactWorldBoundingBox(tr)
        h = bb[4] - bb[1]
        print("imported morph height @24:", h)
        check(abs(h - src_h) < 0.05, "morph deformation matches (src=%s dst=%s)" % (src_h, h))
        cmds.currentTime(1)
        bb1 = cmds.exactWorldBoundingBox(tr)
        h1 = bb1[4] - bb1[1]
        check(abs(h1 - 2.0) < 0.05, "morph at frame 1 is base shape (h=%s)" % h1)

    # ================= translation / scale animation on plain node ==========
    cmds.file(new=True, force=True)
    box = cmds.polyCube(name="animBox")[0]
    cmds.setKeyframe(box, attribute="translateX", t=1, value=0)
    cmds.setKeyframe(box, attribute="translateX", t=24, value=5)
    cmds.setKeyframe(box, attribute="rotateY", t=1, value=0)
    cmds.setKeyframe(box, attribute="rotateY", t=24, value=450)  # >360 degrees
    glb3 = os.path.join(OUT, "anim.glb").replace("\\", "/")
    cmds.file(rename=os.path.join(OUT, "anim_scene.ma"))
    cmds.file(glb3, force=True, options=EXPORT_OPTS, type="GLB Export", pr=True, ea=True)

    cmds.file(new=True, force=True)
    cmds.file(glb3, i=True, type="glTF Import", options="import_animations=1;")
    boxes = [t for t in cmds.ls(type="transform") if "animBox" in t]
    check(len(boxes) >= 1, "animBox imported (got %s)" % boxes)
    if boxes:
        node = boxes[0]
        cmds.currentTime(24)
        tx = cmds.getAttr(node + ".translateX")
        ry = cmds.getAttr(node + ".rotateY")
        check(abs(tx - 5.0) < 1e-2, "translateX 5 at frame 24 (got %s)" % tx)
        check(abs(ry - 450.0) < 2.0, "rotateY 450 at frame 24 (got %s) [euler unroll]" % ry)
        cmds.currentTime(12)
        ry12 = cmds.getAttr(node + ".rotateY")
        check(150.0 < ry12 < 300.0, "rotateY mid-range at frame 12 (got %s)" % ry12)

except Exception:
    traceback.print_exc()
    failures.append("exception")

print("=" * 40)
print("RESULT: %s (%d failures)" % ("OK" if not failures else "NG", len(failures)))
for f in failures:
    print("  - " + f)
sys.exit(1 if failures else 0)
