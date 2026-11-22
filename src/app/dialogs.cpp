#include <windows.h>

#include <algorithm>
#include <cmath>
#include <ctime>

#include "app/app.h"
#include "core/text.h"

namespace fp {

using namespace ui;
using namespace ui::theme;

std::wstring pickSavePath(App& a);

namespace {

void header(App& a, Ctx& c, const Rect& r, const std::wstring& title, bool& done) {
    a.gfx.text(title, {r.x + 24, r.y + 18, r.w - 90, 30}, Font::Subtitle, snow1);
    if (iconButton(c, makeId("dlg-close"), {r.r() - 52, r.y + 14, 36, 36}, Icon::Close, snow2, false, 16)) done = true;
    if (!c.popupOpen() && c.consumeKey(VK_ESCAPE)) done = true;
}

float footerY(const Rect& r) { return r.b() - 24 - buttonH; }

std::vector<DropItem> accountItems(App& a, const std::vector<Account>& accs) {
    std::vector<DropItem> v;
    for (auto& acc : accs) v.push_back({acc.name, accountIcon(acc.type), palette(acc.color), a.money(acc.balance)});
    return v;
}

int indexOfAccount(const std::vector<Account>& accs, i64 id) {
    for (size_t i = 0; i < accs.size(); ++i) if (accs[i].id == id) return (int)i;
    return -1;
}

// ================================================================ transaction

struct TxDialog : Dialog {
    Transaction t;
    bool isNew = true;
    std::wstring amountText, error;
    i64 catId = 0, accId = 0, toAccId = 0;
    std::vector<Account> accs;
    std::wstring payeeKey;
    std::vector<std::wstring> payeeSuggest;
    bool focusAmount = true;

    TxDialog(App& a, const Transaction& tx, bool n) : t(tx), isNew(n) {
        accs = a.accounts;
        // an archived account used by this transaction must stay selectable
        for (auto& l : t.lines)
            if (indexOfAccount(accs, l.account) < 0)
                if (const Account* ac = a.account(l.account)) accs.push_back(*ac);
        if (!isNew) {
            i64 v = t.kind == TxKind::Adjustment ? t.signedAmount() : t.absAmount();
            amountText = formatPlain(v, a.decimals(), a.nf.decimal);
            catId = t.category();
            accId = t.account();
            toAccId = t.toAccount();
        }
        if (!accId && !accs.empty()) accId = accs.front().id;
        if (!toAccId && accs.size() > 1) toAccId = accs[1].id == accId ? accs[0].id : accs[1].id;
    }

    float width() const override { return 640; }

    std::vector<const Category*> tops(App& a) const {
        std::vector<const Category*> v;
        CatType ct = t.kind == TxKind::Income ? CatType::Income : CatType::Expense;
        for (auto& c : a.cats) if (c.type == ct && c.parent == 0 && (!c.archived || c.id == catId)) v.push_back(&c);
        return v;
    }
    std::vector<const Category*> children(App& a, i64 parent) const {
        std::vector<const Category*> v;
        for (auto& c : a.cats) if (c.parent == parent && (!c.archived || c.id == catId)) v.push_back(&c);
        return v;
    }
    i64 topOf(App& a, i64 id) const {
        const Category* c = a.category(id);
        return c ? (c->parent ? c->parent : c->id) : 0;
    }
    bool hasCategories() const { return t.kind == TxKind::Expense || t.kind == TxKind::Income; }
    int gridCols(float w) const { return std::max(3, (int)((w + 8) / (88 + 8))); }
    float gridH(App& a, float w) const {
        if (!hasCategories()) return 0;
        int n = (int)tops(a).size();
        int rows = (n + gridCols(w) - 1) / gridCols(w);
        return std::min(rows, 3) * 84.0f;
    }
    float subH(App& a) const {
        if (!hasCategories()) return 0;
        i64 top = topOf(a, catId);
        return top && !children(a, top).empty() ? 46 : 0;
    }
    float height(App& a) const override {
        float w = width() - 48;
        float h = 64 + 52 + 96;  // header, kind, amount
        if (hasCategories()) h += 28 + gridH(a, w) + subH(a) + 12;
        h += 28 + fieldH + 16;   // account/date
        h += 28 + fieldH + 12;   // payee/note
        if (!payeeSuggest.empty()) h += 40;
        h += 24 + buttonH + 28;  // footer
        return h;
    }

    bool save(App& a, bool next) {
        auto amt = evalAmount(amountText, a.decimals());
        error.clear();
        if (!amt || (t.kind != TxKind::Adjustment && amt.value <= 0)) { error = tr(S::ErrAmount); return false; }
        i64 v = t.kind == TxKind::Adjustment ? amt.value : std::llabs(amt.value);
        Transaction out = t;
        out.payee = trim(t.payee);
        out.note = trim(t.note);
        out.lines.clear();
        switch (t.kind) {
            case TxKind::Expense:
            case TxKind::Income:
                if (!catId) { error = tr(S::ErrCategory); return false; }
                out.lines.push_back({0, accId, catId, t.kind == TxKind::Expense ? -v : v});
                break;
            case TxKind::Transfer:
                if (accId == toAccId || !toAccId) { error = tr(S::ErrSameAccount); return false; }
                out.lines.push_back({0, accId, 0, -v});
                out.lines.push_back({0, toAccId, 0, v});
                break;
            case TxKind::Adjustment:
                out.lines.push_back({0, accId, 0, v});
                break;
        }
        if (!a.saveTransaction(out)) return false;
        if (next) {
            t.id = 0;
            isNew = true;
            amountText.clear();
            t.payee.clear();
            t.note.clear();
            catId = 0;
            focusAmount = true;
            a.showToast(tr(S::TxSaved));
            a.reload();
            accs = a.accounts;
        } else {
            done = true;
        }
        return true;
    }

