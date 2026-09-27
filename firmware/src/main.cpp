// Famicom ROM Dumper - Bring-up firmware (Hardware.md rev4 準拠)
// シリアル 115200bps。"?" でコマンド一覧。
//
// v2: ハードウェアSPIをやめてビットバングに変更。
//   74HC165 は /PL 直後に QH へ最初のビット(H)を出し、CP の立ち上がりでシフトする。
//   SPI MODE0 は同じ立ち上がりでサンプルするため、ESP32 の入力遅延により
//   シフト後の値を読んで先頭ビットを失う（CPU D7 が消え、末尾に U8:SER=GND の 0 が入る）。
//   ビットバングで「読む → クロック」の順を明示して回避する。
#include <Arduino.h>

// ---- ピン割り当て（Hardware.md §5）----
constexpr int PIN_SCK = 18, PIN_MOSI = 23, PIN_MISO = 19;
constexpr int PIN_RCLK_ADDR = 26, PIN_RCLK_DATA = 27, PIN_OE_DATA_N = 25;
constexpr int PIN_PL_165_N  = 33;
constexpr int PIN_M2 = 21, PIN_ROMSEL_N = 22, PIN_RW = 32;
constexpr int PIN_PPU_RD_N = 16, PIN_PPU_WR_N = 17, PIN_PPU_A13_N = 4;
constexpr int PIN_CIRAM_A10 = 34, PIN_CIRAM_CE_N = 35;
constexpr int PIN_BUS_EN_N = 13;

uint32_t clkDelayUs = 1;               // シフトクロックの H/L 各幅

inline void sclk() {                   // SCK 立ち上がりで 595/165 ともにシフト
  digitalWrite(PIN_SCK, HIGH);
  delayMicroseconds(clkDelayUs);
  digitalWrite(PIN_SCK, LOW);
  delayMicroseconds(clkDelayUs);
}

void shiftOutByte(uint8_t b) {         // MSB first: bit7 -> QH
  for (int i = 7; i >= 0; i--) {
    digitalWrite(PIN_MOSI, (b >> i) & 1);
    sclk();
  }
}

uint8_t shiftInByte() {                // QH を読んでからクロック
  uint8_t v = 0;
  for (int i = 0; i < 8; i++) {
    v = (v << 1) | digitalRead(PIN_MISO);
    sclk();
  }
  return v;
}

// ---------------------------------------------------------------
void idleBus() {                       // 全制御線を非アクティブ側へ
  digitalWrite(PIN_M2, LOW);
  digitalWrite(PIN_ROMSEL_N, HIGH);
  digitalWrite(PIN_RW, HIGH);
  digitalWrite(PIN_PPU_RD_N, HIGH);
  digitalWrite(PIN_PPU_WR_N, HIGH);
  digitalWrite(PIN_PPU_A13_N, HIGH);
}

void setAddr(uint16_t cpuA, uint16_t ppuA) {
  uint8_t buf[4] = {
    (uint8_t)((ppuA >> 8) & 0x3F),     // U4: PPU A13..A8
    (uint8_t)( ppuA       & 0xFF),     // U3: PPU A7..A0
    (uint8_t)((cpuA >> 8) & 0x7F),     // U2: CPU A14..A8
    (uint8_t)( cpuA       & 0xFF)      // U1: CPU A7..A0
  };
  for (uint8_t b : buf) shiftOutByte(b);
  digitalWrite(PIN_RCLK_ADDR, HIGH);
  digitalWrite(PIN_RCLK_ADDR, LOW);
}

// 165 に現在のバス値をロードして 16bit 読む。戻り値: 上位=CPU D, 下位=PPU D
uint16_t read165() {
  digitalWrite(PIN_PL_165_N, LOW);
  delayMicroseconds(1);
  digitalWrite(PIN_PL_165_N, HIGH);
  delayMicroseconds(1);
  uint8_t cpuD = shiftInByte();        // U7 が先
  uint8_t ppuD = shiftInByte();        // U8
  return (uint16_t)cpuD << 8 | ppuD;
}

uint8_t readPrg(uint16_t addr) {       // addr: $8000-$FFFF
  setAddr(addr & 0x7FFF, 0x0000);
  digitalWrite(PIN_RW, HIGH);
  digitalWrite(PIN_M2, HIGH);
  digitalWrite(PIN_ROMSEL_N, LOW);
  delayMicroseconds(2);
  uint8_t v = read165() >> 8;
  digitalWrite(PIN_ROMSEL_N, HIGH);
  digitalWrite(PIN_M2, LOW);
  return v;
}

uint8_t readChr(uint16_t addr) {       // addr: $0000-$1FFF
  setAddr(0x0000, addr & 0x1FFF);      // PA13 = 0
  digitalWrite(PIN_PPU_A13_N, HIGH);
  digitalWrite(PIN_PPU_RD_N, LOW);
  delayMicroseconds(2);
  uint8_t v = read165() & 0xFF;
  digitalWrite(PIN_PPU_RD_N, HIGH);
  return v;
}

