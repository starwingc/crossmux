#include "Ao3HtmlScanner.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace {

bool startsWithNoCase(const std::string& s, size_t pos, const char* prefix) {
  for (size_t i = 0; prefix[i]; ++i) {
    if (pos + i >= s.size()) return false;
    if (std::tolower(static_cast<unsigned char>(s[pos + i])) != prefix[i]) return false;
  }
  return true;
}

// Tag name, lowercased, without the leading '/' for end tags.
std::string tagName(const std::string& tag, bool& closing) {
  size_t i = 0;
  closing = false;
  if (i < tag.size() && tag[i] == '/') {
    closing = true;
    ++i;
  }
  std::string name;
  while (i < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[i])) || tag[i] == '-')) {
    name += static_cast<char>(std::tolower(static_cast<unsigned char>(tag[i])));
    ++i;
  }
  return name;
}

void appendUtf8(std::string& out, unsigned long cp) {
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return;
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// "/works/123" exactly (optionally followed by a query string) -> 123.
bool parseWorkHref(const std::string& href, uint32_t& id) {
  static constexpr char kPrefix[] = "/works/";
  constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
  std::string path = href;
  static constexpr char kHost[] = "https://archiveofourown.org";
  if (path.compare(0, sizeof(kHost) - 1, kHost) == 0) path.erase(0, sizeof(kHost) - 1);
  if (path.compare(0, kPrefixLen, kPrefix) != 0) return false;
  size_t i = kPrefixLen;
  uint32_t value = 0;
  size_t digits = 0;
  while (i < path.size() && std::isdigit(static_cast<unsigned char>(path[i]))) {
    if (++digits > 9) return false;
    value = value * 10 + static_cast<uint32_t>(path[i] - '0');
    ++i;
  }
  if (digits == 0 || value == 0) return false;
  if (i != path.size() && path[i] != '?') return false;
  id = value;
  return true;
}

bool relHas(const std::string& rel, const char* token) {
  const size_t len = strlen(token);
  size_t pos = 0;
  while (pos < rel.size()) {
    while (pos < rel.size() && rel[pos] == ' ') ++pos;
    size_t end = rel.find(' ', pos);
    if (end == std::string::npos) end = rel.size();
    if (end - pos == len && rel.compare(pos, len, token) == 0) return true;
    pos = end;
  }
  return false;
}

}  // namespace

std::string Ao3HtmlScanner::decodeText(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  bool pendingSpace = false;
  for (size_t i = 0; i < raw.size();) {
    const char c = raw[i];
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      pendingSpace = !out.empty();
      ++i;
      continue;
    }
    std::string piece;
    if (c == '&') {
      const size_t semi = raw.find(';', i);
      if (semi != std::string::npos && semi - i <= 10) {
        const std::string ent = raw.substr(i + 1, semi - i - 1);
        if (ent == "amp")
          piece = "&";
        else if (ent == "lt")
          piece = "<";
        else if (ent == "gt")
          piece = ">";
        else if (ent == "quot")
          piece = "\"";
        else if (ent == "apos" || ent == "#39")
          piece = "'";
        else if (ent == "nbsp")
          piece = " ";
        else if (ent == "hellip")
          appendUtf8(piece, 0x2026);
        else if (ent == "mdash")
          appendUtf8(piece, 0x2014);
        else if (ent == "ndash")
          appendUtf8(piece, 0x2013);
        else if (ent == "lsquo")
          appendUtf8(piece, 0x2018);
        else if (ent == "rsquo")
          appendUtf8(piece, 0x2019);
        else if (ent == "ldquo")
          appendUtf8(piece, 0x201C);
        else if (ent == "rdquo")
          appendUtf8(piece, 0x201D);
        else if (!ent.empty() && ent[0] == '#') {
          const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
          const unsigned long cp = strtoul(ent.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
          appendUtf8(piece, cp);
        }
        if (!piece.empty() || (!ent.empty() && ent[0] == '#')) {
          i = semi + 1;
          if (piece == " ") {
            pendingSpace = !out.empty();
            continue;
          }
          if (pendingSpace) out += ' ';
          pendingSpace = false;
          out += piece;
          continue;
        }
      }
    }
    if (pendingSpace) out += ' ';
    pendingSpace = false;
    out += c;
    ++i;
  }
  return out;
}

