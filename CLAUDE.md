# CLAUDE.md — glTF-Maya-Importer 保守・改修ガイド

Maya 用 glTF 2.0 インポータ(MIT License)。
[glTF-Maya-Exporter](https://github.com/pelcman/glTF-Maya-Exporter) の姉妹プロジェクトで、
Exporter の出力(既定の Draco 圧縮 GLB を含む)を Maya に読み戻せることを最優先とし、
他ツール製の標準的な glTF 2.0 アセットの読み込みにも対応する。
対象 Maya は **2022〜2027**(Windows で実ビルド・実行検証済み)。

## 改修の基本方針

1. **Exporter との往復整合性を壊さない。** 単位(1 glTF unit = 1 UI 単位、
   既定 cm)、UV の V 反転、座標系(変換なし)は Exporter の慣例に合わせてある。
   変更する場合は必ず Exporter 側との往復テストを更新して確認する。
2. **バージョン分岐は `MAYA_API_VERSION` の `#if` ガードで行う**
   (例: standardSurface は `>= 20200000`、それ未満は lambert フォールバック)。
3. **glTF パース層(GltfDocument / DracoDecoder)は Maya 非依存を維持する。**
   Maya API に触れるのは `glTFImporter.cpp` / `glTFImporterRegister.cpp` のみ。
4. **コミットは小さく、目的単位で。** ビルド修正・機能修正・ドキュメントは
   別コミットにする。ビルドが通る状態でコミットする。

## アーキテクチャ

```
src/glTFImporter/
├─ glTFImporterRegister.cpp  プラグイン登録 (initializePlugin)。翻訳子名 "glTF Import"
├─ glTFImporter.h/.cpp       MPxFileTranslator 実装 + SceneBuilder(Maya シーン構築)
│                            ノード階層/メッシュ統合/マテリアル/スキン/
│                            ブレンドシェイプ/アニメーションのすべてがここ
├─ GltfDocument.h/.cpp       Maya 非依存の glTF 2.0 パーサ(picojson)。
│                            GLB チャンク/外部 .bin/base64 data URI の解決、
│                            アクセサのデコード(byteStride/normalized/sparse)
└─ DracoDecoder.h/.cpp       KHR_draco_mesh_compression デコード
                             (ENABLE_BUILD_WITH_DRACO でガード)
externals/
├─ picojson/                 vendored ヘッダ(kazuho/picojson, BSD-2-Clause)
└─ draco/                    git submodule(google/draco, Exporter と同一の
                             1.3.3 = e3a9d6c に固定)
releases/
├─ Maya{2022..2027}/         配布用 glTFImporter.mll(リリース時に全再ビルド)
└─ scripts/                  インポートオプション UI(MEL)
```

## 実装上の要点(ハマりどころ)

- **Exporter は既定で Draco 圧縮を出力する**(`output_buffer=1`)。Draco 無しの
  テストデータが欲しい場合はエクスポートオプションに `output_buffer=0;` を渡す。
- **複数プリミティブは 1 つの Maya メッシュに統合**し、フェイス範囲ごとに
  SG を割り当てる。UV/頂点カラーは「UV id = 頂点 id」となるよう頂点単位で
  持たせ、欠けるプリミティブ分はゼロ詰めして整列している(PrimitiveData 経由)。
- **スキンメッシュのノードトランスフォームは仕様どおり無視**し、ワールド直下に
  identity で作成する。ウェイトは skinCluster 生成後に MFnSkinCluster::setWeights
  で明示設定し、inverseBindMatrices を bindPreMatrix に直接書き込む。
- **行列の変換**: glTF の column-major 配列を Maya の double[4][4] に
  そのまま流し込むと転置に相当し、それが Maya の row-vector 慣例と一致する
  (GltfMatrixToMaya)。平行移動成分のみ単位換算する。
- **回転アニメーション**はクォータニオンを半球補正(dot<0 で符号反転)して
  からオイラーに変換し、前キーとの差が π を超えないよう ±2π でアンロールする。
  360° 超の回転も Exporter の適応サンプリングと合わせて往復できる。
- **CUBICSPLINE** は (inTangent, value, outTangent) の 3 つ組から value のみを
  取り出し、smooth タンジェントで近似している(厳密なタンジェント変換は未対応)。
- **埋め込み画像**はソースファイル隣の `<basename>_textures/` に抽出する。
  metallicRoughness / normal マップの file ノードは colorSpace=Raw +
  ignoreColorSpaceFileRules=1 を設定する。
- **MEL コマンド組み立てで MString に数値を直接 `+` しない**(バージョンにより
  書式が変わる)。IS()/FS() ヘルパーを使う。
- シェーディング系(shadingNode/sets/skinCluster/blendShape)は MEL 経由、
  ジオメトリ/ウェイト/アニメカーブは API 直接、という使い分け。

## ビルド方法(Windows)

```powershell
# 初回のみ
git submodule update --init --recursive

# CMake は VS2022 同梱のものを使用(PATH に無い場合)
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

# Maya バージョンごとにビルドディレクトリを分ける
& $cmake -G "Visual Studio 17 2022" -A x64 -B build2024 -S . `
    "-DGLTF_MAYA_IMPORTER_MAYA_VERSION=2024"
& $cmake --build build2024 --config Release
```

成果物: `build{ver}/Release/glTFImporter.mll`
インストール: `.mll` を `plug-ins/`、`releases/scripts/*.mel` を `scripts/` へ配置。

### 動作確認(mayapy)

回帰テストは `tests/` にある(実行方法・前提は `tests/README.md` を参照)。
改修後は最低限 `test_smoke.py` を全対象バージョンで、フルスイートを 1 バージョンで
実行すること。往復テストには姉妹ディレクトリの glTF-Maya-Exporter
(`releases/` の配布バイナリ)が必要。

Maya を GUI 起動せずに検証できる。Exporter の配布バイナリと組み合わせた
往復テストが基本:

```powershell
& "C:\Program Files\Autodesk\Maya2024\bin\mayapy.exe" -c @"
import maya.standalone; maya.standalone.initialize()
import maya.cmds as cmds
cmds.loadPlugin(r'<build>/Release/glTFImporter.mll')
cmds.file(r'<path>/asset.glb', i=True, type='glTF Import',
          options='import_animations=1;import_skins=1;import_blendshapes=1;')
print(cmds.ls(type='mesh'))
"@
```

検証済みの往復テスト観点(2026-08-28、Maya 2024 でフル、全バージョンでスモーク):
メッシュ/トランスフォーム/マテリアル値、Draco 圧縮 GLB(スキン込み)、
スキン変形のフレーム単位一致、ブレンドシェイプ変形とウェイトアニメ、
360° 超回転のアンロール、GLB 埋め込みテクスチャと .gltf 外部テクスチャ。

## Maya バージョン対応の要点

| Maya | Windows ツールチェーン | 備考 |
|------|----------------------|------|
| 2022–2023 | VS2019 (v142) | v143 ビルドで動作確認済み(ABI 互換) |
| 2024–2027 | VS2022 (v143) | 実ビルド+スモークテスト済み |

- Maya SDK ヘッダ/ライブラリは各 Maya インストール直下の `include/` `lib/` を参照。
- 使用 API は MPxFileTranslator / MFnMesh / MFnSkinCluster / MFnAnimCurve など
  古く安定したもののみ。findPlug は (name, wantNetworkedPlug) の 2 引数版を使用
  しているため 2018 未満にはそのままでは対応しない(対象外)。

## 既知の制約・今後の課題

- [ ] points / lines プリミティブ(スキップして警告)
- [ ] texture.texCoord=1 の UV セット自動リンク(uvChooser 生成)
- [ ] KHR_texture_transform / KHR_materials_* 拡張、KTX2
- [ ] 複数アニメーションのクリップ分割(現状は単一タイムラインに重ね取り込み)
- [ ] CUBICSPLINE タンジェントの厳密変換
- [ ] macOS / Linux ビルド検証

## Git 運用

- ベースブランチ: `main`
- push 先: `origin` = https://github.com/pelcman/glTF-Maya-Importer.git
- submodule(externals/draco)は公開リポジトリのコミットのみ参照する
