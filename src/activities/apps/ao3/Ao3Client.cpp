#include "Ao3Client.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <strings.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "Ao3Store.h"
#include "activities/reader/ProgressFile.h"
#include "util/BookCacheUtils.h"
#include "util/StringUtils.h"

namespace {

constexpr char kBaseUrl[] = "https://archiveofourown.org";
constexpr char kUserAgent[] = "CrossMux-AO3/1.0 (ESP32 e-reader; +https://github.com/0x1abin/crossmux)";
// AO3's origin often stalls behind Cloudflare; a shorter timeout plus a retry
// recovers faster than one long wait.
constexpr int kTimeoutMs = 25000;
// A download may bounce to another archiveofourown.org host (CDN) at most
// this many times.
constexpr int kMaxDownloadRedirects = 3;
constexpr int kMaxPageRedirects = 3;
constexpr char kPartialName[] = ".ao3-download.part";

bool isRedirect(const int status) { return status == 301 || status == 302 || status == 303 || status == 307; }

// Cloudflare 52x (origin unreachable, e.g. 525 SSL handshake failed) and
// gateway errors: AO3 returns these intermittently and a second try usually
// succeeds.
bool isTransientServerError(const int status) {
  return status == 502 || status == 503 || status == 504 || (status >= 520 && status <= 527);
}
constexpr unsigned long kRetryDelayMs = 1500;
// Measured 2026-09-27: roughly half of AO3 requests failed with 525 or timed
// out. Five tries bring a 50% failure rate down to about 3%.
constexpr int kMaxTransientRetries = 4;
constexpr int kMaxNetworkRetries = 1;

bool hasPrefix(const std::string& s, const char* prefix) { return s.compare(0, strlen(prefix), prefix) == 0; }

// ASCII case-insensitive substring test (strcasestr is not portable to newlib).
bool containsNoCase(const char* haystack, const char* needle) {
  const size_t n = strlen(needle);
  for (; *haystack; ++haystack) {
    if (strncasecmp(haystack, needle, n) == 0) return true;
  }
  return false;
}

// Only archiveofourown.org (and its subdomains) receive the session cookies.
bool isAo3Url(const std::string& url) {
  static constexpr char kScheme[] = "https://";
  if (!hasPrefix(url, kScheme)) return false;
  const size_t hostStart = sizeof(kScheme) - 1;
  size_t hostEnd = url.find('/', hostStart);
  if (hostEnd == std::string::npos) hostEnd = url.size();
  const std::string host = url.substr(hostStart, hostEnd - hostStart);
  static constexpr char kDomain[] = "archiveofourown.org";
  constexpr size_t kDomainLen = sizeof(kDomain) - 1;
  if (host == kDomain) return true;
  return host.size() > kDomainLen + 1 &&
         host.compare(host.size() - kDomainLen - 1, kDomainLen + 1, ".archiveofourown.org") == 0;
}

void appendFormField(std::string& out, const char* name, const std::string& value) {
  if (!out.empty()) out += '&';
  out += name;
  out += '=';
  static constexpr char kHex[] = "0123456789ABCDEF";
  for (const unsigned char c : value) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else if (c == ' ') {
      out += '+';
    } else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 0x0F];
    }
  }
}

Ao3Client::Status statusForHttp(const int status) {
  if (status == 429) return Ao3Client::Status::RateLimited;
  if (status == 404) return Ao3Client::Status::NotFound;
  if (status >= 500) return Ao3Client::Status::ServerError;
  return Ao3Client::Status::NetworkError;
}

// filename="Some Title.epub" -> "Some Title"
std::string dispositionTitle(const std::string& disposition) {
  const size_t key = disposition.find("filename=\"");
  if (key == std::string::npos) return {};
  const size_t start = key + 10;
  const size_t end = disposition.find('"', start);
  if (end == std::string::npos) return {};
  std::string name = disposition.substr(start, end - start);
  const size_t dot = name.rfind('.');
  if (dot != std::string::npos) name.erase(dot);
  // AO3 transliterates spaces to underscores in suggested names.
  std::replace(name.begin(), name.end(), '_', ' ');
  return name;
}

