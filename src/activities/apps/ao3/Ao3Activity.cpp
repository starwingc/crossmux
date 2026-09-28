#include "Ao3Activity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "Ao3Store.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
constexpr fui::ActionId ACTION_CANCEL = 2;
constexpr char kSaveFolder[] = "/AO3";
constexpr char kCacheFolder[] = "/AO3/.cache";
// AO3 index pages show 20 works; leave room for longer pages.
constexpr size_t kMaxWorksPerPage = 40;
// Author -> tag -> author ... chains are unbounded; forget the oldest views.
constexpr size_t kMaxViews = 12;
constexpr int DOWNLOAD_PROGRESS_STEP_PERCENT = 5;
constexpr unsigned long DOWNLOAD_PROGRESS_MIN_UPDATE_MS = 5000;

// Filter option tables (values from AO3's filter form). Index 0 is always
// "any / AO3 default" and adds no parameter.
constexpr const char* kSortColumns[] = {"",           "revised_at",      "created_at",     "kudos_count",     "hits",
                                        "word_count", "bookmarks_count", "comments_count", "title_to_sort_on"};
constexpr StrId kSortLabels[] = {
    StrId::STR_AO3_DEFAULT,        StrId::STR_AO3_SORT_UPDATED,  StrId::STR_AO3_SORT_POSTED,
    StrId::STR_AO3_SORT_KUDOS,     StrId::STR_AO3_SORT_HITS,     StrId::STR_AO3_SORT_WORDS,
    StrId::STR_AO3_SORT_BOOKMARKS, StrId::STR_AO3_SORT_COMMENTS, StrId::STR_AO3_SORT_TITLE};
// Rating tag ids: General 10, Teen 11, Mature 12, Explicit 13, Not Rated 9.
constexpr int kRatings[] = {0, 10, 11, 12, 13, 9};
constexpr StrId kRatingLabels[] = {
    StrId::STR_AO3_ANY,           StrId::STR_AO3_RATING_GENERAL,  StrId::STR_AO3_RATING_TEEN,
    StrId::STR_AO3_RATING_MATURE, StrId::STR_AO3_RATING_EXPLICIT, StrId::STR_AO3_RATING_NOT_RATED};
constexpr StrId kCompleteLabels[] = {StrId::STR_AO3_ANY, StrId::STR_AO3_COMPLETE, StrId::STR_AO3_IN_PROGRESS};
constexpr const char* kLanguages[] = {"", "en", "zh", "ja", "ko"};
constexpr StrId kLanguageLabels[] = {StrId::STR_AO3_ANY, StrId::STR_AO3_LANG_EN, StrId::STR_AO3_LANG_ZH,
                                     StrId::STR_AO3_LANG_JA, StrId::STR_AO3_LANG_KO};
static_assert(sizeof(kSortColumns) / sizeof(kSortColumns[0]) == sizeof(kSortLabels) / sizeof(kSortLabels[0]),
              "sort tables");
static_assert(sizeof(kRatings) / sizeof(kRatings[0]) == sizeof(kRatingLabels) / sizeof(kRatingLabels[0]),
              "rating tables");
static_assert(sizeof(kLanguages) / sizeof(kLanguages[0]) == sizeof(kLanguageLabels) / sizeof(kLanguageLabels[0]),
              "language tables");

template <typename T, size_t N>
constexpr uint8_t countOf(const T (&)[N]) {
  return static_cast<uint8_t>(N);
}

const char* label(const StrId id) { return I18n::getInstance().get(id); }

enum FilterRow : int { RowSort, RowRating, RowComplete, RowLanguage, RowInclude, RowExclude, RowApply, RowClear };

void appendPercent(std::string& out, const unsigned char c) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  out += '%';
  out += kHex[c >> 4];
  out += kHex[c & 0x0F];
}

std::string urlEncode(const std::string& s) {
  std::string out;
  out.reserve(s.size() * 3);
  for (const unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      appendPercent(out, c);
    }
  }
  return out;
}

// AO3 tag URLs escape a few characters with *x* sequences before percent
// encoding: "Gimli/Legolas" -> "Gimli*s*Legolas", "J. R. R." -> "J*d*%20R*d*...".
std::string tagPathSegment(const std::string& tag) {
  std::string out;
  out.reserve(tag.size() * 3);
  for (const unsigned char c : tag) {
    switch (c) {
      case '/':
        out += "*s*";
        break;
      case '&':
        out += "*a*";
        break;
      case '.':
        out += "*d*";
        break;
      case '?':
        out += "*q*";
        break;
      case '#':
        out += "*h*";
        break;
      default:
        if (isalnum(c) || c == '-' || c == '_' || c == '~' || c == '*') {
          out += static_cast<char>(c);
        } else {
          appendPercent(out, c);
        }
    }
  }
  return out;
}

std::string trimmed(const std::string& s) {
  size_t b = 0;
  size_t e = s.size();
  while (b < e && s[b] == ' ') ++b;
  while (e > b && s[e - 1] == ' ') --e;
  return s.substr(b, e - b);
}

// Accepts "123456", "archiveofourown.org/works/123456/chapters/7", etc.
uint32_t parseWorkId(const std::string& input) {
  size_t start = input.find("/works/");
  start = start == std::string::npos ? 0 : start + 7;
  while (start < input.size() && input[start] == ' ') ++start;
  uint32_t id = 0;
  size_t digits = 0;
  size_t i = start;
  for (; i < input.size() && isdigit(static_cast<unsigned char>(input[i])); ++i) {
    if (++digits > 9) return 0;
    id = id * 10 + static_cast<uint32_t>(input[i] - '0');
  }
  if (digits == 0) return 0;
  // A bare ID must be only digits; a link may continue with /chapters etc.
  if (start == 0) {
    while (i < input.size() && input[i] == ' ') ++i;
    if (i != input.size()) return 0;
  }
  return id;
}

