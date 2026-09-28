#include "Ao3Store.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr const char* kCookieNames[Ao3Store::CookieCount] = {"_otwarchive_session", "remember_user_token",
                                                             "user_credentials"};
}  // namespace

const char* Ao3Store::cookieName(const CookieSlot slot) { return slot < CookieCount ? kCookieNames[slot] : ""; }

Ao3Store::CookieSlot Ao3Store::slotForName(const char* name, const size_t nameLen) {
  for (uint8_t i = 0; i < CookieCount; ++i) {
    if (strlen(kCookieNames[i]) == nameLen && strncmp(kCookieNames[i], name, nameLen) == 0) {
      return static_cast<CookieSlot>(i);
    }
  }
  return CookieCount;
}

void Ao3Store::setCredentials(const std::string& login, const std::string& password) {
  login_ = login;
  password_ = password;
}

void Ao3Store::clearCookies() {
  for (auto& cookie : cookies_) cookie.clear();
  dirty_ = true;
}

bool Ao3Store::saveIfDirty(const bool force) {
  if (!dirty_ && !force) return true;
  if (!saveToFile()) return false;
  dirty_ = false;
  return true;
}

uint32_t Ao3Store::touchCachedWork(const uint32_t workId) {
  cachedWorks_.erase(std::remove(cachedWorks_.begin(), cachedWorks_.end(), workId), cachedWorks_.end());
  uint32_t evicted = 0;
  if (cachedWorks_.size() >= kMaxCachedWorks) {
    evicted = cachedWorks_.front();
    cachedWorks_.erase(cachedWorks_.begin());
  }
  cachedWorks_.push_back(workId);
  dirty_ = true;
  return evicted;
}

void Ao3Store::clearAll() {
  login_.clear();
  password_.clear();
  user_.clear();
  clearCookies();
}

void Ao3Store::toJson(JsonDocument& doc) const {
  doc["login"] = login_;
  doc["password_obf"] = obfuscation::obfuscateToBase64(password_);
  doc["user"] = user_;
  JsonArray cached = doc["cached_works"].to<JsonArray>();
  for (const uint32_t id : cachedWorks_) cached.add(id);
  JsonObject cookies = doc["cookies_obf"].to<JsonObject>();
  for (uint8_t i = 0; i < CookieCount; ++i) {
    if (!cookies_[i].empty()) cookies[kCookieNames[i]] = obfuscation::obfuscateToBase64(cookies_[i]);
  }
}

bool Ao3Store::fromJson(JsonVariantConst doc) {
  clearAll();
  login_ = doc["login"] | "";
  user_ = doc["user"] | "";

  bool ok = true;
  bool tooLong = false;
  password_ = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", kMaxPasswordBytes, &ok, &tooLong);
  if (!ok || tooLong) {
    LOG_ERR("AO3", "Stored password unreadable; login required");
    password_.clear();
  }

  const JsonVariantConst cookies = doc["cookies_obf"];
  for (uint8_t i = 0; i < CookieCount; ++i) {
    const char* encoded = cookies[kCookieNames[i]] | "";
    if (!*encoded) continue;
    ok = true;
    tooLong = false;
    std::string value = obfuscation::deobfuscateFromBase64(encoded, kMaxCookieBytes, &ok, &tooLong);
    if (ok && !tooLong) cookies_[i] = std::move(value);
  }
  cachedWorks_.clear();
  for (const JsonVariantConst id : doc["cached_works"].as<JsonArrayConst>()) {
    const uint32_t value = id | 0U;
    if (value != 0 && cachedWorks_.size() < kMaxCachedWorks) cachedWorks_.push_back(value);
  }
  dirty_ = false;
  return true;
}