// Per-request retry allowance. shouldRetry() sleeps with a growing backoff
// before returning true, so callers simply repeat the attempt.
struct RetryBudget {
  int transient = 0;
  int network = 0;
  bool shouldRetry(const Ao3Client::Status st, const int httpStatus, const std::string& what) {
    const bool transientHttp = st == Ao3Client::Status::Ok && isTransientServerError(httpStatus);
    const bool networkFailure = st == Ao3Client::Status::NetworkError;
    if (transientHttp && transient < kMaxTransientRetries) {
      ++transient;
      LOG_INF("AO3", "HTTP %d on %s; retry %d/%d", httpStatus, what.c_str(), transient, kMaxTransientRetries);
      delay(kRetryDelayMs * static_cast<unsigned long>(transient));
      return true;
    }
    if (networkFailure && network < kMaxNetworkRetries) {
      ++network;
      LOG_INF("AO3", "Network error on %s; retrying", what.c_str());
      delay(kRetryDelayMs);
      return true;
    }
    return false;
  }
};
}  // namespace

const char* Ao3Client::statusName(const Status status) {
  switch (status) {
    case Status::Ok:
      return "ok";
    case Status::NetworkError:
      return "network";
    case Status::BadCredentials:
      return "bad-credentials";
    case Status::SessionRejected:
      return "csrf-rejected";
    case Status::NotLoggedIn:
      return "not-logged-in";
    case Status::RateLimited:
      return "rate-limited";
    case Status::ServerError:
      return "server";
    case Status::NotFound:
      return "not-found";
    case Status::Unavailable:
      return "unavailable";
    case Status::StorageError:
      return "storage";
    case Status::Aborted:
      return "aborted";
    case Status::OutOfMemory:
      return "oom";
  }
  return "?";
}

bool Ao3Client::begin() {
  if (buffer_) return true;
  // One TLS read buffer for the whole activity; a 4 KiB stack array would blow
  // the 256-byte local budget, and the buffer must outlive single requests.
  buffer_ = makeUniqueNoThrow<uint8_t[]>(kBufferSize);
  if (!buffer_) {
    LOG_ERR("AO3", "OOM: read buffer (%u bytes)", static_cast<unsigned>(kBufferSize));
    return false;
  }
  return true;
}

void Ao3Client::end() {
  session_.reset();
  buffer_.reset();
}

std::string Ao3Client::cookieHeader() const {
  std::string header;
  for (uint8_t i = 0; i < Ao3Store::CookieCount; ++i) {
    const auto slot = static_cast<Ao3Store::CookieSlot>(i);
    const std::string& value = AO3_STORE.cookie(slot);
    if (value.empty()) continue;
    if (!header.empty()) header += "; ";
    header += Ao3Store::cookieName(slot);
    header += '=';
    header += value;
  }
  return header;
}

void Ao3Client::onHeader(const char* name, const char* value, Response& response) {
  if (strcasecmp(name, "Set-Cookie") == 0) {
    const char* eq = strchr(value, '=');
    if (!eq) return;
    const auto slot = Ao3Store::slotForName(value, static_cast<size_t>(eq - value));
    if (slot == Ao3Store::CookieCount) return;
    const char* end = strchr(eq + 1, ';');
    std::string cookieValue(eq + 1, end ? static_cast<size_t>(end - eq - 1) : strlen(eq + 1));
    // Deletions come as an empty value and/or an expiry in the past.
    const bool deleted =
        cookieValue.empty() || containsNoCase(value, "max-age=0") || containsNoCase(value, "expires=Thu, 01 Jan 1970");
    AO3_STORE.setCookie(slot, deleted ? std::string() : std::move(cookieValue));
  } else if (strcasecmp(name, "Location") == 0) {
    response.location = value;
    const size_t baseLen = sizeof(kBaseUrl) - 1;
    if (hasPrefix(response.location, kBaseUrl) &&
        (response.location.size() == baseLen || response.location[baseLen] == '/')) {
      response.location.erase(0, baseLen);
      if (response.location.empty()) response.location = "/";
    }
  } else if (strcasecmp(name, "Content-Length") == 0) {
    response.contentLength = static_cast<size_t>(strtoul(value, nullptr, 10));
  } else if (strcasecmp(name, "Content-Disposition") == 0) {
    response.disposition = value;
  }
}

