#pragma once
#include "Arduino.h"
enum HTTPMethod { HTTP_GET, HTTP_POST };
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
class ESP8266WebServer {
 public:
  typedef void (*Handler)();
  std::map<std::string, Handler> routes;   // key: METHOD path
  std::map<std::string, String> args;
  int code = 0;
  std::string ctype, body, location, cache;
  void on(const char* uri, HTTPMethod m, Handler h) { routes[std::string(m == HTTP_GET ? "GET " : "POST ") + uri] = h; }
  bool hasArg(const char* n) const { return args.count(n) != 0; }
  const String& arg(const char* n) const { static String empty; auto it = args.find(n); return it == args.end() ? empty : it->second; }
  void setContentLength(size_t) {}
  void send(int c, const __FlashStringHelper* t, const char* b) { code = c; ctype = (const char*)t; body += b; }
  void send(int c) { code = c; }
  void send_P(int c, PGM_P t, PGM_P b) { code = c; ctype = t; body += b; }
  void sendHeader(const __FlashStringHelper* k, const __FlashStringHelper* v) {
    if (std::string((const char*)k) == "Location") location = (const char*)v; else cache = (const char*)v;
  }
  void sendContent(const char* b) { body += b; }
  void sendContent_P(PGM_P b, size_t n) { body.append(b, n); }
  void sendContent_P(PGM_P b) { body += b; }
};