const char* groupLabel(const Ao3HtmlScanner::TagGroup group) {
  using G = Ao3HtmlScanner::TagGroup;
  switch (group) {
    case G::Rating:
      return tr(STR_AO3_TAG_RATING);
    case G::Warning:
      return tr(STR_AO3_TAG_WARNING);
    case G::Category:
      return tr(STR_AO3_TAG_CATEGORY);
    case G::Fandom:
      return tr(STR_AO3_TAG_FANDOM);
    case G::Relationship:
      return tr(STR_AO3_TAG_RELATIONSHIP);
    case G::Character:
      return tr(STR_AO3_TAG_CHARACTER);
    case G::Freeform:
      return tr(STR_AO3_TAG_FREEFORM);
    case G::Other:
      break;
  }
  return nullptr;
}

std::string cachePathFor(const uint32_t workId) {
  char name[32];
  snprintf(name, sizeof(name), "/%u.epub", static_cast<unsigned>(workId));
  return std::string(kCacheFolder) + name;
}
}  // namespace

Ao3Activity::Ao3Activity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Ao3", renderer, mappedInput), UiAppHost(renderer) {}

void Ao3Activity::onEnter() {
  Activity::onEnter();
  AO3_STORE.loadFromFile();
  listNav.reset();
  resetUi();
  app.on(ACTION_ROW, &Ao3Activity::onRowEvent, this);
  app.on(ACTION_CANCEL, &Ao3Activity::onCancelEvent, this);
  app.setScreen(&Ao3Activity::rootScreen, this);
  if (!client.begin()) {
    showErrorText(errorText(Ao3Client::Status::OutOfMemory));
    return;
  }
  showState(State::Menu);
}

void Ao3Activity::onExit() {
  Activity::onExit();
  client.end();
  views.clear();
  works.clear();
  parkedWorks.clear();
  detail = Ao3HtmlScanner::Detail{};
  rowItems.clear();
  rowText.clear();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

// ---------------------------------------------------------------- rows

const char* Ao3Activity::ownText(std::string text) {
  rowText.push_back(std::move(text));
  return rowText.back().c_str();
}

void Ao3Activity::pushRow(const char* labelText, const char* subtitle, const char* value) {
  fui::ListItem item;
  item.label = labelText;
  item.subtitle = subtitle;
  item.value = value;
  item.actionValue = static_cast<int16_t>(rowItems.size());
  rowItems.push_back(item);
}

void Ao3Activity::rebuildRows() {
  closeRouting();
  rowItems.clear();
  rowText.clear();
  // ownText() hands out c_str() pointers: no reallocation while rows are built.
  rowText.reserve(8);
  switch (state) {
    case State::Menu:
      rebuildMenuRows();
      break;
    case State::List:
      rebuildListRows();
      break;
    case State::Detail:
      rebuildDetailRows();
      break;
    case State::Filter:
      rebuildFilterRows();
      break;
    default:
      break;
  }
  listNav.reset();
  if (selectorIndex >= rowCount()) selectorIndex = 0;
  listNav.selected = selectorIndex;
  listNav.follow(rowCount());
}

void Ao3Activity::rebuildMenuRows() {
  menuRows.clear();
  const bool loggedIn = AO3_STORE.hasSession() || AO3_STORE.hasCredentials();
  menuRows.reserve(11);
  if (loggedIn) {
    menuRows.push_back({MenuAction::MyWorks, tr(STR_AO3_MY_WORKS)});
    menuRows.push_back({MenuAction::MarkedForLater, tr(STR_AO3_MARKED_FOR_LATER)});
    menuRows.push_back({MenuAction::History, tr(STR_AO3_HISTORY)});
    menuRows.push_back({MenuAction::Bookmarks, tr(STR_AO3_BOOKMARKS)});
    menuRows.push_back({MenuAction::Subscriptions, tr(STR_AO3_SUBSCRIPTIONS)});
  } else {
    menuRows.push_back({MenuAction::Login, tr(STR_AO3_LOGIN)});
  }
  menuRows.push_back({MenuAction::Search, tr(STR_AO3_SEARCH)});
  menuRows.push_back({MenuAction::TagSearch, tr(STR_AO3_TAG_SEARCH)});
  menuRows.push_back({MenuAction::AuthorSearch, tr(STR_AO3_AUTHOR_SEARCH)});
  menuRows.push_back({MenuAction::ByLink, tr(STR_AO3_BY_LINK)});
  if (loggedIn) menuRows.push_back({MenuAction::Logout, tr(STR_AO3_LOGOUT)});

  rowItems.reserve(menuRows.size());
  for (const auto& row : menuRows) {
    const char* subtitle = nullptr;
    if (row.action == MenuAction::Logout && !AO3_STORE.user().empty()) subtitle = AO3_STORE.user().c_str();
    pushRow(row.label, subtitle);
  }
}

void Ao3Activity::rebuildListRows() {
  if (views.empty()) return;
  const View& view = views.back();
  rowItems.reserve(works.size() + 3);
  if (view.filterable) pushRow(tr(STR_AO3_FILTER), nullptr, view.filter.active() ? tr(STR_AO3_ON) : tr(STR_AO3_NONE));
  if (!prevPath.empty()) pushRow(tr(STR_PREV_PAGE));
  for (const auto& work : works) pushRow(work.title.c_str(), work.author.empty() ? nullptr : work.author.c_str());
  if (!nextPath.empty()) pushRow(tr(STR_NEXT_PAGE));
}

void Ao3Activity::rebuildDetailRows() {
  rowItems.reserve(detail.authors.size() + detail.tags.size() + 2);
  // "12,345 words, 3/10 chapters, Kudos 678"
  std::string stats;
  if (!detail.words.empty()) stats += detail.words + " " + tr(STR_AO3_WORDS);
  if (!detail.chapters.empty()) {
    if (!stats.empty()) stats += ", ";
    stats += detail.chapters + " " + tr(STR_AO3_CHAPTERS);
  }
  if (!detail.kudos.empty()) {
    if (!stats.empty()) stats += ", ";
    stats += "Kudos " + detail.kudos;
  }
  pushRow(tr(STR_AO3_READ_ONLINE), stats.empty() ? nullptr : ownText(std::move(stats)));
  const bool justSaved = !views.empty() && views.back().workId == savedWorkId;
  pushRow(tr(STR_AO3_SAVE),
          justSaved ? tr(STR_AO3_SAVED_TO_FOLDER) : (detail.language.empty() ? nullptr : detail.language.c_str()));
  for (const auto& author : detail.authors) pushRow(author.name.c_str(), tr(STR_AO3_AUTHOR));
  for (const auto& tag : detail.tags) pushRow(tag.name.c_str(), groupLabel(tag.group));
}

void Ao3Activity::rebuildFilterRows() {
  rowItems.reserve(8);
  pushRow(tr(STR_AO3_SORT), nullptr, label(kSortLabels[editFilter.sort]));
  pushRow(tr(STR_AO3_RATING), nullptr, label(kRatingLabels[editFilter.rating]));
  pushRow(tr(STR_AO3_STATUS), nullptr, label(kCompleteLabels[editFilter.complete]));
  pushRow(tr(STR_AO3_LANGUAGE), nullptr, label(kLanguageLabels[editFilter.language]));
  pushRow(tr(STR_AO3_INCLUDE_TAGS), editFilter.includeTags.empty() ? tr(STR_AO3_NONE) : editFilter.includeTags.c_str());
  pushRow(tr(STR_AO3_EXCLUDE_TAGS), editFilter.excludeTags.empty() ? tr(STR_AO3_NONE) : editFilter.excludeTags.c_str());
  pushRow(tr(STR_AO3_APPLY_FILTER));
  pushRow(tr(STR_AO3_CLEAR_FILTER));
}

// Enters a list-like state (or Menu) with rows rebuilt for it.
void Ao3Activity::showState(const State next) {
  switch (next) {
    case State::Menu:
      selectorIndex = menuSelection;
      break;
    case State::List:
    case State::Detail:
      selectorIndex = views.empty() ? 0 : views.back().selection;
      break;
    case State::Filter:
      selectorIndex = filterSelection;
      break;
    default:
      break;
  }
  state = next;
  rebuildRows();
  requestUpdate();
}

void Ao3Activity::rememberSelection() {
  if (state == State::Menu) menuSelection = selectorIndex;
  if (state == State::Filter) filterSelection = selectorIndex;
  if ((state == State::List || state == State::Detail) && !views.empty()) views.back().selection = selectorIndex;
}

void Ao3Activity::moveSelection(const int index) {
  selectorIndex = index;
  rememberSelection();
  listNav.selected = index;
  listNav.follow(rowCount());
  requestUpdate();
}

// ---------------------------------------------------------------- input

void Ao3Activity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<Ao3Activity*>(user);
  const State s = self->state;
  if (s != State::Menu && s != State::List && s != State::Detail && s != State::Filter) return;
  if (event.value < 0 || event.value >= self->rowCount()) return;
  self->selectorIndex = event.value;
  self->app.clearTapFlash();
  self->activateSelected();
}

