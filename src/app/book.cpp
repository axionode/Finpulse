#include <windows.h>

#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "core/text.h"

namespace fp {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr float kPi = 3.14159265358979f;

std::wstring compactNumber(i64 minor, int decimals) {
    double v = (double)minor / (double)pow10i(decimals);
    double a = std::fabs(v);
    wchar_t buf[32];
    if (a >= 1e6) swprintf(buf, 32, L"%.1fM", v / 1e6);
    else if (a >= 1e4) swprintf(buf, 32, L"%.0fk", v / 1e3);
    else if (a >= 1e3) swprintf(buf, 32, L"%.1fk", v / 1e3);
    else swprintf(buf, 32, L"%.0f", v);
    std::wstring s = buf;
    size_t p = s.find(L".0");
    if (p != std::wstring::npos && (p + 2 == s.size() || !iswdigit(s[p + 2]))) s.erase(p, 2);
    return s;
}

i64 niceCeil(i64 v) {
    if (v <= 0) return 1;
    double d = (double)v;
    double mag = std::pow(10.0, std::floor(std::log10(d)));
    double n = d / mag;
    double step = n <= 1 ? 1 : n <= 2 ? 2 : n <= 2.5 ? 2.5 : n <= 5 ? 5 : 10;
    return (i64)(step * mag);
}

// Cached filtered transaction list (recomputed only when inputs change).
struct TxRowsCache {
    const void* src = nullptr;
    size_t srcSize = 0;
    i64 firstId = -1;
    std::wstring search;
    int filter = -1;
    bool trash = false;
    std::vector<const Transaction*> items;
};
TxRowsCache g_rows;

struct BookEditState {
    i64 bookId = 0;
    std::wstring name;
    BookKind kind{};
    int color = 0;
    Icon icon{};
};
BookEditState g_bookEdit;

}  // namespace

// ---------------------------------------------------------------- layout shell

void App::drawBook(const Rect& body) {
    bool compact = body.w < 900;
    float sw = compact ? sidebarCompactW : sidebarW;
    Rect side{body.x, body.y, sw, body.h};
    {
        // only the top-right corner is rounded: the other corners lie outside the clip
        Rect shape{side.x - 2 * radiusM, side.y, side.w + 2 * radiusM, side.h + 2 * radiusM};
        gfx.pushClip({side.x, side.y, side.w + 1, side.h});
        gfx.fillRound(shape, radiusM, night1);
        gfx.strokeRound(shape, radiusM, hairline);
        gfx.popClip();
    }
    drawSidebar(side, compact);

    Rect content{side.r(), body.y, body.w - sw, body.h};
    float pad = content.w > 1100 ? 36 : 24;
    Rect head{content.x + pad, content.y + 18, content.w - 2 * pad, 42};
    const wchar_t* titles[] = {tr(S::NavOverview), tr(S::NavTransactions), tr(S::NavAccounts),
                               tr(S::NavCategories), tr(S::NavReports), tr(S::NavBookSettings)};
    gfx.text(titles[(int)tab], {head.x, head.y, 260, head.h}, Font::Title, snow1);
    float rightX = head.r();
    if (tab == Tab::Overview || tab == Tab::Transactions || tab == Tab::Accounts) {
        float bw = gfx.measure(tr(S::Add), Font::BodyStrong) + 58;
        Rect add{rightX - bw, head.y + 2, bw, buttonH};
        if (button(ctx, makeId("book-add"), add, tr(S::Add), Btn::Primary, Icon::Plus)) {
            if (tab == Tab::Accounts) showDialog(makeAccountDialog(*this, nullptr));
            else newTransaction(TxKind::Expense);
        }
        rightX = add.x - 16;
    }
    if (tab == Tab::Overview || tab == Tab::Transactions || tab == Tab::Reports) {
        float navW = std::min(620.0f, rightX - head.x - 200);
        if (navW > 300) drawPeriodNav({rightX - navW, head.y + 2, navW, buttonH});
    }
    Rect main{content.x + pad, content.y + 80, content.w - 2 * pad, content.h - 80};
    switch (tab) {
        case Tab::Overview: drawOverview(main); break;
        case Tab::Transactions: drawTransactions(main); break;
        case Tab::Accounts: drawAccounts(main); break;
        case Tab::Categories: drawCategories(main); break;
        case Tab::Reports: drawReports(main); break;
        case Tab::BookSettings: drawBookSettings(main); break;
    }
}

void App::drawSidebar(const Rect& r, bool compact) {
    float y = r.y + 18;
    Color accent = palette(book().color);
    if (!compact) {
        iconBadge(ctx, {r.x + 18, y, 40, 40}, book().icon, accent, 0.5f);
        gfx.text(book().name, {r.x + 68, y + 1, r.w - 84, 22}, Font::BodyStrong, snow1);
        gfx.text(kindName(book().kind) + L"  ·  " + fromUtf8(book().currency), {r.x + 68, y + 21, r.w - 84, 18}, Font::Caption, snow3);
    } else {
        iconBadge(ctx, {r.x + (r.w - 40) / 2, y, 40, 40}, book().icon, accent, 0.5f);
    }
    y += 64;
    struct Item { Tab t; Icon i; S s; };
    const Item items[] = {{Tab::Overview, Icon::Grid, S::NavOverview},       {Tab::Transactions, Icon::List, S::NavTransactions},
                          {Tab::Accounts, Icon::Wallet, S::NavAccounts},     {Tab::Categories, Icon::Tag, S::NavCategories},
                          {Tab::Reports, Icon::Pie, S::NavReports},          {Tab::BookSettings, Icon::Sliders, S::NavBookSettings}};
    float sel = 0;
    for (int i = 0; i < 6; ++i) if (items[i].t == tab) sel = (float)i;
    float pos = ctx.anim(makeId("side-sel"), sel, 22);
    Rect pill{r.x + 10, y + pos * 44, r.w - 20, 40};
    gfx.fillRound(pill, 10, night3);
    gfx.fillRound({pill.x, pill.y + 11, 3, 18}, 1.5f, frost);
    for (int i = 0; i < 6; ++i) {
        Rect it{r.x + 10, y + i * 44, r.w - 20, 40};
        Id id = makeId((int64_t)i, makeId("side"));
        bool on = items[i].t == tab;
        if (hoverArea(ctx, id, it, 10, night3.withAlpha(0.5f), false)) setTab(items[i].t);
        Color fg = on ? snow1 : snow2;
        if (compact) gfx.icon(items[i].i, {it.cx() - 10, it.cy() - 10, 20, 20}, on ? frost : snow2);
        else {
            gfx.icon(items[i].i, {it.x + 14, it.cy() - 10, 20, 20}, on ? frost : snow3);
            gfx.text(tr(items[i].s), {it.x + 46, it.y, it.w - 52, it.h}, on ? Font::BodyStrong : Font::Body, fg);
        }
    }
    // bottom actions
    float by = r.b() - 16 - 40;
    if (book().hasPin()) {
        Rect lk{r.x + 10, by, r.w - 20, 40};
        if (hoverArea(ctx, makeId("side-lock"), lk, 10, night3.withAlpha(0.6f))) lockBook();
        if (compact) gfx.icon(Icon::Lock, {lk.cx() - 10, lk.cy() - 10, 20, 20}, snow2);
        else {
            gfx.icon(Icon::Lock, {lk.x + 14, lk.cy() - 10, 20, 20}, snow3);
            gfx.text(tr(S::LockBook), {lk.x + 46, lk.y, lk.w - 52, lk.h}, Font::Body, snow2);
        }
        by -= 44;
    }
    Rect ab{r.x + 10, by, r.w - 20, 40};
    if (hoverArea(ctx, makeId("side-books"), ab, 10, night3.withAlpha(0.6f))) goStart();
    if (compact) gfx.icon(Icon::ArrowLeft, {ab.cx() - 10, ab.cy() - 10, 20, 20}, snow2);
    else {
        gfx.icon(Icon::ArrowLeft, {ab.x + 14, ab.cy() - 10, 20, 20}, snow3);
        gfx.text(tr(S::AllBooks), {ab.x + 46, ab.y, ab.w - 52, ab.h}, Font::Body, snow2);
    }
}

