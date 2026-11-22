#include "app/app.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>

#include "core/text.h"

namespace fp {

using namespace ui;
using namespace ui::theme;

// ---------------------------------------------------------------- shared icon sets

std::vector<Icon> categoryIcons() {
    return {Icon::Cart, Icon::Food, Icon::Coffee, Icon::Bus, Icon::Car, Icon::Fuel, Icon::Home, Icon::Bolt,
            Icon::Wifi, Icon::Phone, Icon::Heart, Icon::Pill, Icon::Shirt, Icon::Film, Icon::Music, Icon::Dumbbell,
            Icon::Book, Icon::Gift, Icon::Plane, Icon::Bed, Icon::Paw, Icon::Baby, Icon::Repeat, Icon::Receipt,
            Icon::Briefcase, Icon::Building, Icon::Laptop, Icon::Users, Icon::User, Icon::Wrench, Icon::Hammer,
            Icon::Box, Icon::Truck, Icon::Megaphone, Icon::Percent, Icon::Bank, Icon::Coins, Icon::Cash, Icon::Card,
            Icon::Globe, Icon::Star, Icon::Leaf, Icon::Sparkle, Icon::Target, Icon::Tag, Icon::Dots};
}

std::vector<Icon> bookIcons() {
    return {Icon::Wallet, Icon::User, Icon::Home, Icon::Briefcase, Icon::Building, Icon::Hammer, Icon::Truck,
            Icon::Laptop, Icon::Card, Icon::Coins, Icon::Bank, Icon::Plane, Icon::Heart, Icon::Star, Icon::Leaf, Icon::Target};
}

Icon accountIcon(AccountType t) {
    switch (t) {
        case AccountType::Cash: return Icon::Cash;
        case AccountType::Card: return Icon::Card;
        case AccountType::Bank: return Icon::Bank;
        case AccountType::Savings: return Icon::Coins;
        case AccountType::Credit: return Icon::Percent;
        case AccountType::Debt: return Icon::Users;
    }
    return Icon::Wallet;
}

// ---------------------------------------------------------------- lifecycle

bool App::init(HWND h) {
    hwnd = h;
    ctx.hwnd = h;
    if (!gfx.init(h)) return false;
    UINT dpi = GetDpiForWindow(h);
    gfx.setDpi((float)(dpi ? dpi : 96));
    Status st = lib.init();
    if (!st) return false;
    Catalog& cat = lib.catalog();
    std::wstring langS = cat.setting("lang");
    prefs.lang = langS.empty() ? systemLang() : (langS == L"ru" ? Lang::Ru : Lang::En);
    prefs.privacy = cat.settingInt("privacy", 0) != 0;
    prefs.autolockMin = cat.settingInt("autolock_min", 5);
    prefs.lockOnMinimize = cat.settingInt("lock_on_minimize", 0) != 0;
    prefs.hideCapture = cat.settingInt("hide_capture", 0) != 0;
    prefs.fdow = cat.settingInt("first_day_of_week", 0);
    prefs.autoBackup = cat.settingInt("auto_backup", 1) != 0;
    wchar_t dec[8] = {};
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, dec, 8) > 0 && (dec[0] == L'.' || dec[0] == L','))
        nf.decimal = dec[0];
    applyPrefs();
    period = makePeriod(pkind, Date::today(), prefs.fdow);
    reloadCards();
    return true;
}

void App::shutdown() {
    savePrefs();
    if (prefs.autoBackup) autoBackup(lib);
    lib.closeCurrent();
    gfx.shutdown();
}

void App::applyPrefs() {
    setLang(prefs.lang);
    SetWindowDisplayAffinity(hwnd, prefs.hideCapture ? 0x11 /*WDA_EXCLUDEFROMCAPTURE*/ : WDA_NONE);
}

void App::savePrefs() {
    Catalog& cat = lib.catalog();
    cat.setSetting("lang", prefs.lang == Lang::Ru ? L"ru" : L"en");
    cat.setSettingInt("privacy", prefs.privacy);
    cat.setSettingInt("autolock_min", prefs.autolockMin);
    cat.setSettingInt("lock_on_minimize", prefs.lockOnMinimize);
    cat.setSettingInt("hide_capture", prefs.hideCapture);
    cat.setSettingInt("first_day_of_week", prefs.fdow);
    cat.setSettingInt("auto_backup", prefs.autoBackup);
}

void App::onTimer() {
    if (screen == Screen::Book && hasBook() && book().hasPin() && prefs.autolockMin > 0 && !dialog &&
        ctx.time - lastInput > prefs.autolockMin * 60.0)
        lockBook();
}

void App::onSessionLock() {
    if (screen == Screen::Book && hasBook() && book().hasPin()) lockBook();
}

void App::onMinimize() {
    if (prefs.lockOnMinimize) onSessionLock();
}

// ---------------------------------------------------------------- data

void App::reloadCards() {
    cards.clear();
    for (auto& r : lib.books()) {
        BookCard c;
        c.rec = r;
        c.balance = lib.peekBalance(r, &c.tx);
        cards.push_back(std::move(c));
    }
    cardsDirty = false;
}