    void draw(App& a, Ctx& c, const Rect& r) override {
        header(a, c, r, isNew ? tr(S::NewTransaction) : tr(S::EditTransaction), done);
        float x = r.x + 24, w = r.w - 48, y = r.y + 64;
        Id amountId = makeId("txd-amount");
        if (focusAmount) { c.requestFocus(amountId); focusAmount = false; }

        // kind
        if (t.kind == TxKind::Adjustment) {
            a.gfx.text(tr(S::Adjustment), {x, y, w, 40}, Font::BodyStrong, snow2);
        } else {
            std::vector<std::wstring> kinds = {tr(S::Expense), tr(S::IncomeOne)};
            if (accs.size() > 1) kinds.push_back(tr(S::Transfer));
            Color acc = t.kind == TxKind::Income ? income : (t.kind == TxKind::Transfer ? transfer : expense);
            int k = segmented(c, makeId("txd-kind"), {x, y, w, 40}, kinds, (int)t.kind, acc);
            if (k != (int)t.kind) {
                t.kind = (TxKind)k;
                if (catId) {
                    const Category* cc = a.category(catId);
                    CatType want = t.kind == TxKind::Income ? CatType::Income : CatType::Expense;
                    if (!cc || cc->type != want) catId = 0;
                }
                error.clear();
            }
        }
        y += 52;

        // amount
        Color kc = t.kind == TxKind::Income ? income : (t.kind == TxKind::Expense ? expense : snow1);
        Rect ar{x, y, w, 64};
        a.gfx.fillRound(ar, 12, night3);
        a.gfx.strokeRound(ar, 12, c.focus == amountId ? kc.withAlpha(0.55f) : hairline, c.focus == amountId ? 1.5f : 1.0f);
        std::wstring sym = a.cur ? a.cur->symbol : L"";
        float symW = a.gfx.measure(sym, Font::AmountLarge);
        a.gfx.text(sym, {ar.r() - 20 - symW, ar.y, symW + 2, ar.h}, Font::AmountLarge, snow3);
        FieldOpts fo;
        fo.font = Font::AmountHuge;
        fo.flat = true;
        fo.align = Align::Right;
        fo.maxLen = 40;
        fo.textColor = snow1;
        auto fr = textField(c, amountId, {ar.x + 8, ar.y, ar.w - 36 - symW, ar.h}, amountText, L"0", fo);
        if (fr.changed) error.clear();
        y += 70;
        std::wstring preview = tr(S::AmountHint);
        Color pc = snow3;
        if (looksLikeExpression(amountText)) {
            auto ev = evalAmount(amountText, a.decimals());
            if (ev) { preview = L"= " + a.money(std::llabs(ev.value)); pc = frost; }
        }
        a.gfx.text(preview, {x, y, w, 20}, Font::Caption, pc, Align::Right);
        y += 26;

        // categories
        if (hasCategories()) {
            sectionLabel(c, {x, y, w, 18}, tr(S::Category));
            y += 28;
            auto list = tops(a);
            int cols = gridCols(w);
            float cw = (w - 8 * (cols - 1)) / cols;
            float gh = gridH(a, w);
            int rows = ((int)list.size() + cols - 1) / cols;
            float off = c.scrollBegin(makeId("txd-grid"), {x - 4, y, w + 8, gh}, rows * 84.0f);
            i64 selTop = topOf(a, catId);
            for (size_t i = 0; i < list.size(); ++i) {
                const Category* cat = list[i];
                Rect cell{x + (i % cols) * (cw + 8), y + (i / cols) * 84.0f - off, cw, 78};
                if (cell.b() < y || cell.y > y + gh) continue;
                bool on = selTop == cat->id;
                Color col = palette(cat->color);
                if (hoverArea(c, makeId(cat->id, makeId("txd-cat")), cell, 10, night3, on, col.withAlpha(0.14f))) {
                    catId = cat->id;
                    error.clear();
                }
                if (on) a.gfx.strokeRound(cell, 10, col.withAlpha(0.6f), 1.5f);
                iconBadge(c, {cell.cx() - 18, cell.y + 8, 36, 36}, cat->icon, col, 0.5f);
                a.gfx.text(cat->name, {cell.x + 4, cell.y + 48, cell.w - 8, 22}, Font::Caption, on ? snow1 : snow2, Align::Center);
            }
            c.scrollEnd();
            y += gh;
            if (subH(a) > 0) {
                y += 8;
                auto ch = children(a, selTop);
                float cx = x;
                const Category* parent = a.category(selTop);
                std::vector<const Category*> chips = {parent};
                chips.insert(chips.end(), ch.begin(), ch.end());
                for (size_t i = 0; i < chips.size(); ++i) {
                    const Category* cat = chips[i];
                    std::wstring label = i == 0 ? std::wstring(L"• ") + cat->name : cat->name;
                    float tw = a.gfx.measure(label, Font::Small) + 28;
                    if (cx + tw > x + w) break;
                    Rect chip{cx, y, tw, 34};
                    bool on = catId == cat->id;
                    Color col = palette(cat->color);
                    if (hoverArea(c, makeId(cat->id, makeId("txd-sub")), chip, 17, night3, on, col.withAlpha(0.18f))) catId = cat->id;
                    a.gfx.strokeRound(chip, 17, on ? col.withAlpha(0.7f) : hairline);
                    a.gfx.text(label, chip, Font::Small, on ? snow1 : snow2, Align::Center);
                    cx += tw + 8;
                }
                y += 38;
            }
            y += 12;
        }

        // account(s) and date
        float half = (w - 16) / 2;
        auto items = accountItems(a, accs);
        if (t.kind == TxKind::Transfer) {
            float third = (w - 32) / 3;
            sectionLabel(c, {x, y, third, 18}, tr(S::FromAccount));
            sectionLabel(c, {x + third + 16, y, third, 18}, tr(S::ToAccount));
            sectionLabel(c, {x + 2 * (third + 16), y, third, 18}, tr(S::DateLabel));
            int s1 = indexOfAccount(accs, accId), s2 = indexOfAccount(accs, toAccId);
            if (dropdown(c, makeId("txd-from"), {x, y + 26, third, fieldH}, items, s1) && s1 >= 0) accId = accs[s1].id;
            if (dropdown(c, makeId("txd-to"), {x + third + 16, y + 26, third, fieldH}, items, s2) && s2 >= 0) toAccId = accs[s2].id;
            dateField(c, makeId("txd-date"), {x + 2 * (third + 16), y + 26, third, fieldH}, t.date, a.prefs.fdow);
        } else if (accs.size() > 1 || t.kind == TxKind::Adjustment) {
            sectionLabel(c, {x, y, half, 18}, tr(S::Account));
            sectionLabel(c, {x + half + 16, y, half, 18}, tr(S::DateLabel));
            int s1 = indexOfAccount(accs, accId);
            if (dropdown(c, makeId("txd-acc"), {x, y + 26, half, fieldH}, items, s1) && s1 >= 0) accId = accs[s1].id;
            dateField(c, makeId("txd-date"), {x + half + 16, y + 26, half, fieldH}, t.date, a.prefs.fdow);
        } else {
            sectionLabel(c, {x, y, half, 18}, tr(S::DateLabel));
            dateField(c, makeId("txd-date"), {x, y + 26, half, fieldH}, t.date, a.prefs.fdow);
        }
        y += 28 + fieldH + 16;

        // payee and note
        sectionLabel(c, {x, y, half, 18}, tr(S::Payee));
        sectionLabel(c, {x + half + 16, y, half, 18}, tr(S::Note));
        auto pr = textField(c, makeId("txd-payee"), {x, y + 26, half, fieldH}, t.payee, tr(S::PayeePlaceholder));
        textField(c, makeId("txd-note"), {x + half + 16, y + 26, half, fieldH}, t.note, tr(S::NotePlaceholder));
        if (pr.changed && isNew && hasCategories()) {
            i64 last = a.db().lastCategoryForPayee(trim(t.payee), t.kind);
            if (last && a.category(last)) catId = last;
        }
        if (payeeKey != t.payee) {
            payeeKey = t.payee;
            payeeSuggest.clear();
            if (!trim(t.payee).empty())
                for (auto& p : a.db().payees(trim(t.payee), 4))
                    if (p != t.payee) payeeSuggest.push_back(p);
        }
        y += 28 + fieldH + 12;
        if (!payeeSuggest.empty()) {
            float cx = x;
            for (size_t i = 0; i < payeeSuggest.size(); ++i) {
                float tw = a.gfx.measure(payeeSuggest[i], Font::Small) + 26;
                if (cx + tw > x + w) break;
                Rect chip{cx, y, tw, 30};
                if (hoverArea(c, makeId((int64_t)i, makeId("txd-psug")), chip, 15, night3)) {
                    t.payee = payeeSuggest[i];
                    if (isNew && hasCategories()) {
                        i64 last = a.db().lastCategoryForPayee(t.payee, t.kind);
                        if (last && a.category(last)) catId = last;
                    }
                }
                a.gfx.strokeRound(chip, 15, hairline);
                a.gfx.text(payeeSuggest[i], chip, Font::Small, snow2, Align::Center);
                cx += tw + 8;
            }
        }

        // footer
        float fy = footerY(r);
        if (!error.empty()) a.gfx.text(error, {x, fy - 28, w, 20}, Font::Small, expense);
        Rect saveB{r.r() - 24 - 140, fy, 140, buttonH};
        Rect cancelB{saveB.x - 12 - 120, fy, 120, buttonH};
        bool doSave = button(c, makeId("txd-save"), saveB, tr(S::Save), Btn::Primary);
        if (button(c, makeId("txd-cancel"), cancelB, tr(S::Cancel), Btn::Secondary)) done = true;
        if (isNew) {
            Rect nextB{x, fy, a.gfx.measure(tr(S::SaveAndNext), Font::BodyStrong) + 32, buttonH};
            if (button(c, makeId("txd-next"), nextB, tr(S::SaveAndNext), Btn::Ghost)) save(a, true);
        } else {
            Rect delB{x, fy, 130, buttonH};
            if (button(c, makeId("txd-del"), delB, tr(S::Delete), Btn::Danger, Icon::Trash)) {
                a.deleteTransaction(t.id);
                done = true;
                return;
            }
        }
        if (!c.popupOpen() && c.consumeKey(VK_RETURN)) {
            if (c.in.ctrl && isNew) save(a, true);
            else doSave = true;
        }
        if (doSave) save(a, false);
    }
};

// ================================================================ book

struct BookDialog : Dialog {
    NewBook nb;
    std::wstring error;
    int curSel = 0;
    bool focus = true;