bool Ao3HtmlScanner::attr(const std::string& tag, const char* name, std::string& out) {
  const size_t nameLen = strlen(name);
  size_t i = 0;
  // Skip the tag name.
  while (i < tag.size() && tag[i] != ' ' && tag[i] != '\t' && tag[i] != '\n' && tag[i] != '\r') ++i;
  while (i < tag.size()) {
    while (i < tag.size() && (tag[i] == ' ' || tag[i] == '\t' || tag[i] == '\n' || tag[i] == '\r' || tag[i] == '/'))
      ++i;
    const size_t nameStart = i;
    while (i < tag.size() && tag[i] != '=' && tag[i] != ' ' && tag[i] != '\t' && tag[i] != '\n' && tag[i] != '\r') ++i;
    const size_t nameEnd = i;
    std::string value;
    if (i < tag.size() && tag[i] == '=') {
      ++i;
      if (i < tag.size() && (tag[i] == '"' || tag[i] == '\'')) {
        const char q = tag[i++];
        const size_t end = tag.find(q, i);
        const size_t stop = end == std::string::npos ? tag.size() : end;
        value = tag.substr(i, stop - i);
        i = stop == tag.size() ? stop : stop + 1;
      } else {
        const size_t start = i;
        while (i < tag.size() && tag[i] != ' ' && tag[i] != '\t') ++i;
        value = tag.substr(start, i - start);
      }
    }
    if (nameEnd - nameStart == nameLen && startsWithNoCase(tag, nameStart, name)) {
      out = decodeText(value);
      return true;
    }
    if (nameEnd == nameStart) ++i;  // stray character; always make progress
  }
  return false;
}

void Ao3HtmlScanner::beginCapture(const Capture kind, const char* endTag) {
  capture_ = kind;
  captureEnd_ = endTag;
  captureText_.clear();
}

void Ao3HtmlScanner::endCapture() {
  const Capture kind = capture_;
  capture_ = Capture::None;
  // The byte cap may have cut a multi-byte character; drop the partial tail.
  size_t end = captureText_.size();
  size_t lead = end;
  while (lead > 0 && (static_cast<unsigned char>(captureText_[lead - 1]) & 0xC0) == 0x80) --lead;
  if (lead > 0) {
    const auto first = static_cast<unsigned char>(captureText_[lead - 1]);
    const size_t need = first >= 0xF0 ? 4 : first >= 0xE0 ? 3 : first >= 0xC0 ? 2 : 1;
    if (end - (lead - 1) < need) captureText_.resize(lead - 1);
  }
  std::string text = decodeText(captureText_);
  captureText_.clear();
  if (kind == Capture::Title) {
    if (text.empty() || works_.size() >= maxWorks_) {
      pendingAuthor_ = false;
      return;
    }
    const uint32_t id = captureWorkId_;
    if (std::any_of(works_.begin(), works_.end(), [id](const Work& w) { return w.id == id; })) {
      pendingAuthor_ = false;
      return;
    }
    Work work;
    work.id = captureWorkId_;
    work.title = std::move(text);
    works_.push_back(std::move(work));
    pendingAuthor_ = true;
    bytesSinceTitle_ = 0;
  } else if (kind == Capture::Author && pendingAuthor_ && !works_.empty() && !text.empty()) {
    std::string& author = works_.back().author;
    if (author.size() + text.size() + 2 > kMaxAuthorBytes) return;
    if (!author.empty()) author += ", ";
    author += text;
  } else if (kind == Capture::DetailTitle) {
    if (detail_.title.empty()) detail_.title = std::move(text);
  } else if (kind == Capture::DetailAuthor) {
    if (!text.empty() && detail_.authors.size() < kMaxDetailAuthors) {
      detail_.authors.push_back(Link{std::move(text), std::move(captureHref_), TagGroup::Other});
    }
  } else if (kind == Capture::Tag) {
    if (!text.empty() && detail_.tags.size() < kMaxTags) {
      detail_.tags.push_back(Link{std::move(text), std::move(captureHref_), tagGroup_});
    }
  } else if (kind == Capture::Summary) {
    detail_.summary = std::move(text);
    summaryTaken_ = true;
  } else if (kind == Capture::Stat) {
    switch (stat_) {
      case Stat::Language:
        detail_.language = std::move(text);
        break;
      case Stat::Words:
        detail_.words = std::move(text);
        break;
      case Stat::Chapters:
        detail_.chapters = std::move(text);
        break;
      case Stat::Kudos:
        detail_.kudos = std::move(text);
        break;
      case Stat::None:
        break;
    }
    stat_ = Stat::None;
  }
}