void App::reload() {
    if (!hasBook()) return;
    BookDb& d = db();
    cur = &currencyByCode(book().currency);
    accountsAll = d.accounts(true);
    accounts.clear();
    accIndex.clear();
    for (size_t i = 0; i < accountsAll.size(); ++i) {
        accIndex[accountsAll[i].id] = i;
        if (!accountsAll[i].archived) accounts.push_back(accountsAll[i]);
    }
    cats = d.categories(true);
    catIndex.clear();
    for (size_t i = 0; i < cats.size(); ++i) catIndex[cats[i].id] = i;
    totals = d.totals(period);
    prevTotals = period.kind == PeriodKind::All ? Totals{} : d.totals(fp::shiftPeriod(period, -1, prefs.fdow));
    balance = d.totalBalance();
    expByCat = d.byCategory(period, CatType::Expense);
    incByCat = d.byCategory(period, CatType::Income);
    BucketSize bs = (period.kind == PeriodKind::Day || period.kind == PeriodKind::Week || period.kind == PeriodKind::Month)
                        ? BucketSize::Day : BucketSize::Month;
    series = d.series(period, bs);
    recent = d.recent(6);
    txList = d.transactions(period);
    if (showTrash) trash = d.trash();
    dirty = false;
}

const Category* App::category(i64 id) const {
    auto it = catIndex.find(id);
    return it == catIndex.end() ? nullptr : &cats[it->second];
}

const Account* App::account(i64 id) const {
    auto it = accIndex.find(id);
    return it == accIndex.end() ? nullptr : &accountsAll[it->second];
}

std::wstring App::money(i64 v, bool plus, bool symbol) const {
    if (prefs.privacy) return L"•••••";
    return formatAmount(v, cur ? *cur : currencyByCode("CZK"), nf, symbol, plus);
}

// Integer part in the big font, fraction and symbol smaller and dimmer (ADR-0003).
float App::drawAmount(const Rect& r, i64 minor, Font big, Font small, Color col, Align a, bool plus, bool symbol) {
    const Currency& c = cur ? *cur : currencyByCode("CZK");
    std::wstring head, tail;
    if (prefs.privacy) {
        head = L"•••••";
    } else {
        AmountParts p = splitAmount(minor, c, nf, plus);
        if (p.symbolBefore && symbol) head = p.sign + p.symbol + p.integer;
        else head = p.sign + p.integer;
        tail = p.fraction;
        if (symbol && !p.symbolBefore && !p.symbol.empty()) tail += L" " + p.symbol;
    }
    float wh = gfx.measure(head, big), wt = tail.empty() ? 0 : gfx.measure(tail, small);
    float total = wh + wt;
    float x = a == Align::Left ? r.x : (a == Align::Right ? r.r() - total : r.cx() - total / 2);
    // both parts share one baseline (ADR-0003: fraction smaller and dimmer, not raised)
    float lhB = gfx.lineHeight(big), lhS = gfx.lineHeight(small);
    float topB = r.y + (r.h - lhB) * 0.5f;
    float baseY = topB + gfx.baseline(big);
    gfx.text(head, {x, topB, wh + 2, lhB}, big, col, Align::Left, false);
    if (!tail.empty())
        gfx.text(tail, {x + wh, baseY - gfx.baseline(small), wt + 2, lhS}, small, col.withAlpha(col.a * 0.6f), Align::Left, false);
    return total;
}

std::wstring App::txTitle(const Transaction& t) const {
    if (t.kind == TxKind::Transfer) return tr(S::Transfer);
    if (t.kind == TxKind::Adjustment) return tr(S::Adjustment);
    const Category* c = category(t.category());
    if (!c) return t.kind == TxKind::Income ? tr(S::IncomeOne) : tr(S::Expense);
    if (c->parent) {
        const Category* p = category(c->parent);
        if (p) return p->name + L" · " + c->name;
    }
    return c->name;
}

Color App::txColor(const Transaction& t) const {
    switch (t.kind) {
        case TxKind::Expense: return expense;
        case TxKind::Income: return income;
        default: return snow2;
    }
}

Icon App::txIcon(const Transaction& t, Color& col) const {
    if (t.kind == TxKind::Transfer) { col = transfer; return Icon::Swap; }
    if (t.kind == TxKind::Adjustment) { col = snow2; return Icon::Sliders; }
    const Category* c = category(t.category());
    if (!c) { col = snow2; return Icon::Tag; }
    col = palette(c->color);
    return c->icon;
}

// ---------------------------------------------------------------- navigation and actions

void App::openBook(i64 id) {
    BookRecord r;
    if (!lib.catalog().getBook(id, r)) return;
    if (r.hasPin()) {
        unlockId = id;
        pin.clear();
        pinMsg.clear();
        screen = Screen::Unlock;
        return;
    }
    Status st = lib.open(id, L"");
    if (!st) { showToast(errorText(st.code), false, true); return; }
    screen = Screen::Book;
    tab = Tab::Overview;
    search.clear();
    showTrash = false;
    undo.clear();
    period = makePeriod(pkind, Date::today(), prefs.fdow);
    dirty = true;
    lastInput = ctx.time;
}

void App::submitPin() {
    Status st = lib.open(unlockId, pin);
    pin.clear();
    if (st) {
        screen = Screen::Book;
        tab = Tab::Overview;
        search.clear();
        showTrash = false;
        undo.clear();
        period = makePeriod(pkind, Date::today(), prefs.fdow);
        dirty = true;
        pinMsg.clear();
        lastInput = ctx.time;
        return;
    }
    shakeAt = ctx.time;
    pinMsg = st.code == Ec::Throttled ? L"" : errorText(st.code);
}