Ao3Client::Status Ao3Client::request(const char* method, const std::string& target, const std::string* body,
                                     const WeReadHttpClient::DataCallback& onData, Response& response) {
  if (!buffer_) return Status::OutOfMemory;
  response = Response{};
  const std::string url = target.empty() || target[0] == '/' ? std::string(kBaseUrl) + target : target;
  const bool ao3 = isAo3Url(url);
  const std::string cookies = ao3 ? cookieHeader() : std::string();

  WeReadHttpClient::Header headers[6];
  size_t headerCount = 0;
  headers[headerCount++] = {"User-Agent", kUserAgent};
  headers[headerCount++] = {"Accept", "text/html,application/xhtml+xml,application/json,application/epub+zip,*/*"};
  if (!cookies.empty()) headers[headerCount++] = {"Cookie", cookies.c_str()};
  if (body) {
    headers[headerCount++] = {"Content-Type", "application/x-www-form-urlencoded"};
    headers[headerCount++] = {"Origin", kBaseUrl};
    headers[headerCount++] = {"Referer", "https://archiveofourown.org/users/login"};
  }

  WeReadHttpClient::RequestOptions options;
  options.method = method;
  options.body = body ? reinterpret_cast<const uint8_t*>(body->data()) : nullptr;
  options.bodySize = body ? body->size() : 0;
  options.headers = headers;
  options.headerCount = headerCount;
  options.timeoutMs = kTimeoutMs;
  options.readBuffer = buffer_.get();
  options.readBufferSize = kBufferSize;

  const auto result = WeReadHttpClient::request(
      session_, url.c_str(), options, onData,
      [this, &response, ao3](const char* name, const char* value) {
        // Never let another host overwrite AO3 cookies.
        if (!ao3 && strcasecmp(name, "Set-Cookie") == 0) return;
        onHeader(name, value, response);
      },
      response.status);
  LOG_DBG("AO3", "%s %s -> %d", method, target.c_str(), response.status);
  if (result == WeReadHttpClient::Result::Aborted) return Status::Aborted;
  if (result != WeReadHttpClient::Result::Ok) return Status::NetworkError;
  return Status::Ok;
}

Ao3Client::Status Ao3Client::fetchToken(std::string& token) {
  token.clear();
  // AO3 serves many pages from cache and hands out live CSRF tokens through
  // this endpoint (its own JavaScript uses it), so prefer it over the token
  // embedded in the login form.
  std::string json;
  Response response;
  Status st = Status::NetworkError;
  RetryBudget retry;
  do {
    json.clear();
    st = request(
        "GET", "/token_dispenser.json", nullptr,
        [&json](const uint8_t* data, const size_t len) {
          if (json.size() + len > 1024) return false;
          json.append(reinterpret_cast<const char*>(data), len);
          return true;
        },
        response);
  } while (retry.shouldRetry(st, response.status, "/token_dispenser.json"));
  if (st == Status::Ok && response.status == 200) {
    const size_t key = json.find("\"token\"");
    const size_t open = key == std::string::npos ? key : json.find('"', json.find(':', key));
    const size_t close = open == std::string::npos ? open : json.find('"', open + 1);
    if (close != std::string::npos) token = json.substr(open + 1, close - open - 1);
  }
  if (!token.empty()) return Status::Ok;
  LOG_INF("AO3", "token_dispenser unavailable (%s/%d); using login form", statusName(st), response.status);

  Ao3HtmlScanner scanner(0);
  retry = RetryBudget{};
  do {
    scanner.reset();
    st = request(
        "GET", "/users/login", nullptr,
        [&scanner](const uint8_t* data, const size_t len) {
          scanner.feed(reinterpret_cast<const char*>(data), len);
          return true;
        },
        response);
  } while (retry.shouldRetry(st, response.status, "/users/login"));
  if (st != Status::Ok) return st;
  if (response.status != 200) return statusForHttp(response.status);
  token = scanner.authenticityToken();
  return token.empty() ? Status::ServerError : Status::Ok;
}