void App::drawPeriodNav(const Rect& r) {
    float segW = std::min(340.0f, r.w * 0.55f);
    Rect seg{r.r() - segW, r.y, segW, r.h};
    std::vector<std::wstring> items = {tr(S::PeriodWeek), tr(S::PeriodMonth), tr(S::PeriodYear), tr(S::PeriodAll)};
    PeriodKind kinds[] = {PeriodKind::Week, PeriodKind::Month, PeriodKind::Year, PeriodKind::All};
    int cur = 1;
    for (int i = 0; i < 4; ++i) if (kinds[i] == period.kind) cur = i;
    int n = segmented(ctx, makeId("period-kind"), seg, items, cur);
    if (n != cur) setPeriodKind(kinds[n]);
    Rect nav{r.x, r.y, seg.x - 14 - r.x, r.h};
    bool all = period.kind == PeriodKind::All;
    Rect next{nav.r() - 34, nav.y + 2, 34, 34};
    float lw = std::min(200.0f, nav.w - 76);
    Rect lbl{next.x - lw, nav.y, lw, nav.h};
    Rect prev{lbl.x - 36, nav.y + 2, 34, 34};
    if (!all) {
        if (iconButton(ctx, makeId("period-prev"), prev, Icon::ChevLeft, snow2, false, 18)) shiftPeriod(-1);
        if (iconButton(ctx, makeId("period-next"), next, Icon::ChevRight, snow2, false, 18)) shiftPeriod(1);
    }
    gfx.text(periodLabel(period), lbl, Font::BodyStrong, snow1, Align::Center);
    Date today = Date::today();
    if (!all && !period.contains(today)) {
        // small "today" dot: click label to return
        Rect hit = lbl;
        if (hoverArea(ctx, makeId("period-today"), hit, 8, night3.withAlpha(0.4f))) {
            period = makePeriod(period.kind, today, prefs.fdow);
            dirty = true;
        }
        gfx.text(periodLabel(period), lbl, Font::BodyStrong, snow1, Align::Center);
        gfx.circle(lbl.cx(), lbl.b() - 3, 2, frost);
    }
}

// ---------------------------------------------------------------- charts

void App::drawDonut(const Rect& r, const std::vector<CategorySum>& data, i64 total, Id id) {
    float cx = r.cx(), cy = r.cy();
    float rad = std::min(r.w, r.h) / 2 - 8;
    float width = std::max(12.0f, rad * 0.2f);
    float prog = ctx.anim(makeId("chart-anim"), 1.0f, 6);
    gfx.ring(cx, cy, rad, night3, width);
    if (total <= 0) {
        gfx.text(tr(S::NoData), {r.x, cy - 10, r.w, 20}, Font::Caption, snow3, Align::Center);
        return;
    }
    // hover
    float dx = ctx.in.mx - cx, dy = ctx.in.my - cy;
    float dist = std::sqrt(dx * dx + dy * dy);
    int hover = -1;
    float ang = std::atan2(dx, -dy);
    if (ang < 0) ang += 2 * kPi;
    bool inRing = ctx.mouseOver(r) && dist > rad - width && dist < rad + width;
    float a = 0;
    float gap = data.size() > 1 ? 0.03f : 0.0f;
    for (size_t i = 0; i < data.size(); ++i) {
        float sweep = 2 * kPi * (float)data[i].amount / (float)total;
        if (inRing && ang >= a && ang < a + sweep) hover = (int)i;
        a += sweep;
    }
    a = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        float sweep = 2 * kPi * (float)data[i].amount / (float)total;
        const Category* c = category(data[i].category);
        Color col = c ? palette(c->color) : snow3;
        float h = ctx.anim(makeId((int64_t)i, makeId(id, 31)), hover == (int)i ? 1.0f : 0.0f, 20);
        float a0 = a * prog, a1 = (a + sweep) * prog;
        if (a1 - a0 > gap) gfx.arc(cx, cy, rad, a0 + gap / 2, a1 - gap / 2, hover >= 0 && hover != (int)i ? col.withAlpha(0.45f) : col, width + h * 6);
        a += sweep;
    }
    if (hover >= 0) {
        const Category* c = category(data[hover].category);
        gfx.text(c ? c->name : L"", {cx - rad + width, cy - 24, 2 * (rad - width), 20}, Font::Caption, snow2, Align::Center);
        drawAmount({cx - rad + width, cy - 4, 2 * (rad - width), 28}, data[hover].amount, Font::AmountMid, Font::AmountSmall, snow1, Align::Center);
        gfx.text(std::to_wstring((int)std::lround(100.0 * data[hover].amount / total)) + L"%", {cx - 40, cy + 24, 80, 18}, Font::Caption, snow3, Align::Center);
    } else {
        gfx.text(reportType == 1 && tab == Tab::Reports ? tr(S::Income) : tr(S::Expenses), {cx - rad, cy - 24, 2 * rad, 20}, Font::Caption, snow3, Align::Center);
        drawAmount({cx - rad + width, cy - 4, 2 * (rad - width), 28}, total, Font::AmountMid, Font::AmountSmall, snow1, Align::Center);
    }
}

