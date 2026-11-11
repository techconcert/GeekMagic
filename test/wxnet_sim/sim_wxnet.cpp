// Host simulation of firmware/wxnet.cpp against a fake lwIP that enforces the rules of the design (use after free, close or abort inside a callback, calls on freed connections, stale DNS answers).
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Itest/wxnet_sim/fake -Ifirmware -Ilib/gqsafe/src -Ilib/gamecore/src test/wxnet_sim/sim_wxnet.cpp firmware/wxnet.cpp lib/gqsafe/src/wxparse.cpp lib/gqsafe/src/wxfsm.cpp && ./a.out   (from the repository root)
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <Arduino.h>
#include <lwip/tcp.h>
#include <lwip/dns.h>
#include "wxnet.h"
#include "safety.h"
#include "config.h"
#include "nightmode.h"
#include "gamecore.h"

#define CHECK(c) do { if (!(c)) { printf("CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

// ---- fake platform
static uint32_t g_now = 100000;
static uint32_t g_heap = 30000, g_block = 20000;
uint32_t millis() { return g_now; }
uint32_t micros() { return g_now * 1000u; }
uint32_t ESPClass::getFreeHeap() { return g_heap; }
uint32_t ESPClass::getMaxFreeBlockSize() { return g_block; }
uint32_t ESPClass::getChipId() { return 0xABCDEF; }
ESPClass ESP;

static bool g_updating = false, g_safe = false, g_sta = true;
static int g_markDepth = 0, g_marksOn = 0, g_marksOff = 0;
namespace safety {
bool updating() { return g_updating; }
bool safeMode() { return g_safe; }
bool staConnected() { return g_sta; }
uint32_t msSinceRequest() { return 0xFFFFFFFFu; }
uint8_t wxStrikes() { return 0; }
void wxMark(bool busy) {
  if (busy) { CHECK(g_markDepth == 0); g_markDepth = 1; g_marksOn++; }
  else { CHECK(g_markDepth == 1); g_markDepth = 0; g_marksOff++; }
}
}  // namespace safety
static GqConf g_conf;
namespace config { const GqConf& get() { return g_conf; } }
namespace nightmode { bool screenOff() { return false; } bool wakeSoon() { return false; } }

// ---- fake lwIP
static bool g_inCb = false;
static int g_dnsMode = 1;                     // 0 cached, 1 in progress, 2 immediate error
static dns_found_callback g_dnsCb; static void* g_dnsArg;
static int g_dnsCalls = 0;
static bool g_newFail = false, g_connectFail = false, g_writeFail = false, g_closeFail = false;
static tcp_pcb g_pool[16]; static int g_poolN = 0;
static tcp_pcb* g_cur = nullptr;
static std::string g_written;
static int g_closes = 0, g_aborts = 0, g_pbufFrees = 0, g_recved = 0;
static int g_errfCalledByAbort = 0;

struct Live { static int count() { int n = 0; for (int i = 0; i < g_poolN; i++) if (!g_pool[i].freed) n++; return n; } };
static void alive(tcp_pcb* p) { CHECK(p != nullptr); CHECK(!p->freed); }