Ao3Client::Status Ao3Client::login(const std::string& login, const std::string& password) {
  if (login.empty() || password.empty()) return Status::BadCredentials;
  // Start from a clean session so a stale cookie cannot mismatch the token.
  AO3_STORE.clearCookies();
  AO3_STORE.setUser("");

  std::string token;
  Status st = fetchToken(token);
  if (st != Status::Ok) return st;

  std::string form;
  form.reserve(256);
  appendFormField(form, "authenticity_token", token);
  appendFormField(form, "user%5Blogin%5D", login);
  appendFormField(form, "user%5Bpassword%5D", password);
  appendFormField(form, "user%5Bremember_me%5D", "1");
  appendFormField(form, "commit", "Log in");

  Ao3HtmlScanner formScanner(0);
  Response response;
  // Only 521/523/525 are resent: Cloudflare never reached AO3, so the login
  // was not submitted. Other failures may have been processed.
  for (int attempt = 0;; ++attempt) {
    formScanner.reset();
    st = request(
        "POST", "/users/login", &form,
        [&formScanner](const uint8_t* data, const size_t len) {
          formScanner.feed(reinterpret_cast<const char*>(data), len);
          return true;
        },
        response);
    const bool notDelivered =
        st == Status::Ok && (response.status == 521 || response.status == 523 || response.status == 525);
    if (!notDelivered || attempt >= kMaxTransientRetries) break;
    LOG_INF("AO3", "HTTP %d on login POST; retry %d/%d", response.status, attempt + 1, kMaxTransientRetries);
    delay(kRetryDelayMs * static_cast<unsigned long>(attempt + 1));
  }
  // The form holds the password; drop it before anything else can log it.
  std::fill(form.begin(), form.end(), '\0');
  if (st != Status::Ok) return st;
  LOG_INF("AO3", "login POST -> %d %s", response.status, response.location.c_str());

  if (response.status == 200) return formScanner.sawLoginForm() ? Status::BadCredentials : Status::ServerError;
  if (!isRedirect(response.status)) return statusForHttp(response.status);
  if (response.location.find("auth_error") != std::string::npos) return Status::SessionRejected;
  if (hasPrefix(response.location, "/users/login")) return Status::BadCredentials;

  // Follow the redirect once: the page header names the logged-in user, which
  // both confirms the login and gives the canonical name when an e-mail
  // address was typed.
  Ao3HtmlScanner landing(0);
  PageResult page;
  st = fetchPageOnce(response.location.empty() ? "/" : response.location, landing, page);
  if (st != Status::Ok) return st;
  if (!page.loggedIn) return Status::BadCredentials;

  AO3_STORE.setCredentials(login, password);
  AO3_STORE.setUser(landing.loggedInUser());
  if (!AO3_STORE.saveIfDirty(true)) LOG_ERR("AO3", "Failed to save session");
  LOG_INF("AO3", "Logged in as %s", AO3_STORE.user().c_str());
  return Status::Ok;
}

Ao3Client::Status Ao3Client::fetchPageOnce(const std::string& path, Ao3HtmlScanner& scanner, PageResult& result) {
  result = PageResult{};
  std::string target = path;
  Response response;
  Status st = Status::NetworkError;
  RetryBudget retry;
  // Tag listings redirect synonyms to their canonical tag; follow a few hops.
  for (int hop = 0; hop <= kMaxPageRedirects; ++hop) {
    scanner.reset();
    st = request(
        "GET", target, nullptr,
        [&scanner](const uint8_t* data, const size_t len) {
          scanner.feed(reinterpret_cast<const char*>(data), len);
          // Work pages: everything needed precedes the chapter text.
          return !scanner.done();
        },
        response);
    if (st == Status::Aborted && scanner.done()) st = Status::Ok;
    if (retry.shouldRetry(st, response.status, target)) {
      --hop;  // a retry is not a redirect hop
      continue;
    }
    if (st != Status::Ok) return st;
    if (!isRedirect(response.status)) break;
    if (hasPrefix(response.location, "/users/login")) {
      result.loginRedirect = true;  // restricted page, or the session is gone
      return Status::Ok;
    }
    if (response.location.empty() || response.location[0] != '/') return Status::NotFound;  // off-site
    target = response.location;
  }
  if (isRedirect(response.status)) return Status::NotFound;  // redirect loop
  if (response.status != 200) return statusForHttp(response.status);
  result.loggedIn = !scanner.loggedInUser().empty();
  result.finalPath = target;
  return Status::Ok;
}