void Ao3Activity::onCancelEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<Ao3Activity*>(user);
  if (self->state != State::Downloading) return;
  self->app.clearTapFlash();
  self->cancelDownload = true;
}

void Ao3Activity::activateSelected() {
  rememberSelection();
  switch (state) {
    case State::Menu:
      if (selectorIndex >= 0 && selectorIndex < static_cast<int>(menuRows.size())) {
        activateMenu(menuRows[selectorIndex].action);
      }
      break;
    case State::List:
      activateListRow(selectorIndex);
      break;
    case State::Detail:
      activateDetailRow(selectorIndex);
      break;
    case State::Filter:
      activateFilterRow(selectorIndex);
      break;
    default:
      break;
  }
}

void Ao3Activity::loop() {
  if (state == State::Child || state == State::Busy || state == State::Downloading) return;

  int tx = 0;
  int ty = 0;
  if (state == State::Error) {
    const bool confirm =
        mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(tx, ty);
    if (confirm && pending != Pending::None) {
      runWhenOnline(pending);
    } else if (confirm || mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      const Pending failed = pending;
      pending = Pending::None;
      pendingPassword.clear();
      // A view that never loaded (unknown tag, missing author) is dropped.
      if (failed == Pending::Load && !views.empty() && !views.back().loaded) {
        goBack();
      } else {
        showState(stateBeforeError);
      }
    }
    return;
  }

  if (state == State::Saved) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(tx, ty)) {
      openBook(savedPath);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      showState(stateAfterDownload);
    }
    return;
  }

  // Menu, List, Detail, Filter.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    goBack();
    return;
  }

  const auto route = routeTouch(mappedInput);
  if (route.routed) {
    if (app.invalidated()) requestUpdate();
    if (route) return;
  }

  const int count = rowCount();
  if (count == 0) return;
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int delta = swipe == MappedInputManager::SwipeDir::Up ? listNav.visibleRows : -listNav.visibleRows;
    if (listNav.scrollBy(delta, count)) requestUpdate();
    return;
  }
  buttonNavigator.onNextRelease([this, count] { moveSelection(ButtonNavigator::nextIndex(selectorIndex, count)); });
  buttonNavigator.onPreviousRelease(
      [this, count] { moveSelection(ButtonNavigator::previousIndex(selectorIndex, count)); });
  buttonNavigator.onNextContinuous(
      [this, count] { moveSelection(ButtonNavigator::nextPageIndex(selectorIndex, count, listNav.visibleRows)); });
  buttonNavigator.onPreviousContinuous(
      [this, count] { moveSelection(ButtonNavigator::previousPageIndex(selectorIndex, count, listNav.visibleRows)); });
}

