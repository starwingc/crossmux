# AO3

Unofficial Archive of Our Own client: log in, browse your lists or search, and
download works as EPUB for the built-in reader.

## Usage

- **Apps → AO3**. Wi-Fi is started only when an action needs it (the shared
  Wi-Fi picker appears if not connected); leaving the app restarts silently to
  free the Wi-Fi heap, like the OPDS browser.
- Logged out: **Log In**, **Search Works**, **Download by Link or ID**. Search
  and link downloads work without an account.
- Logged in: **Marked for Later**, **History**, **Bookmarks**, **Subscriptions**
  (works only), Search, Link/ID, **Log Out** (shows the account name).
- Lists show 20 works per AO3 page with « Previous / Next » rows. Confirm on a
  work downloads it; afterwards Confirm opens the book (silent restart into the
  reader), Back returns to the list.
- Link/ID accepts `123456`, `https://archiveofourown.org/works/123456`, or a
  chapter URL.

EPUBs are saved as `/AO3/<title>-<workId>.epub`; downloading again replaces the
file and clears its reader cache. Downloads stream through
`/AO3/.ao3-download.part` and are renamed only after a complete ZIP arrives, so
a cancelled or failed download never leaves a truncated book.

## Protocol

| Step | Request | Notes |
|---|---|---|
| CSRF token | `GET /token_dispenser.json` | falls back to the `authenticity_token` in `/users/login` |
| Login | `POST /users/login` | form fields `user[login]`, `user[password]`, `user[remember_me]=1`; 302 = submitted, `/auth_error` = CSRF rejected, login form again = wrong password |
| Confirm | `GET <redirect>` | `<nav id="greeting">` link `/users/NAME` proves the login and gives the canonical name (the login may be an e-mail) |
| Lists | `/users/NAME/readings?show=to-read`, `/readings`, `/bookmarks`, `/subscriptions?type=works` | page must still greet the user, otherwise one automatic re-login |
| Search | `/works/search?work_search[query]=…` | no login needed |
| Download | `GET /downloads/ID/ID.epub` | 301 to `download.archiveofourown.org`; the body must start with `PK` |

`Ao3HtmlScanner` parses pages as they stream from the TLS buffer: work rows are
anchors whose href is exactly `/works/ID`, authors are the following
`rel="author"` (or pseud) links, pagination is the pagy `li.next` /
`li.previous`. It keeps only the current tag and anchor text, never the page.

The transport is `WeReadHttpClient` (HTTP/1.1, no automatic redirects, so the
login 302 and its cookies are visible). TLS certificates are not verified by
that transport, matching the WeRead client.

## Storage and privacy

`/.crosspoint/ao3.json` holds the login name, the canonical user name, the
password and the `_otwarchive_session` / `remember_user_token` /
`user_credentials` cookies. The password and cookies are XOR-obfuscated with
the device MAC (as for KOReader sync): unreadable at a glance and bound to this
device, but not encryption — protect the SD card. The file is rewritten only
when AO3 rotates a cookie. **Log Out** deletes it; downloaded EPUBs stay.

## Resources

One 4 KiB read buffer (allocated in `onEnter`, freed in `onExit`), the list of
at most 40 works for the current page, and the TLS session of the shared
client. No PSRAM-only paths; the same code runs on C3 and S3 builds.

## Verifying before flashing

`python3 scripts/verify_ao3_login.py` replays the device's login flow from a
desktop with your account and prints only statuses, redirect targets, cookie
names and counts.