void App::drawBars(const Rect& r, const std::vector<Bucket>& data, BucketSize size, Id id) {
    if (data.empty()) return;
    i64 mx = 0;
    for (auto& b : data) mx = std::max({mx, b.income, b.expense});
    float prog = ctx.anim(makeId("chart-anim"), 1.0f, 6);
    Rect plot{r.x + 46, r.y + 8, r.w - 46, r.h - 34};
    i64 tick = niceCeil((mx + 2) / 3);
    i64 top = tick * 3;
    for (int i = 0; i <= 3; ++i) {
        float y = plot.b() - plot.h * i / 3.0f;
        gfx.line(plot.x, std::round(y) + 0.5f, plot.r(), std::round(y) + 0.5f, i == 0 ? hairlineStrong : hairline, 1);
        if (i > 0 && mx > 0 && !prefs.privacy)
            gfx.text(compactNumber(top * i / 3, decimals()), {r.x, y - 9, 40, 18}, Font::AmountSmall, snow3, Align::Right);
    }
    size_t n = data.size();
    float slot = plot.w / (float)n;
    float bw = std::clamp(slot * 0.3f, 2.0f, 12.0f);
    int hover = -1;
    if (ctx.mouseOver(plot.inset(0, -4))) hover = std::clamp((int)((ctx.in.mx - plot.x) / slot), 0, (int)n - 1);
    int step = std::max(1, (int)std::ceil(n / (plot.w / 46.0f)));
    for (size_t i = 0; i < n; ++i) {
        float x = plot.x + slot * (float)i;
        if ((int)i == hover) gfx.fillRound({x + 1, plot.y - 4, slot - 2, plot.h + 4}, 6, night3.withAlpha(0.6f));
        if (mx > 0) {
            float hi = plot.h * (float)data[i].income / (float)top * prog;
            float he = plot.h * (float)data[i].expense / (float)top * prog;
            float c = x + slot / 2;
            if (hi > 0.5f) gfx.fillRound({c - bw - 1, plot.b() - hi, bw, hi}, std::min(bw / 2, 3.0f), income);
            if (he > 0.5f) gfx.fillRound({c + 1, plot.b() - he, bw, he}, std::min(bw / 2, 3.0f), expense);
        }
        if ((int)i % step == 0) {
            std::wstring lbl = size == BucketSize::Day ? std::to_wstring(data[i].start.d) : monthShort(data[i].start.m);
            gfx.text(lbl, {x - 10, plot.b() + 8, slot + 20, 16}, Font::Caption, snow3, Align::Center);
        }
    }
    if (hover >= 0) {
        const Bucket& b = data[hover];
        std::wstring title = size == BucketSize::Day ? dateLabel(b.start, false) : monthName(b.start.m, true) + L" " + std::to_wstring(b.start.y);
        float tw = 190, th = 82;
        float tx = std::clamp(plot.x + slot * hover + slot / 2 - tw / 2, r.x, r.r() - tw);
        Rect tip{tx, plot.y - 6, tw, th};
        if (tip.y < r.y) tip.y = r.y;
        gfx.shadow(tip, 10, 14, 0.8f);
        gfx.fillRound(tip, 10, night4);
        gfx.strokeRound(tip, 10, hairlineStrong);
        gfx.text(title, {tip.x + 12, tip.y + 8, tip.w - 24, 20}, Font::Caption, snow2);
        gfx.circle(tip.x + 16, tip.y + 41, 3.5f, income);
        drawAmount({tip.x + 28, tip.y + 32, tip.w - 40, 18}, b.income, Font::Amount, Font::AmountSmall, snow1, Align::Left);
        gfx.circle(tip.x + 16, tip.y + 63, 3.5f, expense);
        drawAmount({tip.x + 28, tip.y + 54, tip.w - 40, 18}, b.expense, Font::Amount, Font::AmountSmall, snow1, Align::Left);
    }
}

void App::drawTxRow(const Rect& r, const Transaction& t, Id id) {
    bool clicked = hoverArea(ctx, id, r, 10, night3.withAlpha(0.7f));
    Color ic;
    Icon icon = txIcon(t, ic);
    iconBadge(ctx, {r.x + 12, r.cy() - 18, 36, 36}, icon, ic, 0.5f);
    std::wstring sub;
    auto add = [&](const std::wstring& s) { if (s.empty()) return; if (!sub.empty()) sub += L"  ·  "; sub += s; };
    add(t.payee);
    add(t.note);
    if (t.kind == TxKind::Transfer) {
        const Account* a = account(t.account());
        const Account* b = account(t.toAccount());
        add((a ? a->name : L"?") + L"  →  " + (b ? b->name : L"?"));
    } else if (accountsAll.size() > 1) {
        const Account* a = account(t.account());
        if (a) add(a->name);
    }
    float amtW = 170;
    Rect textR{r.x + 60, r.y, r.w - 60 - amtW - 12, r.h};
    if (sub.empty()) gfx.text(txTitle(t), textR, Font::Body, snow1);
    else {
        gfx.text(txTitle(t), {textR.x, r.y + 8, textR.w, 22}, Font::Body, snow1);
        gfx.text(sub, {textR.x, r.y + 28, textR.w, 18}, Font::Caption, snow3);
    }
    i64 v = t.kind == TxKind::Adjustment ? t.signedAmount() : t.absAmount();
    Color col = txColor(t);
    drawAmount({r.r() - amtW - 14, r.y, amtW, r.h}, v, Font::Amount, Font::AmountSmall, col, Align::Right,
               t.kind == TxKind::Income || (t.kind == TxKind::Adjustment && v > 0));
    if (clicked) {
        if (t.deletedAt) return;
        editTransaction(t.id);
    }
}

// ---------------------------------------------------------------- overview

