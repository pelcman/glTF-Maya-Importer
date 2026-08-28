# -*- coding: utf-8 -*-
# Round-trip test: textured material (embedded GLB texture + .gltf external).
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


def write_png(path, rgb, size=4):
    # minimal solid-color RGB png
    raw = b""
    for _y in range(size):
        raw += b"\x00" + bytes(bytearray(rgb * size))
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + \
          chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


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

    tex_png = os.path.join(OUT, "checker_src.png").replace("\\", "/")
    write_png(tex_png, b"\x00\x80\xff")

    def build_textured_scene():
        cmds.file(new=True, force=True)
        plane = cmds.polyPlane(w=2, h=2, sx=1, sy=1, name="texPlane")[0]
        sh = cmds.shadingNode("standardSurface", asShader=True, name="texMat")
        sg = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name="texSG")
        cmds.connectAttr(sh + ".outColor", sg + ".surfaceShader", force=True)
        fileNode = cmds.shadingNode("file", asTexture=True, isColorManaged=True)
        cmds.setAttr(fileNode + ".fileTextureName", tex_png, type="string")
        cmds.connectAttr(fileNode + ".outColor", sh + ".baseColor", force=True)
        cmds.sets(plane, e=True, forceElement=sg)
        return plane

    def verify_import(label):
        meshes = cmds.ls(type="mesh", noIntermediate=True)
        check(len(meshes) == 1, label + ": mesh imported (got %s)" % meshes)
        files = cmds.ls(type="file")
        check(len(files) == 1, label + ": file texture node created (got %s)" % files)
        ok_conn = False
        tex_path = ""
        if files:
            tex_path = cmds.getAttr(files[0] + ".fileTextureName")
            conns = cmds.listConnections(files[0] + ".outColor", d=True, s=False, plugs=True) or []
            ok_conn = any(".baseColor" in c for c in conns)
        check(os.path.isfile(tex_path), label + ": texture file exists (%s)" % tex_path)
        check(ok_conn, label + ": file.outColor -> baseColor connected")
        p2d = cmds.ls(type="place2dTexture")
        check(len(p2d) == 1, label + ": place2dTexture created")

    # ---- GLB (textures embedded) ----
    build_textured_scene()
    glb = os.path.join(OUT, "tex.glb").replace("\\", "/")
    cmds.file(rename=os.path.join(OUT, "tex_scene.ma"))
    cmds.file(glb, force=True, options="output_buffer=0;", type="GLB Export", pr=True, ea=True)
    check(os.path.isfile(glb), "textured GLB exported")

    cmds.file(new=True, force=True)
    cmds.file(glb, i=True, type="glTF Import", options="")
    verify_import("GLB")

    # ---- .gltf (external files) ----
    build_textured_scene()
    gltf_dir = os.path.join(OUT, "tex_gltf")
    if not os.path.isdir(gltf_dir):
        os.makedirs(gltf_dir)
    gltf = os.path.join(gltf_dir, "tex.gltf").replace("\\", "/")
    cmds.file(rename=os.path.join(OUT, "tex_scene2.ma"))
    cmds.file(gltf, force=True, options="output_buffer=0;", type="GLTF Export", pr=True, ea=True)
    # the exporter writes .gltf into a subdirectory named after the file
    gltf = os.path.join(gltf_dir, "tex", "tex.gltf").replace("\\", "/")
    check(os.path.isfile(gltf), ".gltf exported")

    cmds.file(new=True, force=True)
    cmds.file(gltf, i=True, type="glTF Import", options="")
    verify_import("GLTF")

except Exception:
    traceback.print_exc()
    failures.append("exception")

print("=" * 40)
print("RESULT: %s (%d failures)" % ("OK" if not failures else "NG", len(failures)))
for f in failures:
    print("  - " + f)
sys.exit(1 if failures else 0)