// ---------------------------------------------------------------
void help() {
  Serial.println(F(
    "\n--- Step0-2: no cartridge ---\n"
    " E 0|1        BUS_EN_N (0=enable, 1=Hi-Z)\n"
    " A cccc pppp  set address (CPU 15bit, PPU 14bit) ex: A 7FFF 3FFF\n"
    " C n 0|1      control line n: M2 ROMSEL RW RD WR A13\n"
    " I            all control lines inactive\n"
    "--- Step3: no cartridge, SW1 ON ---\n"
    " R            raw 165 read (CPU D / PPU D)\n"
    " G            read CIRAM A10 / CIRAM /CE\n"
    " S n          shift clock half-period [us] (default 1)\n"
    "--- Step4: cartridge inserted, SW1 ON ---\n"
    " V            vectors ($FFFA-$FFFF)\n"
    " P aaaa [n]   PRG dump ex: P FFF0 16\n"
    " H aaaa [n]   CHR dump ex: H 0000 16\n"
    " M            mirroring check\n"
    " D            full NROM dump (binary; use dump_nes.py)"));
}

void dumpLine(uint16_t base, const uint8_t* d, int n) {
  Serial.printf("%04X:", base);
  for (int i = 0; i < n; i++) Serial.printf(" %02X", d[i]);
  Serial.println();
}

void cmdVector() {
  const char* name[] = {"NMI  ", "RESET", "IRQ  "};
  for (int i = 0; i < 3; i++) {
    uint16_t a = 0xFFFA + i * 2;
    uint16_t v = readPrg(a) | (uint16_t)readPrg(a + 1) << 8;
    Serial.printf("%s $%04X = $%04X %s\n", name[i], a, v,
                  v >= 0x8000 ? "[OK]" : "[NG]");
  }
}

void cmdMirror() {
  setAddr(0, 0x2000 | 0x0400);  // PA10=1, PA11=0
  int a10_1 = digitalRead(PIN_CIRAM_A10);
  setAddr(0, 0x2000 | 0x0800);  // PA10=0, PA11=1
  int a11_1 = digitalRead(PIN_CIRAM_A10);
  setAddr(0, 0x2000);
  int base  = digitalRead(PIN_CIRAM_A10);
  Serial.printf("CIRAM A10: base=%d PA10=%d PA11=%d -> ", base, a10_1, a11_1);
  if (base == 0 && a10_1 == 1 && a11_1 == 0)      Serial.println("VERTICAL (follows PA10)");
  else if (base == 0 && a10_1 == 0 && a11_1 == 1) Serial.println("HORIZONTAL (follows PA11)");
  else                                            Serial.println("UNKNOWN (1-screen / 4-screen / wiring fault)");
}

// ---- 全ダンプ (NROM) ----------------------------------------------
static uint8_t romBuf[0x8000 + 0x2000];          // PRG 32KB + CHR 8KB

uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFF) {
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1));
  }
  return c;
}

char detectMirror() {                  // 'V' / 'H' / '?'
  setAddr(0, 0x2000);          int base = digitalRead(PIN_CIRAM_A10);
  setAddr(0, 0x2000 | 0x0400); int a10  = digitalRead(PIN_CIRAM_A10);
  setAddr(0, 0x2000 | 0x0800); int a11  = digitalRead(PIN_CIRAM_A10);
  if (base == 0 && a10 == 1 && a11 == 0) return 'V';
  if (base == 0 && a10 == 0 && a11 == 1) return 'H';
  return '?';
}

// プロトコル: "DUMP prg=<n> chr=<n> mir=<c>\n" + PRG + CHR + "END crc=<hex>\n"
void cmdDump() {
  uint8_t* prg = romBuf;
  uint8_t* chr = romBuf + 0x8000;
  for (uint32_t i = 0; i < 0x8000; i++) {
    prg[i] = readPrg(0x8000 + i);
    if ((i & 0x3FF) == 0) delay(0);    // 1KBごとに他タスクへ譲る (WDT対策)
  }
  for (uint32_t i = 0; i < 0x2000; i++) {
    chr[i] = readChr(i);
    if ((i & 0x3FF) == 0) delay(0);
  }
  char mir = detectMirror();

  // NROM-128 判定: 前半16KBと後半16KBが一致すれば 16KB 品
  size_t prgSize = memcmp(prg, prg + 0x4000, 0x4000) == 0 ? 0x4000 : 0x8000;
  const uint8_t* prgOut = (prgSize == 0x4000) ? prg + 0x4000 : prg;  // $C000側を採用

  uint32_t c = crc32(prgOut, prgSize);
  c = ~crc32(chr, 0x2000, c);

  Serial.printf("DUMP prg=%u chr=%u mir=%c\n", (unsigned)prgSize, 0x2000u, mir);
  Serial.flush();
  Serial.write(prgOut, prgSize);
  Serial.write(chr, 0x2000);
  Serial.flush();
  Serial.printf("\nEND crc=%08X\n", (unsigned)c);
}