void App::lockBook() {
    if (inFrame) { deferred = [this] { lockBook(); }; return; }
    dialog.reset();
    ctx.closePopup();
    if (hasBook() && book().hasPin()) {
        i64 id = book().id;
        lib.closeCurrent();
        unlockId = id;
        pin.clear();
        pinMsg.clear();
        screen = Screen::Unlock;
    } else {
        goStart();
    }
    undo.clear();
    toast = {};
}

void App::goStart() {
    if (inFrame) { deferred = [this] { goStart(); }; return; }
    dialog.reset();
    ctx.closePopup();
    lib.closeCurrent();
    screen = Screen::Start;
    cardsDirty = true;
    undo.clear();
}

void App::setTab(Tab t) {
    tab = t;
    if (t != Tab::Transactions) showTrash = false;
}

void App::setPeriodKind(PeriodKind k) {
    pkind = k;
    Date anchor = Date::today();
    if (period.contains(anchor) || period.kind == PeriodKind::All) anchor = Date::today();
    else anchor = period.start;
    period = makePeriod(k, anchor, prefs.fdow);
    dirty = true;
}

void App::shiftPeriod(int n) {
    if (period.kind == PeriodKind::All) return;
    period = fp::shiftPeriod(period, n, prefs.fdow);
    dirty = true;
}

void App::showDialog(std::unique_ptr<Dialog> d) {
    ctx.closePopup();
    dialog = std::move(d);
    dialogOpenedAt = ctx.time;
    ctx.snap(makeId("dialog-anim", 0), 0);
    ctx.focus = 0;
}

void App::showToast(const std::wstring& text, bool undoable, bool error) {
    toast.text = text;
    toast.undo = undoable;
    toast.error = error;
    toast.until = ctx.time + (undoable ? 6.0 : 3.2);
    ctx.snap(makeId("toast-anim", 0), 0);
}

i64 App::saveTransaction(const Transaction& t) {
    UndoEntry u;
    if (t.id) {
        u.kind = UndoEntry::Updated;
        db().getTransaction(t.id, u.before);
    } else {
        u.kind = UndoEntry::Created;
    }
    i64 id = db().saveTransaction(t);
    if (!id) { showToast(errorText(Ec::Io), false, true); return 0; }
    u.id = id;
    undo.push_back(std::move(u));
    if (undo.size() > 50) undo.erase(undo.begin());
    dirty = true;
    return id;
}

void App::deleteTransaction(i64 id) {
    UndoEntry u;
    u.kind = UndoEntry::Deleted;
    u.id = id;
    if (!db().softDelete(id)) return;
    undo.push_back(u);
    dirty = true;
    showToast(tr(S::TxDeleted), true);
}

void App::doUndo() {
    if (undo.empty() || !hasBook()) return;
    UndoEntry u = undo.back();
    undo.pop_back();
    switch (u.kind) {
        case UndoEntry::Created: db().hardDelete(u.id); break;
        case UndoEntry::Updated: db().saveTransaction(u.before); break;
        case UndoEntry::Deleted: db().restore(u.id); break;
    }
    dirty = true;
    toast = {};
}

void App::newTransaction(TxKind kind) {
    if (!hasBook()) return;
    if (dirty) reload();
    Transaction t;
    t.kind = kind;
    t.date = Date::today();
    if (period.kind != PeriodKind::All && !period.contains(t.date)) t.date = period.end.addDays(-1);
    showDialog(makeTxDialog(*this, t, true));
}

void App::editTransaction(i64 id) {
    Transaction t;
    if (db().getTransaction(id, t)) showDialog(makeTxDialog(*this, t, false));
}

static std::wstring pickFile(HWND hwnd, bool save, const std::wstring& defName) {
    std::wstring out;
    IFileDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  save ? IID_IFileSaveDialog : IID_IFileOpenDialog, reinterpret_cast<void**>(&dlg));
    if (FAILED(hr) || !dlg) return out;
    COMDLG_FILTERSPEC spec[] = {{L"Finpulse backup (*.fpbackup)", L"*.fpbackup"}};
    dlg->SetFileTypes(1, spec);
    dlg->SetDefaultExtension(L"fpbackup");
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR | (save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST));
    if (save) dlg->SetFileName(defName.c_str());
    if (SUCCEEDED(dlg->Show(hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) { out = p; CoTaskMemFree(p); }
            item->Release();
        }
    }
    dlg->Release();
    return out;
}

void App::exportAll() { showDialog(makeExportDialog(*this)); }

void App::importData() {
    std::wstring path = pickFile(hwnd, false, L"");
    ctx.in = {};
    if (path.empty()) return;
    auto m = backupMode(path);
    if (!m) { showToast(errorText(m.status.code), false, true); return; }
    showDialog(makeImportDialog(*this, path, m.value));
}

// used by the export dialog
std::wstring pickSavePath(App& a) {
    Date t = Date::today();
    wchar_t name[64];
    swprintf(name, 64, L"Finpulse-%04d-%02d-%02d.fpbackup", t.y, t.m, t.d);
    std::wstring p = pickFile(a.hwnd, true, name);
    a.ctx.in = {};
    return p;
}