err_t dns_gethostbyname(const char* host, ip_addr_t* addr, dns_found_callback cb, void* arg) {
  CHECK(!g_inCb);
  CHECK(strcmp(host, "api.open-meteo.com") == 0);
  g_dnsCalls++;
  if (g_dnsMode == 0) { addr->addr = 0x01020304; return ERR_OK; }
  if (g_dnsMode == 2) return ERR_VAL;
  g_dnsCb = cb; g_dnsArg = arg; return ERR_INPROGRESS;
}
tcp_pcb* tcp_new() { CHECK(!g_inCb); if (g_newFail) return nullptr; CHECK(g_poolN < 16); tcp_pcb* p = &g_pool[g_poolN++]; memset(p, 0, sizeof *p); g_cur = p; return p; }
void tcp_arg(tcp_pcb* p, void* a) { alive(p); p->arg = a; }
void tcp_recv(tcp_pcb* p, tcp_recv_fn f) { alive(p); p->recv = f; }
void tcp_err(tcp_pcb* p, tcp_err_fn f) { alive(p); p->errf = f; }
void tcp_sent(tcp_pcb* p, tcp_sent_fn f) { alive(p); p->sent = f; }
void tcp_poll(tcp_pcb* p, tcp_poll_fn f, u8_t) { alive(p); p->poll = f; }
err_t tcp_connect(tcp_pcb* p, const ip_addr_t* ip, u16_t port, tcp_connected_fn f) { alive(p); CHECK(port == 80); CHECK(ip->addr == 0x01020304); if (g_connectFail) return ERR_MEM; p->connected = f; return ERR_OK; }
err_t tcp_write(tcp_pcb* p, const void* d, u16_t n, u8_t flags) { alive(p); CHECK(flags == 0); if (g_writeFail) return ERR_MEM; g_written.assign((const char*)d, n); p->wdata = d; p->wlen = n; return ERR_OK; }
err_t tcp_output(tcp_pcb* p) { alive(p); return ERR_OK; }
void tcp_recved(tcp_pcb* p, u16_t n) { alive(p); g_recved += n; }
err_t tcp_close(tcp_pcb* p) { CHECK(!g_inCb); alive(p); g_closes++; if (g_closeFail) return ERR_MEM; p->freed = 1; return ERR_OK; }
void tcp_abort(tcp_pcb* p) {
  CHECK(!g_inCb); alive(p); g_aborts++;
  if (p->errf) { g_errfCalledByAbort++; p->freed = 1; p->errf(p->arg, ERR_ABRT); }   // what lwIP does
  p->freed = 1;
}
void pbuf_free(pbuf*) { g_pbufFrees++; }

// ---- harness
static void tickMs(uint32_t ms) { for (uint32_t i = 0; i < ms; i += 10) { g_now += 10; wxnet::loop(); } }
static void resetWorld() {
  g_inCb = false; g_dnsMode = 1; g_dnsCb = nullptr; g_newFail = g_connectFail = g_writeFail = g_closeFail = false;
  g_poolN = 0; g_cur = nullptr; g_written.clear(); g_closes = g_aborts = 0; g_updating = false; g_safe = false; g_sta = true;
  g_heap = 30000; g_block = 20000; g_dnsCalls = 0; g_errfCalledByAbort = 0;
  memset(&g_conf, 0, sizeof g_conf);
  g_conf.flags = GQC_F_WEATHER; g_conf.lat_c = -1297; g_conf.lon_c = -3845;
}
static void dnsAnswer(bool ok) { CHECK(g_dnsCb); ip_addr_t ip; ip.addr = ok ? 0x01020304 : 0; g_inCb = true; g_dnsCb("x", ok ? &ip : nullptr, g_dnsArg); g_inCb = false; }
static void connectDone() { tcp_pcb* p = g_cur; alive(p); CHECK(p->connected); g_inCb = true; p->connected(p->arg, p, ERR_OK); g_inCb = false; }
static void deliver(tcp_pcb* p, const std::string& s, size_t piece) {
  // a chain of pbufs of `piece` bytes
  std::vector<pbuf> chain((s.size() + piece - 1) / piece);
  for (size_t i = 0; i < chain.size(); i++) {
    size_t off = i * piece, n = s.size() - off < piece ? s.size() - off : piece;
    chain[i].payload = (void*)(s.data() + off); chain[i].len = (u16_t)n; chain[i].tot_len = (u16_t)(s.size() - off);
    chain[i].next = i + 1 < chain.size() ? &chain[i + 1] : nullptr;
  }
  int before = g_pbufFrees;
  g_inCb = true; err_t e = p->recv(p->arg, p, &chain[0], ERR_OK); g_inCb = false;
  CHECK(e == ERR_OK); CHECK(g_pbufFrees == before + 1);
}
static void peerClose(tcp_pcb* p) { g_inCb = true; p->recv(p->arg, p, nullptr, ERR_OK); g_inCb = false; }
static void lwipKills(tcp_pcb* p) { p->freed = 1; g_inCb = true; if (p->errf) p->errf(p->arg, ERR_RST); g_inCb = false; }