void App::drawOverview(const Rect& r) {
    bool wide = r.w >= 940;
    float gap = 16;
    // layout pass
    int statCols = wide ? 4 : 2;
    float statW = (r.w - gap * (statCols - 1)) / statCols, statH = 116;
    float y0 = 0;
    float statsH = wide ? statH : statH * 2 + gap;
    float rowY = y0 + statsH + gap;
    float catH = 300;
    float leftW = wide ? std::round(r.w * 0.58f) : r.w;
    float accH = std::max(catH, 84.0f + 52.0f * std::min<size_t>(accounts.size(), 8));
    if (wide) accH = catH;
    float flowY = wide ? rowY + catH + gap : rowY + catH + gap + accH + gap;
    float flowH = 270;
    float recentY = flowY + flowH + gap;
    float recentH = recent.empty() ? 180 : 70 + 58.0f * recent.size();
    float contentH = recentY + recentH + 24;

    float off = ctx.scrollBegin(makeId("ov-scroll"), {r.x - 12, r.y, r.w + 24, r.h}, contentH);
    float Y = r.y - off;

    // stat cards
    auto stat = [&](int idx, S label, i64 value, Icon icon, Color col, const std::wstring& sub, Color subCol) {
        int col_ = idx % statCols, row = idx / statCols;
        Rect c{r.x + col_ * (statW + gap), Y + row * (statH + gap), statW, statH};
        card(ctx, c);
        sectionLabel(ctx, {c.x + 20, c.y + 16, c.w - 70, 18}, tr(label));
        iconBadge(ctx, {c.r() - 50, c.y + 14, 30, 30}, icon, col, 0.5f);
        drawAmount({c.x + 20, c.y + 42, c.w - 40, 38}, value, Font::AmountLarge, Font::AmountSmall, snow1, Align::Left);
        gfx.text(sub, {c.x + 20, c.b() - 34, c.w - 40, 18}, Font::Caption, subCol);
    };
    auto delta = [&](i64 now, i64 prev, bool upIsGood, Color& col) -> std::wstring {
        if (period.kind == PeriodKind::All || prev == 0) { col = snow3; return L""; }
        double p = 100.0 * (double)(now - prev) / (double)std::llabs(prev);
        bool up = p >= 0;
        col = (up == upIsGood) ? income : expense;
        wchar_t b[48];
        swprintf(b, 48, L"%s%.0f%%  ", up ? L"+" : L"−", std::fabs(p));
        return std::wstring(b) + tr(S::VsPrevious);
    };
    Color dc;
    std::wstring accTxt = std::to_wstring(accounts.size()) + L" " + plural((long long)accounts.size(), S::AccOne, S::AccFew, S::AccMany);
    stat(0, S::Balance, balance, Icon::Wallet, frost, accTxt, snow3);
    std::wstring di = delta(totals.income, prevTotals.income, true, dc);
    stat(1, S::Income, totals.income, Icon::ArrowDown, income, di, dc);
    std::wstring de = delta(totals.expense, prevTotals.expense, false, dc);
    stat(2, S::Expenses, totals.expense, Icon::ArrowUp, expense, de, dc);
    std::wstring saved;
    if (totals.income > 0) saved = std::wstring(tr(S::SavingsRate)) + L" " + std::to_wstring((int)std::lround(100.0 * totals.net() / totals.income)) + L"%";
    stat(3, S::Net, totals.net(), Icon::Pulse, totals.net() >= 0 ? income : expense, saved, snow3);

    // expenses by category
    Rect cc{r.x, Y + rowY, leftW, catH};
    card(ctx, cc);
    sectionLabel(ctx, {cc.x + 20, cc.y + 16, cc.w - 40, 18}, tr(S::ExpensesByCategory));
    float ds = std::min(cc.h - 64, cc.w * 0.42f);
    drawDonut({cc.x + 20, cc.y + 46, ds, ds}, expByCat, totals.expense, makeId("ov-donut"));
    float lx = cc.x + 40 + ds, lw = cc.r() - 20 - lx;
    size_t maxRows = (size_t)((cc.h - 60) / 38);
    i64 other = 0;
    for (size_t i = 0; i < expByCat.size(); ++i) {
        if (i >= maxRows - 1 && expByCat.size() > maxRows) { other += expByCat[i].amount; continue; }
        const Category* c = category(expByCat[i].category);
        Rect row{lx - 8, cc.y + 46 + 38.0f * i, lw + 8, 36};
        if (hoverArea(ctx, makeId(expByCat[i].category, makeId("ov-cat")), row, 8, night3.withAlpha(0.6f)) && c) {
            search = c->name;
            setTab(Tab::Transactions);
        }
        Color col = c ? palette(c->color) : snow3;
        iconBadge(ctx, {row.x + 6, row.cy() - 13, 26, 26}, c ? c->icon : Icon::Tag, col, 0.55f);
        int pct = totals.expense ? (int)std::lround(100.0 * expByCat[i].amount / totals.expense) : 0;
        gfx.text(c ? c->name : L"?", {row.x + 42, row.y, row.w - 42 - 150, row.h}, Font::Small, snow1);
        gfx.text(std::to_wstring(pct) + L"%", {row.r() - 150, row.y, 40, row.h}, Font::AmountSmall, snow3, Align::Right);
        drawAmount({row.r() - 108, row.y, 100, row.h}, expByCat[i].amount, Font::Amount, Font::AmountSmall, snow1, Align::Right, false, false);
    }
    if (other > 0) {
        Rect row{lx - 8, cc.y + 46 + 38.0f * (maxRows - 1), lw + 8, 36};
        iconBadge(ctx, {row.x + 6, row.cy() - 13, 26, 26}, Icon::Dots, snow3, 0.55f);
        gfx.text(tr(S::OtherCategories), {row.x + 42, row.y, row.w - 190, row.h}, Font::Small, snow2);
        drawAmount({row.r() - 108, row.y, 100, row.h}, other, Font::Amount, Font::AmountSmall, snow2, Align::Right, false, false);
    }

    // accounts
    Rect ac = wide ? Rect{cc.r() + gap, Y + rowY, r.w - leftW - gap, catH} : Rect{r.x, cc.b() + gap, r.w, accH};
    card(ctx, ac);
    sectionLabel(ctx, {ac.x + 20, ac.y + 16, ac.w - 40, 18}, tr(S::NavAccounts));
    float ay = ac.y + 46;
    for (size_t i = 0; i < accounts.size(); ++i) {
        if (ay + 50 > ac.b() - 8) break;
        const Account& a = accounts[i];
        Rect row{ac.x + 8, ay, ac.w - 16, 48};
        if (hoverArea(ctx, makeId(a.id, makeId("ov-acc")), row, 8, night3.withAlpha(0.6f))) showDialog(makeAccountDialog(*this, &a));
        iconBadge(ctx, {row.x + 12, row.cy() - 15, 30, 30}, accountIcon(a.type), palette(a.color), 0.52f);
        gfx.text(a.name, {row.x + 54, row.y, row.w - 54 - 160, row.h}, Font::Body, snow1);
        drawAmount({row.r() - 170, row.y, 158, row.h}, a.balance, Font::Amount, Font::AmountSmall, a.balance < 0 ? expense : snow1, Align::Right);
        ay += 50;
    }

    // cash flow
    Rect fc{r.x, Y + flowY, r.w, flowH};
    card(ctx, fc);
    sectionLabel(ctx, {fc.x + 20, fc.y + 16, 300, 18}, tr(S::CashFlow));
    gfx.circle(fc.r() - 170, fc.y + 25, 4, income);
    gfx.text(tr(S::Income), {fc.r() - 160, fc.y + 15, 70, 20}, Font::Caption, snow2);
    gfx.circle(fc.r() - 84, fc.y + 25, 4, expense);
    gfx.text(tr(S::Expenses), {fc.r() - 74, fc.y + 15, 70, 20}, Font::Caption, snow2);
    BucketSize bs = (period.kind == PeriodKind::Year || period.kind == PeriodKind::All || period.kind == PeriodKind::Quarter)
                        ? BucketSize::Month : BucketSize::Day;
    drawBars({fc.x + 16, fc.y + 50, fc.w - 36, fc.h - 64}, series, bs, makeId("ov-bars"));

    // recent
    Rect rc{r.x, Y + recentY, r.w, recentH};
    card(ctx, rc);
    sectionLabel(ctx, {rc.x + 20, rc.y + 18, 300, 18}, tr(S::Recent));
    if (!recent.empty()) {
        float sw = gfx.measure(tr(S::ShowAll), Font::Small) + 20;
        Rect sa{rc.r() - sw - 12, rc.y + 10, sw, 32};
        if (hoverArea(ctx, makeId("ov-showall"), sa, 8, night3)) setTab(Tab::Transactions);
        gfx.text(tr(S::ShowAll), sa, Font::Small, frost, Align::Center);
    }
    if (recent.empty()) {
        drawEmpty({rc.x, rc.y + 40, rc.w, rc.h - 40}, Icon::Plus, tr(S::NoTransactions), tr(S::NoTransactionsHint));
    } else {
        for (size_t i = 0; i < recent.size(); ++i)
            drawTxRow({rc.x + 8, rc.y + 54 + 58.0f * i, rc.w - 16, 56}, recent[i], makeId(recent[i].id, makeId("ov-tx")));
    }
    ctx.scrollEnd();
}