    explicit BookDialog(App& a) {
        nb.name.clear();
        nb.kind = BookKind::Personal;
        nb.color = (int)(a.cards.size() * 5) % 16;
        nb.icon = Icon::Wallet;
        nb.currency = "CZK";
    }
    float width() const override { return 600; }
    float height(App&) const override { return 70 + 3 * 80 + 72 + 66 + 24 + buttonH + 24; }

    void draw(App& a, Ctx& c, const Rect& r) override {
        header(a, c, r, tr(S::NewBookTitle), done);
        float x = r.x + 24, w = r.w - 48, y = r.y + 70;
        Id nameId = makeId("bd-name");
        if (focus) { c.requestFocus(nameId); focus = false; }
        sectionLabel(c, {x, y, w, 18}, tr(S::Name));
        textField(c, nameId, {x, y + 24, w, fieldH}, nb.name, tr(S::BookNamePlaceholder));
        y += 80;
        sectionLabel(c, {x, y, w, 18}, tr(S::Type));
        std::vector<std::wstring> kinds;
        for (int k = 0; k < 5; ++k) kinds.push_back(kindName((BookKind)k));
        int k = segmented(c, makeId("bd-kind"), {x, y + 24, w, 38}, kinds, (int)nb.kind);
        if (k != (int)nb.kind) {
            nb.kind = (BookKind)k;
            Icon defIcons[] = {Icon::User, Icon::Wallet, Icon::Building, Icon::Hammer, Icon::Star};
            nb.icon = defIcons[k];
        }
        y += 80;
        sectionLabel(c, {x, y, w, 18}, tr(S::Currency));
        std::vector<DropItem> items;
        const auto& cs = currencies();
        for (auto& cc : cs) items.push_back({fromUtf8(cc.code), Icon::None, {0, 0, 0, 0}, cc.symbol});
        if (dropdown(c, makeId("bd-cur"), {x, y + 24, 220, fieldH}, items, curSel)) nb.currency = cs[curSel].code;
        a.gfx.textWrapped(tr(S::CurrencyFixedHint), {x + 236, y + 30, w - 236, 40}, Font::Caption, snow3);
        y += 80;
        sectionLabel(c, {x, y, w, 18}, tr(S::Color));
        float sw = std::min(34.0f, (w + 2) / 16);
        for (int i = 0; i < 16; ++i)
            if (swatch(c, makeId((int64_t)i, makeId("bd-color")), {x + i * sw, y + 22, sw - 2, sw - 2}, palette(i), nb.color == i)) nb.color = i;
        y += 72;
        sectionLabel(c, {x, y, w, 18}, tr(S::IconLabel));
        auto icons = bookIcons();
        float iw = std::min(40.0f, (w + 4) / (float)icons.size());
        for (size_t i = 0; i < icons.size(); ++i) {
            Rect ib{x + i * iw, y + 22, iw - 4, iw - 4};
            bool on = nb.icon == icons[i];
            if (hoverArea(c, makeId((int64_t)i, makeId("bd-icon")), ib, 10, night3, on, palette(nb.color).withAlpha(0.18f))) nb.icon = icons[i];
            a.gfx.icon(icons[i], ib.inset(8), on ? palette(nb.color) : snow2);
        }
        float fy = footerY(r);
        if (!error.empty()) a.gfx.text(error, {x, fy - 28, w, 20}, Font::Small, expense);
        Rect ok{r.r() - 24 - 150, fy, 150, buttonH};
        Rect cancel{ok.x - 12 - 120, fy, 120, buttonH};
        bool create = button(c, makeId("bd-create"), ok, tr(S::Create), Btn::Primary);
        if (button(c, makeId("bd-cancel"), cancel, tr(S::Cancel), Btn::Secondary)) done = true;
        if (!c.popupOpen() && c.consumeKey(VK_RETURN)) create = true;
        if (create) {
            nb.name = trim(nb.name);
            if (nb.name.empty()) { error = tr(S::ErrName); return; }
            auto res = a.lib.createBook(nb);
            if (!res) { error = errorText(res.status.code); return; }
            done = true;
            a.cardsDirty = true;
            a.openBook(res.value);
        }
    }
};

// ================================================================ account

struct AccountDialog : Dialog {
    Account acc;
    bool isNew = true;
    std::wstring openingText, actualText, error;
    bool focus = true;