static std::string readFile(const char* path) { FILE* f = fopen(path, "rb"); CHECK(f); std::string s; char b[512]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f); return s; }

static bool haveReading(int* wx = nullptr, int* t = nullptr, int* fl = nullptr) { int a = 0, b = 0, c = 0; bool r = wxnet::reading(&a, &b, &c); if (wx) *wx = a; if (t) *t = b; if (fl) *fl = c; return r; }
static void boot() { resetWorld(); g_now = 0; wxnet::begin(false); }
// advance until the first fetch starts (uptime >= 40 s, link up >= 20 s)
static void toFirstFetch() { for (int i = 0; i < 6000 && g_dnsCalls == 0; i++) tickMs(10); CHECK(g_dnsCalls == 1); }

static void assertClean() {
  CHECK(Live::count() == 0); CHECK(g_markDepth == 0); CHECK(!g_inCb);
}

int main() {
  std::string ownerReply = readFile("test/fixtures/wx/owner_reply.http");
  std::string redirect = readFile("test/fixtures/wx/redirect301.http");
  char buf[256];

  // 1. happy path, reply in 3 pieces then the peer closes
  boot();
  CHECK(!haveReading());
  tickMs(20000); CHECK(g_dnsCalls == 0);               // not before 40 s uptime
  toFirstFetch();
  CHECK(g_now >= 40000 && g_now < 41100);
  tickMs(300); CHECK(g_pool[0].freed == 0 || g_poolN == 0);
  CHECK(g_poolN == 0);                                 // waits for DNS
  dnsAnswer(true);
  tickMs(20); CHECK(g_poolN == 1);
  connectDone();
  CHECK(g_written.find("GET /v1/forecast?latitude=-12.97&longitude=-38.45&current=temperature_2m,weather_code,wind_speed_10m,is_day HTTP/1.0\r\n") == 0);
  deliver(g_cur, ownerReply, 200);
  tickMs(20);                                           // outcome certain already: finishes without waiting for the close
  CHECK(g_closes == 1 && g_aborts == 0 && g_cur->freed);
  int wx, t, fl;
  CHECK(haveReading(&wx, &t, &fl));
  CHECK(wx == 1 && t == 252 && fl == 0);                // cloudy, 25.2 C, day, fresh, Celsius
  wxnet::statusLine(buf, sizeof buf); printf("status: %s\n", buf);
  CHECK(strstr(buf, "25.2 C") != 0);
  assertClean();
  CHECK(g_recved == (int)ownerReply.size());
  // the next fetch comes in 30 to 31 minutes
  int calls = g_dnsCalls;
  tickMs(29 * 60000); CHECK(g_dnsCalls == calls);
  for (int i = 0; i < 20000 && g_dnsCalls == calls; i++) tickMs(10);
  CHECK(g_dnsCalls == calls + 1);
  dnsAnswer(true); tickMs(20); connectDone();
  // Fahrenheit flag and a changed place
  g_conf.flags |= GQC_F_FAHRENHEIT;
  deliver(g_cur, ownerReply, 536); tickMs(20);
  CHECK(haveReading(&wx, &t, &fl) && (fl & GC_WXF_FAHRENHEIT));
  g_conf.lat_c = 5; CHECK(!haveReading());             // other place: reading is not shown
  g_conf.lat_c = -1297;
  g_conf.flags = 0; CHECK(!haveReading()); g_conf.flags = GQC_F_WEATHER;
  assertClean();

  // 2. stale then expired
  tickMs(1);
  // (no more successes: make every fetch fail with DNS errors and watch the age)
  g_dnsMode = 2;
  uint32_t t0 = g_now;
  while (g_now - t0 < 64 * 60000u) tickMs(1000);
  CHECK(haveReading(&wx, &t, &fl) && !(fl & GC_WXF_STALE));
  while (g_now - t0 < 70 * 60000u) tickMs(1000);
  CHECK(haveReading(&wx, &t, &fl) && (fl & GC_WXF_STALE));
  while (g_now - t0 < 130 * 60000u) tickMs(1000);
  CHECK(!haveReading());
  wxnet::statusLine(buf, sizeof buf); printf("status: %s\n", buf);
  assertClean();

  // 3. DNS never answers: abort at 8 s, late answer ignored, next fetch uses a new generation
  boot(); toFirstFetch();
  tickMs(7900); CHECK(g_poolN == 0 && g_markDepth == 1);
  tickMs(200); CHECK(g_markDepth == 0);                 // given up after 8 s
  dns_found_callback oldCb = g_dnsCb; void* oldArg = g_dnsArg;
  tickMs(30000);
  g_inCb = true; ip_addr_t ip; ip.addr = 0x01020304; oldCb("x", &ip, oldArg); g_inCb = false;   // late answer of the dead fetch
  tickMs(100); CHECK(g_poolN == 0);
  assertClean();
  // retry after 1 minute, and its own DNS answer is taken
  for (int i = 0; i < 20000 && g_dnsCalls == 1; i++) tickMs(10);
  CHECK(g_dnsCalls == 2);
  tickMs(100);
  dnsAnswer(true); tickMs(20); CHECK(g_poolN == 1);
  // the old callback arrives now with the old generation: ignored
  g_inCb = true; oldCb("x", &ip, oldArg); g_inCb = false;
  connectDone(); deliver(g_cur, ownerReply, 100); tickMs(20);
  CHECK(haveReading()); assertClean();

  // 4. DNS fails; DNS cached (ERR_OK path)
  boot(); g_dnsMode = 2; toFirstFetch(); tickMs(20); assertClean(); CHECK(!haveReading());
  boot(); g_dnsMode = 0; toFirstFetch(); tickMs(20); CHECK(g_poolN == 1); connectDone(); deliver(g_cur, ownerReply, 50); tickMs(20); CHECK(haveReading()); assertClean();
  boot(); g_dnsMode = 1; toFirstFetch(); dnsAnswer(false); tickMs(20); assertClean(); CHECK(g_poolN == 0);

  // 5. tcp_new fails; tcp_connect fails; write fails
  boot(); g_newFail = true; toFirstFetch(); dnsAnswer(true); tickMs(20); assertClean();
  boot(); g_connectFail = true; toFirstFetch(); dnsAnswer(true); tickMs(20); CHECK(g_aborts == 1); assertClean();
  boot(); g_writeFail = true; toFirstFetch(); dnsAnswer(true); tickMs(20); connectDone(); tickMs(20); CHECK(g_aborts == 1); CHECK(g_errfCalledByAbort == 0); assertClean();

  // 6. lwIP reports an error (connect refused, reset): pcb is already freed, no call on it
  boot(); toFirstFetch(); dnsAnswer(true); tickMs(20); lwipKills(g_cur); tickMs(20); CHECK(g_aborts == 0 && g_closes == 0); assertClean();
  boot(); toFirstFetch(); dnsAnswer(true); tickMs(20); connectDone(); lwipKills(g_cur); tickMs(20); CHECK(g_aborts == 0 && g_closes == 0); assertClean(); CHECK(!haveReading());

  // 7. connect never completes: abort at 8 s with tcp_abort and the err callback already cleared
  boot(); toFirstFetch(); dnsAnswer(true); tickMs(8100); CHECK(g_aborts == 1 && g_errfCalledByAbort == 0); assertClean();
  // reply never completes
  boot(); toFirstFetch(); dnsAnswer(true); tickMs(20); connectDone(); deliver(g_cur, ownerReply.substr(0, 300), 100); tickMs(8100); CHECK(g_aborts == 1); assertClean(); CHECK(!haveReading());

  // 8. upload starts mid-fetch: abort at once, in the same loop iteration
  boot(); toFirstFetch(); dnsAnswer(true); tickMs(20); connectDone(); tickMs(20);
  g_updating = true; { int a = g_aborts; wxnet::loop(); CHECK(g_aborts == a + 1); }
  assertClean(); tickMs(60000 * 3); CHECK(g_dnsCalls == 1);   // no new fetch while the upload runs
  g_updating = false;
  // upload during DNS
  boot(); toFirstFetch(); g_updating = true; wxnet::loop(); assertClean(); g_updating = false;

  // 9. redirect and garbage replies are failures; close() failing leads to abort
  boot(); toFirstFetch(); dnsAnswer(true); tickMs(20); connectDone(); deliver(g_cur, redirect, 64); tickMs(20); CHECK(!haveReading()); CHECK(g_closes == 1); assertClean();
  wxnet::statusLine(buf, sizeof buf); printf("status: %s\n", buf);
  boot(); g_closeFail = true; toFirstFetch(); dnsAnswer(true); tickMs(20); connectDone(); deliver(g_cur, ownerReply, 536); tickMs(20); CHECK(g_closes == 1 && g_aborts == 1 && haveReading()); CHECK(g_errfCalledByAbort == 0); assertClean();
  boot(); toFirstFetch(); dnsAnswer(true); tickMs(20); connectDone(); deliver(g_cur, std::string("\x01\x02garbage garbage"), 7); peerClose(g_cur); tickMs(20); CHECK(!haveReading()); assertClean();
  // peer closes without a reply
  boot(); toFirstFetch(); dnsAnswer(true); tickMs(20); connectDone(); peerClose(g_cur); tickMs(20); CHECK(!haveReading()); assertClean();
  // data after we are done / stray data in a wrong phase is acknowledged and freed
  boot(); toFirstFetch(); dnsAnswer(true); tickMs(20); connectDone(); deliver(g_cur, ownerReply, 536);
  { int rf = g_pbufFrees; deliver(g_cur, "late", 4); CHECK(g_pbufFrees == rf + 1); }
  tickMs(20); CHECK(haveReading()); assertClean();

  // 10. gates: no fetch with the link down, low heap, not configured, safe mode
  boot(); g_sta = false; tickMs(120000); CHECK(g_dnsCalls == 0);
  g_sta = true; tickMs(10000); CHECK(g_dnsCalls == 0); tickMs(11000); CHECK(g_dnsCalls == 1); dnsAnswer(false); tickMs(20); assertClean();
  boot(); g_heap = 9000; tickMs(120000); CHECK(g_dnsCalls == 0);
  boot(); g_conf.flags = 0; tickMs(120000); CHECK(g_dnsCalls == 0);
  boot(); wxnet::begin(true); tickMs(120000); CHECK(g_dnsCalls == 0 && g_poolN == 0);
  wxnet::statusLine(buf, sizeof buf); CHECK(buf[0] == 0);

  // 11. kick: new settings lead to a fetch within 3 s, and a running fetch is aborted first
  boot(); toFirstFetch(); dnsAnswer(true); tickMs(20); connectDone(); deliver(g_cur, ownerReply, 536); tickMs(20); CHECK(haveReading());
  calls = g_dnsCalls;
  wxnet::kick(); tickMs(3100); CHECK(g_dnsCalls == calls + 1);
  g_conf.lat_c = 4000;
  dnsAnswer(true); tickMs(20); connectDone();
  wxnet::kick(); tickMs(1100); CHECK(g_aborts == 1); assertClean();
  tickMs(3100); CHECK(g_dnsCalls == calls + 2);
  dnsAnswer(true); tickMs(20); connectDone();
  CHECK(g_written.find("latitude=40.00") != std::string::npos);

  printf("wxnet sim: all passed (marks on %d off %d)\n", g_marksOn, g_marksOff);
  return 0;
}