// ---------------------------------------------------------------- transactions

void App::drawTransactions(const Rect& r) {
    // toolbar
    Rect tb{r.x, r.y, r.w, 40};
    float sw = std::min(340.0f, tb.w * 0.4f);
    textField(ctx, makeId("tx-search"), {tb.x, tb.y, sw, 40}, search, tr(S::SearchPlaceholder), {Font::Body, false, false, 80, Align::Left, Icon::Search});
    if (!search.empty()) {
        if (iconButton(ctx, makeId("tx-search-clear"), {tb.x + sw - 36, tb.y + 4, 32, 32}, Icon::Close, snow3, false, 14)) search.clear();
    }
    Rect trashB{tb.r() - 40, tb.y, 40, 40};
    if (iconButton(ctx, makeId("tx-trash"), trashB, Icon::Trash, showTrash ? frost : snow2, showTrash, 18, night3)) {
        showTrash = !showTrash;
        if (showTrash) trash = db().trash();
    }
    float fw = std::min(420.0f, trashB.x - 12 - (tb.x + sw + 12));
    if (!showTrash && fw > 200) {
        std::vector<std::wstring> f = {tr(S::PeriodAll), tr(S::Expenses), tr(S::Income), tr(S::Transfer)};
        if (accounts.size() <= 1) f.pop_back();
        txFilter = std::min(txFilter, (int)f.size() - 1);
        txFilter = segmented(ctx, makeId("tx-filter"), {trashB.x - 12 - fw, tb.y, fw, 40}, f, txFilter);
    }

    // filtered rows (cached)
    const std::vector<Transaction>& src = showTrash ? trash : txList;
    i64 firstId = src.empty() ? 0 : src.front().id;
    if (g_rows.src != &src || g_rows.srcSize != src.size() || g_rows.firstId != firstId || g_rows.search != search ||
        g_rows.filter != txFilter || g_rows.trash != showTrash || dirty) {
        g_rows.src = &src;
        g_rows.srcSize = src.size();
        g_rows.firstId = firstId;
        g_rows.search = search;
        g_rows.filter = txFilter;
        g_rows.trash = showTrash;
        g_rows.items.clear();
        std::wstring q = trim(search);
        for (auto& t : src) {
            if (!showTrash) {
                if (txFilter == 1 && t.kind != TxKind::Expense) continue;
                if (txFilter == 2 && t.kind != TxKind::Income) continue;
                if (txFilter == 3 && t.kind != TxKind::Transfer) continue;
            }
            if (!q.empty() && !containsCI(txTitle(t), q) && !containsCI(t.payee, q) && !containsCI(t.note, q)) continue;
            g_rows.items.push_back(&t);
        }
    }
    auto& items = g_rows.items;
    Rect list{r.x, r.y + 56, r.w, r.h - 56};
    if (showTrash) {
        gfx.text(tr(S::TrashHint), {list.x, list.y - 8, list.w, 20}, Font::Caption, snow3);
        list = list.cutTop(20);
    }
    if (items.empty()) {
        drawEmpty(list, showTrash ? Icon::Trash : Icon::List, showTrash ? tr(S::TrashEmpty) : tr(S::NoTransactions),
                  showTrash ? L"" : tr(S::NoTransactionsHint));
        return;
    }
    // group by day: [header 34][card rows*58+8][gap 18]
    struct Group { Date d; size_t from, to; float y, h; i64 net; };
    std::vector<Group> groups;
    float y = 0;
    for (size_t i = 0; i < items.size();) {
        size_t j = i;
        i64 net = 0;
        while (j < items.size() && items[j]->date == items[i]->date) {
            if (items[j]->kind == TxKind::Income) net += items[j]->absAmount();
            else if (items[j]->kind == TxKind::Expense) net -= items[j]->absAmount();
            ++j;
        }
        float h = 34 + 58.0f * (j - i) + 8;
        groups.push_back({items[i]->date, i, j, y, h, net});
        y += h + 14;
        i = j;
    }
    float off = ctx.scrollBegin(makeId("tx-scroll"), {list.x - 12, list.y, list.w + 24, list.h}, y + 20);
    for (auto& gp : groups) {
        float gy = list.y + gp.y - off;
        if (gy + gp.h < list.y || gy > list.b()) continue;
        gfx.text(dateLabel(gp.d), {list.x + 4, gy, 300, 26}, Font::BodyStrong, snow2);
        if (!showTrash && gp.net != 0)
            drawAmount({list.r() - 220, gy, 216, 26}, gp.net, Font::AmountSmall, Font::AmountSmall, snow3, Align::Right, gp.net > 0);
        Rect cardR{list.x, gy + 34, list.w, gp.h - 34};
        card(ctx, cardR);
        for (size_t k = gp.from; k < gp.to; ++k) {
            const Transaction& t = *items[k];
            Rect row{cardR.x + 4, cardR.y + 4 + 58.0f * (k - gp.from), cardR.w - 8, 56};
            if (row.b() < list.y || row.y > list.b()) continue;
            if (k > gp.from) gfx.line(row.x + 60, row.y - 1, row.r() - 12, row.y - 1, hairline);
            if (showTrash) {
                Rect rowText = row.cutRight(250);
                drawTxRow(rowText, t, makeId(t.id, makeId("trash-row")));
                if (button(ctx, makeId(t.id, makeId("trash-restore")), {row.r() - 244, row.y + 10, 116, 36}, tr(S::Restore), Btn::Subtle)) {
                    db().restore(t.id);
                    trash = db().trash();
                    dirty = true;
                }
                if (button(ctx, makeId(t.id, makeId("trash-del")), {row.r() - 120, row.y + 10, 112, 36}, tr(S::Delete), Btn::Danger)) {
                    db().hardDelete(t.id);
                    trash = db().trash();
                    dirty = true;
                }
            } else {
                drawTxRow(row, t, makeId(t.id, makeId("tx-row")));
            }
        }
    }
    ctx.scrollEnd();
}

