#!/usr/bin/env python3
"""Browser test of the settings page against the stand-in server (Playwright, chromium), with screenshots.

    python3 test/config_page_check.py [screenshot_dir]      # default: a temp directory, printed at the end
    python3 test/config_page_check.py --xcheck               # compare the stand-in's validation with gqconf.cpp (needs g++)

The city search is mocked (page.route) so no internet is needed. Look at the screenshots after a run.
"""
import json
import os
import random
import subprocess
import sys
import tempfile
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, HERE)
import config_page_server as stand  # noqa: E402

CODES = ["", "M_LAT", "M_LON", "M_PAIR", "M_PLACE", "M_SLEEP", "M_WAKE", "M_SAME", "M_NOCOORD"]   # index = GQC_E_* value


def xcheck():
    exe = os.path.join(tempfile.mkdtemp(), "xcheck")
    subprocess.check_call(["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", os.path.join(HERE, "config_xcheck.cpp"),
                           os.path.join(ROOT, "lib/gqsafe/src/gqconf.cpp"), os.path.join(ROOT, "lib/gqsafe/src/gq_crc32.cpp"), "-o", exe])
    rnd = random.Random(7)
    alpha = "0123456789.,-+ :abXY<>&'\""
    fixed = [("38.72", "-9.14", "Lisbon", "23:00", "07:00", "1"), ("90.01", "0", "", "23:00", "07:00", "0"), ("", "", "", "23:00", "07:00", "1"),
             ("1", "", "x", "7:5", "07:00", "0"), ("-0.004", "0.004", "ok", "12:00", "12:00", "0"), ("90.005", "1", "", "00:00", "23:59", "0"),
             ("40" * 20, "1", "", "10:10", "10:11", "0"), ("1,000", "180.004", "A" * 23, "24:00", "00:00", "0"), ("~", "~", "~", "~", "~", "0")]
    cases = list(fixed)
    for _ in range(4000):
        def rs(maxlen):
            return "".join(rnd.choice(alpha) for _ in range(rnd.randint(0, maxlen)))
        t = lambda: "%02d:%02d" % (rnd.randint(0, 25), rnd.randint(0, 61)) if rnd.random() < 0.8 else rs(6)
        cases.append((rs(9), rs(9), rs(26) if rnd.random() < .5 else "Lisbon", t(), t(), str(rnd.randint(0, 1))))
    # numeric-looking values hit the interesting paths far more often than random characters do
    for _ in range(4000):
        n = lambda hi: "%s%d%s" % (rnd.choice(["", "-", "+"]), rnd.randint(0, hi), rnd.choice(["", ".%d" % rnd.randint(0, 99999), ",%d" % rnd.randint(0, 9)]))
        cases.append((n(95), n(185), "Ok", "%02d:%02d" % (rnd.randint(0, 23), rnd.randint(0, 59)), "%02d:%02d" % (rnd.randint(0, 23), rnd.randint(0, 59)), str(rnd.randint(0, 1))))
    inp = "\n".join("\t".join(c) for c in cases) + "\n"
    out = subprocess.run([exe], input=inp, capture_output=True, text=True, check=True).stdout.split("\n")
    bad = 0
    for c, got in zip(cases, out):
        form = {"f": "1"}
        for name, v in zip(("lat", "lon", "place", "st", "wt"), c[:5]):
            if v != "~":
                form[name] = v
        if c[5] == "1":
            form["wx"] = "1"
        stand.STATE.update(weather=0, fahrenheit=0, sleep=0, lat=0, lon=0, place="", sleep_min=1380, wake_min=420)
        err, new = stand.validate(form)
        want = ("%d" % CODES.index(err)) if err else "ok %d %d %d %d" % (new["lat"], new["lon"], new["sleep_min"], new["wake_min"])
        if want != got:
            bad += 1
            if bad < 10:
                print("MISMATCH", c, "python:", want, "c++:", got)
    print("xcheck: %d cases, %d mismatches" % (len(cases), bad))
    return 1 if bad else 0


