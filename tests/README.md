# Tests

mayapy-based regression tests. Run each script with the target Maya's
`mayapy.exe`; exit code 0 means all checks passed.

```powershell
& "C:\Program Files\Autodesk\Maya2024\bin\mayapy.exe" tests\test_smoke.py
& "C:\Program Files\Autodesk\Maya2024\bin\mayapy.exe" tests\test_import_basic.py
& "C:\Program Files\Autodesk\Maya2024\bin\mayapy.exe" tests\test_import_anim.py
& "C:\Program Files\Autodesk\Maya2024\bin\mayapy.exe" tests\test_import_draco.py
& "C:\Program Files\Autodesk\Maya2024\bin\mayapy.exe" tests\test_import_texture.py
```

| script | covers | needs exporter |
|--------|--------|----------------|
| test_smoke.py | plugin load + minimal .gltf | no |
| test_import_basic.py | mesh / transform / material round-trip, base64 .gltf | yes |
| test_import_anim.py | skinning, blend shapes, TRS animation, euler unroll | yes |
| test_import_draco.py | draco-compressed GLB incl. skinned mesh | yes |
| test_import_texture.py | embedded GLB textures, external .gltf textures | yes |

Configuration via environment variables:

- `MAYA_VER` — target version (default `2024`); selects
  `build<ver>/Release/glTFImporter.mll` and the exporter's
  `releases/Maya<ver>` binary
- `GLTF_IMPORTER_MLL` / `GLTF_EXPORTER_MLL` — explicit plugin paths.
  The round-trip tests expect
  [glTF-Maya-Exporter](https://github.com/pelcman/glTF-Maya-Exporter)
  checked out as a sibling directory (its `releases/` binaries are used)
- `GLTF_TEST_OUT` — output directory (default: fresh temp dir)
