#pragma once

#include <WeReadHttpClient.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "Ao3HtmlScanner.h"

// Minimal AO3 web client: form login with the Rails CSRF token, cookie-carrying
// page fetches streamed through Ao3HtmlScanner, and EPUB downloads streamed to
// the SD card. Reuses the WeRead HTTP/1.1 transport, which does not follow
// redirects on its own, so the 302 that carries the login cookies is visible.
//
// Blocking: every call runs on the caller's task (the activity loop), like the
// OPDS browser. One 4 KiB read buffer is allocated in begin() and freed in end().
class Ao3Client {
 public:
  enum class Status : uint8_t {
    Ok,
    NetworkError,
    BadCredentials,   // AO3 re-showed the login form
    SessionRejected,  // CSRF check failed (redirect to /auth_error)
    NotLoggedIn,      // no stored credentials, or the session could not be restored
    RateLimited,      // HTTP 429
    ServerError,      // 5xx, including Cloudflare's 52x
    NotFound,
    Unavailable,  // AO3 redirected a download back to the work page
    StorageError,
    Aborted,
    OutOfMemory,
  };

  // Return false to abort the transfer.
  using ProgressFn = std::function<bool(size_t done, size_t total)>;

  bool begin();
  void end();

  // Logs in and, on success, stores credentials, user name and cookies.
  Status login(const std::string& login, const std::string& password);
  // Streams an AO3 page (path such as "/users/NAME/readings") through the
  // scanner. With requireLogin, restores the session with the stored password
  // once if AO3 no longer recognizes the cookies; public pages (search) skip
  // that and are fetched with whatever cookies there are.
  // Redirects are followed (tag synonyms land on their canonical tag);
  // `finalPath` receives the path that was finally read. Work pages stop
  // reading at the chapter text (Ao3HtmlScanner::done()).
  Status fetchPage(const std::string& path, Ao3HtmlScanner& scanner, bool requireLogin = true,
                   std::string* finalPath = nullptr);
  // Downloads a work's EPUB into `folder` as `fileName`, or when null as
  // "<title>-<id>.epub" (title empty: the name AO3 suggests). If the file
  // already exists with identical bytes it is kept untouched (reader cache and
  // progress stay); if the work changed, the reading position is carried over.
  Status downloadEpub(uint32_t workId, const std::string& title, const char* folder, const char* fileName,
                      const ProgressFn& progress, std::string& savedPath);
  void logout();

  static const char* statusName(Status status);

 private:
  struct Response {
    int status = -1;
    std::string location;  // path when on archiveofourown.org, else full URL
    std::string disposition;
    size_t contentLength = 0;
  };

  Status request(const char* method, const std::string& target, const std::string* body,
                 const WeReadHttpClient::DataCallback& onData, Response& response);
  Status fetchToken(std::string& token);
  struct PageResult {
    bool loggedIn = false;
    bool loginRedirect = false;
    std::string finalPath;
  };
  Status fetchPageOnce(const std::string& path, Ao3HtmlScanner& scanner, PageResult& result);
  Status installDownload(const std::string& partPath, const std::string& finalPath);
  bool sameFileContents(const std::string& a, const std::string& b);
  void onHeader(const char* name, const char* value, Response& response);
  std::string cookieHeader() const;

  static constexpr size_t kBufferSize = 4096;

  WeReadHttpClient::Session session_;
  std::unique_ptr<uint8_t[]> buffer_;
};
