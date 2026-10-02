# Famicom ROM Dumper

ESP32と汎用ロジックICで作る、ファミコン（Family Computer）カセットのROM吸い出し機です。
吸い出したデータはiNES形式（`.nes`）で保存され、エミュレータ（Mesen等）でそのまま起動できます。

A Famicom cartridge ROM dumper built with an ESP32 and standard 74-series logic.
Dumps are saved in iNES format (`.nes`) and boot directly in emulators such as Mesen.

<!-- 基板写真 / board photo -->
<!-- ![board](docs/images/board.jpg) -->

---

## 特徴 / Features

- **ESP32 + シフトレジスタ構成**：74HCT595 ×6 / 74HCT165 ×2 でカセットバス54本をGPIO 16本に圧縮
- **5V系ロジックを直接駆動**：シフトレジスタを5Vで動かし、TTLしきい値（HCT/AHCT）で3.3V信号を受ける。大量のレベル変換ICが不要
- **カセット保護**：カセット電源（`VCC_CART`）の独立遮断と、バス全体のHi-Z化（`BUS_EN_N`）
- **ダンプの信頼性**：2回読み出して一致を確認し、CRC32で転送エラーも検出
- **自動判定**：PRG容量（16 KB / 32 KB）とミラーリング（垂直 / 水平）

- **ESP32 + shift registers**: 54 cartridge bus signals driven from 16 GPIOs using 6× 74HCT595 and 2× 74HCT165
- **Direct 5 V logic drive**: shift registers run at 5 V with TTL input thresholds (HCT/AHCT), accepting 3.3 V signals without bulk level shifters
- **Cartridge protection**: switchable cartridge power rail (`VCC_CART`) and bus tri-state control (`BUS_EN_N`)
- **Verified dumps**: every cartridge is read twice and compared; CRC32 checks the serial transfer
- **Auto detection**: PRG size (16 KB / 32 KB) and nametable mirroring (vertical / horizontal)

## 対応カセット / Supported Cartridges

| 状態 | マッパー | 例 |
|---|---|---|
| ✅ 対応 | Mapper 0（NROM-128 / NROM-256） | スーパーマリオブラザーズ、ドンキーコング、バルーンファイト 等 |
| 🚧 未対応 | バンク切替のあるマッパー（UNROM、MMC1、MMC3 など） | 今後対応予定 |

| Status | Mapper | Examples |
|---|---|---|
| ✅ Supported | Mapper 0 (NROM-128 / NROM-256) | Super Mario Bros., Donkey Kong, Balloon Fight, etc. |
| 🚧 Not yet | Bank-switching mappers (UNROM, MMC1, MMC3, ...) | Planned |

60ピンのファミコン用カセット専用です。72ピンの海外版NESカセットは物理的に挿さりません。
For 60-pin Famicom cartridges only. 72-pin NES cartridges do not fit.

## 動作確認済み / Tested

| タイトル | 構成 | 結果 |
|---|---|---|
| スーパーマリオブラザーズ | NROM-256（PRG 32 KB + CHR 8 KB）、垂直ミラー | ✅ Mesenで起動・プレイ確認 |

## リポジトリ構成 / Repository Layout

```
famicom-rom-dumper/
├── README.md
├── LICENSE
├── hardware/           Kicadの回路図,ガーバーデータ
│   └── ERRATA.md       rev1の既知の問題 / Known issues in rev1
├── firmware/           ESP32ファーム（PlatformIO） / ESP32 firmware
├── tools/              PC側ダンプスクリプト / Host-side dump script
└── docs/
    ├── hardware.md     部品表・全接続・電源設計 / BOM, netlist, power design
    ├── bringup.md      組み立て後〜Mesen起動までの手順 / Bring-up to first boot
    ├── protocol.md     シリアルコマンド仕様 / Serial protocol
    └── design-notes.md 設計判断と技術解説 / Design rationale and technical notes
```

## クイックスタート / Quick Start

詳細は [docs/bringup.md](docs/bringup.md) を参照してください。
See [docs/bringup.md](docs/bringup.md) for the full procedure.