    AccountDialog(App& a, const Account* edit) {
        if (edit) { acc = *edit; isNew = false; }
        else { acc.type = AccountType::Card; acc.color = (int)(a.accountsAll.size() * 3 + 2) % 16; }
        openingText = acc.opening ? formatPlain(acc.opening, a.decimals(), a.nf.decimal) : L"";
    }
    float width() const override { return 560; }
    float height(App&) const override { return 64 + 80 + 128 + 80 + 110 + (isNew ? 0 : 150) + 30 + buttonH + 24; }

    void draw(App& a, Ctx& c, const Rect& r) override {
        header(a, c, r, isNew ? tr(S::NewAccount) : tr(S::EditAccount), done);
        float x = r.x + 24, w = r.w - 48, y = r.y + 70;
        Id nameId = makeId("ad-name");
        if (focus) { c.requestFocus(nameId); focus = false; }
        sectionLabel(c, {x, y, w, 18}, tr(S::Name));
        textField(c, nameId, {x, y + 24, w, fieldH}, acc.name, accountTypeName(acc.type));
        y += 80;
        sectionLabel(c, {x, y, w, 18}, tr(S::Type));
        float tw = (w - 16) / 3;
        for (int i = 0; i < 6; ++i) {
            Rect cell{x + (i % 3) * (tw + 8), y + 24 + (i / 3) * 48.0f, tw, 42};
            bool on = (int)acc.type == i;
            Color col = palette(acc.color);
            if (hoverArea(c, makeId((int64_t)i, makeId("ad-type")), cell, 10, night3, on, col.withAlpha(0.16f))) acc.type = (AccountType)i;
            a.gfx.strokeRound(cell, 10, on ? col.withAlpha(0.6f) : hairline);
            a.gfx.icon(accountIcon((AccountType)i), {cell.x + 12, cell.cy() - 9, 18, 18}, on ? col : snow2);
            a.gfx.text(accountTypeName((AccountType)i), {cell.x + 40, cell.y, cell.w - 48, cell.h}, Font::Small, on ? snow1 : snow2);
        }
        y += 128;
        sectionLabel(c, {x, y, w, 18}, tr(S::OpeningBalance));
        FieldOpts fo;
        fo.font = Font::AmountMid;
        fo.align = Align::Right;
        textField(c, makeId("ad-open"), {x, y + 24, 260, fieldH + 4}, openingText, L"0", fo);
        a.gfx.text(a.cur ? a.cur->symbol : L"", {x + 272, y + 24, 60, fieldH + 4}, Font::Body, snow3);
        y += 80;
        sectionLabel(c, {x, y, w, 18}, tr(S::Color));
        float sw = std::min(34.0f, (w + 2) / 16);
        for (int i = 0; i < 16; ++i)
            if (swatch(c, makeId((int64_t)i, makeId("ad-color")), {x + i * sw, y + 22, sw - 2, sw - 2}, palette(i), acc.color == i)) acc.color = i;
        y += 72;
        bool arch = acc.archived;
        if (!isNew) {
            a.gfx.text(tr(S::Archived), {x, y, w - 70, 40}, Font::Body, snow1);
            if (toggle(c, makeId("ad-arch"), {x + w - 60, y, 60, 40}, arch)) acc.archived = arch;
            y += 52;
            a.gfx.line(x, y, x + w, y, hairline);
            y += 14;
            sectionLabel(c, {x, y, w, 18}, tr(S::CorrectBalance));
            FieldOpts fa;
            fa.font = Font::AmountMid;
            fa.align = Align::Right;
            textField(c, makeId("ad-actual"), {x, y + 24, 260, fieldH + 4}, actualText, formatPlain(acc.balance, a.decimals(), a.nf.decimal), fa);
            if (button(c, makeId("ad-adjust"), {x + 276, y + 26, 160, fieldH}, tr(S::Save), Btn::Secondary) && !trim(actualText).empty()) {
                auto v = evalAmount(actualText, a.decimals());
                if (v && v.value != acc.balance) {
                    Transaction t;
                    t.kind = TxKind::Adjustment;
                    t.date = Date::today();
                    t.lines.push_back({0, acc.id, 0, v.value - acc.balance});
                    a.saveTransaction(t);
                    acc.balance = v.value;
                    actualText.clear();
                    a.showToast(tr(S::TxSaved));
                }
            }
            a.gfx.text(tr(S::ActualBalance), {x, y + 74, w, 18}, Font::Caption, snow3);
        }
        float fy = footerY(r);
        if (!error.empty()) a.gfx.text(error, {x, fy - 28, w, 20}, Font::Small, expense);
        Rect ok{r.r() - 24 - 140, fy, 140, buttonH};
        Rect cancel{ok.x - 12 - 120, fy, 120, buttonH};
        bool save = button(c, makeId("ad-save"), ok, tr(S::Save), Btn::Primary);
        if (button(c, makeId("ad-cancel"), cancel, tr(S::Cancel), Btn::Secondary)) done = true;
        if (!c.popupOpen() && c.consumeKey(VK_RETURN)) save = true;
        if (save) {
            acc.name = trim(acc.name);
            if (acc.name.empty()) acc.name = accountTypeName(acc.type);
            if (!trim(openingText).empty()) {
                auto v = evalAmount(openingText, a.decimals());
                if (!v) { error = tr(S::ErrInvalid); return; }
                acc.opening = v.value;
            } else {
                acc.opening = 0;
            }
            acc.icon = accountIcon(acc.type);
            a.db().saveAccount(acc);
            a.dirty = true;
            done = true;
        }
    }
};

// ================================================================ category

struct CategoryDialog : Dialog {
    Category cat;
    bool isNew = true;
    std::wstring error;
    int mergeSel = -1;
    bool focus = true;

