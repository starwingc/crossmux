#pragma once

#include <string>
#include <vector>

#include "Ao3Client.h"
#include "Ao3HtmlScanner.h"
#include "activities/Activity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Archive of Our Own client. The menu is the user's dashboard (own works,
// Marked for Later, history, bookmarks, subscriptions) plus search by text,
// tag or author. Lists open a work's detail page (stats, authors and every
// tag); authors and tags open their own work lists, so browsing is a stack of
// views that Back unwinds. Work lists can be filtered (sort, rating, status,
// language, include/exclude tags). A work can be read online (cached EPUB,
// opened at once) or saved to /AO3.
//
// Network work runs synchronously in loop(), the same way the OPDS browser
// does; Wi-Fi is brought up only when needed and torn down by a silent
// restart on exit.
class Ao3Activity final : public Activity, private UiAppHost {
 public:
  explicit Ao3Activity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Only while a request is on the wire; idle screens may auto-sleep.
  bool preventAutoSleep() override { return state == State::Busy || state == State::Downloading; }

 private:
  enum class State : uint8_t { Menu, Child, Busy, List, Detail, Filter, Downloading, Saved, Error };
  enum class MenuAction : uint8_t {
    Login,
    MyWorks,
    MarkedForLater,
    History,
    Bookmarks,
    Subscriptions,
    Search,
    TagSearch,
    AuthorSearch,
    ByLink,
    Logout,
  };
  // Work deferred until Wi-Fi is up (and repeated by Retry).
  enum class Pending : uint8_t { None, Login, Load, Save, Read };

  struct Filter {
    uint8_t sort = 0;      // index into kSortColumns (0 = AO3 default)
    uint8_t rating = 0;    // index into kRatings (0 = any)
    uint8_t complete = 0;  // 0 any, 1 complete, 2 in progress
    uint8_t language = 0;  // index into kLanguages (0 = any)
    std::string includeTags;
    std::string excludeTags;
    bool active() const {
      return sort || rating || complete || language || !includeTags.empty() || !excludeTags.empty();
    }
  };

  enum class ViewKind : uint8_t { List, Detail };
  struct View {
    ViewKind kind = ViewKind::List;
    std::string title;
    std::string basePath;  // list without filter/page params; detail: /works/ID
    std::string pagePath;  // list page currently shown (pagination)
    bool filterable = false;
    bool isSearch = false;    // /works/search takes single-valued rating_ids
    bool needsLogin = false;  // the user's own lists
    bool loaded = false;      // fetched at least once (Back from a failed first load pops it)
    Filter filter;
    uint32_t workId = 0;
    int selection = 0;
  };

  struct MenuRow {
    MenuAction action;
    const char* label;
  };

  State state = State::Menu;
  State stateBeforeError = State::Menu;
  Pending pending = Pending::None;

  Ao3Client client;
  ButtonNavigator buttonNavigator;
  freeink::ui::ListNav listNav;
  std::vector<freeink::ui::ListItem> rowItems;
  // Owned text for rows whose labels are composed (filter values, author rows).
  std::vector<std::string> rowText;
  int selectorIndex = 0;

  std::vector<MenuRow> menuRows;
  int menuSelection = 0;

  // Browsing stack; top() is what List/Detail show.
  std::vector<View> views;
  // Data of the top view. When a detail is pushed over a list, the list's
  // rows are parked in `parked*` so Back does not refetch them.
  std::vector<Ao3HtmlScanner::Work> works;
  std::string nextPath;
  std::string prevPath;
  Ao3HtmlScanner::Detail detail;
  // Which view index the buffers above belong to (-1: none). A list pushed
  // over list -> detail parks the older list so Back does not refetch it.
  int listDepth = -1;
  int detailDepth = -1;
  std::vector<Ao3HtmlScanner::Work> parkedWorks;
  std::string parkedNext;
  std::string parkedPrev;
  int parkedDepth = -1;

  // Filter editor works on a copy; Apply commits it to the top list view.
  Filter editFilter;
  int filterSelection = 0;

  std::string pendingLogin;
  std::string pendingPassword;
  uint32_t actionWorkId = 0;
  std::string actionTitle;
  State stateAfterDownload = State::Menu;

  std::string statusMessage;
  std::string errorMessage;
  std::string savedPath;
  uint32_t savedWorkId = 0;  // last work saved to /AO3 (its detail row says so)
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;
  bool cancelDownload = false;
  bool goHomeAfterCancel = false;

  static void rootScreen(UiScreen& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onCancelEvent(const freeink::ui::ActionEvent& event, void* user);
  void screenHeader(UiScreen& screen, const char* title);
  void buildListScreen(UiScreen& screen);
  void buildDownloadScreen(UiScreen& screen);
  void buildStatusScreen(UiScreen& screen);
  const char* screenTitle() const;

  // Rows.
  void rebuildRows();
  void rebuildMenuRows();
  void rebuildListRows();
  void rebuildDetailRows();
  void rebuildFilterRows();
  void pushRow(const char* label, const char* subtitle = nullptr, const char* value = nullptr);
  const char* ownText(std::string text);
  int rowCount() const { return static_cast<int>(rowItems.size()); }
  void moveSelection(int index);
  void rememberSelection();
  void showState(State next);

  // Input.
  void activateSelected();
  void activateMenu(MenuAction action);
  void activateListRow(int row);
  void activateDetailRow(int row);
  void activateFilterRow(int row);
  void goBack();

  // Navigation.
  void pushView(View view);
  void openList(const char* title, const std::string& path, bool filterable, bool isSearch = false,
                bool needsLogin = false);
  void openDetail(uint32_t workId, const std::string& title);
  std::string viewUrl(const View& view) const;
  static std::string filterQuery(const Filter& filter, bool isSearch);

  // Prompts.
  void promptText(const char* title, std::string initial, size_t maxLength, InputType type,
                  void (Ao3Activity::*onText)(const std::string&));
  void onLoginName(const std::string& text);
  void onLoginPassword(const std::string& text);
  void onSearchText(const std::string& text);
  void onTagText(const std::string& text);
  void onAuthorText(const std::string& text);
  void onWorkLink(const std::string& text);
  void onIncludeTags(const std::string& text);
  void onExcludeTags(const std::string& text);

  // Network.
  bool wifiReady() const;
  void runWhenOnline(Pending action);
  void runPending();
  void doLogin();
  void doLoad();
  void doDownload(bool online);
  bool downloadWithProgress(bool online, Ao3Client::Status& status);
  void rememberCached(uint32_t workId);
  void openBook(const std::string& path);

  void showBusy(const char* message);
  void showError(Ao3Client::Status status);
  void showErrorText(const char* message);
  static const char* errorText(Ao3Client::Status status);
};