// ---------------------------------------------------------------- actions

void Ao3Activity::activateMenu(const MenuAction action) {
  const std::string userPath = "/users/" + urlEncode(AO3_STORE.user());
  const bool ownList = action == MenuAction::MyWorks || action == MenuAction::MarkedForLater ||
                       action == MenuAction::History || action == MenuAction::Bookmarks ||
                       action == MenuAction::Subscriptions;
  if (ownList && AO3_STORE.user().empty()) {
    // Credentials without a user name: the session must be rebuilt.
    client.logout();
    stateBeforeError = State::Menu;
    pending = Pending::None;
    showError(Ao3Client::Status::NotLoggedIn);
    return;
  }

  switch (action) {
    case MenuAction::Login:
      promptText(tr(STR_AO3_LOGIN_NAME), AO3_STORE.login(), 100, InputType::Text, &Ao3Activity::onLoginName);
      return;
    case MenuAction::MyWorks:
      openList(tr(STR_AO3_MY_WORKS), userPath + "/works", true, false, true);
      return;
    case MenuAction::MarkedForLater:
      openList(tr(STR_AO3_MARKED_FOR_LATER), userPath + "/readings?show=to-read", false, false, true);
      return;
    case MenuAction::History:
      openList(tr(STR_AO3_HISTORY), userPath + "/readings", false, false, true);
      return;
    case MenuAction::Bookmarks:
      openList(tr(STR_AO3_BOOKMARKS), userPath + "/bookmarks", false, false, true);
      return;
    case MenuAction::Subscriptions:
      openList(tr(STR_AO3_SUBSCRIPTIONS), userPath + "/subscriptions?type=works", false, false, true);
      return;
    case MenuAction::Search:
      promptText(tr(STR_AO3_SEARCH), "", 120, InputType::Text, &Ao3Activity::onSearchText);
      return;
    case MenuAction::TagSearch:
      promptText(tr(STR_AO3_TAG_NAME), "", 150, InputType::Text, &Ao3Activity::onTagText);
      return;
    case MenuAction::AuthorSearch:
      promptText(tr(STR_AO3_AUTHOR_NAME), "", 60, InputType::Text, &Ao3Activity::onAuthorText);
      return;
    case MenuAction::ByLink:
      promptText(tr(STR_AO3_WORK_LINK), "", 200, InputType::Url, &Ao3Activity::onWorkLink);
      return;
    case MenuAction::Logout:
      client.logout();
      menuSelection = 0;
      showState(State::Menu);
      return;
  }
}

void Ao3Activity::activateListRow(const int row) {
  if (views.empty()) return;
  View& view = views.back();
  int index = row;
  if (view.filterable) {
    if (index == 0) {
      editFilter = view.filter;
      filterSelection = 0;
      showState(State::Filter);
      return;
    }
    --index;
  }
  if (!prevPath.empty()) {
    if (index == 0) {
      view.pagePath = prevPath;
      view.selection = 0;
      runWhenOnline(Pending::Load);
      return;
    }
    --index;
  }
  if (index >= static_cast<int>(works.size())) {
    if (nextPath.empty()) return;
    view.pagePath = nextPath;
    view.selection = 0;
    runWhenOnline(Pending::Load);
    return;
  }
  openDetail(works[index].id, works[index].title);
}

void Ao3Activity::activateDetailRow(const int row) {
  if (views.empty()) return;
  actionWorkId = views.back().workId;
  actionTitle = detail.title;
  stateAfterDownload = State::Detail;
  if (row == 0) {
    runWhenOnline(Pending::Read);
    return;
  }
  if (row == 1) {
    runWhenOnline(Pending::Save);
    return;
  }
  size_t index = static_cast<size_t>(row - 2);
  if (index < detail.authors.size()) {
    // Byline links point at the pseud; its works live one level down.
    const auto& author = detail.authors[index];
    openList(author.name.c_str(), author.href + "/works", true);
    return;
  }
  index -= detail.authors.size();
  if (index < detail.tags.size()) {
    const auto& tag = detail.tags[index];
    openList(tag.name.c_str(), tag.href, true);
  }
}

void Ao3Activity::activateFilterRow(const int row) {
  switch (row) {
    case RowSort:
      editFilter.sort = static_cast<uint8_t>((editFilter.sort + 1) % countOf(kSortColumns));
      break;
    case RowRating:
      editFilter.rating = static_cast<uint8_t>((editFilter.rating + 1) % countOf(kRatings));
      break;
    case RowComplete:
      editFilter.complete = static_cast<uint8_t>((editFilter.complete + 1) % countOf(kCompleteLabels));
      break;
    case RowLanguage:
      editFilter.language = static_cast<uint8_t>((editFilter.language + 1) % countOf(kLanguages));
      break;
    case RowInclude:
      promptText(tr(STR_AO3_INCLUDE_TAGS), editFilter.includeTags, 200, InputType::Text, &Ao3Activity::onIncludeTags);
      return;
    case RowExclude:
      promptText(tr(STR_AO3_EXCLUDE_TAGS), editFilter.excludeTags, 200, InputType::Text, &Ao3Activity::onExcludeTags);
      return;
    case RowApply:
    case RowClear:
      if (views.empty()) return;
      views.back().filter = row == RowApply ? editFilter : Filter{};
      views.back().pagePath.clear();
      views.back().selection = 0;
      state = State::List;
      runWhenOnline(Pending::Load);
      return;
    default:
      return;
  }
  showState(State::Filter);  // redraw the changed value
}

