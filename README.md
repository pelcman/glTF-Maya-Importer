# glTF-Maya-Importer

glTF 2.0 importer plugin for Autodesk Maya (2022–2027, Windows).

Companion project to [glTF-Maya-Exporter](https://github.com/pelcman/glTF-Maya-Exporter):
files exported with it (including its default Draco-compressed output) round-trip
back into Maya, and standard glTF 2.0 assets from other tools import as well.

## Features

- `.gltf` / `.glb` (GLB BIN chunk, external `.bin`, base64 data URIs)
- `KHR_draco_mesh_compression` decode (draco 1.3.3)
- Scene graph: node hierarchy, TRS / matrix transforms, node names
- Meshes: normals, two UV sets (`map1` / `map2`), vertex colors;
  multiple primitives are merged into one mesh with per-face material
  assignment; triangle strips / fans are converted
- Materials: `standardSurface` with baseColor / metallic / roughness /
  emissive / normal map / alpha; file textures with `place2dTexture`
  and sampler wrap modes; embedded images are extracted to
  `<name>_textures/` next to the source file
- Skinning: joints, `skinCluster` with exact weights and
  `bindPreMatrix` from `inverseBindMatrices`
- Morph targets: `blendShape` (front of chain), default weights,
  `extras.targetNames`
- Animation: translation / rotation / scale / morph-weights channels;
  quaternions converted to continuous euler curves (hemisphere fix +
  unroll); `STEP` / `LINEAR` / `CUBICSPLINE` (approximated)

### Limitations

- Points / lines primitives are skipped
- Sparse accessors are supported; `KHR_texture_basisu` (KTX2) is not
- `texCoord: 1` on a texture is not auto-linked to the second UV set
  (use Maya's UV linking editor)
- Multiple animations are imported onto the single Maya timeline
- Units follow the exporter's convention: 1 glTF unit = 1 Maya UI unit
  (centimeters by default), not 1 meter

## Install

1. Copy `releases/Maya<version>/glTFImporter.mll` into a Maya plug-in
   path (e.g. `Documents/maya/<version>/plug-ins/`).
2. Copy `releases/scripts/glTFImporterOptions.mel` into a Maya script
   path (e.g. `Documents/maya/<version>/scripts/`).
3. Enable **glTFImporter.mll** in *Windows > Settings/Preferences >
   Plug-in Manager*.
4. Use *File > Import...* and choose the **glTF Import** file type
   (or just pick a `.gltf` / `.glb` file).

## Build (Windows)

```powershell
git clone --recursive https://github.com/pelcman/glTF-Maya-Importer.git
cmake -G "Visual Studio 17 2022" -A x64 -B build2024 -S . `
    -DGLTF_MAYA_IMPORTER_MAYA_VERSION=2024
cmake --build build2024 --config Release
# -> build2024/Release/glTFImporter.mll
```

Requires Visual Studio 2022 and a local Maya installation (SDK headers
under `<Maya>/include`). See `CLAUDE.md` for maintenance notes.

## License

MIT License (see `LICENSE`). Bundled third-party code:
[picojson](https://github.com/kazuho/picojson) (BSD-2-Clause) and
[draco](https://github.com/google/draco) (Apache-2.0, git submodule).