Ao3Client::Status Ao3Client::fetchPage(const std::string& path, Ao3HtmlScanner& scanner, const bool requireLogin,
                                       std::string* finalPath) {
  PageResult page;
  const auto finish = [&](const Status st) {
    if (st == Status::Ok && finalPath) *finalPath = page.finalPath;
    if (st == Status::Ok && AO3_STORE.hasSession()) AO3_STORE.saveIfDirty();  // AO3 rotates the session cookie
    return st;
  };

  if (!requireLogin) {
    const Status st = fetchPageOnce(path, scanner, page);
    if (st == Status::Ok && page.loginRedirect) return Status::NotLoggedIn;
    return finish(st);
  }

  if (!AO3_STORE.hasSession()) {
    if (!AO3_STORE.hasCredentials()) return Status::NotLoggedIn;
    const Status st = login(AO3_STORE.login(), AO3_STORE.password());
    if (st != Status::Ok) return st == Status::BadCredentials ? Status::NotLoggedIn : st;
  }

  Status st = fetchPageOnce(path, scanner, page);
  if (st != Status::Ok || page.loggedIn) return finish(st);

  LOG_INF("AO3", "Session expired; logging in again");
  if (!AO3_STORE.hasCredentials()) return Status::NotLoggedIn;
  st = login(AO3_STORE.login(), AO3_STORE.password());
  if (st != Status::Ok) return st == Status::BadCredentials ? Status::NotLoggedIn : st;
  st = fetchPageOnce(path, scanner, page);
  if (st == Status::Ok && !page.loggedIn) return Status::NotLoggedIn;
  return finish(st);
}

bool Ao3Client::sameFileContents(const std::string& a, const std::string& b) {
  HalFile fa;
  HalFile fb;
  if (!Storage.openFileForRead("AO3", a.c_str(), fa) || !Storage.openFileForRead("AO3", b.c_str(), fb)) return false;
  if (fa.fileSize() != fb.fileSize()) return false;
  // Split the idle read buffer in two instead of allocating more.
  const size_t half = kBufferSize / 2;
  uint8_t* bufA = buffer_.get();
  uint8_t* bufB = buffer_.get() + half;
  while (true) {
    const int na = fa.read(bufA, half);
    const int nb = fb.read(bufB, half);
    if (na != nb || na < 0) return false;
    if (na == 0) return true;
    if (memcmp(bufA, bufB, static_cast<size_t>(na)) != 0) return false;
  }
}

Ao3Client::Status Ao3Client::installDownload(const std::string& partPath, const std::string& finalPath) {
  if (Storage.exists(finalPath.c_str())) {
    // AO3 serves the same file until the work changes. Keep the old one so the
    // reader's layout cache and progress stay valid.
    if (sameFileContents(partPath, finalPath)) {
      LOG_INF("AO3", "Unchanged, keeping %s", finalPath.c_str());
      Storage.remove(partPath.c_str());
      return Status::Ok;
    }
  }

  // The work changed (new chapters): the layout cache must go, but carry the
  // reading position over. It is a few dozen bytes.
  const std::string cachePath = bookCachePath(finalPath);
  std::string progress;
  {
    HalFile f;
    if (!cachePath.empty() && Storage.exists((cachePath + "/progress.bin").c_str()) &&
        Storage.openFileForRead("AO3", cachePath + "/progress.bin", f)) {
      const size_t size = f.fileSize();
      if (size > 0 && size <= 512) {
        progress.resize(size);
        if (f.read(&progress[0], size) != static_cast<int>(size)) progress.clear();
      }
    }
  }

  if (Storage.exists(finalPath.c_str())) Storage.remove(finalPath.c_str());
  if (!Storage.rename(partPath.c_str(), finalPath.c_str())) {
    LOG_ERR("AO3", "rename failed: %s", finalPath.c_str());
    Storage.remove(partPath.c_str());
    return Status::StorageError;
  }
  clearBookCache(finalPath);
  if (!progress.empty() && Storage.mkdir(cachePath.c_str()) &&
      ProgressFile::writeAtomic(cachePath, reinterpret_cast<const uint8_t*>(progress.data()), progress.size())) {
    LOG_INF("AO3", "Kept reading progress for %s", finalPath.c_str());
  }
  return Status::Ok;
}