// ---------------------------------------------------------------- accounts

void App::drawAccounts(const Rect& r) {
    const std::vector<Account>& list = showArchived ? accountsAll : accounts;
    Rect top{r.x, r.y, r.w, 64};
    sectionLabel(ctx, {top.x, top.y, 300, 18}, tr(S::TotalBalance));
    drawAmount({top.x, top.y + 20, 420, 44}, balance, Font::AmountHuge, Font::AmountSmall, snow1, Align::Left);
    bool sa = showArchived;
    gfx.text(tr(S::ShowArchived), {top.r() - 260, top.y + 24, 200, 40}, Font::Small, snow2, Align::Right);
    if (toggle(ctx, makeId("acc-archived"), {top.r() - 60, top.y + 24, 60, 40}, sa)) showArchived = sa;

    int cols = r.w > 980 ? 3 : (r.w > 620 ? 2 : 1);
    float gap = 16, cw = (r.w - gap * (cols - 1)) / cols, ch = 128;
    Rect area{r.x, r.y + 88, r.w, r.h - 88};
    int rows = ((int)list.size() + cols - 1) / cols;
    float off = ctx.scrollBegin(makeId("acc-scroll"), {area.x - 12, area.y, area.w + 24, area.h}, rows * (ch + gap) + 8);
    for (size_t i = 0; i < list.size(); ++i) {
        const Account& a = list[i];
        Rect c{area.x + (i % cols) * (cw + gap), area.y + (i / cols) * (ch + gap) - off, cw, ch};
        if (c.b() < area.y || c.y > area.b()) continue;
        Id id = makeId(a.id, makeId("acc-card"));
        bool over = ctx.mouseOver(c);
        float h = ctx.anim(makeId(id, 2), over ? 1.0f : 0.0f, 18);
        Color col = palette(a.color);
        gfx.fillRound(c, radiusM, mix(night2, rgb(0x1F2530), h));
        gfx.strokeRound(c, radiusM, mix(hairline, col.withAlpha(0.35f), h));
        if (a.archived) gfx.pushOpacity(0.5f);
        iconBadge(ctx, {c.x + 18, c.y + 18, 38, 38}, accountIcon(a.type), col, 0.5f);
        gfx.text(a.name, {c.x + 68, c.y + 18, c.w - 86, 22}, Font::BodyStrong, snow1);
        gfx.text(a.archived ? std::wstring(tr(S::Archived)) : accountTypeName(a.type), {c.x + 68, c.y + 38, c.w - 86, 18}, Font::Caption, snow3);
        drawAmount({c.x + 18, c.b() - 54, c.w - 36, 36}, a.balance, Font::AmountLarge, Font::AmountSmall, a.balance < 0 ? expense : snow1, Align::Left);
        if (a.archived) gfx.popOpacity();
        if (over) ctx.cursor = Cursor::Hand;
        if (ctx.clicked(id, c, over)) showDialog(makeAccountDialog(*this, &a));
    }
    ctx.scrollEnd();
}

// ---------------------------------------------------------------- categories

void App::drawCategories(const Rect& r) {
    bool two = r.w >= 760;
    float gap = 16;
    float colW = two ? (r.w - gap) / 2 : r.w;
    auto listFor = [&](CatType t) {
        std::vector<const Category*> out;
        for (auto& c : cats)
            if (c.type == t && c.parent == 0) {
                out.push_back(&c);
                for (auto& ch : cats) if (ch.parent == c.id) out.push_back(&ch);
            }
        std::stable_partition(out.begin(), out.end(), [](const Category* c) { return !c->archived; });
        return out;
    };
    auto exp = listFor(CatType::Expense), inc = listFor(CatType::Income);
    float rowH = 46;
    float hExp = 64 + rowH * exp.size() + 8, hInc = 64 + rowH * inc.size() + 8;
    float contentH = two ? std::max(hExp, hInc) : hExp + gap + hInc;
    float off = ctx.scrollBegin(makeId("cat-scroll"), {r.x - 12, r.y, r.w + 24, r.h}, contentH + 16);
    auto column = [&](const Rect& c, CatType t, const std::vector<const Category*>& items) {
        card(ctx, c);
        sectionLabel(ctx, {c.x + 20, c.y + 22, c.w - 120, 18}, t == CatType::Expense ? tr(S::Expenses) : tr(S::Income));
        if (iconButton(ctx, makeId((int)t, makeId("cat-add")), {c.r() - 48, c.y + 12, 36, 36}, Icon::Plus, frost, false, 18, night3))
            showDialog(makeCategoryDialog(*this, nullptr, t));
        for (size_t i = 0; i < items.size(); ++i) {
            const Category* cat = items[i];
            float indent = cat->parent ? 30 : 0;
            Rect row{c.x + 8 + indent, c.y + 60 + rowH * i, c.w - 16 - indent, rowH - 2};
            if (row.b() < r.y || row.y > r.b()) continue;
            if (hoverArea(ctx, makeId(cat->id, makeId("cat-row")), row, 8, night3.withAlpha(0.6f)))
                showDialog(makeCategoryDialog(*this, cat, t));
            if (cat->archived) gfx.pushOpacity(0.45f);
            float bs = cat->parent ? 26 : 32;
            iconBadge(ctx, {row.x + 10, row.cy() - bs / 2, bs, bs}, cat->icon, palette(cat->color), 0.52f);
            gfx.text(cat->name, {row.x + 20 + bs, row.y, row.w - 40 - bs - 80, row.h}, cat->parent ? Font::Small : Font::Body, snow1);
            if (cat->archived) gfx.text(tr(S::Archived), {row.r() - 110, row.y, 100, row.h}, Font::Caption, snow3, Align::Right);
            if (cat->archived) gfx.popOpacity();
        }
    };
    Rect e{r.x, r.y - off, colW, hExp};
    Rect in = two ? Rect{r.x + colW + gap, r.y - off, colW, hInc} : Rect{r.x, e.b() + gap, colW, hInc};
    column(e, CatType::Expense, exp);
    column(in, CatType::Income, inc);
    ctx.scrollEnd();
}

