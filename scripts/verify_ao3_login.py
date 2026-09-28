#!/usr/bin/env python3
"""Replays the AO3 app's login flow from a desktop, before flashing.

Mirrors src/activities/apps/ao3/Ao3Client.cpp step by step: CSRF token from
/token_dispenser.json (login-form fallback), form POST without following the
redirect, then the landing page's greeting to confirm the login and read the
canonical user name. Finally fetches the first "Marked for Later" page and
counts the work links the device would list.

Credentials come from AO3_LOGIN / AO3_PASSWORD or an interactive prompt; the
password is never printed. Only statuses, redirect targets, cookie names and
counts are shown. Standard library only.

    python3 scripts/verify_ao3_login.py
"""

import getpass
import http.cookiejar
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

BASE = "https://archiveofourown.org"
UA = "CrossMux-AO3/1.0 (ESP32 e-reader; +https://github.com/0x1abin/crossmux)"
KEPT_COOKIES = ("_otwarchive_session", "remember_user_token", "user_credentials")


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def main() -> int:
    login = os.environ.get("AO3_LOGIN") or input("AO3 username or email: ").strip()
    password = os.environ.get("AO3_PASSWORD") or getpass.getpass("AO3 password: ")

    jar = http.cookiejar.CookieJar()
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar), NoRedirect)

    def request(path, data=None, extra=None):
        headers = {"User-Agent": UA, "Accept": "text/html,application/json,*/*"}
        headers.update(extra or {})
        req = urllib.request.Request(BASE + path if path.startswith("/") else path, data=data, headers=headers)
        try:
            resp = opener.open(req, timeout=45)
            return resp.status, resp.headers, resp.read()
        except urllib.error.HTTPError as err:  # 3xx/4xx/5xx land here without redirects
            return err.code, err.headers, err.read()

    status, _, body = request("/token_dispenser.json")
    token = ""
    if status == 200:
        try:
            token = json.loads(body).get("token", "")
        except ValueError:
            pass
    print(f"1. token_dispenser: HTTP {status}, token {'ok' if token else 'missing'}")
    if not token:
        status, _, body = request("/users/login")
        m = re.search(rb'name="authenticity_token" value="([^"]+)"', body)
        token = m.group(1).decode() if m else ""
        print(f"   fallback login form: HTTP {status}, token {'ok' if token else 'missing'}")
    if not token:
        print("FAIL: no CSRF token (AO3 down or blocking this client)")
        return 1

    form = urllib.parse.urlencode(
        {
            "authenticity_token": token,
            "user[login]": login,
            "user[password]": password,
            "user[remember_me]": "1",
            "commit": "Log in",
        }
    ).encode()
    status, headers, body = request(
        "/users/login",
        data=form,
        extra={
            "Content-Type": "application/x-www-form-urlencoded",
            "Origin": BASE,
            "Referer": BASE + "/users/login",
        },
    )
    location = headers.get("Location", "")
    cookies = sorted(c.name for c in jar if c.name in KEPT_COOKIES)
    print(f"2. login POST: HTTP {status} -> {location or '(no redirect)'}; cookies: {', '.join(cookies) or 'none'}")
    if status == 200:
        print("FAIL: login form shown again (wrong username/password)")
        return 1
    if "auth_error" in location:
        print("FAIL: CSRF rejected (/auth_error)")
        return 1
    if status not in (301, 302, 303) or "/users/login" in location:
        print("FAIL: unexpected login response")
        return 1

    status, _, body = request(location or "/")
    m = re.search(rb'<nav id="greeting".*?href="/users/([^"/?]+)"', body, re.S)
    user = m.group(1).decode() if m else ""
    print(f"3. landing page: HTTP {status}, greeting user: {user or 'none'}")
    if not user:
        print("FAIL: landing page does not show a logged-in user")
        return 1

    print("OK: login works for this account")

    # The device retries Cloudflare 52x / gateway errors once; do the same.
    for attempt in range(2):
        status, _, body = request(f"/users/{user}/readings?show=to-read")
        if status not in (502, 503, 504) and not 520 <= status <= 527:
            break
        if attempt == 0:
            print(f"   HTTP {status} (AO3 origin unreachable), retrying once...")
            time.sleep(2)
    ids = set(re.findall(rb'href="/works/(\d+)"', body))
    print(f"4. Marked for Later: HTTP {status}, {len(ids)} works on page 1")
    if status != 200:
        print("WARN: list fetch failed; AO3 itself returned an error, try again later")
        return 2
    print("OK: list fetch works too")
    return 0


if __name__ == "__main__":
    sys.exit(main())