GEO = {"results": [
    {"name": "Lisbon", "admin1": "Lisbon", "country": "Portugal", "latitude": 38.71667, "longitude": -9.13333},
    {"name": "Lisbon", "admin1": "Maine", "country": "United States", "latitude": 44.03118, "longitude": -70.10508},
    {"name": "São Paulo", "admin1": "São Paulo", "country": "Brasil", "latitude": -23.5475, "longitude": -46.63611},
]}


def main():
    if "--xcheck" in sys.argv:
        return xcheck()
    from playwright.sync_api import sync_playwright
    shots = next((a for a in sys.argv[1:] if not a.startswith("-")), None) or tempfile.mkdtemp(prefix="cfgshots_")
    os.makedirs(shots, exist_ok=True)
    port = 8700 + os.getpid() % 200
    srv = subprocess.Popen([sys.executable, os.path.join(HERE, "config_page_server.py"), str(port)], stdout=subprocess.PIPE)
    srv.stdout.readline()
    base = "http://127.0.0.1:%d" % port
    fails = []

    def check(name, cond):
        print(("ok   " if cond else "FAIL ") + name)
        if not cond:
            fails.append(name)

    def state():
        return json.loads(urllib.request.urlopen(base + "/config.json").read())

    def log():
        return json.loads(urllib.request.urlopen(base + "/_test/log").read())

    def post(path, d):
        urllib.request.urlopen(urllib.request.Request(base + path, json.dumps(d).encode(), {"Content-Type": "application/json"}))

    try:
        with sync_playwright() as pw:
            try:
                br = pw.chromium.launch()
            except Exception:   # the Python package wants a newer chromium than the one preinstalled: use any installed one
                import glob
                exe = os.environ.get("PW_CHROMIUM") or (glob.glob("/opt/pw-browsers/chromium-*/chrome-linux64/chrome") or [None])[0]
                br = pw.chromium.launch(executable_path=exe, args=["--no-sandbox"])
            pg = br.new_page(viewport={"width": 420, "height": 1500})
            geo_calls = []

            def geo(route):
                geo_calls.append(route.request.url)
                mode = geo.mode
                if mode == "fail":
                    route.abort()
                elif mode == "empty":
                    route.fulfill(status=200, content_type="application/json", body='{"generationtime_ms":0.5}')
                else:
                    route.fulfill(status=200, content_type="application/json", body=json.dumps(GEO))
            geo.mode = "ok"
            pg.route("https://geocoding-api.open-meteo.com/**", geo)
            errors = []
            pg.on("pageerror", lambda e: errors.append(str(e)))

            pg.goto(base + "/config")
            pg.wait_for_function("document.getElementById('stor').textContent.length > 0")
            check("defaults: sleep time 23:00 and wake 07:00", pg.input_value("#st") == "23:00" and pg.input_value("#wt") == "07:00")
            check("defaults: Celsius, weather off, nothing typed", pg.is_checked("#uc") and not pg.is_checked("#wx") and pg.input_value("#lat") == "0.00")
            check("storage line", "survives restarts and updates, not unplugging" in pg.inner_text("#stor"))
            check("Try weather again hidden while weather is off", not pg.is_visible("#retry"))
            check("attribution", "Open-Meteo.com" in pg.inner_text("body") and "CC BY 4.0" in pg.inner_text("body"))
            pg.screenshot(path=os.path.join(shots, "1-defaults.png"), full_page=True)

            # weather on without coordinates is refused, nothing changes
            pg.fill("#lat", ""); pg.fill("#lon", "")
            pg.check("#wx"); pg.click("button:has-text('Save settings')")
            pg.wait_for_function("document.getElementById('msg').textContent.includes('needs a latitude')")
            check("no coordinates: refused", state()["weather"] == 0 and state()["seq"] == 0)
            pg.screenshot(path=os.path.join(shots, "2-refused.png"), full_page=True)

            # search, pick, save
            pg.fill("#q", "Lisbon"); pg.click("#go")
            pg.wait_for_selector("#res button")
            check("search called the geocoding API over HTTPS", geo_calls and geo_calls[0].startswith("https://geocoding-api.open-meteo.com/v1/search?name=Lisbon&count=5&language=en&format=json"))
            btns = pg.locator("#res button")
            check("three result buttons", btns.count() == 3 and "Lisbon, Lisbon, Portugal" in btns.nth(0).inner_text())
            pg.screenshot(path=os.path.join(shots, "3-search.png"), full_page=True)
            btns.nth(0).click()
            check("button fills latitude, longitude, label and ticks live weather",
                  pg.input_value("#lat") == "38.72" and pg.input_value("#lon") == "-9.13" and pg.input_value("#place") == "Lisbon, Portugal" and pg.is_checked("#wx"))
            pg.click("button:has-text('Save settings')")
            pg.wait_for_function("document.getElementById('msg').textContent == 'Saved.'")
            s = state()
            check("saved on the clock", s["weather"] == 1 and s["lat"] == "38.72" and s["lon"] == "-9.13" and s["place"] == "Lisbon, Portugal" and s["seq"] == 1 and not s["defaults"])
            pg.wait_for_timeout(1000)
            pg.screenshot(path=os.path.join(shots, "4-saved.png"), full_page=True)

            # accents are stripped, southern/western values keep their sign
            pg.fill("#q", "Sao Paulo"); pg.click("#go"); pg.wait_for_selector("#res button")
            pg.locator("#res button").nth(2).click()
            check("accents stripped from the label, negative latitude", pg.input_value("#place") == "Sao Paulo, Brasil" and pg.input_value("#lat") == "-23.55")
            pg.reload(); pg.wait_for_function("document.getElementById('lat').value == '38.72'")
            check("reload shows what the clock stored", pg.input_value("#place") == "Lisbon, Portugal")

            # search problems
            geo.mode = "fail"; pg.fill("#q", "Anywhere"); pg.click("#go")
            pg.wait_for_function("document.getElementById('res').textContent.includes('No internet')")
            check("no internet message", "Type latitude and longitude" in pg.inner_text("#res") and "home WiFi" in pg.inner_text("#res"))
            pg.screenshot(path=os.path.join(shots, "5-no-internet.png"), full_page=True)
            geo.mode = "empty"; pg.click("#go")
            pg.wait_for_function("document.getElementById('res').textContent.includes('No place found')")
            check("no result message", True)
            geo.mode = "ok"
            pg.fill("#q", ""); pg.click("#go")
            check("empty search does nothing", pg.inner_text("#res").strip() == "No place found.")


            # pressing Enter in a field saves (it must not trigger one of the action buttons)
            n0 = len(log())
            pg.evaluate("document.getElementById('msg').textContent=''")
            pg.focus("#lat"); pg.keyboard.press("Enter")
            pg.wait_for_function("document.getElementById('msg').textContent == 'Saved.'")
            check("Enter in a field submits the form, no action button", log()[n0:] == [] or log()[n0:] == ["save"])
            pg.wait_for_timeout(900)

            # manual entry: every validation message, and a refused form changes nothing on the clock
            def refused(fields, text, shot=None):
                before = state()
                for k, v in fields.items():
                    pg.fill(k, v)
                pg.evaluate("document.getElementById('msg').textContent=''")
                pg.click("button:has-text('Save settings')")
                pg.wait_for_function("t => document.getElementById('msg').textContent.includes(t)", arg=text)
                check("refused: " + text, state() == before and "warn" in pg.get_attribute("#msg", "class"))
                if shot:
                    pg.screenshot(path=os.path.join(shots, shot), full_page=True)
                pg.reload()
                pg.wait_for_function("document.getElementById('lat').value.length > 0")

            refused({"#lat": "99"}, "Latitude must be", "6-bad-latitude.png")
            refused({"#lon": "abc"}, "Longitude must be")
            refused({"#lon": ""}, "Give both latitude and longitude")
            pg.evaluate("document.getElementById('place').removeAttribute('maxlength')")
            refused({"#place": "<script>alert(1)</script>"}, "Place name:")
            pg.evaluate("document.getElementById('place').removeAttribute('maxlength')")
            refused({"#place": "A" * 24}, "Place name:")
            refused({"#st": ""}, "Sleep time must look like")
            refused({"#wt": ""}, "Wake time must look like")
            refused({"#st": "08:00", "#wt": "08:00"}, "must be different")

            pg.fill("#lat", "40,4168"); pg.fill("#lon", "-3.7038"); pg.fill("#place", "Madrid"); pg.click("#uf")
            pg.click("button:has-text('Save settings')")
            pg.wait_for_function("document.getElementById('msg').textContent == 'Saved.'")
            s = state()
            check("comma decimal, rounding to hundredths, Fahrenheit", s["lat"] == "40.42" and s["lon"] == "-3.70" and s["place"] == "Madrid" and s["unit"] == "F")

            # current reading, age and last error come from the clock
            post("/_test/state", {"reading": {"wx": 2, "t10": 73}, "status": "Last reading 4 minutes ago.", "strikes": 0})
            pg.reload(); pg.wait_for_function("document.getElementById('now').textContent.length > 0")
            check("reading shown in Fahrenheit", pg.inner_text("#now") == "Now: Rain, 45.1 F")
            check("status text shown", "Last reading 4 minutes ago." in pg.inner_text("#wxs"))
            check("Try weather again hidden while all is well", not pg.is_visible("#retry"))
            pg.screenshot(path=os.path.join(shots, "7-reading.png"), full_page=True)
            post("/_test/state", {"reading": None, "status": "Weather off: 2 failures in a row (no reply).", "strikes": 2})
            pg.reload(); pg.wait_for_selector("#retry:not([hidden])")
            check("failure shown with Try weather again", "2 failures" in pg.inner_text("#wxs") and "No weather reading yet." in pg.inner_text("#now"))
            pg.screenshot(path=os.path.join(shots, "8-error-retry.png"), full_page=True)
            pg.click("#retry")
            pg.wait_for_function("document.getElementById('msg').textContent.includes('Trying the weather again')")
            check("retry reached the clock", log()[-1] == "retry" and state()["strikes"] == 0)

            # sleep box
            pg.check("#sl"); pg.fill("#st", "23:30"); pg.fill("#wt", "06:45")
            pg.click("button:has-text('Save settings')"); pg.wait_for_function("document.getElementById('msg').textContent == 'Saved.'")
            s = state()
            check("sleep schedule saved", s["sleep"] == 1 and s["sleep_time"] == "23:30" and s["wake_time"] == "06:45")
            pg.wait_for_timeout(1000)                     # the page refills itself 0.7 s after a save
            pg.fill("#st", "22:15")                       # an unsaved edit must survive the button presses below
            pg.click("button:has-text('Wake now')"); pg.wait_for_function("document.getElementById('msg').textContent == 'Wake now sent.'")
            pg.click("button:has-text('Sleep now')"); pg.wait_for_function("document.getElementById('msg').textContent == 'Sleep now sent.'")
            pg.wait_for_timeout(1000)
            check("Wake now and Sleep now reached nightmode", log()[-2:] == ["wake", "sleep"])
            check("buttons do not save or reset the form", state()["sleep_time"] == "23:30" and pg.input_value("#st") == "22:15")
            pg.screenshot(path=os.path.join(shots, "9-sleep.png"), full_page=True)

            check("no script errors in the browser", not errors)
            pg.set_viewport_size({"width": 360, "height": 900})
            pg.reload(); pg.wait_for_function("document.getElementById('lat').value.length > 0")
            check("no horizontal scroll at 360 px", pg.evaluate("document.documentElement.scrollWidth <= window.innerWidth"))
            pg.screenshot(path=os.path.join(shots, "10-phone.png"), full_page=True)
            pg.set_viewport_size({"width": 1100, "height": 900})
            pg.screenshot(path=os.path.join(shots, "11-desktop.png"), full_page=True)
            br.close()
    finally:
        srv.terminate()
    print("screenshots in", shots)
    print("config_page_check:", "FAILED %d" % len(fails) if fails else "all passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
