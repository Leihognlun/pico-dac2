Language: [English](README.md) | [日本語](README.ja.md)

# Raspberry Pi Pico USB DAC

Raspberry Pi Pico を USB DAC（Digital-to-Analog Converter）として機能させるためのファームウェアです。USB Audio Class 2.0（UAC2）に対応しており、PC やスマートフォンなどのホストに接続するだけで、高品質なオーディオ出力デバイスとして利用できます。

## 主な特徴

- **I2S + SPDIF 同時出力（既定）:** `PICODAC_OUTPUT=BOTH`。
  SPDIF は GPIO 22、I2S は DATA=18、BCLK=16、LRCLK=17 です。
  PCM は両方に出力し、圧縮音声の透過転送時は SPDIF のみにデータを送り、
  I2S はクロックを維持してゼロサンプルを出力します。`SPDIF` / `I2S` 単独出力も選択できます。
- **AC-3 / DTS 透過転送:** BOTH/SPDIF ビルドの altset 4/5/6/7 は
  44.1/48/88.2/96/192 kHz のステレオ 16 bit IEC 61937 キャリアに対応します。
  Windows との互換性のため、全 altset で同じ端子とクロックソースを使用します。
  エンコード済みデータをそのまま転送し、音量・ミュート処理を迂回します。
  多チャンネル PCM を AC-3/DTS に変換するエンコーダーではありません。
- **現行版のユーザー実測（`bcdDevice=0x010a`）:** Windows でのサウンドカード認識と、
  Raspberry Pi Linux での E-AC-3 / Dolby Atmos 透過転送が確認されています。
  USB の形式宣言は引き続き AC-3/DTS であり、独立した E-AC-3 形式は追加していません。
  Windows での E-AC-3/Atmos 再生や TrueHD Atmos、すべての機器での動作を示す結果ではありません。
  詳細は [SPDIF の設定と検証（中国語）](SPDIF.md) を参照してください。
- **USB Audio Class 2.0 対応:**
  - ドライバーのインストールなしで多くの OS（Windows, macOS, Linux）で動作します。
  - Feedback Endpoint によるフロー制御に対応
- **ハイレゾ対応:**
  - **PCM サンプリング周波数:** 44.1kHz, 48kHz, 88.2kHz, 96kHz（192kHz PCM は非対応）
  - **量子化ビット数:** 16bit, 24bit, 32bit
- **HID コントロール:**
  - ファームウェアのカスタム制御用に HID（Human Interface Device）エンドポイントを実装しています。(現状はダミーデータ送受信のみ。将来機能追加予定)
- **独自 USB スタックの利用**
  - 必要最小限の機能を備えた Raspberry Pi Pico 用の USB スタックを独自実装しています。
    - 将来的には USB プロトコルスタックのみを独立したライブラリにする予定です

## 必要なハードウェア

- Raspberry Pi Pico
- 3.3 V ロジック入力に対応した光 SPDIF 送信モジュール、または同軸 SPDIF 駆動回路
- I2S 出力を使う場合は I2S 対応 DAC モジュール（PCM5102A など）
- USB ケーブル

## ビルド方法

### 1. 開発環境のセットアップ

Raspberry Pi Pico の C/C++開発環境をセットアップする必要があります。公式ドキュメントを参考に、Pico SDK とツールチェーンをインストールしてください。

- [Getting started with Raspberry Pi Pico](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf)

### 2. ソースコードの取得

```bash
git clone https://github.com/td2sk/pico-dac2
cd pico-dac2
```

### 3. ビルド

標準的な CMake のビルド手順でファームウェアをビルドします。

```bash
mkdir build
cd build
cmake .. -DPICODAC_OUTPUT=BOTH -DPICODAC_SPDIF_PIN=22
cmake --build .
```

ビルドが成功すると、`build`ディレクトリ内に`mdac_adc2.uf2`という名前のファイルが生成されます。

Windows で設定済みの `build` を使う場合は、プロジェクトの VS Code タスクと同じ
SDK 付属 Ninja でもビルドできます（プロジェクトのルートから実行）：

```powershell
& "$env:USERPROFILE/.pico-sdk/ninja/v1.13.2/ninja.exe" -C build
```

### GPIO ピンの変更

I2S に使用する GPIO ピンは`CMakeLists.txt`で変更できます。デフォルト設定は以下の通りです。

- **I2S DATA:** GPIO 18
- **I2S BCLK:** GPIO 16
- **I2S LRCLK:** GPIO 17

```cmake
# CMakeLists.txt

# user configurations
set (PICODAC_I2S_DATA_PIN 18 CACHE STRING "I2S Data Pin")
set (PICODAC_I2S_BASE_CLOCK_PIN 16 CACHE STRING "I2S Base Clock Pin. LRCLK is BASE + 1")
```

## インストール

1. Raspberry Pi Pico の`BOOTSEL`ボタンを押しながら、PC に USB ケーブルで接続します。
2. PC に`RPI-RP2`という名前のマスストレージデバイスとして認識されます。
3. ビルドして生成された`mdac_adc2.uf2`ファイルを、その`RPI-RP2`ドライブにドラッグ＆ドロップします。
4. コピーが完了すると、Pico は自動的に再起動し、ファームウェアの実行が開始されます。

## 使い方

1. [SPDIF.md](SPDIF.md) に従って SPDIF 送信回路を接続するか、設定した GPIO に I2S DAC を接続します。
2. ファームウェアを書き込んだ Pico をホスト（PC など）に USB で接続します。
3. ホストの OS は、`mdac_adc2`（または同様の名前）という新しいオーディオ出力デバイスを自動的に認識します。
4. OS のサウンド設定で、このデバイスを出力先に選択し、音楽などを再生してください。

## HID 通信

このファームウェアは、カスタムコマンドを送受信するための HID インターフェースを備えています。`tools/comm.py` は、Python の `hid` ライブラリを使用してデバイスと通信する簡単なサンプルスクリプトです。

ベンダー ID と プロダクト ID は `CMakeLists.txt` で設定できます。

- **ベンダー ID:** `PICODAC_VENDOR_ID` (デフォルト値: `0xcafe`)
- **プロダクト ID:** `PICODAC_PRODUCT_ID` (デフォルト値: `0xbabe`)

## TODO

- [ ] ドキュメントの整備
- [ ] HID によるカスタム制御
- [ ] HID によるデバッグ/統計情報の取得
- [ ] USB プロトコルスタックを独立したライブラリ化