// ---------------------------------------------------------------- reports

void App::drawReports(const Rect& r) {
    Rect segR{r.x, r.y, 300, 40};
    reportType = segmented(ctx, makeId("rep-type"), segR, {tr(S::Expenses), tr(S::Income)}, reportType, reportType ? income : expense);
    const auto& data = reportType ? incByCat : expByCat;
    i64 total = reportType ? totals.income : totals.expense;
    float gap = 16;
    float rowH = 44;
    float listH = std::max(320.0f, 70 + rowH * data.size());
    bool wide = r.w >= 900;
    float donutCardH = wide ? listH : 340 + listH;
    float flowH = 300, statsH = 120;
    float contentH = 56 + donutCardH + gap + statsH + gap + flowH + 24;
    float off = ctx.scrollBegin(makeId("rep-scroll"), {r.x - 12, r.y + 56, r.w + 24, r.h - 56}, contentH - 56);
    float Y = r.y + 56 - off;

    // stats row
    int nTx = 0;
    for (auto& d : data) nTx += d.count;
    int days = std::max(1, period.kind == PeriodKind::All ? (Date::today().days() - db().firstDate().days() + 1) : period.lengthDays());
    bool perMonth = period.kind == PeriodKind::Year || period.kind == PeriodKind::All;
    i64 avg = perMonth ? total * 30 / days : total / days;
    float sw = (r.w - gap * 2) / 3;
    struct St { const wchar_t* label; std::wstring value; bool amount; i64 v; };
    const Category* big = data.empty() ? nullptr : category(data.front().category);
    St st[3] = {{reportType ? tr(S::Income) : tr(S::Expenses), L"", true, total},
                {perMonth ? tr(S::AveragePerMonth) : tr(S::AveragePerDay), L"", true, avg},
                {tr(S::Largest), big ? big->name : L"—", false, 0}};
    for (int i = 0; i < 3; ++i) {
        Rect c{r.x + i * (sw + gap), Y, sw, statsH};
        card(ctx, c);
        sectionLabel(ctx, {c.x + 20, c.y + 18, c.w - 40, 18}, st[i].label);
        if (st[i].amount) drawAmount({c.x + 20, c.y + 46, c.w - 40, 40}, st[i].v, Font::AmountLarge, Font::AmountSmall, snow1, Align::Left);
        else gfx.text(st[i].value, {c.x + 20, c.y + 46, c.w - 40, 40}, Font::Subtitle, snow1);
        if (i == 0) gfx.text(std::to_wstring(nTx) + L" " + plural(nTx, S::OpOne, S::OpFew, S::OpMany), {c.x + 20, c.b() - 30, c.w - 40, 18}, Font::Caption, snow3);
    }
    Y += statsH + gap;

    // donut + list
    Rect dc{r.x, Y, r.w, donutCardH};
    card(ctx, dc);
    sectionLabel(ctx, {dc.x + 24, dc.y + 20, 400, 18}, reportType ? tr(S::IncomeByCategory) : tr(S::ExpensesByCategory));
    float ds = wide ? std::min(300.0f, std::min(dc.w * 0.36f, listH - 70)) : std::min(300.0f, dc.w - 48);
    Rect donut = wide ? Rect{dc.x + 24, dc.y + 56, ds, ds} : Rect{dc.cx() - ds / 2, dc.y + 56, ds, ds};
    drawDonut(donut, data, total, makeId("rep-donut"));
    Rect lst = wide ? Rect{donut.r() + 32, dc.y + 50, dc.r() - donut.r() - 56, listH - 60}
                    : Rect{dc.x + 16, donut.b() + 24, dc.w - 32, listH - 60};
    for (size_t i = 0; i < data.size(); ++i) {
        const Category* c = category(data[i].category);
        Rect row{lst.x, lst.y + rowH * i, lst.w, rowH - 4};
        if (hoverArea(ctx, makeId(data[i].category, makeId("rep-cat")), row, 8, night3.withAlpha(0.6f)) && c) {
            search = c->name;
            txFilter = reportType ? 2 : 1;
            setTab(Tab::Transactions);
        }
        Color col = c ? palette(c->color) : snow3;
        iconBadge(ctx, {row.x + 8, row.cy() - 14, 28, 28}, c ? c->icon : Icon::Tag, col, 0.55f);
        float share = total ? (float)data[i].amount / (float)total : 0;
        float nameW = std::min(220.0f, row.w * 0.32f);
        gfx.text(c ? c->name : L"?", {row.x + 46, row.y, nameW, row.h}, Font::Small, snow1);
        Rect bar{row.x + 56 + nameW, row.cy() - 3, row.w - nameW - 56 - 210, 6};
        if (bar.w > 30) {
            gfx.fillRound(bar, 3, night3);
            float prog = ctx.anim(makeId("chart-anim"), 1.0f, 6);
            gfx.fillRound({bar.x, bar.y, std::max(6.0f, bar.w * share * prog), bar.h}, 3, col);
        }
        gfx.text(std::to_wstring((int)std::lround(share * 100)) + L"%", {row.r() - 200, row.y, 44, row.h}, Font::AmountSmall, snow3, Align::Right);
        drawAmount({row.r() - 150, row.y, 142, row.h}, data[i].amount, Font::Amount, Font::AmountSmall, snow1, Align::Right);
    }
    if (data.empty()) gfx.text(tr(S::NoData), lst.takeTop(40), Font::Small, snow3);
    Y += donutCardH + gap;

    // cash flow
    Rect fc{r.x, Y, r.w, flowH};
    card(ctx, fc);
    sectionLabel(ctx, {fc.x + 24, fc.y + 20, 400, 18}, tr(S::CashFlow));
    BucketSize bs = (period.kind == PeriodKind::Year || period.kind == PeriodKind::All || period.kind == PeriodKind::Quarter)
                        ? BucketSize::Month : BucketSize::Day;
    drawBars({fc.x + 20, fc.y + 56, fc.w - 40, fc.h - 72}, series, bs, makeId("rep-bars"));
    ctx.scrollEnd();
}