void setCtrl(const String& n, int v) {
  int pin = -1;
  if      (n == "M2")     pin = PIN_M2;
  else if (n == "ROMSEL") pin = PIN_ROMSEL_N;
  else if (n == "RW")     pin = PIN_RW;
  else if (n == "RD")     pin = PIN_PPU_RD_N;
  else if (n == "WR")     pin = PIN_PPU_WR_N;
  else if (n == "A13")    pin = PIN_PPU_A13_N;
  if (pin < 0) { Serial.println("bad control line name"); return; }
  digitalWrite(pin, v);
  Serial.printf("%s = %d\n", n.c_str(), v);
}

void handle(String line) {
  line.trim(); line.toUpperCase();
  if (line.isEmpty()) return;
  char c = line[0];
  String a1, a2;
  int sp1 = line.indexOf(' ');
  if (sp1 > 0) {
    String rest = line.substring(sp1 + 1); rest.trim();
    int sp2 = rest.indexOf(' ');
    a1 = sp2 > 0 ? rest.substring(0, sp2) : rest;
    a2 = sp2 > 0 ? rest.substring(sp2 + 1) : "";
    a2.trim();
  }
  auto hex = [](const String& s) { return (uint32_t)strtoul(s.c_str(), nullptr, 16); };

  switch (c) {
    case '?': help(); break;
    case 'E':
      digitalWrite(PIN_BUS_EN_N, a1.toInt() ? HIGH : LOW);
      Serial.printf("BUS_EN_N = %d (%s)\n", a1.toInt(), a1.toInt() ? "Hi-Z" : "enabled");
      break;
    case 'A':
      setAddr(hex(a1), hex(a2));
      Serial.printf("CPU A=$%04X  PPU A=$%04X\n", hex(a1) & 0x7FFF, hex(a2) & 0x3FFF);
      break;
    case 'C': setCtrl(a1, a2.toInt() ? HIGH : LOW); break;
    case 'I': idleBus(); Serial.println("control lines idle"); break;
    case 'R': { uint16_t v = read165();
      Serial.printf("CPU D=%02X  PPU D=%02X\n", v >> 8, v & 0xFF); } break;
    case 'G':
      Serial.printf("CIRAM_A10=%d  CIRAM_CE_N=%d\n",
                    digitalRead(PIN_CIRAM_A10), digitalRead(PIN_CIRAM_CE_N));
      break;
    case 'S':
      clkDelayUs = a1.toInt() > 1 ? (uint32_t)a1.toInt() : 1;
      Serial.printf("shift clock half-period = %u us\n", (unsigned)clkDelayUs);
      break;
    case 'V': cmdVector(); break;
    case 'P': case 'H': {
      uint16_t base = hex(a1);
      int n = a2.length() ? a2.toInt() : 16;
      uint8_t buf[16];
      for (int i = 0; i < n; i += 16) {
        int m = min(16, n - i);
        for (int j = 0; j < m; j++)
          buf[j] = (c == 'P') ? readPrg(base + i + j) : readChr(base + i + j);
        dumpLine(base + i, buf, m);
      }
    } break;
    case 'M': cmdMirror(); break;
    case 'D': cmdDump(); break;
    default: Serial.println("unknown command. ? for help");
  }
}

// ---------------------------------------------------------------
void setup() {
  // 出力ピンは「非アクティブ値を先に書いてから OUTPUT」にしてグリッチを防ぐ
  digitalWrite(PIN_BUS_EN_N, HIGH);  pinMode(PIN_BUS_EN_N, OUTPUT);
  digitalWrite(PIN_OE_DATA_N, HIGH); pinMode(PIN_OE_DATA_N, OUTPUT);
  digitalWrite(PIN_PL_165_N, HIGH);  pinMode(PIN_PL_165_N, OUTPUT);
  digitalWrite(PIN_RCLK_ADDR, LOW);  pinMode(PIN_RCLK_ADDR, OUTPUT);
  digitalWrite(PIN_RCLK_DATA, LOW);  pinMode(PIN_RCLK_DATA, OUTPUT);
  for (int p : {PIN_M2, PIN_ROMSEL_N, PIN_RW, PIN_PPU_RD_N, PIN_PPU_WR_N, PIN_PPU_A13_N})
    pinMode(p, OUTPUT);
  idleBus();
  pinMode(PIN_CIRAM_A10, INPUT);
  pinMode(PIN_CIRAM_CE_N, INPUT);

  digitalWrite(PIN_SCK, LOW);        pinMode(PIN_SCK, OUTPUT);
  digitalWrite(PIN_MOSI, LOW);       pinMode(PIN_MOSI, OUTPUT);
  pinMode(PIN_MISO, INPUT);
  setAddr(0, 0);

  Serial.begin(115200);
  delay(300);
  Serial.println("\nFamicom Dumper v3 (bit-bang + dump). BUS_EN_N=1 (Hi-Z) at boot");
  help();
}

void loop() {
  static String buf;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\r' || ch == '\n') { handle(buf); buf = ""; }
    else buf += ch;
  }
}
