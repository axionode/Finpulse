#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/money.h"
#include "services/backup.h"
#include "services/i18n.h"
#include "services/library.h"
#include "ui/ui.h"

namespace fp {

using ui::Rect;
using ui::Color;
using ui::Font;
using ui::Align;
using ui::Id;

class App;

struct Dialog {
    virtual ~Dialog() = default;
    virtual float width() const { return 520; }
    virtual float height(App& a) const = 0;
    virtual void draw(App& a, ui::Ctx& c, const Rect& r) = 0;
    bool done = false;
};

enum class Screen { Start, Unlock, Book, Settings };
enum class Tab { Overview, Transactions, Accounts, Categories, Reports, BookSettings };

struct Toast {
    std::wstring text;
    bool undo = false;
    bool error = false;
    double until = 0;
};

struct UndoEntry {
    enum Kind { Created, Updated, Deleted } kind;
    Transaction before;
    i64 id = 0;
};

struct Prefs {
    Lang lang = Lang::En;
    bool privacy = false;
    int autolockMin = 5;
    bool lockOnMinimize = false;
    bool hideCapture = false;
    int fdow = 0;  // 0 = Monday, 6 = Sunday
    bool autoBackup = true;
};

class App {
public:
    bool init(HWND hwnd);
    void shutdown();
    void frame();
    void onActivity() { lastInput = ctx.time; }
    void onTimer();
    void onSessionLock();
    void onMinimize();
    bool maximized = false;

    HWND hwnd = nullptr;
    ui::Gfx gfx;
    ui::Ctx ctx{gfx};
    Library lib;
    Prefs prefs;
    NumberFormat nf;
    Screen screen = Screen::Start;
    Tab tab = Tab::Overview;
    double lastInput = 0;

    // start screen
    struct BookCard {
        BookRecord rec;
        std::optional<i64> balance;
        int tx = 0;
    };
    std::vector<BookCard> cards;
    bool cardsDirty = true;

    // unlock screen
    i64 unlockId = 0;
    std::wstring pin;
    std::wstring pinMsg;
    double shakeAt = -10;

    // open book caches (reloaded when dirty)
    bool dirty = true;
    PeriodKind pkind = PeriodKind::Month;
    Period period;
    const Currency* cur = nullptr;
    std::vector<Account> accounts;      // active
    std::vector<Account> accountsAll;   // incl. archived
    std::vector<Category> cats;
    std::unordered_map<i64, size_t> catIndex;
    std::unordered_map<i64, size_t> accIndex;
    Totals totals, prevTotals;
    i64 balance = 0;
    std::vector<CategorySum> expByCat, incByCat;
    std::vector<Bucket> series;
    std::vector<Transaction> recent, txList, trash;
    std::wstring search;
    int txFilter = 0;           // 0 all, 1 expenses, 2 income, 3 transfers
    bool showTrash = false;
    bool showArchived = false;
    int reportType = 0;         // 0 expenses, 1 income

    std::unique_ptr<Dialog> dialog;
    double dialogOpenedAt = 0;
    // Navigation that closes the current book runs after the frame, never in the middle of drawing it.
    bool inFrame = false;
    std::function<void()> deferred;
    Toast toast;
    std::vector<UndoEntry> undo;

    // actions
    void reload();
    void reloadCards();
    void openBook(i64 id);
    void lockBook();
    void goStart();
    void setTab(Tab t);
    void setPeriodKind(PeriodKind k);
    void shiftPeriod(int n);
    void showDialog(std::unique_ptr<Dialog> d);
    void showToast(const std::wstring& text, bool undoable = false, bool error = false);
    i64 saveTransaction(const Transaction& t);
    void deleteTransaction(i64 id);
    void doUndo();
    void newTransaction(TxKind kind);
    void editTransaction(i64 id);
    void savePrefs();
    void applyPrefs();
    void exportAll();
    void importData();
    void createShortcut(bool desktop);
    void openDataFolder();

    // lookups and formatting
    const Category* category(i64 id) const;
    const Account* account(i64 id) const;
    std::wstring money(i64 v, bool plus = false, bool symbol = true) const;
    float drawAmount(const Rect& r, i64 minor, Font big, Font small, Color col, Align a, bool plus = false, bool symbol = true);
    std::wstring txTitle(const Transaction& t) const;
    Color txColor(const Transaction& t) const;
    Icon txIcon(const Transaction& t, Color& col) const;
    bool hasBook() { return lib.current() != nullptr; }
    BookRecord& book() { return lib.current()->rec; }
    BookDb& db() { return lib.current()->db; }
    int decimals() const { return cur ? cur->decimals : 2; }

private:
    void drawTitleBar(const Rect& r);
    void drawStart(const Rect& r);
    void drawUnlock(const Rect& r);
    void drawSettings(const Rect& r);
    void drawBook(const Rect& r);
    void drawSidebar(const Rect& r, bool compact);
    void drawPeriodNav(const Rect& r);
    void drawOverview(const Rect& r);
    void drawTransactions(const Rect& r);
    void drawAccounts(const Rect& r);
    void drawCategories(const Rect& r);
    void drawReports(const Rect& r);
    void drawBookSettings(const Rect& r);
    void drawToast();
    void drawDialog();
    void handleShortcuts();
    void submitPin();
    void drawDonut(const Rect& r, const std::vector<CategorySum>& data, i64 total, Id id);
    void drawBars(const Rect& r, const std::vector<Bucket>& data, BucketSize size, Id id);
    void drawTxRow(const Rect& r, const Transaction& t, Id id);
    void drawEmpty(const Rect& r, Icon icon, const std::wstring& title, const std::wstring& hint);
};

// dialogs (dialogs.cpp)
std::unique_ptr<Dialog> makeBookDialog(App& a, const BookRecord* edit);
std::unique_ptr<Dialog> makeTxDialog(App& a, const Transaction& t, bool isNew);
std::unique_ptr<Dialog> makeAccountDialog(App& a, const Account* edit);
std::unique_ptr<Dialog> makeCategoryDialog(App& a, const Category* edit, CatType type);
enum class PinMode { Set, Change, Remove };
std::unique_ptr<Dialog> makePinDialog(App& a, PinMode mode);
std::unique_ptr<Dialog> makeConfirmDialog(const std::wstring& title, const std::wstring& text, const std::wstring& action,
                                          std::function<void(App&)> onConfirm);
std::unique_ptr<Dialog> makeExportDialog(App& a);
std::unique_ptr<Dialog> makeImportDialog(App& a, const std::wstring& path, BackupMode mode);

// shared drawing helpers
std::vector<Icon> categoryIcons();
std::vector<Icon> bookIcons();
Icon accountIcon(AccountType t);

}  // namespace fp
