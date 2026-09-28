#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Streaming extractor for AO3 HTML pages. Fed arbitrary chunks straight from
// the TLS read buffer, it never holds the page: only the current tag (bounded)
// and the anchor text being captured. Pure C++ so host tests can drive it.
//
// It recognizes:
//   <a href="/works/123">Title</a>          -> a work row (exact path only; the
//                                               /works/123/kudos etc. stat links
//                                               do not match)
//   <a rel="author" href=...>Name</a>       -> author of the preceding work
//   <li class="next"|"previous"><a href=...> -> pagination links (pagy)
//   <nav id="greeting"> ... <a href="/users/NAME">
//                                           -> logged in as NAME (the header
//                                              shows a login form otherwise)
//   <input name="authenticity_token" ...>   -> CSRF token fallback
//   <form id="new_user" ...>                -> page is the full login form
//
// On a work page (/works/ID) it also collects the detail block: the title
// heading, byline authors, summary, every <a class="tag"> inside
// <dl class="work meta group"> grouped by its <dd> class, and the stats.
// done() turns true at <div id="chapters">, so callers can stop reading
// before the work text.
class Ao3HtmlScanner {
 public:
  struct Work {
    uint32_t id = 0;
    std::string title;
    std::string author;
  };

  enum class TagGroup : uint8_t { Rating, Warning, Category, Fandom, Relationship, Character, Freeform, Other };
  struct Link {
    std::string name;
    std::string href;
    TagGroup group = TagGroup::Other;
  };
  struct Detail {
    std::string title;
    std::vector<Link> authors;  // href is the pseud path, e.g. /users/x/pseuds/y
    std::vector<Link> tags;     // href is the tag works listing
    std::string summary;
    std::string language;
    std::string words;
    std::string chapters;
    std::string kudos;
  };

  explicit Ao3HtmlScanner(size_t maxWorks = 40) : maxWorks_(maxWorks) {}

  void feed(const char* data, size_t len);
  // Forget everything seen so far (same work limit), e.g. before a retry.
  void reset() { *this = Ao3HtmlScanner(maxWorks_); }

  std::vector<Work>& works() { return works_; }
  const std::string& nextHref() const { return nextHref_; }
  const std::string& prevHref() const { return prevHref_; }
  // Non-empty only when the page header greets a logged-in user.
  const std::string& loggedInUser() const { return loggedInUser_; }
  const std::string& authenticityToken() const { return authenticityToken_; }
  bool sawLoginForm() const { return sawLoginForm_; }
  Detail& detail() { return detail_; }
  bool done() const { return done_; }

  // Decodes HTML entities and collapses whitespace runs. Exposed for tests.
  static std::string decodeText(const std::string& raw);

 private:
  enum class Mode : uint8_t { Text, Tag, RawText };
  enum class Capture : uint8_t { None, Title, Author, DetailTitle, DetailAuthor, Tag, Summary, Stat };
  enum class Stat : uint8_t { None, Language, Words, Chapters, Kudos };
  enum class PageLink : uint8_t { None, Next, Prev };

  static constexpr size_t kMaxTagBytes = 768;
  static constexpr size_t kMaxCaptureBytes = 320;
  static constexpr size_t kMaxAuthorBytes = 120;
  // A pseud link this close after a title (e.g. "Title</a> by <a href=
  // "/users/x/pseuds/x">") is the author on pages without rel="author".
  static constexpr size_t kPseudAuthorWindow = 200;
  static constexpr size_t kMaxSummaryBytes = 600;
  static constexpr size_t kMaxTags = 60;
  static constexpr size_t kMaxDetailAuthors = 4;

  void onTag();
  void beginCapture(Capture kind, const char* endTag = "a");
  void endCapture();
  static bool attr(const std::string& tag, const char* name, std::string& out);

  Mode mode_ = Mode::Text;
  std::string tag_;
  bool tagOverflow_ = false;
  char quote_ = 0;
  std::string rawEnd_;  // "</script" / "</style" while in RawText
  size_t rawMatch_ = 0;

  Capture capture_ = Capture::None;
  std::string captureText_;
  size_t bytesSinceTitle_ = SIZE_MAX;
  bool pendingAuthor_ = false;  // last work may still receive author names
  uint32_t captureWorkId_ = 0;
  PageLink pageLink_ = PageLink::None;
  bool inGreeting_ = false;
  std::string captureEnd_ = "a";  // closing tag that ends the capture
  std::string captureHref_;
  // Work page detail state.
  int metaDepth_ = 0;  // >0 inside <dl class="work meta group"> (it nests dl.stats)
  TagGroup tagGroup_ = TagGroup::Other;
  Stat stat_ = Stat::None;
  bool inByline_ = false;
  bool inSummary_ = false;
  int blockquoteDepth_ = 0;  // summaries/notes: their links are not list rows
  bool summaryTaken_ = false;
  bool done_ = false;
  Detail detail_;

  size_t maxWorks_;
  std::vector<Work> works_;
  std::string nextHref_;
  std::string prevHref_;
  std::string loggedInUser_;
  std::string authenticityToken_;
  bool sawLoginForm_ = false;
};