void App::createShortcut(bool desktop) {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(desktop ? FOLDERID_Desktop : FOLDERID_Programs, 0, nullptr, &folder))) return;
    std::wstring lnk = std::wstring(folder) + L"\\Finpulse.lnk";
    CoTaskMemFree(folder);
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&sl))))
        return;
    std::wstring exe = exePath();
    sl->SetPath(exe.c_str());
    sl->SetWorkingDirectory(exeDirectory().c_str());
    sl->SetIconLocation(exe.c_str(), 0);
    sl->SetDescription(L"Finpulse");
    IPersistFile* pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pf)))) {
        ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
        pf->Release();
    }
    sl->Release();
    showToast(ok ? tr(S::ShortcutCreated) : errorText(Ec::Io), false, !ok);
}

void App::openDataFolder() { ShellExecuteW(hwnd, L"open", lib.location().dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

// ---------------------------------------------------------------- keyboard shortcuts

void App::handleShortcuts() {
    if (dialog || ctx.popupOpen()) return;
    bool ctrl = ctx.in.ctrl, alt = ctx.in.alt;
    if (screen == Screen::Book && hasBook()) {
        if (ctrl && ctx.consumeKey('N')) newTransaction(TxKind::Expense);
        else if (ctrl && ctx.consumeKey('I')) newTransaction(TxKind::Income);
        else if (ctrl && ctx.consumeKey('T')) { if (accounts.size() > 1) newTransaction(TxKind::Transfer); }
        else if (ctrl && ctx.consumeKey('L')) lockBook();
        else if (ctrl && ctx.consumeKey('F')) { setTab(Tab::Transactions); ctx.requestFocus(makeId("tx-search")); }
        else if (ctrl && !ctx.textFocused && ctx.consumeKey('Z')) doUndo();
        else if (alt && ctx.consumeKey(VK_LEFT)) shiftPeriod(-1);
        else if (alt && ctx.consumeKey(VK_RIGHT)) shiftPeriod(1);
        else if (ctrl && ctx.consumeKey('1')) setTab(Tab::Overview);
        else if (ctrl && ctx.consumeKey('2')) setTab(Tab::Transactions);
        else if (ctrl && ctx.consumeKey('3')) setTab(Tab::Accounts);
        else if (ctrl && ctx.consumeKey('4')) setTab(Tab::Categories);
        else if (ctrl && ctx.consumeKey('5')) setTab(Tab::Reports);
    }
    if (ctrl && ctx.consumeKey('H')) prefs.privacy = !prefs.privacy;
}

// ---------------------------------------------------------------- frame

void App::frame() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    ctx.beginFrame((double)c.QuadPart / (double)f.QuadPart);
    inFrame = true;
    if (lastInput == 0) lastInput = ctx.time;
    if (!gfx.begin(night0)) { ctx.endFrame(); inFrame = false; return; }
    if (cardsDirty && screen == Screen::Start) reloadCards();
    if (dirty && screen == Screen::Book) reload();
    handleShortcuts();

    float W = gfx.widthDip(), H = gfx.heightDip();
    Rect body{0, titleBarH, W, H - titleBarH};
    switch (screen) {
        case Screen::Start: drawStart(body); break;
        case Screen::Unlock: drawUnlock(body); break;
        case Screen::Settings: drawSettings(body); break;
        case Screen::Book: if (hasBook()) drawBook(body); else screen = Screen::Start; break;
    }
    drawToast();
    drawTitleBar({0, 0, W, titleBarH});
    drawDialog();
    ctx.drawPopups();
    ctx.endFrame();
    gfx.end();
    inFrame = false;
    if (deferred) {
        auto fn = std::move(deferred);
        deferred = nullptr;
        fn();
        ctx.repaint = true;
    }
    if (dirty && screen == Screen::Book) ctx.repaint = true;
}

void App::drawDialog() {
    if (!dialog) return;
    float W = gfx.widthDip(), H = gfx.heightDip();
    ctx.setLayer(1);
    float t = ctx.anim(makeId("dialog-anim", 0), 1.0f, 20);
    gfx.fill({0, 0, W, H}, scrim.withAlpha(scrim.a * t));
    float dw = std::min(dialog->width(), W - 32), dh = std::min(dialog->height(*this), H - 48);
    Rect r{std::round((W - dw) / 2), std::round((H - dh) / 2 + (1 - t) * 14), dw, dh};
    gfx.pushOpacity(t);
    gfx.shadow(r, radiusL, 34, 1.0f);
    gfx.fillRound(r, radiusL, night2);
    gfx.strokeRound(r, radiusL, hairlineStrong);
    dialog->draw(*this, ctx, r);
    gfx.popOpacity();
    if (dialog && dialog->done) { dialog.reset(); ctx.focus = 0; ctx.closePopup(); }
}

void App::drawToast() {
    if (toast.text.empty()) return;
    if (ctx.time > toast.until) { toast = {}; return; }
    float t = ctx.anim(makeId("toast-anim", 0), 1.0f, 18);
    float W = gfx.widthDip(), H = gfx.heightDip();
    float tw = gfx.measure(toast.text, Font::Body);
    float uw = toast.undo ? gfx.measure(tr(S::Undo), Font::BodyStrong) + 36 : 0;
    float w = tw + 48 + uw, h = 46;
    Rect r{std::round((W - w) / 2), H - 28 - h + (1 - t) * 16, w, h};
    gfx.pushOpacity(t);
    gfx.shadow(r, 12, 20, 0.9f);
    gfx.fillRound(r, 12, night4);
    gfx.strokeRound(r, 12, hairlineStrong);
    gfx.icon(toast.error ? Icon::Info : Icon::Check, {r.x + 14, r.cy() - 9, 18, 18}, toast.error ? expense : income);
    gfx.text(toast.text, {r.x + 40, r.y, tw + 4, h}, Font::Body, snow1);
    if (toast.undo) {
        Rect ub{r.r() - uw - 6, r.y + 6, uw, h - 12};
        if (button(ctx, makeId("toast-undo"), ub, tr(S::Undo), Btn::Ghost)) doUndo();
    }
    gfx.popOpacity();
    ctx.repaint = true;  // keep checking expiry
}

// ---------------------------------------------------------------- title bar

void App::drawTitleBar(const Rect& r) {
    // window controls must stay usable even under dialogs, so they bypass layer checks
    float bw = 46;
    Rect closeR{r.r() - bw, r.y, bw, r.h}, maxR{closeR.x - bw, r.y, bw, r.h}, minR{maxR.x - bw, r.y, bw, r.h};
    auto winBtn = [&](const Rect& b, Icon ic, Id id, bool danger) -> bool {
        bool over = b.contains(ctx.in.mx, ctx.in.my) && !ctx.popupOpen();
        if (over && ctx.in.pressed) ctx.active = id;
        bool clicked = over && ctx.in.released && (ctx.active == id || b.contains(ctx.in.downX, ctx.in.downY));
        float h = ctx.anim(makeId(id, 1), over ? 1.0f : 0.0f, 24);
        if (h > 0.01f) gfx.fill(b, danger ? rgb(0xC42B1C, h) : night3.withAlpha(h));
        gfx.icon(ic, {b.cx() - 8, b.cy() - 8, 16, 16}, danger && h > 0.5f ? snow1 : snow2, 1.2f);
        ctx.captionExclusions.push_back(b);
        if (clicked) ctx.consumeMouse();
        return clicked;
    };
    if (winBtn(closeR, Icon::WinClose, makeId("win-close"), true)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    if (winBtn(maxR, maximized ? Icon::WinRestore : Icon::WinMax, makeId("win-max"), false))
        ShowWindow(hwnd, maximized ? SW_RESTORE : SW_MAXIMIZE);
    if (winBtn(minR, Icon::WinMin, makeId("win-min"), false)) ShowWindow(hwnd, SW_MINIMIZE);

    float x = r.x + 16;
    if (screen == Screen::Book && hasBook()) {
        Rect back{x - 6, r.y + 6, 32, 32};
        if (iconButton(ctx, makeId("tb-books"), back, Icon::Grid, snow2, false, 17)) goStart();
        ctx.captionExclusions.push_back(back);
        x += 36;
        gfx.circle(x + 4, r.cy(), 4, palette(book().color));
        x += 16;
        gfx.text(book().name, {x, r.y, 360, r.h}, Font::BodyStrong, snow1);
    } else {
        gfx.icon(Icon::Pulse, {x, r.cy() - 9, 18, 18}, frost, 2.0f);
        gfx.text(tr(S::AppName), {x + 28, r.y, 200, r.h}, Font::BodyStrong, snow1);
    }
    float rx = minR.x - 8;
    Rect eye{rx - 34, r.y + 6, 32, 32};
    if (iconButton(ctx, makeId("tb-privacy"), eye, prefs.privacy ? Icon::EyeOff : Icon::Eye, prefs.privacy ? frost : snow2,
                   false, 17)) {
        prefs.privacy = !prefs.privacy;
        savePrefs();
    }
    ctx.captionExclusions.push_back(eye);
    rx = eye.x - 4;
    if (screen == Screen::Book && hasBook()) {
        Rect lk{rx - 34, r.y + 6, 32, 32};
        if (iconButton(ctx, makeId("tb-lock"), lk, book().hasPin() ? Icon::Lock : Icon::ArrowLeft, snow2, false, 17)) lockBook();
        ctx.captionExclusions.push_back(lk);
    }
}

// ---------------------------------------------------------------- start screen

void App::drawEmpty(const Rect& r, Icon icon, const std::wstring& title, const std::wstring& hint) {
    float cy = r.cy() - 40;
    iconBadge(ctx, {r.cx() - 32, cy - 32, 64, 64}, icon, frost, 0.45f);
    gfx.text(title, {r.x, cy + 44, r.w, 28}, Font::Subtitle, snow1, Align::Center);
    gfx.textWrapped(hint, {r.cx() - 220, cy + 76, 440, 60}, Font::Small, snow2, Align::Center);
}

void App::drawStart(const Rect& body) {
    float W = body.w;
    float colW = std::min(1040.0f, W - 64);
    float x0 = std::round(body.x + (W - colW) / 2);
    float y = body.y + 36;
    gfx.text(tr(S::StartTitle), {x0, y, colW, 36}, Font::Title, snow1);
    gfx.text(tr(S::StartSubtitle), {x0, y + 36, colW - 360, 22}, Font::Small, snow3);
    float bx = x0 + colW;
    float nbw = gfx.measure(tr(S::NewBook), Font::BodyStrong) + 58;
    Rect nb{bx - nbw, y + 6, nbw, buttonH};
    if (button(ctx, makeId("start-new"), nb, tr(S::NewBook), Btn::Primary, Icon::Plus)) showDialog(makeBookDialog(*this, nullptr));
    float ibw = gfx.measure(tr(S::Import), Font::BodyStrong) + 58;
    Rect ib{nb.x - 10 - ibw, y + 6, ibw, buttonH};
    if (button(ctx, makeId("start-import"), ib, tr(S::Import), Btn::Secondary, Icon::Download)) importData();
    Rect sb{ib.x - 10 - buttonH, y + 6, buttonH, buttonH};
    if (iconButton(ctx, makeId("start-settings"), sb, Icon::Sliders, snow2, false, 18, night3)) screen = Screen::Settings;
    gfx.strokeRound(sb, radiusS + 2, hairline);
    y += 88;

    Rect area{x0, y, colW, body.b() - y - 16};
    if (cards.empty()) {
        drawEmpty(area, Icon::Wallet, tr(S::EmptyBooksTitle), tr(S::EmptyBooksHint));
        return;
    }
    int cols = colW > 900 ? 3 : (colW > 560 ? 2 : 1);
    float gap = 16, cw = (colW - gap * (cols - 1)) / cols, ch = 176;
    int rows = ((int)cards.size() + cols - 1) / cols;
    float contentH = rows * (ch + gap);
    float off = ctx.scrollBegin(makeId("start-scroll"), {area.x - 8, area.y, area.w + 16, area.h}, contentH);
    for (size_t i = 0; i < cards.size(); ++i) {
        const BookCard& bc = cards[i];
        int col = (int)i % cols, row = (int)i / cols;
        Rect r{area.x + col * (cw + gap), area.y + row * (ch + gap) - off, cw, ch};
        if (r.b() < area.y || r.y > area.b()) continue;
        Id id = makeId(bc.rec.id, makeId("card"));
        bool over = ctx.mouseOver(r);
        // hover changes colour only: geometry never moves, so text stays pixel-sharp
        float h = ctx.anim(makeId(id, 9), over ? 1.0f : 0.0f, 14);
        const Rect& rr = r;
        Color accent = palette(bc.rec.color);
        gfx.fillRound(rr, radiusM, mix(night2, rgb(0x1E232D), h));
        gfx.strokeRound(rr, radiusM, mix(hairline, accent.withAlpha(0.32f), h));
        gfx.fillRound({rr.x + 20, rr.y, 36, 3}, 1.5f, accent);
        iconBadge(ctx, {rr.x + 20, rr.y + 22, 40, 40}, bc.rec.icon, accent, 0.5f);
        gfx.text(bc.rec.name, {rr.x + 72, rr.y + 22, rr.w - 92, 22}, Font::Subtitle, snow1);
        gfx.text(kindName(bc.rec.kind) + L"  ·  " + fromUtf8(bc.rec.currency), {rr.x + 72, rr.y + 44, rr.w - 92, 18},
                 Font::Caption, snow3);
        Rect amt{rr.x + 20, rr.b() - 70, rr.w - 40, 40};
        if (bc.rec.hasPin()) {
            gfx.icon(Icon::Lock, {amt.x, amt.cy() - 9, 18, 18}, snow3);
            gfx.text(tr(S::Locked), {amt.x + 28, amt.y, 200, amt.h}, Font::Body, snow3);
        } else if (bc.balance) {
            const Currency* keep = cur;
            cur = &currencyByCode(bc.rec.currency);
            drawAmount(amt, *bc.balance, Font::AmountLarge, Font::AmountSmall, snow1, Align::Left);
            cur = keep;
        }
        if (!bc.rec.hasPin())
            gfx.text(std::to_wstring(bc.tx) + L" " + plural(bc.tx, S::OpOne, S::OpFew, S::OpMany),
                     {rr.x + 20, rr.b() - 32, rr.w - 40, 18}, Font::Caption, snow3);
        if (over) ctx.cursor = Cursor::Hand;
        if (ctx.clicked(id, r, over)) openBook(bc.rec.id);
    }
    ctx.scrollEnd();
}

// ---------------------------------------------------------------- unlock screen

void App::drawUnlock(const Rect& body) {
    BookRecord r;
    if (!lib.catalog().getBook(unlockId, r)) { screen = Screen::Start; return; }
    int wait = lib.throttleSeconds(r);
    if (wait > 0) ctx.repaint = true;
    float cx = body.cx(), top = body.y + std::max(40.0f, body.h / 2 - 300);
    Rect back{body.x + 24, body.y + 20, 120, 34};
    if (button(ctx, makeId("unlock-back"), back, tr(S::Back), Btn::Ghost, Icon::ChevLeft)) { screen = Screen::Start; return; }
    Color accent = palette(r.color);
    iconBadge(ctx, {cx - 32, top, 64, 64}, r.icon, accent, 0.46f);
    gfx.text(r.name, {body.x, top + 76, body.w, 30}, Font::Title, snow1, Align::Center);
    gfx.text(tr(S::EnterPin), {body.x, top + 108, body.w, 22}, Font::Small, snow3, Align::Center);

    // keyboard input
    if (wait == 0) {
        for (wchar_t ch : ctx.in.chars)
            if (ch >= L'0' && ch <= L'9' && pin.size() < 4) { pin += ch; pinMsg.clear(); }
        if (ctx.consumeKey(VK_BACK) && !pin.empty()) pin.pop_back();
    }
    if (ctx.consumeKey(VK_ESCAPE)) { screen = Screen::Start; return; }

    float shake = 0;
    double since = ctx.time - shakeAt;
    if (since < 0.45) { shake = (float)(std::sin(since * 48) * 9 * (1 - since / 0.45)); ctx.repaint = true; }
    float dotsY = top + 158;
    for (int i = 0; i < 4; ++i) {
        float x = cx - 54 + i * 36 + shake;
        bool filled = i < (int)pin.size();
        float f = ctx.anim(makeId((int64_t)i, makeId("pin-dot")), filled ? 1.0f : 0.0f, 26);
        gfx.ring(x, dotsY, 7, snow3.withAlpha(1 - f), 1.5f);
        if (f > 0.01f) gfx.circle(x, dotsY, 7 * (0.6f + 0.4f * f), frost.withAlpha(f));
    }
    std::wstring msg = pinMsg;
    if (wait > 0) msg = std::wstring(tr(S::PinWait)) + L" " + std::to_wstring(wait) + L" " + tr(S::Seconds);
    if (!msg.empty()) gfx.text(msg, {body.x, dotsY + 22, body.w, 22}, Font::Small, expense, Align::Center);

    // keypad
    float kw = 72, kh = 56, gap = 12;
    float kx = cx - (kw * 3 + gap * 2) / 2, ky = dotsY + 60;
    const wchar_t* keys[12] = {L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"", L"0", L"<"};
    for (int i = 0; i < 12; ++i) {
        if (i == 9) continue;
        Rect k{kx + (i % 3) * (kw + gap), ky + (i / 3) * (kh + gap), kw, kh};
        Id id = makeId((int64_t)i, makeId("pin-key"));
        bool over = ctx.mouseOver(k) && wait == 0;
        if (over) ctx.cursor = Cursor::Hand;
        float h = ctx.anim(makeId(id, 1), over ? 1.0f : 0.0f, 22);
        gfx.fillRound(k, 14, mix(night2, night3, h));
        gfx.strokeRound(k, 14, hairline);
        if (i == 11) gfx.icon(Icon::ChevLeft, {k.cx() - 10, k.cy() - 10, 20, 20}, snow2);
        else gfx.text(keys[i], k, Font::PinDigit, wait ? snow3 : snow1, Align::Center);
        if (ctx.clicked(id, k, over)) {
            if (i == 11) { if (!pin.empty()) pin.pop_back(); }
            else if (pin.size() < 4) { pin += keys[i]; pinMsg.clear(); }
        }
    }
    if (pin.size() == 4 && wait == 0) submitPin();
}

// ---------------------------------------------------------------- app settings screen

void App::drawSettings(const Rect& body) {
    float colW = std::min(760.0f, body.w - 64);
    float x0 = std::round(body.x + (body.w - colW) / 2);
    Rect view{body.x, body.y, body.w, body.h};
    float contentH = 1180;
    float off = ctx.scrollBegin(makeId("settings-scroll"), view, contentH);
    float y = body.y + 28 - off;
    Rect back{x0 - 10, y, 120, 34};
    if (button(ctx, makeId("settings-back"), back, tr(S::Back), Btn::Ghost, Icon::ChevLeft) || ctx.consumeKey(VK_ESCAPE)) {
        savePrefs();
        screen = Screen::Start;
        cardsDirty = true;
        ctx.scrollEnd();
        return;
    }
    y += 46;
    gfx.text(tr(S::AppSettings), {x0, y, colW, 36}, Font::Title, snow1);
    y += 56;

    auto rowLabel = [&](const Rect& r, const std::wstring& text) { gfx.text(text, r, Font::Body, snow1); };
    bool changed = false;

    // Display
    sectionLabel(ctx, {x0, y, colW, 18}, tr(S::Display));
    y += 26;
    Rect c1{x0, y, colW, 3 * 56 + 16};
    card(ctx, c1);
    float ry = c1.y + 8;
    rowLabel({c1.x + 20, ry, 300, 56}, tr(S::Language));
    int li = segmented(ctx, makeId("set-lang"), {c1.r() - 260, ry + 10, 240, 36}, {L"English", L"Русский"}, (int)prefs.lang);
    if (li != (int)prefs.lang) { prefs.lang = (Lang)li; setLang(prefs.lang); changed = true; }
    ry += 56;
    gfx.line(c1.x + 20, ry, c1.r() - 20, ry, hairline);
    rowLabel({c1.x + 20, ry, 300, 56}, tr(S::FirstDayOfWeek));
    int fd = segmented(ctx, makeId("set-fdow"), {c1.r() - 260, ry + 10, 240, 36}, {tr(S::Monday), tr(S::Sunday)},
                       prefs.fdow == 6 ? 1 : 0);
    if ((fd == 1 ? 6 : 0) != prefs.fdow) { prefs.fdow = fd == 1 ? 6 : 0; changed = true; }
    ry += 56;
    gfx.line(c1.x + 20, ry, c1.r() - 20, ry, hairline);
    rowLabel({c1.x + 20, ry, 400, 56}, tr(S::PrivacyMode));
    if (toggle(ctx, makeId("set-privacy"), {c1.r() - 80, ry, 60, 56}, prefs.privacy)) changed = true;
    y = c1.b() + 28;

    // Security
    sectionLabel(ctx, {x0, y, colW, 18}, tr(S::Security));
    y += 26;
    Rect c2{x0, y, colW, 3 * 56 + 16};
    card(ctx, c2);
    ry = c2.y + 8;
    rowLabel({c2.x + 20, ry, colW - 260, 56}, tr(S::AutoLock));
    std::vector<int> mins = {0, 1, 5, 15, 30, 60};
    std::vector<DropItem> items;
    int sel = 2;
    for (size_t i = 0; i < mins.size(); ++i) {
        items.push_back({mins[i] == 0 ? std::wstring(tr(S::Never)) : std::to_wstring(mins[i]) + L" " + tr(S::Minutes)});
        if (mins[i] == prefs.autolockMin) sel = (int)i;
    }
    if (dropdown(ctx, makeId("set-autolock"), {c2.r() - 200, ry + 9, 180, 38}, items, sel)) { prefs.autolockMin = mins[sel]; changed = true; }
    ry += 56;
    gfx.line(c2.x + 20, ry, c2.r() - 20, ry, hairline);
    rowLabel({c2.x + 20, ry, colW - 120, 56}, tr(S::LockOnMinimize));
    if (toggle(ctx, makeId("set-lockmin"), {c2.r() - 80, ry, 60, 56}, prefs.lockOnMinimize)) changed = true;
    ry += 56;
    gfx.line(c2.x + 20, ry, c2.r() - 20, ry, hairline);
    rowLabel({c2.x + 20, ry, colW - 120, 56}, tr(S::HideFromCapture));
    if (toggle(ctx, makeId("set-capture"), {c2.r() - 80, ry, 60, 56}, prefs.hideCapture)) { changed = true; applyPrefs(); }
    y = c2.b() + 28;

    // Data
    sectionLabel(ctx, {x0, y, colW, 18}, tr(S::Data));
    y += 26;
    Rect c3{x0, y, colW, 4 * 56 + 16 + 22};
    card(ctx, c3);
    ry = c3.y + 8;
    rowLabel({c3.x + 20, ry, 200, 34}, tr(S::DataFolder));
    gfx.text(lib.location().dir, {c3.x + 20, ry + 26, colW - 200, 22}, Font::Caption, snow3);
    if (!lib.location().portable) gfx.text(tr(S::DataFolderFallback), {c3.x + 20, ry + 44, colW - 200, 18}, Font::Caption, warning);
    if (button(ctx, makeId("set-open"), {c3.r() - 170, ry + 12, 150, 36}, tr(S::OpenFolder), Btn::Secondary, Icon::Folder)) openDataFolder();
    ry += 78;
    gfx.line(c3.x + 20, ry, c3.r() - 20, ry, hairline);
    rowLabel({c3.x + 20, ry, colW - 240, 56}, tr(S::ExportAll));
    if (button(ctx, makeId("set-export"), {c3.r() - 170, ry + 10, 150, 36}, tr(S::Export), Btn::Secondary, Icon::Upload)) exportAll();
    ry += 56;
    gfx.line(c3.x + 20, ry, c3.r() - 20, ry, hairline);
    rowLabel({c3.x + 20, ry, colW - 240, 56}, tr(S::ImportData));
    if (button(ctx, makeId("set-import"), {c3.r() - 170, ry + 10, 150, 36}, tr(S::Import), Btn::Secondary, Icon::Download)) importData();
    ry += 56;
    gfx.line(c3.x + 20, ry, c3.r() - 20, ry, hairline);
    rowLabel({c3.x + 20, ry, colW - 120, 56}, tr(S::AutoBackup));
    if (toggle(ctx, makeId("set-autobackup"), {c3.r() - 80, ry, 60, 56}, prefs.autoBackup)) changed = true;
    y = c3.b() + 28;

    // Shortcuts
    sectionLabel(ctx, {x0, y, colW, 18}, tr(S::Shortcuts));
    y += 26;
    Rect c4{x0, y, colW, 72};
    card(ctx, c4);
    float hw = (colW - 52) / 2;
    if (button(ctx, makeId("set-desk"), {c4.x + 18, c4.y + 17, hw, 38}, tr(S::DesktopShortcut), Btn::Secondary, Icon::Monitor)) createShortcut(true);
    if (button(ctx, makeId("set-startm"), {c4.x + 34 + hw, c4.y + 17, hw, 38}, tr(S::StartMenuShortcut), Btn::Secondary, Icon::Grid)) createShortcut(false);
    y = c4.b() + 28;

    // About
    sectionLabel(ctx, {x0, y, colW, 18}, tr(S::About));
    y += 26;
    Rect c5{x0, y, colW, 132};
    card(ctx, c5);
    gfx.icon(Icon::Pulse, {c5.x + 20, c5.y + 20, 22, 22}, frost, 2.0f);
    gfx.text(std::wstring(L"Finpulse  ") + tr(S::Version) + L" 0.1.0", {c5.x + 54, c5.y + 18, colW - 80, 26}, Font::BodyStrong, snow1);
    gfx.icon(Icon::Shield, {c5.x + 20, c5.y + 56, 20, 20}, income);
    gfx.text(tr(S::OfflineNote), {c5.x + 54, c5.y + 54, colW - 80, 24}, Font::Small, snow2);
    gfx.text(tr(S::KeysText), {c5.x + 20, c5.y + 92, colW - 40, 22}, Font::Caption, snow3);
    ctx.scrollEnd();
    if (changed) savePrefs();
}

}  // namespace fp
