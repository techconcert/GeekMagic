// Host tests for the live weather client's pure code (DESIGN-R3.md 9.2, chunk W1): wxparse and wxfsm.
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror test/test_wx.cpp lib/gqsafe/src/*.cpp
// Run from the repository root (the fixtures are read from test/fixtures/wx; the test also tries ../ and the test directory).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "../lib/gqsafe/src/wxparse.h"
#include "../lib/gqsafe/src/wxfsm.h"

static const char* g_test = "";
#define CHECK(c)                                                                          \
  do {                                                                                    \
    if (!(c)) {                                                                           \
      fprintf(stderr, "CHECK failed: %s  (%s:%d, %s)\n", #c, __FILE__, __LINE__, g_test); \
      exit(1);                                                                            \
    }                                                                                     \
  } while (0)

typedef std::string S;

// ------------------------------------------------------------------ helpers

static S loadFixture(const char* name) {
  static const char* dirs[] = {"test/fixtures/wx/", "fixtures/wx/", "../test/fixtures/wx/", "../fixtures/wx/"};
  for (const char* d : dirs) {
    FILE* f = fopen((S(d) + name).c_str(), "rb");
    if (!f) continue;
    S s;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
  }
  fprintf(stderr, "fixture %s not found (run from the repository root)\n", name);
  exit(1);
}

struct Res { int rc; WxReading r; };

static Res parseWhole(const S& in) {
  WxParser p;
  wxp_init(&p);
  wxp_feed(&p, (const uint8_t*)in.data(), (uint16_t)in.size());
  Res res; memset(&res, 0xA5, sizeof res);
  res.rc = wxp_finish(&p, &res.r);
  return res;
}

static Res parsePieces(const S& in, size_t piece) {
  WxParser p;
  wxp_init(&p);
  for (size_t i = 0; i < in.size(); i += piece) {
    size_t n = in.size() - i < piece ? in.size() - i : piece;
    wxp_feed(&p, (const uint8_t*)in.data() + i, (uint16_t)n);
  }
  Res res; memset(&res, 0xA5, sizeof res);
  res.rc = wxp_finish(&p, &res.r);
  return res;
}

static Res parseSplit(const S& in, size_t at) {
  WxParser p;
  wxp_init(&p);
  wxp_feed(&p, (const uint8_t*)in.data(), (uint16_t)at);
  wxp_feed(&p, (const uint8_t*)in.data() + at, (uint16_t)(in.size() - at));
  Res res; memset(&res, 0xA5, sizeof res);
  res.rc = wxp_finish(&p, &res.r);
  return res;
}

static S http(const S& body, const char* status = "200 OK", const S& extraHeaders = "") {
  return S("HTTP/1.1 ") + status + "\r\nServer: nginx\r\nContent-Type: application/json\r\n" + extraHeaders + "Connection: close\r\n\r\n" + body;
}

static S chunked(const S& body, const std::vector<size_t>& sizes, bool upper = false, bool ext = false) {
  S out;
  size_t pos = 0, k = 0;
  while (pos < body.size()) {
    size_t n = sizes[k++ % sizes.size()];
    if (n > body.size() - pos) n = body.size() - pos;
    char hex[16];
    snprintf(hex, sizeof hex, upper ? "%zX" : "%zx", n);
    out += hex;
    if (ext) out += ";name=value";
    out += "\r\n" + body.substr(pos, n) + "\r\n";
    pos += n;
  }
  return out + "0\r\n\r\n";
}

static S cur(const char* inner) { return S("{\"latitude\":1.0,\"current\":{") + inner + "}}"; }

static void expectOk(const S& reply, int temp10, int wmo, int wind, int day) {
  Res r = parseWhole(reply);
  if (r.rc != WXP_OK) fprintf(stderr, "rc=%d for: %.200s\n", r.rc, reply.c_str());
  CHECK(r.rc == WXP_OK);
  CHECK(r.r.temp_c10 == temp10);
  CHECK(r.r.wmo == wmo);
  CHECK(r.r.wind_kmh == wind);
  CHECK(r.r.is_day == day);
}

static void expectFail(const S& reply, int code) {
  Res r = parseWhole(reply);
  if (r.rc != code) fprintf(stderr, "rc=%d want %d for: %.200s\n", r.rc, code, reply.c_str());
  CHECK(r.rc == code);
  // out is written only for WXP_OK: the 0xA5 pattern must still be there
  const uint8_t* b = (const uint8_t*)&r.r;
  for (size_t i = 0; i < sizeof r.r; i++) CHECK(b[i] == 0xA5);
}

static uint64_t s_rng = 0x1234567887654321ull;
static uint32_t rnd() {
  s_rng ^= s_rng << 13; s_rng ^= s_rng >> 7; s_rng ^= s_rng << 17;
  return (uint32_t)(s_rng >> 16);
}

// ------------------------------------------------------------------ parser tests

static void testFixtures() {
  g_test = "fixtures";
  CHECK(sizeof(WxParser) <= 64);
  S body = loadFixture("owner_body.json");
  S reply = loadFixture("owner_reply.http");
  CHECK(body.size() == 415);
  // the owner's captured reply has no is_day: it counts as day
  expectOk(reply, 252, 3, 15, 1);              // 25.2 C, code 3, wind 14.7 rounds to 15
  expectOk(http(body), 252, 3, 15, 1);
  S dbody = loadFixture("design_body.json");
  S dreply = loadFixture("design_reply.http");
  expectOk(dreply, 73, 61, 12, 1);
  expectOk(http(dbody), 73, 61, 12, 1);
  // mapping of both
  WxReading r = {252, 3, 15, 1};
  CHECK(wx_state(&r) == 1);                    // cloudy
  r.temp_c10 = 73; r.wmo = 61; r.wind_kmh = 12;
  CHECK(wx_state(&r) == 2);                    // rain
}

static void testSplits(const S& reply, const char* name) {
  g_test = name;
  Res ref = parseWhole(reply);
  CHECK(ref.rc == WXP_OK);
  for (size_t at = 0; at <= reply.size(); at++) {
    Res r = parseSplit(reply, at);
    CHECK(r.rc == WXP_OK);
    CHECK(r.r.temp_c10 == ref.r.temp_c10 && r.r.wmo == ref.r.wmo && r.r.wind_kmh == ref.r.wind_kmh && r.r.is_day == ref.r.is_day);
  }
  static const size_t pieces[] = {1, 2, 7, 536};
  for (size_t pc : pieces) {
    Res r = parsePieces(reply, pc);
    CHECK(r.rc == WXP_OK);
    CHECK(r.r.temp_c10 == ref.r.temp_c10 && r.r.wmo == ref.r.wmo && r.r.wind_kmh == ref.r.wind_kmh && r.r.is_day == ref.r.is_day);
  }
}

// Every prefix: a failure code, or the right values. And OK exactly when the closing brace of "current" is in the prefix.
static void testTruncation(const S& reply, const S& body, const char* name) {
  g_test = name;
  Res ref = parseWhole(reply);
  CHECK(ref.rc == WXP_OK);
  // the closing brace of "current" is the second last '}' of the reply (the chunk framing has no braces)
  size_t last = reply.rfind('}');
  CHECK(last != S::npos && last > 0 && body.size() > 2);
  size_t okFrom = reply.rfind('}', last - 1) + 1;     // shortest prefix that includes it
  for (size_t n = 0; n <= reply.size(); n++) {
    Res r = parseWhole(reply.substr(0, n));
    if (r.rc == WXP_OK) {
      CHECK(r.r.temp_c10 == ref.r.temp_c10 && r.r.wmo == ref.r.wmo && r.r.wind_kmh == ref.r.wind_kmh && r.r.is_day == ref.r.is_day);
      CHECK(n >= okFrom);
    }
    CHECK((r.rc == WXP_OK) == (n >= okFrom));
  }
}

static void testChunked() {
  g_test = "chunked";
  S body = loadFixture("owner_body.json");
  S dbody = loadFixture("design_body.json");
  static const std::vector<size_t> plans[] = {{1}, {2}, {7}, {64}, {5, 1, 300}, {400}, {1000}, {33, 2, 2, 91}};
  for (const auto& plan : plans) {
    for (int variant = 0; variant < 4; variant++) {
      S te = (variant & 1) ? "transfer-encoding: CHUNKED\r\n" : "Transfer-Encoding: chunked\r\n";
      S ch = chunked(body, plan, variant & 1, variant & 2);
      expectOk(http(ch, "200 OK", te), 252, 3, 15, 1);
      expectOk(http(chunked(dbody, plan, variant & 1, variant & 2), "200 OK", te), 73, 61, 12, 1);
    }
  }
  // a chunk boundary inside a number: split right after "25." and after "2"
  size_t at = body.find("25.2");
  CHECK(at != S::npos);
  S two = chunked(body, {at + 3}, false, false);                    // first chunk ends after "25."
  expectOk(http(two, "200 OK", "Transfer-Encoding: chunked\r\n"), 252, 3, 15, 1);
  S three = chunked(body, {at + 1}, false, false);                  // first chunk ends after "2"
  expectOk(http(three, "200 OK", "Transfer-Encoding: chunked\r\n"), 252, 3, 15, 1);
  // "gzip, chunked" and a header line that only contains the word elsewhere
  expectOk(http(chunked(body, {50}), "200 OK", "Transfer-Encoding: gzip, chunked\r\n"), 252, 3, 15, 1);
  expectOk(http(body, "200 OK", "X-Note: chunked\r\nX-Transfer-Encoding: chunked\r\nTransfer-Encoding-Foo: chunked\r\n"), 252, 3, 15, 1);
  // framing errors
  expectFail(http("zz\r\n" + body + "\r\n0\r\n\r\n", "200 OK", "Transfer-Encoding: chunked\r\n"), WXP_NO_BODY);
  expectFail(http("\r\n" + body, "200 OK", "Transfer-Encoding: chunked\r\n"), WXP_NO_BODY);
  expectOk(http("FFFF\r\n" + body, "200 OK", "Transfer-Encoding: chunked\r\n"), 252, 3, 15, 1);   // a big declared size is fine while the data stops early
  expectFail(http("12345\r\n" + body, "200 OK", "Transfer-Encoding: chunked\r\n"), WXP_TOO_BIG);
  {
    char hex[16];
    snprintf(hex, sizeof hex, "%zx", body.size());
    expectFail(http(S(hex) + "\r\n" + body + "XX0\r\n\r\n", "200 OK", "Transfer-Encoding: chunked\r\n"), WXP_NO_BODY);   // junk where CRLF belongs
  }
  // terminal chunk with nothing before it
  expectFail(http("0\r\n\r\n", "200 OK", "Transfer-Encoding: chunked\r\n"), WXP_NO_BODY);
  // all two-piece splits and byte pieces of a chunked reply
  testSplits(http(chunked(body, {40, 3, 200}), "200 OK", "Transfer-Encoding: chunked\r\n"), "chunked-splits");
  testTruncation(http(chunked(body, {40, 3, 200}), "200 OK", "Transfer-Encoding: chunked\r\n"),
                 body, "chunked-truncation");
}

static void testRefused() {
  g_test = "refused";
  S body = loadFixture("owner_body.json");
  static const char* bad[] = {"301 Moved Permanently", "302 Found", "307 Temporary Redirect", "308 Permanent Redirect", "400 Bad Request",
                              "404 Not Found", "429 Too Many Requests", "500 Internal Server Error", "503 Service Unavailable",
                              "201 Created", "204 No Content", "2000 OK", "20 OK", "100 Continue"};
  for (const char* st : bad) expectFail(http(body, st), WXP_HTTP_STATUS);
  expectFail(loadFixture("redirect301.http"), WXP_HTTP_STATUS);
  expectFail("HTTP/2 200\r\n\r\n" + body, WXP_HTTP_STATUS);
  expectFail("HTTP/1.2 200 OK\r\n\r\n" + body, WXP_HTTP_STATUS);
}

static void testStatusForms() {
  g_test = "status-forms";
  S body = loadFixture("owner_body.json");
  expectOk("HTTP/1.1 200\r\n\r\n" + body, 252, 3, 15, 1);
  expectOk("HTTP/1.0 200 OK\r\n\r\n" + body, 252, 3, 15, 1);
  expectOk("HTTP/1.1 200 OK\n\n" + body, 252, 3, 15, 1);             // bare LF
  expectOk("HTTP/1.1 200 OK\nContent-Type: x\n\n" + body, 252, 3, 15, 1);
  expectOk("HTTP/1.1 200\n\n" + body, 252, 3, 15, 1);
  // garbage, empty, partial heads
  expectFail("", WXP_NO_BODY);
  expectFail("HTTP/1.1 200 OK\r\n", WXP_NO_BODY);
  expectFail("HTTP/1.1 200 OK\r\nServer: x\r\n", WXP_NO_BODY);
  expectFail("HTTP/1.1 200 OK\r\nServer: x\r\n\r\n", WXP_NO_BODY);      // empty body
  expectFail("HTTP/1.1 20", WXP_NO_BODY);
  expectFail("HTTP/1.1 200", WXP_NO_BODY);                             // no end of line yet
  expectFail("garbage garbage", WXP_HTTP_STATUS);
  expectFail("<html><body>login here</body></html>", WXP_HTTP_STATUS);
  expectFail(S("\x00\x01\x02\xff\xfe", 5), WXP_HTTP_STATUS);
  expectFail("HTTP/1.1 200 OK\r\n\r\nnot json at all", WXP_MISSING);
  expectFail("HTTP/1.1 200 OK\r\n\r\n<html>nope</html>", WXP_MISSING);
  expectFail("HTTP/1.1 200 OK\r\n\r\n{}", WXP_MISSING);
  expectFail("HTTP/1.1 200 OK\r\n\r\n{\"error\":true,\"reason\":\"Latitude must be in range of -90 to 90\"}", WXP_MISSING);
  // huge head
  S big = "HTTP/1.1 200 OK\r\n";
  while (big.size() < 1100) big += "X-Pad: 0123456789012345678901234567890123456789\r\n";
  expectFail(big + "\r\n" + body, WXP_TOO_BIG);
  expectFail(S(2000, 'A'), WXP_HTTP_STATUS);
  // one unterminated header line without end
  expectFail("HTTP/1.1 200 OK\r\n" + S(1500, 'h'), WXP_TOO_BIG);
}

static void testBodies() {
  g_test = "bodies";
  const S ok = "\"temperature_2m\":7.3,\"weather_code\":61,\"wind_speed_10m\":12.2,\"is_day\":1";
  expectOk(http(cur(ok.c_str())), 73, 61, 12, 1);
  // key order shuffled, whitespace, extra keys
  expectOk(http(cur("\"wind_speed_10m\":12.2,\"is_day\":0,\"temperature_2m\":7.3,\"weather_code\":61")), 73, 61, 12, 0);
  expectOk(http(cur("\"is_day\":0,\"weather_code\":61,\"wind_speed_10m\":12.2,\"temperature_2m\":7.3")), 73, 61, 12, 0);
  expectOk(http(cur(" \"wind_speed_10m\" : 12.2 ,\n\t\"is_day\" : 0 , \"temperature_2m\"\r\n:\r\n 7.3 , \"weather_code\" : 61 ")), 73, 61, 12, 0);
  expectOk(http(cur("\"time\":\"2026-11-11T10:00\",\"interval\":900,\"temperature_2m\":7.3,\"extra\":[1,2,{\"a\":3}],\"weather_code\":61,\"x\":{\"temperature_2m\":99.9},\"wind_speed_10m\":12.2")), 73, 61, 12, 1);
  expectOk(http("  {\n \"current\" :\n {\"temperature_2m\":7.3,\"weather_code\":61,\"wind_speed_10m\":12.2}\n}\n"), 73, 61, 12, 1);
  expectOk(http("{\"current\":{\"temperature_2m\":7.3,\"weather_code\":61,\"wind_speed_10m\":12.2}"), 73, 61, 12, 1);   // outer brace missing: still complete
  // temperatures
  struct T { const char* txt; int t10; } temps[] = {
      {"0", 0}, {"0.0", 0}, {"-0", 0}, {"-0.0", 0}, {"-0.4", -4}, {"0.4", 4}, {"12", 120}, {"12.0", 120}, {"12.96", 130},
      {"-12.96", -130}, {"12.94", 129}, {"12.95", 130}, {"-12.95", -130}, {"-3.5", -35}, {"25.2", 252}, {"25.25", 253},
      {"7.349999", 73}, {"7.35", 74}, {"-0.04", 0}, {"-0.05", -1}, {"0.05", 1}, {"-89.96", -900}, {"-90", -900}, {"-90.0", -900},
      {"60", 600}, {"60.0", 600}, {"59.99", 600}, {"60.04", 600}, {"-90.04", -900}, {"1.000000000000", 10}, {"-12.9999999999", -130}};
  for (const auto& t : temps) {
    S in = S("\"temperature_2m\":") + t.txt + ",\"weather_code\":0,\"wind_speed_10m\":0";
    g_test = t.txt;
    expectOk(http(cur(in.c_str())), t.t10, 0, 0, 1);
  }
  g_test = "bodies";
  static const char* badTemps[] = {"60.05", "60.1", "61", "100", "-90.05", "-90.1", "-91", "-100", "9999", "99999999", "-99999999",
                                   "1e2", "1E2", "12x", "--1", "+5", ".5", "12.", "-", "-.5", "0x10", "null", "true", "\"7.3\"", "[7.3]", "{}"};
  for (const char* t : badTemps) {
    S in = S("\"temperature_2m\":") + t + ",\"weather_code\":0,\"wind_speed_10m\":0";
    g_test = t;
    Res r = parseWhole(http(cur(in.c_str())));
    CHECK(r.rc != WXP_OK);
  }
  g_test = "bodies";
  // weather code and wind boundaries
  expectOk(http(cur("\"temperature_2m\":1,\"weather_code\":0,\"wind_speed_10m\":0")), 10, 0, 0, 1);
  expectOk(http(cur("\"temperature_2m\":1,\"weather_code\":99,\"wind_speed_10m\":400")), 10, 99, 400, 1);
  expectOk(http(cur("\"temperature_2m\":1,\"weather_code\":99.0,\"wind_speed_10m\":400.0")), 10, 99, 400, 1);
  expectOk(http(cur("\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":28.4")), 10, 3, 28, 1);
  expectOk(http(cur("\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":28.5")), 10, 3, 29, 1);
  expectOk(http(cur("\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":0.04")), 10, 3, 0, 1);
  expectFail(http(cur("\"temperature_2m\":1,\"weather_code\":100,\"wind_speed_10m\":1")), WXP_RANGE);
  expectFail(http(cur("\"temperature_2m\":1,\"weather_code\":-1,\"wind_speed_10m\":1")), WXP_RANGE);
  expectFail(http(cur("\"temperature_2m\":1,\"weather_code\":3.5,\"wind_speed_10m\":1")), WXP_RANGE);
  expectFail(http(cur("\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":400.5")), WXP_RANGE);
  expectFail(http(cur("\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":401")), WXP_RANGE);
  expectFail(http(cur("\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":-1")), WXP_RANGE);
  expectFail(http(cur("\"temperature_2m\":61,\"weather_code\":3,\"wind_speed_10m\":1")), WXP_RANGE);
  expectFail(http(cur("\"temperature_2m\":-91,\"weather_code\":3,\"wind_speed_10m\":1")), WXP_RANGE);
  expectFail(http(cur("\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1,\"is_day\":2")), WXP_RANGE);
  expectFail(http(cur("\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1,\"is_day\":-1")), WXP_RANGE);
  expectFail(http(cur("\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1,\"is_day\":true")), WXP_RANGE);
  expectOk(http(cur("\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1,\"is_day\":0")), 10, 3, 1, 0);
  // missing keys
  expectFail(http(cur("\"weather_code\":3,\"wind_speed_10m\":1")), WXP_MISSING);
  expectFail(http(cur("\"temperature_2m\":1,\"wind_speed_10m\":1")), WXP_MISSING);
  expectFail(http(cur("\"temperature_2m\":1,\"weather_code\":3")), WXP_MISSING);
  expectFail(http(cur("")), WXP_MISSING);
  expectFail(http(cur("\"temperature_2m\":\"1\",\"weather_code\":3,\"wind_speed_10m\":1")), WXP_MISSING);   // string value
  // keys only in current_units (all three, with numbers even) or at the top level or in a sibling object
  expectFail(http("{\"current_units\":{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1},\"current\":{\"time\":\"x\"}}"), WXP_MISSING);
  expectFail(http("{\"current_units\":{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1}}"), WXP_MISSING);
  expectFail(http("{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1}"), WXP_MISSING);
  expectFail(http("{\"hourly\":{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1}}"), WXP_MISSING);
  expectFail(http("{\"daily\":{\"current\":{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1}}}"), WXP_MISSING);   // "current" not at the top level
  expectFail(http("{\"current\":[{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1}]}"), WXP_MISSING);
  expectFail(http("{\"current\":\"{\",\"x\":{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1}}"), WXP_MISSING);
  expectFail(http("{\"currentx\":{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1}}"), WXP_MISSING);
  expectFail(http("{\"curren\":{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1}}"), WXP_MISSING);
  expectFail(http("{\"current_units\":{\"a\":1},\"x\":\"current\",\"y\":{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1}}"), WXP_MISSING);
  expectFail(http("{\"cur\\u0072ent\":{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1}}"), WXP_MISSING);   // escapes never spell a key
  // a string value that looks like a key does not count
  expectOk(http("{\"a\":\"current\",\"current\":{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1}}"), 10, 3, 1, 1);
  expectOk(http("{\"a\":\"\\\"current\\\":{\",\"current\":{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1}}"), 10, 3, 1, 1);
  // after the object is complete, trailing junk and a second "current" change nothing
  expectOk(http("{\"current\":{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1},\"current\":{\"temperature_2m\":5,\"weather_code\":4,\"wind_speed_10m\":2}}"), 10, 3, 1, 1);
  // a body over the cap is refused (cap counts decoded body bytes): exactly 2048 passes, 2049 does not
  for (int extra = 2040; extra <= 2060; extra++) {
    S head = "{\"pad\":\"";
    S tail = "\",\"current\":{\"temperature_2m\":1,\"weather_code\":3,\"wind_speed_10m\":1}}";
    size_t padn = (size_t)extra - head.size() - tail.size();
    S b = head + S(padn, 'p') + tail;
    CHECK((int)b.size() == extra);
    Res r = parseWhole(http(b));
    CHECK(r.rc == (extra <= WXP_BODY_MAX ? WXP_OK : WXP_TOO_BIG));
    Res r2 = parseWhole(http(chunked(b, {300})));   // not announced as chunked: treated as body text, still capped
    CHECK(r2.rc != WXP_OK || extra <= WXP_BODY_MAX);
    Res r3 = parseWhole(http(chunked(b, {300}), "200 OK", "Transfer-Encoding: chunked\r\n"));
    CHECK(r3.rc == (extra <= WXP_BODY_MAX ? WXP_OK : WXP_TOO_BIG));
  }
  g_test = "bodies";
  expectFail(http(S(5000, ' ') + "{}"), WXP_TOO_BIG);
  // settled
  {
    WxParser p;
    wxp_init(&p);
    S in = http(loadFixture("owner_body.json"));
    size_t cut = in.find("14.7") + 4;
    wxp_feed(&p, (const uint8_t*)in.data(), (uint16_t)cut);
    CHECK(!wxp_settled(&p));
    wxp_feed(&p, (const uint8_t*)in.data() + cut, 1);          // the '}' that closes "current"
    CHECK(wxp_settled(&p));
    WxReading r;
    CHECK(wxp_finish(&p, &r) == WXP_OK);
    // an error settles it as well
    wxp_init(&p);
    wxp_feed(&p, (const uint8_t*)"HTTP/1.1 404 x\r\n", 16);
    CHECK(wxp_settled(&p));
    CHECK(wxp_finish(&p, 0) == WXP_HTTP_STATUS);
  }
}

static void testMapping() {
  g_test = "mapping";
  for (int wind = 0; wind <= 40; wind += 40) {
    for (int c = 0; c <= 99; c++) {
      WxReading r = {100, (uint8_t)c, (uint16_t)wind, 1};
      int want;
      if (c == 0 || c == 1) want = 0;
      else if (c == 2 || c == 3) want = 1;
      else if (c == 45 || c == 48) want = 4;
      else if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) want = 2;
      else if ((c >= 71 && c <= 77) || c == 85 || c == 86) want = 5;
      else if (c >= 95 && c <= 99) want = 3;
      else want = 1;
      if (c <= 3 && wind >= 29) want = 6;
      int got = wx_state(&r);
      if (got != want) fprintf(stderr, "code %d wind %d: got %d want %d\n", c, wind, got, want);
      CHECK(got == want);
      CHECK(got >= 0 && got <= 6);
    }
  }
  WxReading r = {0, 1, 28, 1};
  CHECK(wx_state(&r) == 0);
  r.wind_kmh = 29;
  CHECK(wx_state(&r) == 6);
  r.wmo = 61;                                        // rain stays rain in wind
  CHECK(wx_state(&r) == 2);
  r.wmo = 95;
  CHECK(wx_state(&r) == 3);
  // codes the table does not know, even above 99
  r.wmo = 200; r.wind_kmh = 0;
  CHECK(wx_state(&r) == 1);
  CHECK(wx_state(&r) >= 0 && wx_state(&r) <= 6);
}

static void testRequest() {
  g_test = "request";
  char buf[256];
  int n = wx_build_request(buf, sizeof buf, -1297, -3845);
  CHECK(n > 0 && n == (int)strlen(buf));
  CHECK(strncmp(buf, "GET /v1/forecast?latitude=-12.97&longitude=-38.45&current=temperature_2m,weather_code,wind_speed_10m,is_day HTTP/1.0\r\n", 117) == 0);
  CHECK(strstr(buf, "Host: api.open-meteo.com\r\n") != 0);
  CHECK(strstr(buf, "Connection: close\r\n") != 0);
  CHECK(n >= 4 && strcmp(buf + n - 4, "\r\n\r\n") == 0);
  struct C { int lat, lon; const char* la; const char* lo; } cs[] = {
      {9000, 18000, "latitude=90.00&", "longitude=180.00&"}, {-9000, -18000, "latitude=-90.00&", "longitude=-180.00&"},
      {0, 0, "latitude=0.00&", "longitude=0.00&"}, {-50, -50, "latitude=-0.50&", "longitude=-0.50&"},
      {50, 5, "latitude=0.50&", "longitude=0.05&"}, {-1, 1, "latitude=-0.01&", "longitude=0.01&"},
      {3872, -914, "latitude=38.72&", "longitude=-9.14&"}, {-100, 100, "latitude=-1.00&", "longitude=1.00&"},
      {7, -17999, "latitude=0.07&", "longitude=-179.99&"}};
  int maxn = 0;
  for (const auto& c : cs) {
    n = wx_build_request(buf, sizeof buf, (int16_t)c.lat, (int16_t)c.lon);
    CHECK(n > 0);
    CHECK(strstr(buf, c.la) != 0);
    CHECK(strstr(buf, c.lo) != 0);
    if (n > maxn) maxn = n;
    // exact fit: needs n + 1 bytes for the NUL
    char small[256];
    memset(small, 'X', sizeof small);
    CHECK(wx_build_request(small, n + 1, (int16_t)c.lat, (int16_t)c.lon) == n);
    CHECK(small[n] == 0 && small[n + 1] == 'X');
    memset(small, 'X', sizeof small);
    CHECK(wx_build_request(small, n, (int16_t)c.lat, (int16_t)c.lon) == 0);
    CHECK(small[n + 1] == 'X' && small[n + 5] == 'X');
    CHECK(wx_build_request(small, 10, (int16_t)c.lat, (int16_t)c.lon) == 0);
    CHECK(wx_build_request(small, 0, (int16_t)c.lat, (int16_t)c.lon) == 0);
    CHECK(wx_build_request(small, 1, (int16_t)c.lat, (int16_t)c.lon) == 0);
  }
  CHECK(maxn < 200);                                  // fits the 200 byte static buffer of the design
  CHECK(wx_build_request(buf, sizeof buf, 9001, 0) == 0);
  CHECK(wx_build_request(buf, sizeof buf, -9001, 0) == 0);
  CHECK(wx_build_request(buf, sizeof buf, 0, 18001) == 0);
  CHECK(wx_build_request(buf, sizeof buf, 0, -18001) == 0);
  CHECK(wx_build_request(buf, sizeof buf, 32767, -32768) == 0);
  CHECK(wx_build_request(0, 100, 0, 0) == 0);
}

static void testTruncationAndSplits() {
  S body = loadFixture("owner_body.json");
  S dbody = loadFixture("design_body.json");
  testSplits(loadFixture("owner_reply.http"), "splits-owner");
  testSplits(loadFixture("design_reply.http"), "splits-design");
  testTruncation(loadFixture("owner_reply.http"), body, "trunc-owner");
  testTruncation(loadFixture("design_reply.http"), dbody, "trunc-design");
}

static bool inRange(const WxReading& r) {
  return r.temp_c10 >= -900 && r.temp_c10 <= 600 && r.wmo <= 99 && r.wind_kmh <= 400 && r.is_day <= 1;
}

static void testFuzz() {
  g_test = "fuzz";
  std::vector<S> seeds;
  S ob = loadFixture("owner_body.json"), db = loadFixture("design_body.json");
  seeds.push_back(loadFixture("owner_reply.http"));
  seeds.push_back(loadFixture("design_reply.http"));
  seeds.push_back(http(chunked(ob, {50, 7, 1, 120}), "200 OK", "Transfer-Encoding: chunked\r\n"));
  seeds.push_back(http(chunked(db, {13}), "200 OK", "Transfer-Encoding: chunked\r\n"));
  seeds.push_back(http(cur("\"temperature_2m\":-3.5,\"weather_code\":61,\"wind_speed_10m\":40.2,\"is_day\":0")));
  seeds.push_back(loadFixture("redirect301.http"));
  static const char alphabet[] = "{}[]\",:.-0123456789 \r\n\\tabcdefghijklmnopqrstuvwxyz_;HTP/";
  long ok = 0, total = 0;
  for (int it = 0; it < 100000; it++) {
    S in;
    int kind = (int)(rnd() % 10);
    if (kind == 0) {                                   // pure random bytes
      size_t n = rnd() % 300;
      for (size_t i = 0; i < n; i++) in += (char)(rnd() & 0xFF);
    } else if (kind == 1) {                            // random text from the JSON/HTTP alphabet after a valid head
      in = "HTTP/1.1 200 OK\r\n\r\n";
      size_t n = rnd() % 400;
      for (size_t i = 0; i < n; i++) in += alphabet[rnd() % (sizeof alphabet - 1)];
    } else {                                           // mutated seed
      in = seeds[rnd() % seeds.size()];
      int muts = 1 + (int)(rnd() % 6);
      for (int m = 0; m < muts && !in.empty(); m++) {
        size_t pos = rnd() % in.size();
        switch (rnd() % 6) {
          case 0: in[pos] = (char)(rnd() & 0xFF); break;
          case 1: in[pos] = alphabet[rnd() % (sizeof alphabet - 1)]; break;
          case 2: in.erase(pos, 1 + rnd() % 5); break;
          case 3: in.insert(pos, 1, alphabet[rnd() % (sizeof alphabet - 1)]); break;
          case 4: in.resize(pos + 1); break;
          default: { size_t q = rnd() % in.size(); in.insert(pos, in.substr(q, rnd() % 40)); } break;
        }
        if (in.empty()) break;
      }
    }
    // random split into pieces
    WxParser p;
    wxp_init(&p);
    size_t i = 0;
    while (i < in.size()) {
      size_t n = 1 + rnd() % 64;
      if (n > in.size() - i) n = in.size() - i;
      wxp_feed(&p, (const uint8_t*)in.data() + i, (uint16_t)n);
      i += n;
    }
    WxReading r; memset(&r, 0xEE, sizeof r);
    int rc = wxp_finish(&p, &r);
    CHECK(rc >= WXP_OK && rc <= WXP_TOO_BIG);
    total++;
    if (rc == WXP_OK) {
      ok++;
      CHECK(inRange(r));
      CHECK(wx_state(&r) >= 0 && wx_state(&r) <= 6);
      CHECK(wxp_settled(&p));
    } else {
      CHECK(r.temp_c10 == (int16_t)0xEEEE && r.wmo == 0xEE);
    }
    // whole-buffer parse gives the same verdict as the split parse
    Res whole = parseWhole(in);
    CHECK(whole.rc == rc);
  }
  CHECK(ok > 1000 && ok < total);                      // the mutations really do produce both outcomes
  printf("  fuzz: %ld ok of %ld\n", ok, total);
}

// ------------------------------------------------------------------ wxfsm tests

static WxIn good(uint32_t now) {
  WxIn in;
  memset(&in, 0, sizeof in);
  in.now_ms = now;
  in.uptime_ms = 100000;
  in.free_heap = 30000;
  in.max_block = 20000;
  in.sta_up_s = 100;
  in.configured = 1;
  return in;
}

static void testFsmGates() {
  g_test = "fsm-gates";
  WxFsm f;
  WxIn in;
  // each blocker alone prevents a start for 100 s; removing it starts at once
  struct G { const char* name; void (*set)(WxIn*); void (*clear)(WxIn*); } gates[] = {
      {"not configured", [](WxIn* i) { i->configured = 0; }, [](WxIn* i) { i->configured = 1; }},
      {"safe mode", [](WxIn* i) { i->safe_mode = 1; }, [](WxIn* i) { i->safe_mode = 0; }},
      {"updating", [](WxIn* i) { i->updating = 1; }, [](WxIn* i) { i->updating = 0; }},
      {"strikes 2", [](WxIn* i) { i->strikes = 2; }, [](WxIn* i) { i->strikes = 1; }},
      {"strikes 3", [](WxIn* i) { i->strikes = 3; }, [](WxIn* i) { i->strikes = 0; }},
      {"first 40 s", [](WxIn* i) { i->uptime_ms = 39999; }, [](WxIn* i) { i->uptime_ms = 40000; }},
      {"link down", [](WxIn* i) { i->sta_up_s = 0; }, [](WxIn* i) { i->sta_up_s = 20; }},
      {"link 19 s", [](WxIn* i) { i->sta_up_s = 19; }, [](WxIn* i) { i->sta_up_s = 20; }},
      {"http recent", [](WxIn* i) { i->http_recent = 1; }, [](WxIn* i) { i->http_recent = 0; }},
      {"low heap", [](WxIn* i) { i->free_heap = 14 * 1024 - 1; }, [](WxIn* i) { i->free_heap = 14 * 1024; }},
      {"small block", [](WxIn* i) { i->max_block = 6 * 1024 - 1; }, [](WxIn* i) { i->max_block = 6 * 1024; }},
      {"screen off", [](WxIn* i) { i->screen_off = 1; }, [](WxIn* i) { i->screen_off = 1; i->wake_soon = 1; }},
  };
  for (const auto& g : gates) {
    g_test = g.name;
    wxfsm_init(&f, 5000, 7);
    in = good(5000);
    g.set(&in);
    for (int s = 0; s < 100; s++) { in.now_ms = 5000 + (uint32_t)s * 1000; CHECK(wxfsm_step(&f, &in) == WXA_NONE); }
    g.clear(&in);
    in.now_ms += 1000;
    CHECK(wxfsm_step(&f, &in) == WXA_START);
  }
  // screen off with wake_soon, then a success: nothing more until the screen is on and 30 minutes passed
  g_test = "wake_soon";
  wxfsm_init(&f, 0, 9);
  in = good(0);
  in.screen_off = 1; in.wake_soon = 1;
  CHECK(wxfsm_step(&f, &in) == WXA_START);
  wxfsm_result(&f, 2000, 1);
  for (uint32_t t = 3000; t < 29 * 60000UL; t += 1000) { in.now_ms = t; CHECK(wxfsm_step(&f, &in) == WXA_NONE); }
  // a start in the middle of a fetch is never given
  g_test = "running";
  wxfsm_init(&f, 0, 9);
  in = good(0);
  CHECK(wxfsm_step(&f, &in) == WXA_START);
  for (uint32_t t = 1000; t < 8000; t += 1000) { in.now_ms = t; CHECK(wxfsm_step(&f, &in) == WXA_NONE); }
}

static void testFsmAbort() {
  g_test = "fsm-abort";
  WxFsm f;
  for (uint32_t start : {0u, 123456u, 0xFFFFFFFFu - 3000u}) {
    wxfsm_init(&f, start, 3);
    WxIn in = good(start);
    CHECK(wxfsm_step(&f, &in) == WXA_START);
    // 100 ms steps: no abort before 8000 ms, abort at exactly 8000 ms
    for (uint32_t d = 100; d < 8000; d += 100) { in.now_ms = start + d; CHECK(wxfsm_step(&f, &in) == WXA_NONE); }
    in.now_ms = start + 7999; CHECK(wxfsm_step(&f, &in) == WXA_NONE);
    in.now_ms = start + 8000; CHECK(wxfsm_step(&f, &in) == WXA_ABORT);
    in.now_ms = start + 9000; CHECK(wxfsm_step(&f, &in) == WXA_ABORT);       // keeps asking until the result arrives
    wxfsm_result(&f, start + 9000, 0);
    in.now_ms = start + 9100; CHECK(wxfsm_step(&f, &in) == WXA_NONE);
    in.now_ms = start + 9000 + 59999; CHECK(wxfsm_step(&f, &in) == WXA_NONE);
    in.now_ms = start + 9000 + 60000; CHECK(wxfsm_step(&f, &in) == WXA_START);
  }
  // abort at once when an upload starts, safe mode begins, or the feature is switched off
  struct Z { void (*set)(WxIn*); } zs[] = {{[](WxIn* i) { i->updating = 1; }}, {[](WxIn* i) { i->safe_mode = 1; }}, {[](WxIn* i) { i->configured = 0; }}};
  for (const auto& z : zs) {
    wxfsm_init(&f, 0, 3);
    WxIn in = good(0);
    CHECK(wxfsm_step(&f, &in) == WXA_START);
    in.now_ms = 1000; CHECK(wxfsm_step(&f, &in) == WXA_NONE);
    z.set(&in);
    in.now_ms = 2000; CHECK(wxfsm_step(&f, &in) == WXA_ABORT);
  }
}

// Runs the machine in 1 s steps; a fetch takes `dur` ms; returns the start times.
static std::vector<uint32_t> runFsm(WxFsm* f, uint32_t startNow, uint32_t seconds, bool succeed, uint32_t dur, uint32_t* wrapCheck = 0) {
  std::vector<uint32_t> starts;
  uint32_t resultAt = 0;
  bool running = false;
  (void)wrapCheck;
  for (uint32_t s = 0; s < seconds; s++) {
    uint32_t now = startNow + s * 1000u;
    WxIn in = good(now);
    in.uptime_ms = s * 1000u;
    if (running && now - resultAt < 0x80000000u && now >= resultAt - 0u && (int32_t)(now - resultAt) >= 0) {
      wxfsm_result(f, resultAt, succeed ? 1 : 0);
      running = false;
    }
    uint8_t a = wxfsm_step(f, &in);
    if (a == WXA_START) {
      CHECK(!running);
      starts.push_back(now);
      running = true;
      resultAt = now + dur;
      if (dur == 0) { wxfsm_result(f, now, succeed ? 1 : 0); running = false; }
    } else if (a == WXA_ABORT) {
      wxfsm_result(f, now, 0);
      running = false;
    }
  }
  return starts;
}

static void testFsmSchedule() {
  g_test = "fsm-schedule";
  // 7 days, everything succeeds: about 48 fetches a day; also across the millis() wrap
  for (uint32_t startNow : {0u, 0xFFFFFFFFu - 3 * 86400u * 1000u - 500u, 0xFFFFFFF0u}) {
    WxFsm f;
    wxfsm_init(&f, startNow, 42);
    std::vector<uint32_t> st = runFsm(&f, startNow, 7 * 86400u, true, 2000);
    // the first start is when the 40 s uptime gate opens
    CHECK(st.size() > 7 * 46 && st.size() < 7 * 50 + 2);
    CHECK(st[0] - startNow == 40000u);
    for (size_t i = 1; i < st.size(); i++) {
      uint32_t gap = st[i] - st[i - 1];                // wrap-safe
      CHECK(gap >= 30u * 60000u + 2000u && gap <= 30u * 60000u + 60000u + 3000u);
    }
    // per day counts
    for (int d = 0; d < 7; d++) {
      int cnt = 0;
      for (uint32_t t : st) { uint32_t off = t - startNow; if (off >= (uint32_t)d * 86400000u && off < (uint32_t)(d + 1) * 86400000u) cnt++; }
      CHECK(cnt >= 46 && cnt <= 50);
    }
  }
  // all fail: the ladder 1, 2, 5, 15 min, then every 30 min (fetch of 0 ms: gaps are exactly the delays)
  {
    WxFsm f;
    wxfsm_init(&f, 777, 5);
    std::vector<uint32_t> st = runFsm(&f, 777, 12 * 3600u, false, 0);
    CHECK(st.size() >= 12);
    const uint32_t want[] = {60, 120, 300, 900, 1800, 1800, 1800, 1800};
    for (int i = 0; i < 8; i++) CHECK(st[i + 1] - st[i] == want[i] * 1000u);
    CHECK(wxfsm_fails(&f) >= 8);
  }
  // all fail with timeouts of 8 s each: gap = 8 s + delay
  {
    WxFsm f;
    wxfsm_init(&f, 0, 5);
    std::vector<uint32_t> st = runFsm(&f, 0, 6 * 3600u, false, 20000);          // never answers: the machine aborts at 8 s
    CHECK(st.size() >= 7);
    const uint32_t want[] = {60, 120, 300, 900, 1800};
    for (int i = 0; i < 5; i++) CHECK(st[i + 1] - st[i] == 8000u + want[i] * 1000u);
  }
  // a success after failures resets the ladder
  {
    WxFsm f;
    wxfsm_init(&f, 0, 5);
    WxIn in = good(0);
    CHECK(wxfsm_step(&f, &in) == WXA_START);
    wxfsm_result(&f, 0, 0);
    CHECK(wxfsm_fails(&f) == 1);
    in.now_ms = 60000; CHECK(wxfsm_step(&f, &in) == WXA_START);
    wxfsm_result(&f, 60000, 0);
    in.now_ms = 60000 + 120000; CHECK(wxfsm_step(&f, &in) == WXA_START);
    wxfsm_result(&f, in.now_ms, 1);
    CHECK(wxfsm_fails(&f) == 0);
    uint32_t t = in.now_ms;
    in.now_ms = t + 30u * 60000u - 1; CHECK(wxfsm_step(&f, &in) == WXA_NONE);
    in.now_ms = t + 30u * 60000u + 60000u; CHECK(wxfsm_step(&f, &in) == WXA_START);
    wxfsm_result(&f, in.now_ms, 0);
    CHECK(wxfsm_fails(&f) == 1);
    in.now_ms += 60000; CHECK(wxfsm_step(&f, &in) == WXA_START);              // ladder starts from the top again
  }
  // a blocker that lasts: the fetch is simply due as soon as it ends (no failure counted)
  {
    WxFsm f;
    wxfsm_init(&f, 0, 5);
    WxIn in = good(0);
    in.free_heap = 5000;
    for (uint32_t t = 0; t < 3600000u; t += 10000) { in.now_ms = t; CHECK(wxfsm_step(&f, &in) == WXA_NONE); }
    CHECK(wxfsm_fails(&f) == 0);
    in.free_heap = 30000; in.now_ms += 1000;
    CHECK(wxfsm_step(&f, &in) == WXA_START);
  }
}

static void testFsmAge() {
  g_test = "fsm-age";
  for (uint32_t base : {1000u, 0xFFFFFFFFu - 600000u}) {
    WxFsm f;
    wxfsm_init(&f, base, 11);
    CHECK(wxfsm_age_class(&f, base) == 0);
    CHECK(wxfsm_age_ms(&f, base) == 0xFFFFFFFFu);
    WxIn in = good(base);
    CHECK(wxfsm_step(&f, &in) == WXA_START);
    wxfsm_result(&f, base, 1);
    CHECK(wxfsm_age_class(&f, base) == 1);
    CHECK(wxfsm_age_class(&f, base + 65u * 60000u) == 1);
    CHECK(wxfsm_age_class(&f, base + 65u * 60000u + 1) == 2);
    CHECK(wxfsm_age_class(&f, base + 125u * 60000u) == 2);
    CHECK(wxfsm_age_class(&f, base + 125u * 60000u + 1) == 3);
    CHECK(wxfsm_age_ms(&f, base + 1234) == 1234);
    // expiry is latched by stepping, so the age cannot come back to "fresh" after 49.7 days without a good reading
    in.configured = 0;                                 // no fetches meanwhile
    for (uint32_t m = 0; m <= 130; m++) { in.now_ms = base + m * 60000u; wxfsm_step(&f, &in); }
    CHECK(wxfsm_age_class(&f, base + 130u * 60000u) == 3);
    CHECK(wxfsm_age_class(&f, base + 0xFFFFFFF0u) == 3);
    CHECK(wxfsm_age_class(&f, base + 5) == 3);
    // a new success clears it
    in.configured = 1;
    in.now_ms = base + 131u * 60000u;
    CHECK(wxfsm_step(&f, &in) == WXA_START);
    wxfsm_result(&f, in.now_ms, 1);
    CHECK(wxfsm_age_class(&f, in.now_ms) == 1);
  }
  // failures do not refresh the age
  WxFsm f;
  wxfsm_init(&f, 0, 11);
  WxIn in = good(0);
  CHECK(wxfsm_step(&f, &in) == WXA_START);
  wxfsm_result(&f, 0, 1);
  in.now_ms = 40u * 60000u; in.uptime_ms = 40u * 60000u;
  wxfsm_result(&f, in.now_ms, 0);
  CHECK(wxfsm_age_class(&f, 66u * 60000u) == 2);
}

static void testFsmKick() {
  g_test = "fsm-kick";
  for (uint32_t base : {0u, 0xFFFFFF00u}) {
    WxFsm f;
    wxfsm_init(&f, base, 21);
    WxIn in = good(base);
    CHECK(wxfsm_step(&f, &in) == WXA_START);
    wxfsm_result(&f, base, 1);
    in.now_ms = base + 10 * 60000u;
    CHECK(wxfsm_step(&f, &in) == WXA_NONE);
    wxfsm_kick(&f);
    uint32_t t0 = in.now_ms;
    int startedAt = -1;
    for (int s = 0; s <= 3; s++) {
      in.now_ms = t0 + (uint32_t)s * 1000u;
      if (wxfsm_step(&f, &in) == WXA_START) { startedAt = s; break; }
    }
    CHECK(startedAt >= 0 && startedAt <= 3);
    CHECK(startedAt == 2);
    wxfsm_result(&f, in.now_ms, 1);
    // kick after a long failure streak resets the ladder
    wxfsm_result(&f, in.now_ms, 0);
    wxfsm_result(&f, in.now_ms, 0);
    CHECK(wxfsm_fails(&f) == 2);
    wxfsm_kick(&f);
    in.now_ms += 1000; wxfsm_step(&f, &in);
    CHECK(wxfsm_fails(&f) == 0);
    // kick while a fetch is running: abort it, then fetch again 2 s after the result
    WxFsm g;
    wxfsm_init(&g, base, 21);
    in = good(base);
    CHECK(wxfsm_step(&g, &in) == WXA_START);
    in.now_ms = base + 1000; CHECK(wxfsm_step(&g, &in) == WXA_NONE);
    wxfsm_kick(&g);
    in.now_ms = base + 2000; CHECK(wxfsm_step(&g, &in) == WXA_ABORT);
    wxfsm_result(&g, base + 2000, 0);
    in.now_ms = base + 3000; CHECK(wxfsm_step(&g, &in) == WXA_NONE);
    in.now_ms = base + 4000; CHECK(wxfsm_step(&g, &in) == WXA_START);
    CHECK(wxfsm_fails(&g) == 0);
    // a kick while blocked by something: starts when the blocker ends
    wxfsm_result(&g, base + 4000, 1);
    wxfsm_kick(&g);
    in.free_heap = 100;
    for (int s = 0; s < 20; s++) { in.now_ms = base + 5000 + (uint32_t)s * 1000u; CHECK(wxfsm_step(&g, &in) == WXA_NONE); }
    in.free_heap = 30000; in.now_ms += 1000;
    CHECK(wxfsm_step(&g, &in) == WXA_START);
  }
}

int main() {
  testFixtures();
  testTruncationAndSplits();
  testChunked();
  testRefused();
  testStatusForms();
  testBodies();
  testMapping();
  testRequest();
  testFuzz();
  testFsmGates();
  testFsmAbort();
  testFsmSchedule();
  testFsmAge();
  testFsmKick();
  printf("test_wx: all passed\n");
  return 0;
}