void Ao3Activity::goBack() {
  if (state == State::Filter) {
    showState(State::List);
    return;
  }
  if (state == State::Menu) {
    activityManager.goToApps();
    return;
  }
  if (views.empty()) {
    showState(State::Menu);
    return;
  }

  const int popped = static_cast<int>(views.size()) - 1;
  views.pop_back();
  if (listDepth == popped) listDepth = -1;
  if (detailDepth == popped) detailDepth = -1;
  if (parkedDepth == popped) parkedDepth = -1;
  if (views.empty()) {
    works.clear();
    showState(State::Menu);
    return;
  }

  const int top = static_cast<int>(views.size()) - 1;
  if (views.back().kind == ViewKind::Detail) {
    if (detailDepth == top) {
      showState(State::Detail);
    } else {
      runWhenOnline(Pending::Load);
    }
    return;
  }
  if (listDepth != top && parkedDepth == top) {
    works.swap(parkedWorks);
    nextPath.swap(parkedNext);
    prevPath.swap(parkedPrev);
    std::swap(listDepth, parkedDepth);
  }
  if (listDepth == top) {
    showState(State::List);
  } else {
    runWhenOnline(Pending::Load);
  }
}

// ---------------------------------------------------------------- navigation

void Ao3Activity::pushView(View view) {
  if (views.size() >= kMaxViews) {
    // Forget the oldest view; indices of the kept ones shift down by one.
    views.erase(views.begin());
    listDepth = listDepth > 0 ? listDepth - 1 : -1;
    detailDepth = detailDepth > 0 ? detailDepth - 1 : -1;
    parkedDepth = parkedDepth > 0 ? parkedDepth - 1 : -1;
  }
  views.push_back(std::move(view));
}

void Ao3Activity::openList(const char* title, const std::string& path, const bool filterable, const bool isSearch,
                           const bool needsLogin) {
  View view;
  view.kind = ViewKind::List;
  view.title = title;
  view.basePath = path;
  view.filterable = filterable;
  view.isSearch = isSearch;
  view.needsLogin = needsLogin;
  pushView(std::move(view));
  runWhenOnline(Pending::Load);
}

void Ao3Activity::openDetail(const uint32_t workId, const std::string& title) {
  View view;
  view.kind = ViewKind::Detail;
  view.title = title.empty() ? "#" + std::to_string(workId) : title;
  view.workId = workId;
  char path[32];
  snprintf(path, sizeof(path), "/works/%u", static_cast<unsigned>(workId));
  view.basePath = path;
  pushView(std::move(view));
  runWhenOnline(Pending::Load);
}

std::string Ao3Activity::filterQuery(const Filter& f, const bool isSearch) {
  std::string q;
  const auto add = [&q](const char* key, const std::string& value) {
    if (!q.empty()) q += '&';
    q += key;
    q += '=';
    q += value;
  };
  if (!isSearch) add("commit", "Sort+and+Filter");
  if (f.sort > 0) add("work_search%5Bsort_column%5D", kSortColumns[f.sort]);
  if (f.rating > 0) {
    // The filter sidebar takes a checkbox list; advanced search a single value.
    add(isSearch ? "work_search%5Brating_ids%5D" : "include_work_search%5Brating_ids%5D%5B%5D",
        std::to_string(kRatings[f.rating]));
  }
  if (f.complete == 1) add("work_search%5Bcomplete%5D", "T");
  if (f.complete == 2) add("work_search%5Bcomplete%5D", "F");
  if (f.language > 0) add("work_search%5Blanguage_id%5D", kLanguages[f.language]);
  if (!f.includeTags.empty()) add("work_search%5Bother_tag_names%5D", urlEncode(f.includeTags));
  if (!f.excludeTags.empty()) add("work_search%5Bexcluded_tag_names%5D", urlEncode(f.excludeTags));
  return q;
}

std::string Ao3Activity::viewUrl(const View& view) const {
  if (view.kind == ViewKind::Detail) return view.basePath + "?view_adult=true";
  if (!view.pagePath.empty()) return view.pagePath;  // AO3 page links keep the filter
  if (!view.filter.active()) return view.basePath;
  const char joiner = view.basePath.find('?') == std::string::npos ? '?' : '&';
  return view.basePath + joiner + filterQuery(view.filter, view.isSearch);
}

// ---------------------------------------------------------------- prompts

void Ao3Activity::promptText(const char* title, std::string initial, const size_t maxLength, const InputType type,
                             void (Ao3Activity::*onText)(const std::string&)) {
  const State returnTo = state;
  state = State::Child;
  requestUpdate();
  startActivityForResultWith<KeyboardEntryActivity>(
      [this, returnTo, onText](const ActivityResult& result) {
        state = returnTo;
        if (result.isCancelled) {
          showState(returnTo);
          return;
        }
        (this->*onText)(std::get<KeyboardResult>(result.data).text);
      },
      title, std::move(initial), maxLength, type);
}

void Ao3Activity::onLoginName(const std::string& text) {
  pendingLogin = trimmed(text);
  if (pendingLogin.empty()) {
    showState(State::Menu);
    return;
  }
  promptText(tr(STR_AO3_LOGIN_PASSWORD), "", 128, InputType::Password, &Ao3Activity::onLoginPassword);
}

void Ao3Activity::onLoginPassword(const std::string& text) {
  if (text.empty()) {
    showState(State::Menu);
    return;
  }
  pendingPassword = text;
  runWhenOnline(Pending::Login);
}

void Ao3Activity::onSearchText(const std::string& text) {
  const std::string query = trimmed(text);
  if (query.empty()) {
    showState(State::Menu);
    return;
  }
  openList(query.c_str(), "/works/search?work_search%5Bquery%5D=" + urlEncode(query), true, true);
}

void Ao3Activity::onTagText(const std::string& text) {
  const std::string tag = trimmed(text);
  if (tag.empty()) {
    showState(State::Menu);
    return;
  }
  openList(tag.c_str(), "/tags/" + tagPathSegment(tag) + "/works", true);
}

