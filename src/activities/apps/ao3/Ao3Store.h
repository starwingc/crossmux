#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// AO3 account and session cookies on the SD card. The password and every
// cookie value are XOR-obfuscated with the device MAC (same scheme as the
// KOReader credentials): it keeps them out of casual view and ties them to
// this device, but it is not encryption.
//
// Only the cookies that carry the login are kept: the Rails session and
// Devise's remember-me token (valid for weeks, so a restart rarely needs the
// password again).
class Ao3Store : public PersistableStore<Ao3Store> {
 public:
  static const char* getFilePath() { return "/.crosspoint/ao3.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  enum CookieSlot : uint8_t { SessionCookie = 0, RememberCookie, CredentialsCookie, CookieCount };
  static const char* cookieName(CookieSlot slot);
  // Returns the slot for a cookie name, or CookieCount if it is not kept.
  static CookieSlot slotForName(const char* name, size_t nameLen);

  void setCredentials(const std::string& login, const std::string& password);
  const std::string& login() const { return login_; }
  const std::string& password() const { return password_; }
  bool hasCredentials() const { return !login_.empty() && !password_.empty(); }

  // Canonical AO3 user name (the login may be an e-mail address).
  void setUser(const std::string& user) { user_ = user; }
  const std::string& user() const { return user_; }

  const std::string& cookie(CookieSlot slot) const { return cookies_[slot]; }
  void setCookie(CookieSlot slot, std::string value) {
    if (cookies_[slot] == value) return;
    cookies_[slot] = std::move(value);
    dirty_ = true;
  }
  bool hasSession() const {
    return !user_.empty() && (!cookies_[RememberCookie].empty() || !cookies_[SessionCookie].empty());
  }
  // Writes the file only when a cookie changed since the last save, so page
  // fetches that return the same session do not touch the SD card.
  bool saveIfDirty(bool force = false);
  void clearCookies();

  // Drops credentials, user and cookies (the reading cache list stays).
  void clearAll();

  // Works kept in the online-reading cache, oldest first.
  static constexpr size_t kMaxCachedWorks = 5;
  const std::vector<uint32_t>& cachedWorks() const { return cachedWorks_; }
  // Moves `workId` to the newest slot; returns the evicted id or 0.
  uint32_t touchCachedWork(uint32_t workId);

 private:
  Ao3Store() = default;
  ~Ao3Store() = default;
  friend class PersistableStore<Ao3Store>;

  // Rails session cookies are a few hundred bytes; cap reads so a corrupt
  // file cannot make fromJson allocate an unbounded string.
  static constexpr size_t kMaxCookieBytes = 2048;
  static constexpr size_t kMaxPasswordBytes = 256;

  std::string login_;
  std::string password_;
  std::string user_;
  std::string cookies_[CookieCount];
  std::vector<uint32_t> cachedWorks_;
  bool dirty_ = false;
};

#define AO3_STORE Ao3Store::getInstance()