Ao3Client::Status Ao3Client::downloadEpub(const uint32_t workId, const std::string& title, const char* folder,
                                          const char* fileName, const ProgressFn& progress, std::string& savedPath) {
  savedPath.clear();
  if (!Storage.exists(folder) && !Storage.mkdir(folder)) {
    LOG_ERR("AO3", "mkdir failed: %s", folder);
    return Status::StorageError;
  }
  std::string partPath = std::string(folder) + "/" + kPartialName;

  // The download route only reads the work id; the file-name segment is free.
  char path[48];
  snprintf(path, sizeof(path), "/downloads/%u/%u.epub", static_cast<unsigned>(workId), static_cast<unsigned>(workId));
  std::string target = path;

  Response response;
  Status st = Status::NetworkError;
  RetryBudget retry;
  for (int hop = 0; hop <= kMaxDownloadRedirects; ++hop) {
    if (Storage.exists(partPath.c_str())) Storage.remove(partPath.c_str());
    HalFile file;
    if (!Storage.openFileForWrite("AO3", partPath.c_str(), file)) return Status::StorageError;

    size_t written = 0;
    bool writeFailed = false;
    bool notEpub = false;
    st = request(
        "GET", target, nullptr,
        [&](const uint8_t* data, const size_t len) {
          if (response.status != 200) return true;  // redirect/error body: discard
          // EPUB is a ZIP: anything else is an HTML error page.
          if (written == 0 && (len < 2 || data[0] != 'P' || data[1] != 'K')) {
            notEpub = true;
            return false;
          }
          if (file.write(data, len) != len) {
            writeFailed = true;
            return false;
          }
          written += len;
          return !progress || progress(written, response.contentLength);
        },
        response);
    file.close();

    if (st == Status::Ok && isRedirect(response.status) && !response.location.empty()) {
      // Back to the work page means AO3 refused (draft, hidden, downloads off).
      if (hasPrefix(response.location, "/works/")) {
        st = Status::Unavailable;
        break;
      }
      const std::string next =
          response.location[0] == '/' ? std::string(kBaseUrl) + response.location : response.location;
      if (!isAo3Url(next)) {
        st = Status::Unavailable;
        break;
      }
      target = next;
      continue;
    }
    // Storage and not-an-EPUB failures are final; transport ones are not.
    if (!writeFailed && !notEpub && retry.shouldRetry(st, response.status, target)) {
      --hop;  // a retry is not a redirect hop
      continue;
    }
    if (writeFailed)
      st = Status::StorageError;
    else if (notEpub)
      st = Status::Unavailable;
    else if (st == Status::Ok && response.status != 200)
      st = statusForHttp(response.status);
    else if (st == Status::Ok && written == 0)
      st = Status::Unavailable;
    break;
  }

  if (st != Status::Ok) {
    Storage.remove(partPath.c_str());
    return st;
  }

  std::string name;
  if (fileName) {
    name = fileName;
  } else {
    name = title.empty() ? dispositionTitle(response.disposition) : title;
    char idSuffix[16];
    snprintf(idSuffix, sizeof(idSuffix), "-%u", static_cast<unsigned>(workId));
    name = StringUtils::sanitizeFilename(name.empty() ? std::string("AO3") : name, 80) + idSuffix + ".epub";
  }
  savedPath = std::string(folder) + "/" + name;
  st = installDownload(partPath, savedPath);
  if (st != Status::Ok) savedPath.clear();
  return st;
}

void Ao3Client::logout() {
  session_.reset();
  AO3_STORE.clearAll();
  // Rewrite rather than delete: the online-reading cache list must survive.
  AO3_STORE.saveIfDirty(true);
}