void Ao3Activity::onAuthorText(const std::string& text) {
  const std::string name = trimmed(text);
  if (name.empty()) {
    showState(State::Menu);
    return;
  }
  openList(name.c_str(), "/users/" + urlEncode(name) + "/works", true);
}

void Ao3Activity::onWorkLink(const std::string& text) {
  const uint32_t id = parseWorkId(trimmed(text));
  if (id == 0) {
    stateBeforeError = State::Menu;
    pending = Pending::None;
    showErrorText(tr(STR_AO3_ERR_BAD_LINK));
    return;
  }
  openDetail(id, "");
}

void Ao3Activity::onIncludeTags(const std::string& text) {
  editFilter.includeTags = trimmed(text);
  showState(State::Filter);
}

void Ao3Activity::onExcludeTags(const std::string& text) {
  editFilter.excludeTags = trimmed(text);
  showState(State::Filter);
}

// ---------------------------------------------------------------- network

bool Ao3Activity::wifiReady() const { return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0); }

void Ao3Activity::runWhenOnline(const Pending action) {
  pending = action;
  if (wifiReady()) {
    runPending();
    return;
  }
  const State returnTo = state == State::Child || state == State::Error ? State::Menu : state;
  state = State::Child;
  requestUpdate();
  startActivityForResultWith<WifiSelectionActivity>([this, returnTo](const ActivityResult& result) {
    if (result.isCancelled || !wifiReady()) {
      stateBeforeError = returnTo;
      showErrorText(tr(STR_WIFI_CONN_FAILED));
      return;
    }
    runPending();
  });
}

void Ao3Activity::runPending() {
  switch (pending) {
    case Pending::Login:
      doLogin();
      break;
    case Pending::Load:
      doLoad();
      break;
    case Pending::Save:
      doDownload(false);
      break;
    case Pending::Read:
      doDownload(true);
      break;
    case Pending::None:
      break;
  }
}

void Ao3Activity::doLogin() {
  stateBeforeError = State::Menu;
  showBusy(tr(STR_AO3_LOGGING_IN));
  const auto status = client.login(pendingLogin, pendingPassword);
  if (status != Ao3Client::Status::Ok) {
    LOG_ERR("AO3", "Login failed: %s", Ao3Client::statusName(status));
    // Wrong credentials need new input, not a retry of the same ones; other
    // failures keep the typed password so Retry can resend it.
    if (status == Ao3Client::Status::BadCredentials) {
      pending = Pending::None;
      pendingPassword.clear();
    }
    showError(status);
    return;
  }
  pending = Pending::None;
  pendingPassword.clear();
  menuSelection = 0;
  showState(State::Menu);
}

void Ao3Activity::doLoad() {
  if (views.empty()) return;
  const int depth = static_cast<int>(views.size()) - 1;
  View& view = views.back();
  const bool isDetail = view.kind == ViewKind::Detail;
  // Where Back from an error lands: this view as it was, or the one below.
  if (view.loaded) {
    stateBeforeError = isDetail ? State::Detail : State::List;
  } else if (depth == 0) {
    stateBeforeError = State::Menu;
  } else {
    stateBeforeError = views[depth - 1].kind == ViewKind::Detail ? State::Detail : State::List;
  }
  showBusy(tr(STR_LOADING));

  // Public pages still carry the session when there is one, so restricted
  // works show up for logged-in users.
  const bool requireLogin = view.needsLogin || AO3_STORE.hasCredentials();
  Ao3HtmlScanner scanner(isDetail ? 0 : kMaxWorksPerPage);
  std::string finalPath;
  const auto status = client.fetchPage(viewUrl(view), scanner, requireLogin, &finalPath);
  if (status != Ao3Client::Status::Ok) {
    LOG_ERR("AO3", "Load failed: %s", Ao3Client::statusName(status));
    if (status == Ao3Client::Status::NotLoggedIn && AO3_STORE.hasCredentials()) {
      // The stored password no longer works: forget it so the menu offers
      // Log In again instead of retrying a dead session.
      pending = Pending::None;
      client.logout();
    }
    showError(status);
    return;
  }
  pending = Pending::None;
  view.loaded = true;

  if (isDetail) {
    detail = std::move(scanner.detail());
    if (!detail.title.empty()) view.title = detail.title;
    detailDepth = depth;
    showState(State::Detail);
    return;
  }

  // A tag synonym redirects to its canonical tag; filter against that one.
  if (!view.isSearch && view.pagePath.empty() && !finalPath.empty()) {
    view.basePath = finalPath.substr(0, finalPath.find('?'));
  }
  if (listDepth >= 0 && listDepth != depth) {
    // Keep the older list (under a detail) so Back does not refetch it.
    parkedWorks.swap(works);
    parkedNext.swap(nextPath);
    parkedPrev.swap(prevPath);
    parkedDepth = listDepth;
  }
  works = std::move(scanner.works());
  nextPath = scanner.nextHref();
  prevPath = scanner.prevHref();
  listDepth = depth;
  showState(State::List);
}