    CategoryDialog(App& a, const Category* edit, CatType type) {
        if (edit) { cat = *edit; isNew = false; }
        else { cat.type = type; cat.icon = Icon::Tag; cat.color = (int)(a.cats.size() * 7) % 16; }
    }
    float width() const override { return 600; }
    float height(App&) const override { return 64 + 64 + 80 + 80 + 230 + 80 + (isNew ? 0 : 120) + 30 + buttonH + 20; }

    void draw(App& a, Ctx& c, const Rect& r) override {
        header(a, c, r, isNew ? tr(S::NewCategory) : tr(S::EditCategory), done);
        float x = r.x + 24, w = r.w - 48, y = r.y + 66;
        Id nameId = makeId("cd-name");
        if (focus) { c.requestFocus(nameId); focus = false; }
        bool hasChildren = false;
        for (auto& cc : a.cats) if (cc.parent == cat.id && cat.id) hasChildren = true;
        int t = segmented(c, makeId("cd-type"), {x, y, w, 38}, {tr(S::Expenses), tr(S::Income)}, (int)cat.type,
                          cat.type == CatType::Income ? income : expense);
        if (t != (int)cat.type && (isNew || cat.parent == 0)) { cat.type = (CatType)t; cat.parent = 0; }
        y += 60;
        sectionLabel(c, {x, y, w, 18}, tr(S::Name));
        textField(c, nameId, {x, y + 24, w, fieldH}, cat.name, L"");
        y += 80;
        sectionLabel(c, {x, y, w, 18}, tr(S::ParentCategory));
        std::vector<DropItem> parents = {{tr(S::TopLevel)}};
        std::vector<i64> pids = {0};
        for (auto& cc : a.cats)
            if (cc.type == cat.type && cc.parent == 0 && cc.id != cat.id && !cc.archived) {
                parents.push_back({cc.name, cc.icon, palette(cc.color)});
                pids.push_back(cc.id);
            }
        int ps = 0;
        for (size_t i = 0; i < pids.size(); ++i) if (pids[i] == cat.parent) ps = (int)i;
        if (!hasChildren) {
            if (dropdown(c, makeId("cd-parent"), {x, y + 24, w, fieldH}, parents, ps)) cat.parent = pids[ps];
        } else {
            a.gfx.text(tr(S::TopLevel), {x, y + 24, w, fieldH}, Font::Body, snow3);
        }
        y += 80;
        sectionLabel(c, {x, y, w, 18}, tr(S::IconLabel));
        auto icons = categoryIcons();
        int cols = 12;
        float cs = (w + 4) / cols;
        Color col = palette(cat.color);
        for (size_t i = 0; i < icons.size(); ++i) {
            Rect ib{x + (i % cols) * cs, y + 24 + (i / cols) * cs, cs - 4, cs - 4};
            bool on = cat.icon == icons[i];
            if (hoverArea(c, makeId((int64_t)i, makeId("cd-icon")), ib, 9, night3, on, col.withAlpha(0.18f))) cat.icon = icons[i];
            a.gfx.icon(icons[i], ib.inset(cs * 0.22f), on ? col : snow2);
        }
        y += 24 + ((icons.size() + cols - 1) / cols) * cs + 12;
        sectionLabel(c, {x, y, w, 18}, tr(S::Color));
        float sw = std::min(34.0f, (w + 2) / 16);
        for (int i = 0; i < 16; ++i)
            if (swatch(c, makeId((int64_t)i, makeId("cd-color")), {x + i * sw, y + 22, sw - 2, sw - 2}, palette(i), cat.color == i)) cat.color = i;
        y += 72;
        if (!isNew) {
            bool arch = cat.archived;
            a.gfx.text(tr(S::Archived), {x, y, w - 70, 40}, Font::Body, snow1);
            if (toggle(c, makeId("cd-arch"), {x + w - 60, y, 60, 40}, arch)) cat.archived = arch;
            y += 50;
            sectionLabel(c, {x, y, w, 18}, tr(S::MergeInto));
            std::vector<DropItem> targets;
            std::vector<i64> tids;
            for (auto& cc : a.cats)
                if (cc.type == cat.type && cc.id != cat.id && cc.parent != cat.id) {
                    targets.push_back({cc.parent ? L"   " + cc.name : cc.name, cc.icon, palette(cc.color)});
                    tids.push_back(cc.id);
                }
            dropdown(c, makeId("cd-merge"), {x, y + 24, w - 160, fieldH}, targets, mergeSel, tr(S::MergeHint));
            if (button(c, makeId("cd-merge-go"), {x + w - 148, y + 24, 148, fieldH}, tr(S::Continue), Btn::Secondary, Icon::None, mergeSel >= 0)) {
                i64 to = tids[mergeSel], from = cat.id;
                done = true;
                a.showDialog(makeConfirmDialog(tr(S::MergeInto), tr(S::MergeHint), tr(S::Continue), [from, to](App& app) {
                    app.db().mergeCategory(from, to);
                    app.dirty = true;
                }));
                return;
            }
        }
        float fy = footerY(r);
        if (!error.empty()) a.gfx.text(error, {x, fy - 28, w, 20}, Font::Small, expense);
        Rect ok{r.r() - 24 - 140, fy, 140, buttonH};
        Rect cancel{ok.x - 12 - 120, fy, 120, buttonH};
        bool save = button(c, makeId("cd-save"), ok, tr(S::Save), Btn::Primary);
        if (button(c, makeId("cd-cancel"), cancel, tr(S::Cancel), Btn::Secondary)) done = true;
        if (!c.popupOpen() && c.consumeKey(VK_RETURN)) save = true;
        if (save) {
            cat.name = trim(cat.name);
            if (cat.name.empty()) { error = tr(S::ErrName); return; }
            a.db().saveCategory(cat);
            a.dirty = true;
            done = true;
        }
    }
};

// ================================================================ PIN

struct PinDialog : Dialog {
    PinMode mode;
    std::wstring current, p1, p2, error;
    bool focus = true;
    explicit PinDialog(PinMode m) : mode(m) {}
    float width() const override { return 440; }
    float height(App&) const override {
        int n = mode == PinMode::Set ? 2 : (mode == PinMode::Change ? 3 : 1);
        return 64 + 50 + n * 84 + 40 + buttonH + 28;
    }
    void draw(App& a, Ctx& c, const Rect& r) override {
        const wchar_t* title = mode == PinMode::Set ? tr(S::SetPin) : (mode == PinMode::Change ? tr(S::ChangePin) : tr(S::RemovePin));
        header(a, c, r, title, done);
        float x = r.x + 24, w = r.w - 48, y = r.y + 64;
        a.gfx.textWrapped(tr(S::PinHint), {x, y, w, 40}, Font::Caption, snow3);
        y += 50;
        FieldOpts fo;
        fo.font = Font::AmountMid;
        fo.password = true;
        fo.digitsOnly = true;
        fo.maxLen = 4;
        fo.align = Align::Center;
        Id first = 0;
        auto field = [&](S label, std::wstring& v, const char* key) {
            sectionLabel(c, {x, y, w, 18}, tr(label));
            Id id = makeId(key);
            if (!first) first = id;
            if (textField(c, id, {x, y + 24, w, fieldH + 6}, v, L"", fo).changed) error.clear();
            y += 84;
        };
        if (mode != PinMode::Set) field(S::PinCurrent, current, "pd-cur");
        if (mode != PinMode::Remove) { field(S::PinNew, p1, "pd-p1"); field(S::PinRepeat, p2, "pd-p2"); }
        if (focus) { c.requestFocus(first); focus = false; }
        float fy = footerY(r);
        if (!error.empty()) a.gfx.text(error, {x, fy - 30, w, 20}, Font::Small, expense);
        Rect ok{r.r() - 24 - 150, fy, 150, buttonH};
        Rect cancel{ok.x - 12 - 110, fy, 110, buttonH};
        bool go = button(c, makeId("pd-ok"), ok, mode == PinMode::Remove ? tr(S::RemovePin) : tr(S::Save),
                         mode == PinMode::Remove ? Btn::Danger : Btn::Primary);
        if (button(c, makeId("pd-cancel"), cancel, tr(S::Cancel), Btn::Secondary)) done = true;
        if (!c.popupOpen() && c.consumeKey(VK_RETURN)) go = true;
        if (!go) return;
        if (mode != PinMode::Set) {
            crypto::Key k{};
            Status st = a.lib.keyFromPin(a.book(), current, k);
            wipe(k.data(), k.size());
            if (!st) { error = tr(S::PinWrong); current.clear(); return; }
        }
        if (mode == PinMode::Remove) {
            Status st = a.lib.removePin();
            if (!st) { error = errorText(st.code); return; }
        } else {
            if (!Library::validPin(p1)) { error = tr(S::PinHint); return; }
            if (p1 != p2) { error = tr(S::PinMismatch); p2.clear(); return; }
            Status st = a.lib.setPin(p1);
            if (!st) { error = errorText(st.code); return; }
        }
        wipe(current.data(), current.size() * sizeof(wchar_t));
        wipe(p1.data(), p1.size() * sizeof(wchar_t));
        wipe(p2.data(), p2.size() * sizeof(wchar_t));
        a.showToast(tr(S::TxSaved));
        done = true;
    }
};

// ================================================================ confirm

struct ConfirmDialog : Dialog {
    std::wstring title, text, action;
    std::function<void(App&)> onConfirm;
    float width() const override { return 460; }
    float height(App& a) const override { return 64 + a.gfx.wrappedHeight(text, Font::Body, 412) + 24 + buttonH + 32; }
    void draw(App& a, Ctx& c, const Rect& r) override {
        header(a, c, r, title, done);
        a.gfx.textWrapped(text, {r.x + 24, r.y + 64, r.w - 48, r.h - 140}, Font::Body, snow2);
        float fy = footerY(r);
        Rect ok{r.r() - 24 - 150, fy, 150, buttonH};
        Rect cancel{ok.x - 12 - 110, fy, 110, buttonH};
        if (button(c, makeId("cf-ok"), ok, action, Btn::Danger)) {
            done = true;
            auto fn = onConfirm;
            fn(a);
            return;
        }
        if (button(c, makeId("cf-cancel"), cancel, tr(S::Cancel), Btn::Secondary)) done = true;
    }
};

// ================================================================ export

struct ExportDialog : Dialog {
    std::wstring pass1, pass2, error;
    struct Locked { BookRecord rec; std::wstring pin; bool bad = false; };
    std::vector<Locked> locked;
    explicit ExportDialog(App& a) {
        for (auto& b : a.lib.books())
            if (b.hasPin() && !(a.lib.current() && a.lib.current()->rec.id == b.id)) locked.push_back({b, L"", false});
    }
    float width() const override { return 560; }
    float height(App&) const override {
        return 64 + 50 + 2 * 80 + 40 + (locked.empty() ? 0 : 50 + locked.size() * 52.0f) + 30 + buttonH + 24;
    }
    void draw(App& a, Ctx& c, const Rect& r) override {
        header(a, c, r, tr(S::ExportTitle), done);
        float x = r.x + 24, w = r.w - 48, y = r.y + 64;
        a.gfx.textWrapped(tr(S::ExportHint), {x, y, w, 44}, Font::Small, snow2);
        y += 50;
        FieldOpts fo;
        fo.password = true;
        sectionLabel(c, {x, y, w, 18}, tr(S::BackupPassword));
        textField(c, makeId("ex-p1"), {x, y + 24, w, fieldH}, pass1, L"", fo);
        y += 80;
        sectionLabel(c, {x, y, w, 18}, tr(S::BackupPasswordRepeat));
        textField(c, makeId("ex-p2"), {x, y + 24, w, fieldH}, pass2, L"", fo);
        y += 80;
        if (pass1.empty()) {
            a.gfx.icon(Icon::Info, {x, y + 1, 18, 18}, warning);
            a.gfx.text(tr(S::NoPasswordWarn), {x + 26, y, w - 26, 20}, Font::Caption, warning);
        }
        y += 40;
        if (!locked.empty()) {
            a.gfx.textWrapped(tr(S::PinForExport), {x, y, w, 40}, Font::Caption, snow3);
            y += 40;
            FieldOpts pf;
            pf.password = true;
            pf.digitsOnly = true;
            pf.maxLen = 4;
            pf.align = Align::Center;
            for (size_t i = 0; i < locked.size(); ++i) {
                Locked& L = locked[i];
                iconBadge(c, {x, y + 6, 32, 32}, L.rec.icon, palette(L.rec.color), 0.5f);
                a.gfx.text(L.rec.name, {x + 44, y, w - 44 - 140, 44}, Font::Body, snow1);
                pf.error = L.bad;
                if (textField(c, makeId(L.rec.id, makeId("ex-pin")), {x + w - 120, y + 2, 120, 40}, L.pin, L"PIN", pf).changed) L.bad = false;
                y += 52;
            }
        }
        float fy = footerY(r);
        if (!error.empty()) a.gfx.text(error, {x, fy - 28, w, 20}, Font::Small, expense);
        Rect ok{r.r() - 24 - 170, fy, 170, buttonH};
        Rect cancel{ok.x - 12 - 110, fy, 110, buttonH};
        bool go = button(c, makeId("ex-go"), ok, tr(S::Export), Btn::Primary, Icon::Upload);
        if (button(c, makeId("ex-cancel"), cancel, tr(S::Cancel), Btn::Secondary)) done = true;
        if (!go) return;
        if (pass1 != pass2) { error = tr(S::PasswordMismatch); return; }
        std::vector<std::pair<i64, std::wstring>> pins;
        bool anyBad = false;
        for (auto& L : locked) {
            if (L.pin.empty()) continue;
            crypto::Key k{};
            if (!a.lib.keyFromPin(L.rec, L.pin, k)) { L.bad = true; anyBad = true; }
            else pins.emplace_back(L.rec.id, L.pin);
            wipe(k.data(), k.size());
        }
        if (anyBad) { error = tr(S::PinWrong); return; }
        std::wstring path = pickSavePath(a);
        if (path.empty()) return;
        Status st = exportBackup(a.lib, path, pass1.empty() ? BackupMode::Plain : BackupMode::Password, pass1, pins);
        for (auto& p : pins) wipe(p.second.data(), p.second.size() * sizeof(wchar_t));
        if (!st) { error = errorText(st.code); return; }
        a.showToast(tr(S::ExportDone));
        done = true;
    }
};

// ================================================================ import

struct ImportDialog : Dialog {
    std::wstring path, password, error;
    BackupMode mode;
    bool loaded = false;
    BackupPreview pv;
    int importMode = 0;   // 0 add, 1 replace
    bool resetPins = false;
    bool focus = true;
    ImportDialog(const std::wstring& p, BackupMode m) : path(p), mode(m) {}
    float width() const override { return 580; }
    float height(App&) const override {
        if (!loaded) return 64 + 40 + 80 + 30 + buttonH + 24;
        return 64 + 60 + std::min<size_t>(pv.books.size(), 6) * 50.0f + 20 + 56 + 50 + 50 + 30 + buttonH + 24;
    }
    void load(App& a) {
        auto r = readBackupPreview(path, password);
        if (!r) { error = errorText(r.status.code); password.clear(); return; }
        pv = std::move(r.value);
        loaded = true;
        error.clear();
        (void)a;
    }
    void draw(App& a, Ctx& c, const Rect& r) override {
        header(a, c, r, tr(S::ImportTitle), done);
        float x = r.x + 24, w = r.w - 48, y = r.y + 64;
        size_t slash = path.find_last_of(L"\\/");
        std::wstring fname = slash == std::wstring::npos ? path : path.substr(slash + 1);
        if (!loaded && mode != BackupMode::Password && error.empty()) load(a);
        if (!loaded) {
            a.gfx.icon(Icon::Lock, {x, y + 1, 18, 18}, snow3);
            a.gfx.text(fname + L"  ·  " + tr(S::ImportPasswordPrompt), {x + 26, y, w - 26, 20}, Font::Small, snow2);
            y += 40;
            if (mode == BackupMode::Password) {
                Id pid = makeId("im-pass");
                if (focus) { c.requestFocus(pid); focus = false; }
                sectionLabel(c, {x, y, w, 18}, tr(S::Password));
                FieldOpts fo;
                fo.password = true;
                fo.error = !error.empty();
                textField(c, pid, {x, y + 24, w, fieldH}, password, L"", fo);
            }
            float fy = footerY(r);
            if (!error.empty()) a.gfx.text(error, {x, fy - 28, w, 20}, Font::Small, expense);
            Rect ok{r.r() - 24 - 150, fy, 150, buttonH};
            Rect cancel{ok.x - 12 - 110, fy, 110, buttonH};
            bool go = button(c, makeId("im-cont"), ok, tr(S::Continue), Btn::Primary);
            if (button(c, makeId("im-cancel"), cancel, tr(S::Cancel), Btn::Secondary)) done = true;
            if (!c.popupOpen() && c.consumeKey(VK_RETURN)) go = true;
            if (go && mode == BackupMode::Password) load(a);
            return;
        }
        std::wstring created;
        if (pv.created) {
            time_t tt = (time_t)pv.created;
            tm lt{};
            localtime_s(&lt, &tt);
            created = dateLabel({lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday}, false);
        }
        a.gfx.text(fname, {x, y, w, 20}, Font::BodyStrong, snow1);
        a.gfx.text(std::wstring(tr(S::BooksInFile)) + L": " + std::to_wstring(pv.books.size()) + L"   ·   " + tr(S::Created) + L" " + created,
                   {x, y + 22, w, 18}, Font::Caption, snow3);
        y += 60;
        auto existing = a.lib.books();
        size_t shown = std::min<size_t>(pv.books.size(), 6);
        for (size_t i = 0; i < shown; ++i) {
            const auto& b = pv.books[i];
            iconBadge(c, {x, y + 7, 34, 34}, b.rec.icon, palette(b.rec.color), 0.5f);
            a.gfx.text(b.rec.name, {x + 46, y + 4, w - 200, 22}, Font::Body, snow1);
            std::wstring sub = kindName(b.rec.kind) + L"  ·  " + fromUtf8(b.rec.currency);
            bool dup = false;
            for (auto& e : existing) if (e.uuid == b.rec.uuid) dup = true;
            if (dup && importMode == 0) sub += std::wstring(L"  ·  ") + tr(S::Exists);
            a.gfx.text(sub, {x + 46, y + 25, w - 120, 18}, Font::Caption, snow3);
            if (b.rec.hasPin()) a.gfx.icon(Icon::Lock, {x + w - 22, y + 15, 18, 18}, snow3);
            y += 50;
        }
        y += 20;
        importMode = segmented(c, makeId("im-mode"), {x, y, w, 40}, {tr(S::ImportModeAdd), tr(S::ImportModeReplace)}, importMode);
        y += 56;
        a.gfx.text(tr(S::ImportResetPins), {x, y, w - 70, 40}, Font::Body, snow1);
        toggle(c, makeId("im-reset"), {x + w - 60, y, 60, 40}, resetPins);
        y += 50;
        if (importMode == 1) {
            a.gfx.icon(Icon::Info, {x, y + 1, 18, 18}, warning);
            a.gfx.textWrapped(tr(S::ImportReplaceWarn), {x + 26, y, w - 26, 40}, Font::Caption, warning);
        }
        float fy = footerY(r);
        if (!error.empty()) a.gfx.text(error, {x, fy - 28, w, 20}, Font::Small, expense);
        Rect ok{r.r() - 24 - 170, fy, 170, buttonH};
        Rect cancel{ok.x - 12 - 110, fy, 110, buttonH};
        bool go = button(c, makeId("im-go"), ok, tr(S::Import), importMode == 1 ? Btn::Danger : Btn::Primary, Icon::Download);
        if (button(c, makeId("im-cancel2"), cancel, tr(S::Cancel), Btn::Secondary)) done = true;
        if (!go) return;
        if (importMode == 1) a.lib.closeCurrent();
        int n = 0;
        Status st = importBackup(a.lib, path, password, importMode == 1 ? ImportMode::Replace : ImportMode::Add, resetPins, &n);
        wipe(password.data(), password.size() * sizeof(wchar_t));
        if (!st) { error = errorText(st.code); return; }
        if (importMode == 1) {
            // settings may have changed
            Catalog& cat = a.lib.catalog();
            std::wstring l = cat.setting("lang");
            if (!l.empty()) a.prefs.lang = l == L"ru" ? Lang::Ru : Lang::En;
            a.prefs.privacy = cat.settingInt("privacy", 0) != 0;
            a.prefs.autolockMin = cat.settingInt("autolock_min", 5);
            a.prefs.lockOnMinimize = cat.settingInt("lock_on_minimize", 0) != 0;
            a.prefs.hideCapture = cat.settingInt("hide_capture", 0) != 0;
            a.prefs.fdow = cat.settingInt("first_day_of_week", 0);
            a.prefs.autoBackup = cat.settingInt("auto_backup", 1) != 0;
            a.applyPrefs();
        }
        a.screen = Screen::Start;
        a.cardsDirty = true;
        a.showToast(std::wstring(tr(S::ImportDone)) + L": " + std::to_wstring(n));
        done = true;
    }
};

}  // namespace

std::unique_ptr<Dialog> makeBookDialog(App& a, const BookRecord*) { return std::make_unique<BookDialog>(a); }
std::unique_ptr<Dialog> makeTxDialog(App& a, const Transaction& t, bool isNew) { return std::make_unique<TxDialog>(a, t, isNew); }
std::unique_ptr<Dialog> makeAccountDialog(App& a, const Account* edit) { return std::make_unique<AccountDialog>(a, edit); }
std::unique_ptr<Dialog> makeCategoryDialog(App& a, const Category* edit, CatType type) {
    return std::make_unique<CategoryDialog>(a, edit, type);
}
std::unique_ptr<Dialog> makePinDialog(App&, PinMode mode) { return std::make_unique<PinDialog>(mode); }
std::unique_ptr<Dialog> makeConfirmDialog(const std::wstring& title, const std::wstring& text, const std::wstring& action,
                                          std::function<void(App&)> onConfirm) {
    auto d = std::make_unique<ConfirmDialog>();
    d->title = title;
    d->text = text;
    d->action = action;
    d->onConfirm = std::move(onConfirm);
    return d;
}
std::unique_ptr<Dialog> makeExportDialog(App& a) { return std::make_unique<ExportDialog>(a); }
std::unique_ptr<Dialog> makeImportDialog(App&, const std::wstring& path, BackupMode mode) {
    return std::make_unique<ImportDialog>(path, mode);
}

}  // namespace fp