1. `firmware/` をPlatformIOでESP32に書き込む / Flash `firmware/` to the ESP32 with PlatformIO
2. カセットを挿してカセット電源スイッチ（SW1）をON / Insert a cartridge and turn SW1 (cartridge power) on
3. PCでダンプ / Dump from the PC:

```
pip install pyserial
python tools/dump_nes.py COM5 game.nes
```

4. SW1をOFFにしてからカセットを抜く / Turn SW1 off before removing the cartridge

```
BUS_EN_N = 0 (enabled)
pass 1: PRG 32KB  CHR 8KB  mirror=V  CRC32=D445F698  (11.5s)
pass 2: CRC32=D445F698
BUS_EN_N = 1 (Hi-Z)
OK: game.nes (40976 bytes)  PRG+CHR CRC32=D445F698
```

## ハードウェア概要 / Hardware Overview

| 部品 / Part | 型番 / Part number | 数 / Qty | 役割 / Role |
|---|---|---|---|
| MCU | ESP32-WROOM-32E DevKit（30ピン） | 1 | 制御・USBシリアル / Control, USB serial　URL：[https://ja.aliexpress.com/item/1005008771142129.html?spm=a2g0o.productlist.main.6.6dc83d53RGPe6w&algo_pvid=33590654-20dd-4552-814f-4d09bc6131f6&algo_exp_id=33590654-20dd-4552-814f-4d09bc6131f6-5&pdp_ext_f=%7B%22order%22%3A%222684%22%2C%22spu_best_type%22%3A%22price%22%2C%22eval%22%3A%221%22%2C%22fromPage%22%3A%22search%22%7D&pdp_npi=6%40dis%21JPY%21485%21407%21%21%2120.19%2116.94%21%402140ec2d17909587041287044e0f77%2112000046602507590%21sea%21JP%213042568723%21X%211%210%21n_tag%3A-29919%3Bd%3A5c8fe142%3Bm03_new_user%3A-29895%3BpisId%3A5000000218885134&curPageLogUid=D1ZQcJyiMkLP&utparam-url=scene%3Asearch%7Cquery_from%3A%7Cx_object_id%3A1005008771142129%7C_p_origin_prod%3A](https://ja.aliexpress.com/item/1005008771142129.html?channel=twinner ) |
| シフトレジスタ（出力） | SN74AHCT595D | 6 | アドレス29本 + データ書き込み16本 / Address and data out |
| シフトレジスタ（入力） | CD74HCT165M | 2 | データ読み出し16本 / Data in |
| バッファ | SN74AHCT244 | 1 | 制御線6本 3.3 V → 5 V / Control lines |
| レベル変換 | SN74LVC1T45 | 3 | 5 V → 3.3 V（MISO、CIRAM 2本） |
| コネクタ | ファミコン60ピン 2.54 mmピッチ | 1 | カセットスロット / Cartridge slot |

全部品表と接続は [docs/hardware.md](docs/hardware.md) にあります。
The full BOM and netlist are in [docs/hardware.md](docs/hardware.md).

## 注意 / Legal Notice

- **吸い出したROMイメージを配布しないでください。** このツールは、自分が所有するカセットのバックアップや研究目的の私的使用を想定しています。このリポジトリにROMイメージは含まれていません
- **Do not distribute ROM images.** This tool is intended for personal backup and research using cartridges you own. No ROM images are included in this repository.

- カセットの抜き差しは必ずカセット電源（SW1）をOFFにしてから行ってください。通電中の抜き差しはカセットと基板を破損するおそれがあります
- Always turn off cartridge power (SW1) before inserting or removing a cartridge. Hot-swapping can damage both the cartridge and the board.

## 参考資料 / References

- [NESdev Wiki](https://www.nesdev.org/wiki/) — Cartridge connector, iNES format, mapper documentation
- [MesenCE](https://github.com/nesdev-org/MesenCE) — エミュレータ / emulator used for verification

## ライセンス / License

MIT License. ハードウェア（KiCadデータ）・ファームウェア・ツールのすべてに適用します。
MIT License, applied to the hardware design files, firmware, and tools.