bool Ao3Activity::downloadWithProgress(const bool online, Ao3Client::Status& status) {
  state = State::Downloading;
  statusMessage = actionTitle.empty() ? "#" + std::to_string(actionWorkId) : actionTitle;
  downloadProgress = downloadTotal = 0;
  cancelDownload = false;
  goHomeAfterCancel = false;
  requestUpdate(true);

  char cacheName[24];
  snprintf(cacheName, sizeof(cacheName), "%u.epub", static_cast<unsigned>(actionWorkId));
  int lastRenderedPercent = -1;
  unsigned long lastProgressUpdateMs = 0;
  status = client.downloadEpub(
      actionWorkId, actionTitle, online ? kCacheFolder : kSaveFolder, online ? cacheName : nullptr,
      [this, &lastRenderedPercent, &lastProgressUpdateMs](const size_t done, const size_t total) {
        downloadProgress = done;
        downloadTotal = total;
        // loop() is blocked for the whole download; pump input here so Back,
        // the Cancel button or the home gesture can abort mid-transfer.
        mappedInput.update();
        if (mappedInput.wasReleased(MappedInputManager::Button::Back)) cancelDownload = true;
        if (mappedInput.wasHomeGesture()) {
          cancelDownload = true;
          goHomeAfterCancel = true;
        }
        routeTouch(mappedInput);
        const int percent = total > 0 ? static_cast<int>(static_cast<uint64_t>(done) * 100 / total) : 0;
        const unsigned long now = millis();
        if (percent >= 100 || lastRenderedPercent < 0 ||
            percent >= lastRenderedPercent + DOWNLOAD_PROGRESS_STEP_PERCENT ||
            now - lastProgressUpdateMs >= DOWNLOAD_PROGRESS_MIN_UPDATE_MS) {
          lastRenderedPercent = percent;
          lastProgressUpdateMs = now;
          requestUpdate(true);
        }
        return !cancelDownload;
      },
      savedPath);

  if (status == Ao3Client::Status::Aborted) {
    pending = Pending::None;
    if (goHomeAfterCancel) {
      onGoHome();
    } else {
      showState(stateAfterDownload);
    }
    return false;
  }
  return status == Ao3Client::Status::Ok;
}

void Ao3Activity::doDownload(const bool online) {
  stateBeforeError = stateAfterDownload;
  Ao3Client::Status status = Ao3Client::Status::NetworkError;
  if (downloadWithProgress(online, status)) {
    pending = Pending::None;
    LOG_INF("AO3", "Saved %s", savedPath.c_str());
    if (online) {
      rememberCached(actionWorkId);
      openBook(savedPath);
      return;
    }
    // Stay on the work: the row now says it is saved, nothing opens.
    savedWorkId = actionWorkId;
    showState(stateAfterDownload);
    return;
  }
  if (status == Ao3Client::Status::Aborted) return;

  LOG_ERR("AO3", "Download %u failed: %s", static_cast<unsigned>(actionWorkId), Ao3Client::statusName(status));
  if (online) {
    // Offline or AO3 down: an earlier copy still reads fine.
    const std::string cached = cachePathFor(actionWorkId);
    if (Storage.exists(cached.c_str())) {
      pending = Pending::None;
      LOG_INF("AO3", "Opening cached copy %s", cached.c_str());
      openBook(cached);
      return;
    }
  }
  showError(status);
}

void Ao3Activity::rememberCached(const uint32_t workId) {
  const uint32_t evicted = AO3_STORE.touchCachedWork(workId);
  if (evicted != 0 && evicted != workId) {
    const std::string path = cachePathFor(evicted);
    clearBookCache(path);
    Storage.remove(path.c_str());
    LOG_INF("AO3", "Evicted cached work %u", static_cast<unsigned>(evicted));
  }
  AO3_STORE.saveIfDirty();
}

void Ao3Activity::openBook(const std::string& path) {
  if (path.empty()) return;
  if (WiFi.getMode() == WIFI_MODE_NULL) {
    activityManager.goToReader(path);
    return;
  }
  // Reboot straight into the book: it drops the Wi-Fi/TLS heap fragmentation
  // before the reader lays out the EPUB (same hand-off as WeRead).
  APP_STATE.openEpubPath = path;
  APP_STATE.readerActivityLoadCount = 0;
  if (!APP_STATE.saveToFile()) {
    LOG_ERR("AO3", "Failed to persist reader target; opening without restart");
    activityManager.goToReader(path);
    return;
  }
  WiFi.disconnect(false);
  delay(30);
  silentRestartToReader();
}

// ---------------------------------------------------------------- status

void Ao3Activity::showBusy(const char* message) {
  state = State::Busy;
  statusMessage = message;
  requestUpdate(true);
}

const char* Ao3Activity::errorText(const Ao3Client::Status status) {
  switch (status) {
    case Ao3Client::Status::BadCredentials:
      return tr(STR_AO3_ERR_CREDENTIALS);
    case Ao3Client::Status::SessionRejected:
      return tr(STR_AO3_ERR_SESSION);
    case Ao3Client::Status::NotLoggedIn:
      return AO3_STORE.hasCredentials() ? tr(STR_AO3_ERR_NOT_LOGGED_IN) : tr(STR_AO3_ERR_LOGIN_REQUIRED);
    case Ao3Client::Status::RateLimited:
      return tr(STR_AO3_ERR_RATE_LIMITED);
    case Ao3Client::Status::ServerError:
      return tr(STR_AO3_ERR_SERVER);
    case Ao3Client::Status::NotFound:
      return tr(STR_AO3_ERR_NOT_FOUND);
    case Ao3Client::Status::Unavailable:
      return tr(STR_AO3_ERR_UNAVAILABLE);
    case Ao3Client::Status::StorageError:
      return tr(STR_AO3_ERR_STORAGE);
    case Ao3Client::Status::OutOfMemory:
      return tr(STR_AO3_ERR_MEMORY);
    case Ao3Client::Status::NetworkError:
    case Ao3Client::Status::Aborted:
    case Ao3Client::Status::Ok:
      break;
  }
  return tr(STR_AO3_ERR_NETWORK);
}

void Ao3Activity::showError(const Ao3Client::Status status) { showErrorText(errorText(status)); }

void Ao3Activity::showErrorText(const char* message) {
  errorMessage = message;
  state = State::Error;
  requestUpdate();
}

// ---------------------------------------------------------------- render

const char* Ao3Activity::screenTitle() const {
  if (state == State::Filter) return tr(STR_AO3_FILTER);
  if ((state == State::List || state == State::Detail) && !views.empty()) return views.back().title.c_str();
  return tr(STR_AO3_TITLE);
}

