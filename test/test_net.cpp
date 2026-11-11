// Host tests for the WiFi state machine (design 11.2, scenarios N1..N11).
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror test/test_net.cpp lib/gqsafe/src/*.cpp
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "../lib/gqsafe/src/netfsm.h"

#define CHECK(c)                                                                  \
  do {                                                                            \
    if (!(c)) {                                                                   \
      fprintf(stderr, "CHECK failed: %s  (%s:%d, %s)\n", #c, __FILE__, __LINE__, g_test); \
      exit(1);                                                                    \
    }                                                                             \
  } while (0)

static const char* g_test = "";

struct Ev { uint32_t t; uint8_t bits; };  // t = ms since the scenario's reference time

struct Sim {
  NetFsm f;
  NetIn in;
  uint32_t t0;       // time of init (ms)
  std::vector<Ev> ev;

  explicit Sim(uint32_t start, uint32_t seed = 1, uint8_t restarts = 0) { reset(start, seed, restarts); }
  void reset(uint32_t start, uint32_t seed, uint8_t restarts) {
    t0 = start;
    netfsm_init(&f, start, seed);
    in = NetIn();
    in.now_ms = start;
    in.sta_up = 1;
    in.have_creds = 1;
    in.ever_connected = 1;
    in.restarts_done = restarts;
    ev.clear();
  }
  uint32_t elapsed() const { return in.now_ms - t0; }
  // advance dt ms, step once, record events
  uint8_t step(uint32_t dt = 1000) {
    in.now_ms += dt;
    uint8_t o = netfsm_step(&f, &in);
    if (o) ev.push_back({in.now_ms - t0, o});
    return o;
  }
  void run(uint32_t seconds) { for (uint32_t i = 0; i < seconds; i++) step(); }
  void ntpNow() { in.last_ntp_ms = in.now_ms ? in.now_ms : 1; }
  // link up, healthy NTP, run for `seconds`
  void runHealthy(uint32_t seconds) {
    for (uint32_t i = 0; i < seconds; i++) {
      if (i % 3600 == 0) ntpNow();
      step();
    }
  }
  std::vector<uint32_t> times(uint8_t bit, uint32_t from = 0) const {
    std::vector<uint32_t> r;
    for (const Ev& e : ev) if ((e.bits & bit) && e.t >= from) r.push_back(e.t);
    return r;
  }
};

// small deterministic RNG for the tests themselves
static uint32_t g_rs = 12345;
static uint32_t trnd() {
  g_rs ^= g_rs << 13; g_rs ^= g_rs >> 17; g_rs ^= g_rs << 5;
  return g_rs;
}

static const uint8_t GOLDEN_OK = NA_BEGIN | NA_STA_STOP | NA_AP_ON | NA_MDNS_RESTART;

// ---------------------------------------------------------------- N1
static void n1() {
  g_test = "N1";
  Sim s(0xFFF00000u);     // wraps after about 17 minutes
  s.in.sta_up = 1;
  const uint32_t days60 = 60u * 86400u;
  s.runHealthy(days60);
  CHECK(s.ev.size() == 1);
  CHECK(s.ev[0].bits == NA_AP_OFF);
  CHECK(s.ev[0].t >= 600000 && s.ev[0].t <= 602000);
  printf("N1 ok\n");
}

// ---------------------------------------------------------------- N2
static void n2() {
  g_test = "N2";
  for (uint32_t seed = 1; seed <= 50; seed++) {
    Sim s(5000, seed);
    s.runHealthy(1200);                       // AP gets switched off after the golden window
    CHECK(s.times(NA_AP_OFF).size() == 1);
    s.ev.clear();
    const uint32_t base = s.elapsed();        // outage starts with the next step
    s.in.sta_up = 0;
    s.run(200);
    // retry timing: 3, 18, 48, 108 s plus up to 10% per delay (+1 s step granularity each)
    static const uint32_t d[4] = {3000, 15000, 30000, 60000};
    std::vector<uint32_t> b = s.times(NA_BEGIN);
    CHECK(b.size() == 4);                     // the 5th is due at about 228 s
    uint32_t lo = 0, hi = 0;
    for (int i = 0; i < 4; i++) {
      lo += d[i];
      hi += d[i] + d[i] / 10 + 1000;
      uint32_t rel = b[i] - (base + 1000);    // the outage is noticed at its first step, base+1000
      CHECK(rel >= lo && rel <= hi);
    }
    // stops: 20 s after a begin when no newer begin came first
    std::vector<uint32_t> st = s.times(NA_STA_STOP);
    size_t expectStops = 0;
    for (size_t i = 0; i < b.size(); i++) {
      uint32_t next = i + 1 < b.size() ? b[i + 1] : 0xFFFFFFFFu;
      if (next - b[i] > 21000) {
        bool found = false;
        for (uint32_t x : st) if (x >= b[i] + 20000 && x <= b[i] + 21000) found = true;
        CHECK(found);
        expectStops++;
      }
    }
    CHECK(st.size() == expectStops);
    // hotspot at 60 s down
    std::vector<uint32_t> on = s.times(NA_AP_ON);
    CHECK(on.size() == 1);
    CHECK(on[0] - (base + 1000) >= 60000 && on[0] - (base + 1000) <= 61000);
  }
  // recovery after 2 minutes
  Sim s(5000, 7);
  s.runHealthy(1200);
  s.ev.clear();
  s.in.sta_up = 0;
  s.run(120);
  s.ev.clear();
  s.in.sta_up = 1;
  s.step();
  uint32_t up = s.ev.empty() ? 0 : s.ev[0].t;
  CHECK(!s.ev.empty() && (s.ev[0].bits & NA_MDNS_RESTART));
  s.ntpNow();
  s.run(100);
  std::vector<uint32_t> off = s.times(NA_AP_OFF);
  CHECK(off.size() == 1);
  CHECK(off[0] - up >= 60000 && off[0] - up <= 62000);
  CHECK(s.times(NA_MDNS_RESTART).size() == 1);
  printf("N2 ok\n");
}

// ---------------------------------------------------------------- N3
static void n3() {
  g_test = "N3";
  uint32_t now = 123456;
  Sim s(now, 3, 0);
  s.in.sta_up = 0;
  const uint32_t total = 3u * 86400u;
  uint32_t restarts = 0, begins = 0;
  uint32_t bootStart = 0;                      // seconds since scenario start
  uint32_t expectH[4] = {6, 12, 24, 24};
  std::vector<uint32_t> cyc;                   // cycle times in the current boot (seconds)
  for (uint32_t sec = 0; sec < total; sec++) {
    uint8_t o = s.step();
    if (o & NA_BEGIN) begins++;
    if (o & NA_STA_CYCLE) {
      cyc.push_back(sec + 1 - bootStart);
    }
    if (o & NA_RESTART) {
      uint32_t up = sec + 1 - bootStart;
      CHECK(restarts < 4);
      CHECK(up >= expectH[restarts] * 3600 && up <= expectH[restarts] * 3600 + 5);
      // cycles of this boot: first at 10 min, then every 30 min
      CHECK(!cyc.empty() && cyc[0] >= 600 && cyc[0] <= 602);
      for (size_t i = 1; i < cyc.size(); i++) CHECK(cyc[i] - cyc[i - 1] >= 1800 && cyc[i] - cyc[i - 1] <= 1802);
      cyc.clear();
      restarts++;
      uint32_t t = s.in.now_ms;
      s.reset(t, 100 + restarts, (uint8_t)restarts);
      s.in.sta_up = 0;
      bootStart = sec + 1;
    }
  }
  CHECK(restarts == 4);
  printf("begins=%u limit=%u\n", begins, total / 300 + 10);
  CHECK(begins <= total / 300 + 10);
  CHECK(begins > total / 330);                 // and it really keeps trying
  printf("N3 ok (begins=%u)\n", begins);
}

// ---------------------------------------------------------------- N4
static void randomizeInputs(NetIn& in, bool allowSafe) {
  in.sta_up = trnd() & 1;
  in.have_creds = (trnd() % 5) != 0;
  in.ap_clients = (trnd() % 3 == 0) ? (uint8_t)(1 + trnd() % 3) : 0;
  in.updating = (trnd() % 6) == 0;
  in.safe_mode = allowSafe && (trnd() % 8) == 0;
  in.ever_connected = trnd() & 1;
  in.restarts_done = (uint8_t)(trnd() % 5);
  in.last_http_ms = (trnd() & 1) ? 0 : in.now_ms - (trnd() % 20000000u);
  in.last_ntp_ms = (trnd() & 1) ? 0 : in.now_ms - (trnd() % 20000000u);
}

static void n4() {
  g_test = "N4";
  for (uint32_t run = 0; run < 300; run++) {
    Sim s(trnd(), run + 1);
    for (uint32_t i = 0; i < 599; i++) {
      if (trnd() % 4 == 0) randomizeInputs(s.in, true);
      uint8_t o = s.step();
      CHECK((o & (NA_AP_OFF | NA_STA_CYCLE | NA_RESTART)) == 0);
      CHECK((o & ~GOLDEN_OK) == 0);            // design 8.4: only these four actions
    }
  }
  printf("N4 ok\n");
}

// ---------------------------------------------------------------- N5
static void n5() {
  g_test = "N5";
  for (uint32_t run = 0; run < 20; run++) {
    Sim s(trnd(), run + 1);
    s.in.ap_clients = 1;
    uint8_t lastUp = 0;
    std::vector<uint32_t> b;
    for (uint32_t i = 0; i < 12 * 3600; i++) {
      if (i % 1800 == 0) lastUp = (trnd() % 3) != 0 ? 0 : 1;   // mostly down, sometimes up
      s.in.sta_up = lastUp;
      s.in.ap_clients = (uint8_t)(1 + (i / 7200) % 2);
      if (i % 3600 == 0) s.ntpNow();
      uint8_t o = s.step();
      CHECK((o & (NA_AP_OFF | NA_STA_CYCLE | NA_RESTART)) == 0);
      if (o & NA_BEGIN) b.push_back(s.in.now_ms);
    }
    CHECK(b.size() > 20);
    for (size_t i = 1; i < b.size(); i++) CHECK(b[i] - b[i - 1] >= 300000);
  }
  printf("N5 ok\n");
}

// ---------------------------------------------------------------- N6
static void n6() {
  g_test = "N6";
  Sim s(777, 6);
  s.runHealthy(1200);
  CHECK(s.times(NA_AP_OFF).size() == 1);       // hotspot is off now
  s.ev.clear();
  s.in.updating = 1;
  s.in.sta_up = 0;
  for (uint32_t i = 0; i < 3 * 86400u / 10; i++) {
    if (i % 500 == 0) { s.in.sta_up = trnd() & 1; s.in.ap_clients = trnd() % 2; }
    if (i % 100 == 0) s.in.last_ntp_ms = 0;
    s.step(10000);
  }
  for (const Ev& e : s.ev) CHECK((e.bits & ~NA_AP_ON) == 0);
  CHECK(s.times(NA_AP_ON).size() == 1);
  printf("N6 ok\n");
}

// ---------------------------------------------------------------- N7
static void n7() {
  g_test = "N7";
  Sim s(0xFFFFF000u, 9);
  s.in.safe_mode = 1;
  s.in.sta_up = 0;
  std::vector<uint32_t> b;
  for (uint32_t i = 0; i < 24 * 3600; i++) {
    if (i % 7200 == 3600) s.in.ap_clients = 1;
    if (i % 7200 == 0) s.in.ap_clients = 0;
    if (i % 9000 == 0) s.in.sta_up = trnd() & 1;
    uint8_t o = s.step();
    CHECK((o & ~(NA_AP_ON | NA_BEGIN)) == 0);
    if (o & NA_BEGIN) b.push_back(s.in.now_ms);
  }
  CHECK(b.size() > 50);
  for (size_t i = 1; i < b.size(); i++) CHECK(b[i] - b[i - 1] >= 300000);
  printf("N7 ok\n");
}

// ---------------------------------------------------------------- N8
static void n8() {
  g_test = "N8";
  Sim s(100000, 8);
  s.in.last_ntp_ms = s.in.now_ms - 7u * 3600 * 1000;
  s.in.last_http_ms = s.in.now_ms - 7u * 3600 * 1000;
  s.run(1000);
  std::vector<uint32_t> st = s.times(NA_STA_STOP);
  CHECK(st.size() == 1);
  std::vector<uint32_t> b = s.times(NA_BEGIN);
  CHECK(b.size() == 1);
  CHECK(b[0] - st[0] <= 3000 && b[0] > st[0]);
  std::vector<uint32_t> k = s.times(NA_SNTP_KICK);
  CHECK(k.size() == 1 && k[0] == b[0]);
  uint32_t first = st[0];
  s.ev.clear();
  // not again for 6 h (NTP and requests stay silent, link stays "up")
  while (s.elapsed() < first + 6u * 3600 * 1000 - 2000) s.step();
  CHECK(s.times(NA_STA_STOP).empty());
  CHECK(s.times(NA_BEGIN).empty());
  CHECK(s.times(NA_SNTP_KICK).empty());
  s.run(10);
  CHECK(s.times(NA_STA_STOP).size() == 1);     // then it is allowed again
  // a link that never got NTP: acted on after 30 minutes up
  Sim n(3000, 5);
  n.in.last_ntp_ms = 0;
  n.in.last_http_ms = 0;
  n.run(1700);
  CHECK(n.times(NA_STA_STOP).empty());
  n.run(200);
  CHECK(n.times(NA_STA_STOP).size() == 1 && n.times(NA_SNTP_KICK).size() == 1);
  // healthy link: nothing
  Sim h(3000, 5);
  h.runHealthy(86400);
  CHECK(h.times(NA_STA_STOP).empty());
  printf("N8 ok\n");
}

// ---------------------------------------------------------------- N9
static void n9() {
  g_test = "N9";
  Sim s(4000, 2);
  s.runHealthy(1200);
  s.ev.clear();
  s.in.have_creds = 0;
  for (uint32_t i = 0; i < 3 * 86400u / 10; i++) {
    if (i % 400 == 0) s.in.sta_up = trnd() & 1;
    if (i % 900 == 0) s.in.ap_clients = trnd() % 2;
    s.step(10000);
  }
  for (const Ev& e : s.ev) CHECK((e.bits & ~NA_AP_ON) == 0);
  CHECK(s.times(NA_AP_ON).size() == 1);
  CHECK(netfsm_stage(&s.f) == NS_NO_CREDS);
  printf("N9 ok\n");
}

// ---------------------------------------------------------------- N10
static void n10() {
  g_test = "N10";
  uint32_t total = 0;
  for (int mode = 0; mode < 4; mode++) {
    Sim s(trnd(), 100 + mode);
    uint32_t flipUp = mode < 2 ? 30 : 30000;     // how often the link state changes
    uint32_t restarts = 0;
    bool haveBegin = false;
    uint32_t lastBegin = 0;
    for (uint32_t i = 0; i < 250000; i++) {
      if (trnd() % flipUp == 0) s.in.sta_up = trnd() & 1;
      if (trnd() % 700 == 0) s.in.ap_clients = (trnd() % 4 == 0) ? 1 : 0;
      if (trnd() % 40000 == 0) s.in.updating = !s.in.updating;
      if (trnd() % 90000 == 0) s.in.safe_mode = !s.in.safe_mode;
      if (trnd() % 50000 == 0) s.in.have_creds = !s.in.have_creds;
      if (trnd() % 3000 == 0) { s.in.last_http_ms = s.in.now_ms; }
      if (trnd() % 6000 == 0) { s.in.last_ntp_ms = s.in.now_ms ? s.in.now_ms : 1; }
      if (trnd() % 20000 == 0) netfsm_kick(&s.f);
      s.in.restarts_done = (uint8_t)(mode & 3);
      if (mode == 3) s.in.ever_connected = 0;
      uint32_t dt = (trnd() % 10 == 0) ? 500 + trnd() % 4000 : 1000;
      uint8_t o = s.step(dt);
      if (o & NA_BEGIN) {
        if (haveBegin) CHECK(s.in.now_ms - lastBegin >= 3000);
        haveBegin = true;
        lastBegin = s.in.now_ms;
      }
      if (o & NA_RESTART) restarts++;
      CHECK(netfsm_stage(&s.f) <= NS_NO_CREDS);
      total++;
    }
    CHECK(restarts <= 1);
  }
  CHECK(total >= 1000000);
  printf("N10 ok (%u steps)\n", total);
}

// ---------------------------------------------------------------- N11
static void n11() {
  g_test = "N11";
  Sim s(9000, 11);
  s.runHealthy(1200);
  s.ev.clear();
  netfsm_kick(&s.f);
  uint8_t o = s.step();
  CHECK(o & NA_BEGIN);
  // kick bypasses the AP-client throttle, but never breaks the 3 s gap
  Sim c(9000, 12);
  c.in.sta_up = 0;
  c.in.ap_clients = 1;
  c.run(100);
  c.ev.clear();
  c.run(1);
  netfsm_kick(&c.f);
  o = c.step();
  CHECK(o & NA_BEGIN);
  netfsm_kick(&c.f);
  o = c.step();
  CHECK(!(o & NA_BEGIN));                      // 1 s after the last begin
  c.step();
  o = c.step();
  CHECK(o & NA_BEGIN);                         // 3 s after: allowed
  // safe mode honours the kick too
  Sim m(9000, 13);
  m.in.safe_mode = 1;
  m.in.sta_up = 0;
  m.run(10);
  netfsm_kick(&m.f);
  CHECK(m.step() & NA_BEGIN);
  // golden window rules hold for a kick as well
  Sim g(9000, 14);
  netfsm_kick(&g.f);
  o = g.step();
  CHECK((o & ~GOLDEN_OK) == 0);
  printf("N11 ok\n");
}

int main() {
  n1(); n2(); n3(); n4(); n5(); n6(); n7(); n8(); n9(); n10(); n11();
  printf("test_net: all passed\n");
  return 0;
}
