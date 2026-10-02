#!/usr/bin/env python3
"""Famicom NROM dumper - PC side.

使い方:
    pip install pyserial
    python dump_nes.py COM5 smb.nes

前提: カセット挿入済み・SW1 ON。シリアルモニタは閉じておくこと（ポート占有）。
開始時に E 0（バス有効）、終了時に E 1（バスHi-Z）を自動で送る。
2回ダンプして一致した場合のみファイルを書き出す（接点不良による化けを検出）。
"""
import sys
import time
import zlib
import serial


def open_port(port: str) -> serial.Serial:
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 30
    s.dtr = False          # ESP32 DevKit の自動リセットを避ける
    s.rts = False
    s.open()
    time.sleep(2.0)        # 万一リセットされた場合の起動メッセージを待って捨てる
    s.reset_input_buffer()
    return s


def command(s: serial.Serial, cmd: str, expect: str) -> str:
    s.write((cmd + "\n").encode())
    for _ in range(20):
        line = s.readline().decode(errors="replace").strip()
        if line.startswith(expect):
            return line
    raise RuntimeError(f"no response to {cmd!r}")


def dump_once(s: serial.Serial):
    s.reset_input_buffer()
    s.write(b"D\n")
    header = s.readline().decode(errors="replace").strip()
    if not header.startswith("DUMP"):
        raise RuntimeError(f"unexpected header: {header!r}")
    info = dict(kv.split("=") for kv in header.split()[1:])
    prg_size, chr_size, mir = int(info["prg"]), int(info["chr"]), info["mir"]

    body = s.read(prg_size + chr_size)
    if len(body) != prg_size + chr_size:
        raise RuntimeError(f"short read: {len(body)} bytes")

    s.readline()                                  # 本体直後の改行
    tail = s.readline().decode(errors="replace").strip()
    crc_dev = int(tail.split("=")[1], 16)
    crc_pc = zlib.crc32(body) & 0xFFFFFFFF
    if crc_dev != crc_pc:
        raise RuntimeError(f"CRC mismatch: device {crc_dev:08X} / pc {crc_pc:08X}")

    return body[:prg_size], body[prg_size:], mir, crc_pc


def ines_header(prg_size: int, chr_size: int, mir: str) -> bytes:
    flags6 = 0x01 if mir == "V" else 0x00        # bit0: 1 = 垂直ミラー, Mapper 0
    return bytes([0x4E, 0x45, 0x53, 0x1A,
                  prg_size // 0x4000, chr_size // 0x2000,
                  flags6, 0x00]) + bytes(8)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    port, out = sys.argv[1], sys.argv[2]

    s = open_port(port)
    try:
        print(command(s, "E 0", "BUS_EN_N"))     # バス有効化

        t0 = time.time()
        prg1, chr1, mir1, crc1 = dump_once(s)
        print(f"pass 1: PRG {len(prg1)//1024}KB  CHR {len(chr1)//1024}KB  "
              f"mirror={mir1}  CRC32={crc1:08X}  ({time.time()-t0:.1f}s)")
        prg2, chr2, mir2, crc2 = dump_once(s)
        print(f"pass 2: CRC32={crc2:08X}")
    finally:
        # エラーで止まった場合も必ずバスをHi-Zに戻す（そのまま抜き差しできる状態にする）
        try:
            time.sleep(0.5)
            s.reset_input_buffer()
            print(command(s, "E 1", "BUS_EN_N"))
        except Exception:
            print("警告: E 1 を送れませんでした。カセットを抜く前にESP32のENボタンを押してください。")
        s.close()

    if (prg1, chr1, mir1) != (prg2, chr2, mir2):
        print("NG: 2回のダンプが一致しません。接点清掃・挿し直し後に再実行してください。")
        sys.exit(2)
    if mir1 == "?":
        print("注意: ミラーリング判定不能。水平(H)として書き出します。")

    rom = ines_header(len(prg1), len(chr1), mir1) + prg1 + chr1
    with open(out, "wb") as f:
        f.write(rom)
    print(f"OK: {out} ({len(rom)} bytes)  PRG+CHR CRC32={crc1:08X}")


if __name__ == "__main__":
    main()