void Ao3Activity::rootScreen(UiScreen& screen, void* user) {
  auto* self = static_cast<Ao3Activity*>(user);
  switch (self->state) {
    case State::Menu:
    case State::List:
    case State::Detail:
    case State::Filter:
      self->buildListScreen(screen);
      break;
    case State::Downloading:
      self->buildDownloadScreen(screen);
      break;
    default:
      self->buildStatusScreen(screen);
      break;
  }
}

void Ao3Activity::screenHeader(UiScreen& screen, const char* title) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.takeBottom(static_cast<int16_t>(metrics.buttonHintsHeight));
  screen.spacer(static_cast<int16_t>(metrics.topPadding));
  if (UITheme::getInstance().hasMainTabs()) {
    const fui::Rect headerRect = screen.takeTop(static_cast<int16_t>(metrics.headerHeight));
    GUI.drawHeader(renderer, Rect{headerRect.x, headerRect.y, headerRect.width, headerRect.height}, title);
    screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
    return;
  }
  fui::HeaderProps header;
  header.title = title;
  header.borderEdges = fui::EdgeBottom;
  screen.header(header);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
}

void Ao3Activity::buildListScreen(UiScreen& screen) {
  screenHeader(screen, screenTitle());

  if (rowItems.empty()) {
    screen.centeredText(tr(STR_NO_ENTRIES), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  listNav.selected = selectorIndex;
  int16_t rowHeight = screen.theme().rowHeight;
  if (!mappedInput.hasTouch() || state != State::Menu) {
    // List, detail and filter rows carry a subtitle (author, tag group, value).
    rowHeight = static_cast<int16_t>(UITheme::getInstance().getMetrics().listWithSubtitleRowHeight);
    props.rowHeight = rowHeight;
  }
  listNav.syncToProps(screen.body(), rowHeight, screen.theme().listRowGap, rowCount(), props);
  screen.list(props);
}

void Ao3Activity::buildDownloadScreen(UiScreen& screen) {
  screenHeader(screen, tr(STR_AO3_TITLE));

  const auto& theme = screen.theme();
  fui::TextStyle centered = theme.bodyText;
  centered.align = fui::TextAlign::Center;
  const int16_t lh = screen.target().lineHeight(centered.font);
  const int16_t gap = theme.spaceMd;
  const int16_t barH = 16;
  const int16_t btnH = theme.rowHeight;
  const int16_t blockH = static_cast<int16_t>(lh * 2 + barH + btnH + gap * 3);
  const fui::Rect body = screen.body();
  if (body.height > blockH) screen.spacer(static_cast<int16_t>((body.height - blockH) / 2));

  screen.target().text(screen.takeTop(lh, gap), tr(STR_DOWNLOADING), centered);
  screen.target().text(screen.takeTop(lh, gap), statusMessage.c_str(), centered);

  const fui::Rect bar = screen.takeTop(barH, gap).inset(fui::Insets{0, 50, 0, 50});
  if (downloadTotal > 0) {
    fui::ProgressBarProps progress;
    progress.value = static_cast<int32_t>(downloadProgress);
    progress.max = static_cast<int32_t>(downloadTotal);
    progress.border = fui::Paint::solid(fui::Color::Black);
    progress.borderWidth = 1;
    fui::progressBar(screen.frame(), bar, progress);
  }

  const fui::Rect btnArea = screen.takeTop(btnH);
  const int16_t btnW = static_cast<int16_t>(btnArea.width / 3);
  fui::ButtonProps cancel;
  cancel.label = tr(STR_CANCEL);
  cancel.action = ACTION_CANCEL;
  screen.button(cancel, fui::Rect{static_cast<int16_t>(btnArea.x + (btnArea.width - btnW) / 2), btnArea.y, btnW, btnH});
}

void Ao3Activity::buildStatusScreen(UiScreen& screen) {
  screenHeader(screen, tr(STR_AO3_TITLE));

  fui::TextStyle centered = screen.theme().bodyText;
  centered.align = fui::TextAlign::Center;
  if (state == State::Error || state == State::Saved) {
    const bool error = state == State::Error;
    const char* line1 = error ? tr(STR_ERROR_MSG) : tr(STR_AO3_SAVED);
    const char* line2 = error ? errorMessage.c_str() : savedPath.c_str();
    const int16_t lh = screen.target().lineHeight(centered.font);
    const int16_t gap = screen.theme().spaceMd;
    const bool showTapHint = error && mappedInput.hasTouch() && pending != Pending::None;
    const int16_t blockH = static_cast<int16_t>(lh * (showTapHint ? 3 : 2) + gap * (showTapHint ? 2 : 1));
    const fui::Rect body = screen.body();
    if (body.height > blockH) screen.spacer(static_cast<int16_t>((body.height - blockH) / 2));
    screen.target().text(screen.takeTop(lh, gap), line1, centered);
    screen.target().text(screen.takeTop(lh, gap), line2, centered);
    if (showTapHint) screen.target().text(screen.takeTop(lh), tr(STR_TAP_TO_RETRY), centered);
    return;
  }
  screen.centeredText(statusMessage.c_str(), centered);
}

void Ao3Activity::render(RenderLock&&) {
  renderer.clearScreen();

  MappedInputManager::Labels labels;
  switch (state) {
    case State::Menu:
    case State::List:
    case State::Detail:
    case State::Filter:
      labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      break;
    case State::Downloading:
      labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
      break;
    case State::Saved:
      labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), "", "");
      break;
    case State::Error:
      labels = mappedInput.mapLabels(tr(STR_BACK), pending != Pending::None ? tr(STR_RETRY) : tr(STR_DONE), "", "");
      break;
    default:
      labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      break;
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderUi();
  renderer.displayBuffer();
}
