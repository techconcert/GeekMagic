// WiFi watchdog: carries out the actions of the pure state machine in
// lib/gqsafe/src/netfsm.cpp. Event handlers only set flags; all work happens
// in loop(). Nothing here blocks.
#pragma once
#include <Arduino.h>

namespace netwatch {

// Reads the stored station credentials into RAM, switches SDK auto-reconnect
// off and registers the WiFi event handlers. Call after the soft AP is up.
void begin(bool safeMode);
// Call every loop iteration; does its work once per second.
void loop();
// From the WiFi form: update the RAM copy of the credentials and attempt now.
void setCredentials(const char* ssid, const char* pass);

uint8_t stage();            // 0 online, 1 retrying, 2 cycling, 3 restart pending, 4 no credentials
uint32_t attempts();        // connection attempts since the link was lost
uint32_t downSeconds();     // seconds since the link was lost (0 while online)
uint8_t lastReason();       // SDK reason code of the last disconnect (0 = none yet)

// RECOVERY-CHANGE (chunk1): diagnostics for the status page and /info
uint32_t reconnects();      // times the link came up since power-on
uint32_t ntpAgeSeconds();   // seconds since the last NTP sync, 0xFFFFFFFF = never
// i = 0 is the newest of the last 8 events (kept in RTC blocks 112..127). False when there is none.
bool eventLine(uint8_t i, char* buf, size_t n);

void noteNtp();             // a time sync succeeded
void noteHttp();            // a web request was served

}  // namespace netwatch
