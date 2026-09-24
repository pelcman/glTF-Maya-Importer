# glTF-Maya-Importer(日本語)

Autodesk Maya(2022〜2027、Windows)用の glTF 2.0 インポータプラグインです。
[glTF-Maya-Exporter](https://github.com/pelcman/glTF-Maya-Exporter) の姉妹プロジェクトで、
Exporter が出力したファイル(既定の Draco 圧縮 GLB を含む)を Maya に読み戻せます。
他ツールで作成された標準的な glTF 2.0 アセットの読み込みにも対応しています。

## 対応機能

- `.gltf` / `.glb`(GLB バイナリチャンク、外部 `.bin`、base64 埋め込み)
- `KHR_draco_mesh_compression`(Draco 圧縮メッシュ)のデコード
- ノード階層・トランスフォーム(TRS / matrix)
- メッシュ: 法線、UV 2 セット、頂点カラー、複数マテリアル(フェイス単位割当)
- マテリアル: standardSurface(baseColor / metalness / roughness / emissive /
  ノーマルマップ / アルファ)、テクスチャ(埋め込み画像は
  `<ファイル名>_textures/` に展開)
- スキニング: ジョイント、skinCluster、バインド行列
- ブレンドシェイプ(モーフターゲット)と既定ウェイト
- アニメーション: 移動 / 回転 / スケール / モーフウェイト
  (回転はクォータニオンから連続したオイラーカーブに変換)

## 制限事項

- ポイント / ライン形式のプリミティブは読み込まれません
- KTX2(`KHR_texture_basisu`)テクスチャは未対応です
- テクスチャの `texCoord: 1`(第 2 UV セット参照)は自動リンクされません
  (Maya の UV リンクエディタで手動設定してください)
- 複数アニメーションは 1 つのタイムラインにまとめて読み込まれます
- glTF にはジョイント型が無いため、`skins[].joints` に列挙されたノードだけが
  Maya の joint になります。スキンを含まないアニメーションのみのファイル
  (glTF-Maya-Exporter の「Output animation only」で「write skin」が OFF の出力など)
  は transform として読み込まれます
- 読み込みは常に新しいノードを作成します。アニメーションのみのファイルを
  シーン内の既存スケルトンに名前で適用する機能は未対応です
- 単位は Exporter と同じ「1 glTF 単位 = Maya の UI 単位(既定 cm)」です

## インストール

1. `releases/Maya<バージョン>/glTFImporter.mll` を Maya のプラグインパス
   (例: `ドキュメント/maya/<バージョン>/plug-ins/`)にコピー
2. `releases/scripts/glTFImporterOptions.mel` を Maya のスクリプトパス
   (例: `ドキュメント/maya/<バージョン>/scripts/`)にコピー
3. Maya の「ウィンドウ > 設定/プリファレンス > プラグインマネージャ」で
   **glTFImporter.mll** をロード
4. 「ファイル > 読み込み...」でファイルタイプ **glTF Import** を選択するか、
   `.gltf` / `.glb` ファイルをそのまま指定してください

## 読み込みオプション

| オプション | 既定 | 内容 |
|-----------|------|------|
| Animations | ON | アニメーションを読み込む |
| Skins | ON | スキニング(skinCluster)を再構築する |
| Blend Shapes | ON | ブレンドシェイプを再構築する |

## ライセンス

MIT License。同梱のサードパーティ:
[picojson](https://github.com/kazuho/picojson)(BSD-2-Clause)、
[draco](https://github.com/google/draco)(Apache-2.0)。
