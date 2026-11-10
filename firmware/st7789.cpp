#include "st7789.h"
#include <SPI.h>

namespace lcd {

static constexpr uint8_t PIN_DC = 0;
static constexpr uint8_t PIN_RST = 2;
static constexpr uint8_t PIN_BL = 5;        // active LOW
static constexpr uint32_t SPI_HZ = 40000000;

static uint8_t s_madctl = 0x00;
static bool s_inv = true;
static int s_bright = 80;

static inline void cmd(uint8_t c) {
  digitalWrite(PIN_DC, LOW);
  SPI.write(c);
  digitalWrite(PIN_DC, HIGH);
}

static inline void data8(uint8_t d) { SPI.write(d); }

static void cmdData(uint8_t c, const uint8_t* d, size_t n) {
  cmd(c);
  for (size_t i = 0; i < n; i++) data8(d[i]);
}

static void window(int x0, int y0, int x1, int y1) {
  uint8_t ca[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
  uint8_t ra[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
  cmdData(0x2A, ca, 4);
  cmdData(0x2B, ra, 4);
  cmd(0x2C);
}

void setBrightness(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  s_bright = percent;
  pinMode(PIN_BL, OUTPUT);
  if (percent == 0) { digitalWrite(PIN_BL, HIGH); return; }
  if (percent == 100) { digitalWrite(PIN_BL, LOW); return; }
  analogWriteRange(1000);
  analogWrite(PIN_BL, 1000 - percent * 10);  // inverted: LOW = on
}

int brightness() { return s_bright; }

void begin() {
  // Backlight off while the panel initialises (avoids a white flash).
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);
  pinMode(PIN_DC, OUTPUT);
  digitalWrite(PIN_DC, HIGH);

  SPI.begin();
  SPI.setFrequency(SPI_HZ);
  SPI.setDataMode(SPI_MODE3);
  SPI.setBitOrder(MSBFIRST);

  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, HIGH); delay(50);
  digitalWrite(PIN_RST, LOW);  delay(50);
  digitalWrite(PIN_RST, HIGH); delay(120);

  cmd(0x11); delay(120);                                         // sleep out
  { const uint8_t d[] = {0x1F, 0x1F, 0x00, 0x33, 0x33}; cmdData(0xB2, d, 5); }  // porch
  { const uint8_t d[] = {0x00}; cmdData(0x35, d, 1); }           // tearing effect off
  cmdData(0x36, &s_madctl, 1);                                   // MADCTL
  { const uint8_t d[] = {0x05}; cmdData(0x3A, d, 1); }           // RGB565
  { const uint8_t d[] = {0x00}; cmdData(0xB7, d, 1); }
  { const uint8_t d[] = {0x36}; cmdData(0xBB, d, 1); }
  { const uint8_t d[] = {0x2C}; cmdData(0xC0, d, 1); }
  { const uint8_t d[] = {0x01}; cmdData(0xC2, d, 1); }
  { const uint8_t d[] = {0x13}; cmdData(0xC3, d, 1); }
  { const uint8_t d[] = {0x20}; cmdData(0xC4, d, 1); }
  { const uint8_t d[] = {0x13}; cmdData(0xC6, d, 1); }
  { const uint8_t d[] = {0xA1}; cmdData(0xD6, d, 1); }
  { const uint8_t d[] = {0xA4, 0xA1}; cmdData(0xD0, d, 2); }
  { const uint8_t d[] = {0xA1}; cmdData(0xD6, d, 1); }
  { const uint8_t d[] = {0xF0, 0x08, 0x0E, 0x09, 0x08, 0x04, 0x2F, 0x33, 0x45, 0x36, 0x13, 0x12, 0x2A, 0x2D};
    cmdData(0xE0, d, 14); }
  { const uint8_t d[] = {0xF0, 0x0E, 0x12, 0x0C, 0x0A, 0x15, 0x2E, 0x32, 0x44, 0x39, 0x17, 0x18, 0x2B, 0x2F};
    cmdData(0xE1, d, 14); }
  { const uint8_t d[] = {0x1D, 0x00, 0x00}; cmdData(0xE4, d, 3); }
  cmd(s_inv ? 0x21 : 0x20);                                       // inversion
  cmd(0x29);                                                      // display on
  fill(0x0000);
  setBrightness(s_bright);
}

void pushRows(int y, int h, const uint16_t* px) {
  window(0, y, 239, y + h - 1);
  SPI.writeBytes((const uint8_t*)px, (uint32_t)240 * h * 2);
}

void fill(uint16_t c) {
  window(0, 0, 239, 239);
  uint8_t pat[2] = {(uint8_t)(c & 0xFF), (uint8_t)(c >> 8)};  // c is already big-endian in memory order
  SPI.writePattern(pat, 2, 240 * 240);
}

void setMadctl(uint8_t v) { s_madctl = v; cmdData(0x36, &s_madctl, 1); }
void setInversion(bool on) { s_inv = on; cmd(on ? 0x21 : 0x20); }
uint8_t madctl() { return s_madctl; }
bool inversion() { return s_inv; }

}  // namespace lcd