namespace {
bool classHas(const std::string& cls, const char* token) { return relHas(cls, token); }

Ao3HtmlScanner::TagGroup groupForClass(const std::string& cls) {
  using G = Ao3HtmlScanner::TagGroup;
  if (classHas(cls, "rating")) return G::Rating;
  if (classHas(cls, "warning")) return G::Warning;
  if (classHas(cls, "category")) return G::Category;
  if (classHas(cls, "fandom")) return G::Fandom;
  if (classHas(cls, "relationship")) return G::Relationship;
  if (classHas(cls, "character")) return G::Character;
  if (classHas(cls, "freeform")) return G::Freeform;
  return G::Other;
}
}  // namespace

void Ao3HtmlScanner::onTag() {
  if (tagOverflow_ || tag_.empty() || tag_[0] == '!' || tag_[0] == '?') return;
  bool closing = false;
  const std::string name = tagName(tag_, closing);

  if (capture_ == Capture::Summary && (name == "p" || name == "br")) captureText_ += ' ';

  if (closing) {
    if (capture_ != Capture::None && name == captureEnd_) endCapture();
    if (name == "dl" && metaDepth_ > 0) --metaDepth_;
    if (name == "blockquote" && blockquoteDepth_ > 0) --blockquoteDepth_;
    if (name == "h3") inByline_ = false;
    if (name == "div") inSummary_ = false;
    if (name == "nav") inGreeting_ = false;
    if (name == "li") pageLink_ = PageLink::None;
    return;
  }

  if (name == "nav") {
    std::string id;
    inGreeting_ = attr(tag_, "id", id) && id == "greeting";
    return;
  }

  if (name == "script" || name == "style") {
    mode_ = Mode::RawText;
    rawEnd_ = "</" + name;
    rawMatch_ = 0;
    return;
  }

  if (name == "div") {
    std::string value;
    if (attr(tag_, "id", value) && value == "chapters") {
      done_ = true;
    } else if (!summaryTaken_ && attr(tag_, "class", value) && classHas(value, "summary") &&
               classHas(value, "module")) {
      inSummary_ = true;
    }
    return;
  }

  if (name == "dl") {
    std::string cls;
    if (metaDepth_ > 0) {
      ++metaDepth_;
    } else if (attr(tag_, "class", cls) && classHas(cls, "work") && classHas(cls, "meta")) {
      metaDepth_ = 1;
    }
    return;
  }

  if (name == "dd" && metaDepth_ > 0) {
    std::string cls;
    attr(tag_, "class", cls);
    tagGroup_ = groupForClass(cls);
    stat_ = Stat::None;
    if (classHas(cls, "language"))
      stat_ = Stat::Language;
    else if (classHas(cls, "words"))
      stat_ = Stat::Words;
    else if (classHas(cls, "chapters"))
      stat_ = Stat::Chapters;
    else if (classHas(cls, "kudos"))
      stat_ = Stat::Kudos;
    if (stat_ != Stat::None && capture_ == Capture::None) beginCapture(Capture::Stat, "dd");
    return;
  }

  if (name == "h2") {
    std::string cls;
    if (detail_.title.empty() && attr(tag_, "class", cls) && classHas(cls, "title") && capture_ == Capture::None) {
      beginCapture(Capture::DetailTitle, "h2");
    }
    return;
  }

  if (name == "h3") {
    std::string cls;
    inByline_ = attr(tag_, "class", cls) && classHas(cls, "byline");
    return;
  }

  if (name == "blockquote") {
    ++blockquoteDepth_;
    if (inSummary_ && !summaryTaken_ && capture_ == Capture::None) beginCapture(Capture::Summary, "blockquote");
    return;
  }

  if (name == "form") {
    std::string id;
    // The header's small login form (new_user_session_small) is on every
    // logged-out page; only the full form marks the login page itself.
    if (attr(tag_, "id", id) && id == "new_user") sawLoginForm_ = true;
    return;
  }

  if (name == "input") {
    std::string inputName;
    if (authenticityToken_.empty() && attr(tag_, "name", inputName) && inputName == "authenticity_token") {
      attr(tag_, "value", authenticityToken_);
    }
    return;
  }

  if (name == "li" || name == "dt") {
    // A new list row: the previous work cannot pick up more authors.
    pendingAuthor_ = false;
    pageLink_ = PageLink::None;
    std::string cls;
    if (name == "li" && attr(tag_, "class", cls)) {
      if (cls == "next") pageLink_ = PageLink::Next;
      if (cls == "previous") pageLink_ = PageLink::Prev;
    }
    return;
  }

  if (name != "a") return;
  if (capture_ != Capture::None && captureEnd_ == "a") endCapture();  // unclosed anchor
  if (capture_ != Capture::None) return;                              // link inside a summary or stat

  std::string href;
  if (!attr(tag_, "href", href)) return;
  std::string rel;
  attr(tag_, "rel", rel);

  if (metaDepth_ > 0) {
    std::string cls;
    if (attr(tag_, "class", cls) && classHas(cls, "tag")) {
      captureHref_ = href;
      beginCapture(Capture::Tag);
    }
    return;
  }
  if (inByline_ && relHas(rel, "author")) {
    captureHref_ = href;
    beginCapture(Capture::DetailAuthor);
    return;
  }

  if (relHas(rel, "next") || pageLink_ == PageLink::Next) {
    if (nextHref_.empty()) nextHref_ = href;
    pageLink_ = PageLink::None;
    return;
  }
  if (relHas(rel, "prev") || pageLink_ == PageLink::Prev) {
    if (prevHref_.empty()) prevHref_ = href;
    pageLink_ = PageLink::None;
    return;
  }

  uint32_t id = 0;
  if (blockquoteDepth_ == 0 && parseWorkHref(href, id)) {
    captureWorkId_ = id;
    beginCapture(Capture::Title);
    return;
  }

  if (relHas(rel, "author")) {
    if (pendingAuthor_) beginCapture(Capture::Author);
    return;
  }

  static constexpr char kUsers[] = "/users/";
  constexpr size_t kUsersLen = sizeof(kUsers) - 1;
  if (href.compare(0, kUsersLen, kUsers) == 0) {
    const std::string rest = href.substr(kUsersLen);
    const size_t slash = rest.find('/');
    if (slash == std::string::npos) {
      if (inGreeting_ && loggedInUser_.empty() && !rest.empty() && rest.find('?') == std::string::npos) {
        loggedInUser_ = rest;
      }
    } else if (pendingAuthor_ && bytesSinceTitle_ < kPseudAuthorWindow && works_.back().author.empty() &&
               rest.compare(slash, 8, "/pseuds/") == 0 && rest.find('/', slash + 8) == std::string::npos) {
      beginCapture(Capture::Author);
    }
  }
}

