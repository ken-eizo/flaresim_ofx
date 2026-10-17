# FlareSim OFX — Nuke本家との仕様・画質比較

## 方針

**Nuke原作の挙動を標準、追加機能は別枠**とする。オリジナルの光学レンズデータ（1,370件）、レイ計算、波長、輝点検出、ブラー処理を維持する。特定ホスト用のOFX拡張を必須にしない。

Golden Angle（Vogel）だけはAE由来の追加機能で、Pupil Jitterの末尾4番目に置く。既存Nukeの3方式の番号と標準値は変更しない。

## 初期設定（Nuke FlareSim.cppとの照合）

| 分類 | Nuke / OFX |
|---|---|
| Lens File | 未指定（空文字列） |
| Flare Gain | 1000 |
| Ray Grid (NxN) | 64 |
| Pupil Jitter | Off |
| Jitter Seed / Auto Seed | 0 / 有効 |
| Threshold / Source Cap | 2 / 0 |
| Max Sources / Downsample / Cluster Radius | 512 / 4 / 0 |
| Preview / Preview Grid / Sources / Downsample / Spectral | Off / 16 / 100 / 8 / 3 |
| Use Sensor Size / Sensor Preset | Off / Custom |
| FOV H / FOV V / Auto FOV V | 40° / 24° / On |
| Sensor W / H / Focal Length | 36mm / 24mm / 50mm |
| Aperture Blades / Rotation | 0 / 0° |
| Spectral Samples | 3 |
| Ghost Blur / Passes | 0.003 / 3 |
| Haze Gain / Radius / Passes | 0 / 0.15 / 3 |
| Starburst Gain / Scale | 0 / 0.15 |
| Show Sources | Off |
| Light Source | Detect Highlights |
| Ghost Pairs | 原則としてフィルタ後の全有効ペア |

Nukeでは**Lens File未選択時に光学Ghostは発生しない**。OFXの追加プリセットも初期値は `None (Nuke default)`。ユーザーがレンズを選ぶと描画できる。

## 本家との直接比較

条件：本家の `ghost_cuda.cu` を同じWindows NVIDIA GPU上で独立DLLにコンパイル。Nikkor 50mm f/1.2、96×54、Ray Grid 64、単一光源、78反射ペア、Ghost Blurなし。

| 対象 | 本家CUDAとの差（相対L1） |
|---|---:|
| 原作Nuke CUDA → OFX参照CUDA | **約0.000353%** |
| 原作Nuke CUDA → OFX CPU光学計算 | 約0.00430% |
| 原作Nuke CUDA → AE最適化CUDA | 約3.93% |
| 原作Nukeのポストブラー → OFXポストブラー | **画素差0**（独立した別のテスト画像） |

相対L1 = `sum(abs(reference - output)) / sum(abs(reference))`。同一GPUで原作を連続2回実行した際の相対差は約 `3×10^-8`。極小の非一致はGPUの浮動小数演算とアトミック加算順序による。

### OFXの計算経路

- **Windows CUDA / Nuke既定の3サンプリング方式**: 本家の `ghost_cuda.cu` をそのまま独立DLL化した `FlareSimNukeReference.dll` を優先。
- **Windows CUDA非対応、macOS Metalの通常Auto**: 本家に近いCPU経路を優先。
- **Golden Angle**: AE版のVogelサンプリングを使用し、GPUがある環境では最適化GPU経路を利用。
- **GPU (Fast)を明示指定**: CUDA/Metalの最適化経路を試す。CPUへフォールバックする。

CUDA参照DLLを紛失したときは別レンズへ置き換えたりせず、CPUへフォールバックする。

## 残る本家との違い・検証範囲

- **出力チャンネル**: Nuke標準の `Separate channels` は専用の `flare.rgb`、`source.rgb` 等を持つ。汎用OFXが必ずしも任意のチャンネルを保持できないため、OFXではRGBA出力モードを使用する。初期 Output Modeの `Composite (OFX)` は本家と**完全同一仕様ではない**。
- **Pairs UI**: Nuke版にある個別反射ペアのチェックボックス一覧はまだ完全移植されていない。標準設定では全有効ペアをレンダリングする。
- **Haze、Starburst、マスク、複数輝点**: 実装されているが、あらゆる設定におけるピクセル単位の原作比較は未完了。上記0.000353%はGhost単一光源テストのみ。
- **macOS Metal**: ビルド・実機比較は未検証。高速化経路の画質一致を保証しない。標準AutoのCPU経路を推奨。
- **実際のNuke/Resolve/Houdiniホスト**: 独立原作CUDA関数との比較であり、Nukeアプリケーション上の合成結果との完全比較ではない。

テストは `FlareSimNukeGpuParityTest`、`FlareSimNukeBlurParityTest`、`FlareSimCoreTest`、`FlareSimOFXHostTest`、`FlareSimCatalogTest` ほか。Golden Angleは `FlareSimCoreTest` のPupilサンプリング検査対象。

## 本家ライセンスとソース

光学コードと配布されるレンズ定義は [LocalStarlight/flaresim_nuke](https://github.com/LocalStarlight/flaresim_nuke)。OFXは公式OpenFX C APIを利用。CPU最適化・Golden Angleの実装は [ken-eizo/flaresim_ae](https://github.com/ken-eizo/flaresim_ae) に由来する。詳細はLICENSE、NOTICE.md参照。

## UI相互運用性の修正（2026-10-18）

- DescribeInContextでは、paramDefineの戻り値であるディスクリプタに、ファイル選択形式・グループ所属・旧パラメータ非表示の属性を直接設定する。instance専用paramGetHandleを使用しない。
- Nuke本家のLens Fileを最初に表示。次のLens Presetには本家1,370本すべてを標準Choiceとして登録する。ファイル参照ボタンはホスト対応が必要だがドロップダウンはホスト標準UIで利用できる。
- ギズモは標準Overlay Interact V1(OpenGL)、V2(DrawSuite)で描画。ドラッグでManual Positionへ移行する。ホストがオーバーレイ未対応ならXY数値入力を使う。
- 名前のNuke/OFX装飾を消し、追加機能はAdditional Controlsへ集約。
- Output ModeのSources Onlyはレンズ未選択でも検出された明点を表示する。
- NukeのSeparate channelsは任意の名前付き出力を必要とし、OFX全ホスト共通では保証されないため削除。標準出力はFlare as RGBとし、NukeのSeparate channels初期状態とは異なることを明示する。
- 初期Threshold=2は本家HDR設定のまま。SDR素材では検出閾値を下げる必要がある。
- Resolveを含む実ホストでのUI表示・最終結果は引き続き未検証。標準OFXでの実装のみでは全ホスト動作保証にはならない。
