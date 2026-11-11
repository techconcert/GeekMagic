// Minimal ST7789 240x240 driver for the GeekMagic clock (hardware SPI).
// Pins (confirmed by the GeekMagic-Open-Firmware teardown):
//   MOSI 13, SCLK 14, DC 0, RST 2, CS tied to GND, backlight 5 (active LOW).
#pragma once
#include <Arduino.h>

namespace lcd {

void begin();
// Push a 240 x h block of big-endian RGB565 pixels starting at row y.
void pushRows(int y, int h, const uint16_t* px);
void fill(uint16_t color_be);
// 0..100 %. 0 = backlight off.
void setBrightness(int percent);
int brightness();
// Runtime fixes for colour order / inversion (not saved; for testing only).
void setMadctl(uint8_t v);
void setInversion(bool on);
// R3 S1: display off + sleep in + backlight off; wake() re-runs begin() (hardware reset, init sequence, black, backlight).
// While asleep pushRows/fill/setBrightness/setMadctl/setInversion only record state. Called from nightmode.cpp only.
void sleep();
void wake();
bool asleep();
uint8_t madctl();
bool inversion();

}  // namespace lcd