void Ao3HtmlScanner::feed(const char* data, const size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const char c = data[i];
    if (bytesSinceTitle_ != SIZE_MAX) ++bytesSinceTitle_;
    switch (mode_) {
      case Mode::Text:
        if (c == '<') {
          mode_ = Mode::Tag;
          tag_.clear();
          tagOverflow_ = false;
          quote_ = 0;
        } else if (capture_ != Capture::None &&
                   captureText_.size() < (capture_ == Capture::Summary ? kMaxSummaryBytes : kMaxCaptureBytes)) {
          captureText_ += c;
        }
        break;
      case Mode::Tag:
        if (quote_) {
          if (c == quote_) quote_ = 0;
        } else if (c == '"' || c == '\'') {
          // Only quotes that open an attribute value count; a lone quote in
          // text-like tags would otherwise swallow the rest of the page.
          if (!tag_.empty() && tag_.back() == '=') quote_ = c;
        } else if (c == '>') {
          mode_ = Mode::Text;
          onTag();
          break;
        }
        if (tag_.size() < kMaxTagBytes) {
          tag_ += c;
        } else {
          tagOverflow_ = true;
        }
        break;
      case Mode::RawText: {
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == rawEnd_[rawMatch_]) {
          if (++rawMatch_ == rawEnd_.size()) {
            // Consume the rest of the end tag as an ordinary tag.
            mode_ = Mode::Tag;
            tag_ = rawEnd_.substr(1);
            tagOverflow_ = false;
            quote_ = 0;
            rawMatch_ = 0;
          }
        } else {
          rawMatch_ = lower == rawEnd_[0] ? 1 : 0;
        }
        break;
      }
    }
  }
}
