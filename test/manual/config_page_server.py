#!/usr/bin/env python3
"""Stand-in for the clock's settings routes, to try the real page in a desktop browser (docs/DESIGN-R3.md 8.3, P1).

    python3 test/config_page_server.py [port]        # default 8765, then open http://127.0.0.1:8765/config

The page itself is NOT a copy: it is cut out of firmware/config_page.h, the page head out of firmware/safety.cpp and the
validation messages out of firmware/config.cpp, so the browser sees what the clock sends. Only the validation rules are
ported (same rules as lib/gqsafe/src/gqconf.cpp; `test/config_page_check.py --xcheck` compares both on 8000 inputs).
Routes: GET /config, GET /config.json, POST /config, POST /config/night, POST /config/retry (as config.cpp), plus test hooks
POST /_test/state (JSON: reading {wx,t10} or null, status, strikes) and GET /_test/log (what the buttons did).
"""
import json
import os
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


PAGE = re.search(r'R"HTML\((.*)\)HTML"', read("firmware/config_page.h"), re.S).group(1)
HEAD = re.search(r'PAGE_HEAD\[\] PROGMEM = R"HTML\((.*?)\)HTML"', read("firmware/safety.cpp"), re.S).group(1)
TAIL = "</body></html>"
_cfg = read("firmware/config.cpp")
MSG = {m.group(1): m.group(2) for m in re.finditer(r'static const char (M_\w+)\[\] PROGMEM = "((?:[^"\\]|\\.)*)";', _cfg)}

ALLOWED = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ,.'-")


def parse_coord(t, max_deg):
    """Hundredths of a degree, or None. Mirrors gqc_parse_coord (integer maths only)."""
    m = re.fullmatch(r" *([+-]?)([0-9]*)(?:[.,]([0-9]*))? *", t)
    if not m:
        return None
    sign, whole, frac = m.group(1), m.group(2), m.group(3) or ""
    if not whole and not frac:
        return None
    if len(whole) > 12 or len(frac) > 6 or (whole and int(whole) > 999):
        return None
    hund = int(whole or "0") * 100 + int((frac + "00")[:2]) + (1 if len(frac) >= 3 and int(frac[2]) >= 5 else 0)
    if hund > max_deg * 100:
        return None
    return -hund if sign == "-" else hund


def parse_time(t):
    m = re.fullmatch(r"([0-9]{2}):([0-9]{2})", t)
    if not m or int(m.group(1)) > 23 or int(m.group(2)) > 59:
        return None
    return int(m.group(1)) * 60 + int(m.group(2))


def fmt_coord(c):
    return ("-" if c < 0 else "") + "%d.%02d" % (abs(c) // 100, abs(c) % 100)


def fmt_time(m):
    return "%02d:%02d" % (m // 60, m % 60)


STATE = dict(weather=0, fahrenheit=0, sleep=0, lat=0, lon=0, place="", sleep_min=1380, wake_min=420, seq=0, saved=False,
             reading=None, status="", strikes=0, log=[])


def validate(form):
    """Returns (error message name or None, new values). `form` maps names to single strings."""
    s = dict(STATE)
    whole = "f" in form
    lat, lon = form.get("lat"), form.get("lon")
    if lat is not None and lon is not None:
        if not lat.strip(" ") and not lon.strip(" "):
            s["lat"] = s["lon"] = 0
        elif not lat.strip(" ") or not lon.strip(" "):
            return "M_PAIR", s
        else:
            v = parse_coord(lat, 90)
            if v is None:
                return "M_LAT", s
            s["lat"] = v
            v = parse_coord(lon, 180)
            if v is None:
                return "M_LON", s
            s["lon"] = v
    elif lat is not None:
        v = parse_coord(lat, 90)
        if v is None:
            return "M_LAT", s
        s["lat"] = v
    elif lon is not None:
        v = parse_coord(lon, 180)
        if v is None:
            return "M_LON", s
        s["lon"] = v
    if "place" in form:
        p = form["place"]
        if len(p) > 23 or any(c not in ALLOWED for c in p):
            return "M_PLACE", s
        s["place"] = p
    if "st" in form:
        v = parse_time(form["st"])
        if v is None:
            return "M_SLEEP", s
        s["sleep_min"] = v
    if "wt" in form:
        v = parse_time(form["wt"])
        if v is None:
            return "M_WAKE", s
        s["wake_min"] = v
    if s["sleep_min"] == s["wake_min"]:
        return "M_SAME", s
    if whole:
        s["weather"] = 1 if "wx" in form else 0
        s["sleep"] = 1 if "sl" in form else 0
    if "unit" in form:
        s["fahrenheit"] = 1 if form["unit"] == "F" else 0
    if s["weather"] and s["lat"] == 0 and s["lon"] == 0:
        return "M_NOCOORD", s
    return None, s


def config_json():
    s = STATE
    return json.dumps({
        "weather": s["weather"], "lat": fmt_coord(s["lat"]), "lon": fmt_coord(s["lon"]), "place": s["place"],
        "unit": "F" if s["fahrenheit"] else "C", "sleep": s["sleep"], "sleep_time": fmt_time(s["sleep_min"]),
        "wake_time": fmt_time(s["wake_min"]), "seq": s["seq"], "defaults": not s["saved"], "storage": "rtc",
        "reading": s["reading"], "strikes": s["strikes"],
        "retry": bool(s["weather"] and (s["strikes"] > 0 or not s["reading"])), "status": s["status"]},
        separators=(",", ":"))


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="text/plain", extra=None):
        b = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/config":
            self.send(200, HEAD + PAGE + TAIL, "text/html; charset=utf-8")
        elif path == "/config.json":
            self.send(200, config_json(), "application/json", {"Cache-Control": "no-store"})
        elif path == "/_test/log":
            self.send(200, json.dumps(STATE["log"]), "application/json")
        elif path == "/":
            self.send(200, "<a href='/config'>Clock settings</a>", "text/html")
        else:
            self.send(404, "Not found")

    def do_POST(self):
        path = urlparse(self.path).path
        n = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(n).decode("utf-8", "replace")
        if path == "/_test/state":
            d = json.loads(raw)
            for k in ("reading", "status", "strikes"):
                if k in d:
                    STATE[k] = d[k]
            return self.send(200, "ok")
        form = {k: v[0] for k, v in parse_qs(raw, keep_blank_values=True).items()}
        js = "j" in form
        done = lambda text: self.send(200, text) if js else self.send(303, "", extra={"Location": "/config"})
        if path == "/config":
            err, new = validate(form)
            if err:
                return self.send(400, MSG[err])
            if any(new[k] != STATE[k] for k in ("weather", "fahrenheit", "sleep", "lat", "lon", "place", "sleep_min", "wake_min")):
                new["seq"] = STATE["seq"] + 1
                new["saved"] = True
                STATE.update(new)
                STATE["log"].append("save")
            done(MSG["M_OK"])
        elif path == "/config/night" and form.get("cmd") in ("wake", "sleep"):
            STATE["log"].append(form["cmd"])
            done(MSG["M_WAKE_SENT" if form["cmd"] == "wake" else "M_SLEEP_SENT"])
        elif path == "/config/night":
            self.send(400, MSG["M_CMD"])
        elif path == "/config/retry":
            STATE["strikes"] = 0
            STATE["log"].append("retry")
            done(MSG["M_RETRY"])
        else:
            self.send(404, "Not found")


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    srv = ThreadingHTTPServer(("127.0.0.1", port), H)
    print("settings page stand-in on http://127.0.0.1:%d/config" % port, flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