// ---------------------------------------------------------------- book settings

void App::drawBookSettings(const Rect& r) {
    BookRecord& b = book();
    if (g_bookEdit.bookId != b.id) g_bookEdit = {b.id, b.name, b.kind, b.color, b.icon};
    float colW = std::min(760.0f, r.w);
    float contentH = 820;
    float off = ctx.scrollBegin(makeId("bs-scroll"), {r.x - 12, r.y, r.w + 24, r.h}, contentH);
    float y = r.y - off;

    // book card
    Rect c1{r.x, y, colW, 420};
    card(ctx, c1);
    float x = c1.x + 24, w = c1.w - 48, cy = c1.y + 20;
    sectionLabel(ctx, {x, cy, w, 18}, tr(S::Name));
    textField(ctx, makeId("bs-name"), {x, cy + 24, w, fieldH}, g_bookEdit.name, tr(S::BookNamePlaceholder));
    cy += 84;
    sectionLabel(ctx, {x, cy, w, 18}, tr(S::Type));
    std::vector<std::wstring> kinds;
    for (int k = 0; k < 5; ++k) kinds.push_back(kindName((BookKind)k));
    g_bookEdit.kind = (BookKind)segmented(ctx, makeId("bs-kind"), {x, cy + 24, w, 38}, kinds, (int)g_bookEdit.kind);
    cy += 80;
    sectionLabel(ctx, {x, cy, w, 18}, tr(S::Color));
    for (int i = 0; i < 16; ++i)
        if (swatch(ctx, makeId((int64_t)i, makeId("bs-color")), {x + i * 36.0f, cy + 22, 32, 32}, palette(i), g_bookEdit.color == i)) g_bookEdit.color = i;
    cy += 70;
    sectionLabel(ctx, {x, cy, w, 18}, tr(S::IconLabel));
    auto icons = bookIcons();
    for (size_t i = 0; i < icons.size(); ++i) {
        Rect ib{x + i * 42.0f, cy + 22, 38, 38};
        bool on = g_bookEdit.icon == icons[i];
        if (hoverArea(ctx, makeId((int64_t)i, makeId("bs-icon")), ib, 10, night3, on, palette(g_bookEdit.color).withAlpha(0.18f))) g_bookEdit.icon = icons[i];
        gfx.icon(icons[i], ib.inset(9), on ? palette(g_bookEdit.color) : snow2);
    }
    bool changed = g_bookEdit.name != b.name || g_bookEdit.kind != b.kind || g_bookEdit.color != b.color || g_bookEdit.icon != b.icon;
    Rect save{c1.r() - 24 - 150, c1.b() - 24 - buttonH, 150, buttonH};
    if (button(ctx, makeId("bs-save"), save, tr(S::Save), Btn::Primary, Icon::None, changed && !trim(g_bookEdit.name).empty())) {
        BookRecord upd = b;
        upd.name = trim(g_bookEdit.name);
        upd.kind = g_bookEdit.kind;
        upd.color = g_bookEdit.color;
        upd.icon = g_bookEdit.icon;
        lib.updateBookInfo(upd);
        g_bookEdit.name = upd.name;
        showToast(tr(S::TxSaved));
    }
    gfx.text(fromUtf8(b.currency) + L"  ·  " + tr(S::CurrencyFixedHint), {x, save.y, save.x - x - 16, buttonH}, Font::Caption, snow3);
    y = c1.b() + 16;

    // security
    Rect c2{r.x, y, colW, 150};
    card(ctx, c2);
    sectionLabel(ctx, {c2.x + 24, c2.y + 20, 300, 18}, tr(S::Security));
    iconBadge(ctx, {c2.x + 24, c2.y + 50, 40, 40}, b.hasPin() ? Icon::Lock : Icon::Unlock, b.hasPin() ? income : snow3, 0.5f);
    gfx.text(b.hasPin() ? tr(S::PinOn) : tr(S::PinOff), {c2.x + 76, c2.y + 50, 300, 22}, Font::BodyStrong, snow1);
    gfx.textWrapped(tr(S::PinHint), {c2.x + 76, c2.y + 74, c2.w - 76 - 24, 60}, Font::Caption, snow3);
    if (b.hasPin()) {
        if (button(ctx, makeId("bs-pin-change"), {c2.r() - 24 - 330, c2.b() - 24 - 36, 160, 36}, tr(S::ChangePin), Btn::Secondary))
            showDialog(makePinDialog(*this, PinMode::Change));
        if (button(ctx, makeId("bs-pin-remove"), {c2.r() - 24 - 160, c2.b() - 24 - 36, 160, 36}, tr(S::RemovePin), Btn::Secondary))
            showDialog(makePinDialog(*this, PinMode::Remove));
    } else {
        if (button(ctx, makeId("bs-pin-set"), {c2.r() - 24 - 180, c2.b() - 24 - 36, 180, 36}, tr(S::SetPin), Btn::Primary, Icon::Lock))
            showDialog(makePinDialog(*this, PinMode::Set));
    }
    y = c2.b() + 16;

    // danger
    Rect c3{r.x, y, colW, 96};
    card(ctx, c3);
    gfx.text(tr(S::DeleteBook), {c3.x + 24, c3.y + 18, c3.w - 260, 22}, Font::BodyStrong, snow1);
    gfx.text(tr(S::DeleteBookConfirm), {c3.x + 24, c3.y + 42, c3.w - 260, 36}, Font::Caption, snow3);
    if (button(ctx, makeId("bs-delete"), {c3.r() - 24 - 180, c3.cy() - 18, 180, 36}, tr(S::DeleteBook), Btn::Danger, Icon::Trash)) {
        i64 id = b.id;
        showDialog(makeConfirmDialog(tr(S::DeleteConfirmTitle), b.name + L"\n" + tr(S::DeleteBookConfirm), tr(S::Delete),
                                     [id](App& a) { a.lib.deleteBook(id); a.screen = Screen::Start; a.cardsDirty = true; }));
    }
    ctx.scrollEnd();
}

}  // namespace fp
