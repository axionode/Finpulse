#include "ui/ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "services/i18n.h"

namespace fp::ui {

using namespace theme;

Id makeId(std::wstring_view s, Id seed) {
    Id h = seed ? seed : 1469598103934665603ULL;
    for (wchar_t ch : s) { h ^= (Id)ch; h *= 1099511628211ULL; }
    return h;
}

Id makeId(int64_t v, Id seed) {
    Id h = seed ? seed : 1469598103934665603ULL;
    for (int i = 0; i < 8; ++i) { h ^= (Id)((v >> (8 * i)) & 0xFF); h *= 1099511628211ULL; }
    return h;
}

// ---------------------------------------------------------------- context

void Ctx::beginFrame(double now) {
    dt = std::clamp(now - time, 0.0, 0.05);
    if (time == 0) dt = 0.016;
    time = now;
    topLayerPrev = topLayer;
    if (popupKind_ != PopupKind::None) topLayerPrev = std::max(topLayerPrev, 10);
    topLayer = 0;
    layer = 0;
    repaint = false;
    wantsCaret = false;
    textFocused = false;
    cursor = Cursor::Arrow;
    focusables.clear();
    captionExclusions.clear();
    clips_.clear();
}

void Ctx::endFrame() {
    // Tab cycles text fields
    bool tab = false, back = false;
    for (unsigned k : in.keys) if (k == VK_TAB) { tab = true; back = in.shift; }
    if (tab && !focusables.empty()) {
        auto it = std::find(focusables.begin(), focusables.end(), focus);
        size_t i = it == focusables.end() ? 0 : (size_t)(it - focusables.begin());
        if (it != focusables.end()) i = back ? (i + focusables.size() - 1) % focusables.size() : (i + 1) % focusables.size();
        pendingFocus = focusables[i];
        repaint = true;
    }
    if (focus && std::find(focusables.begin(), focusables.end(), focus) == focusables.end()) focus = 0;
    if (!in.down) active = 0;
    in.clearFrame();
}

bool Ctx::mouseOver(const Rect& r) const {
    if (!interactive()) return false;
    if (!r.contains(in.mx, in.my)) return false;
    for (auto& c : clips_) if (!c.contains(in.mx, in.my)) return false;
    return true;
}

float Ctx::anim(Id id, float target, float speed) {
    auto it = anims_.find(id);
    if (it == anims_.end()) { anims_[id] = target; return target; }
    float& v = it->second;
    float k = 1.0f - std::exp(-speed * (float)dt);
    v += (target - v) * k;
    if (std::fabs(target - v) < 0.002f) v = target;
    else repaint = true;
    return v;
}

bool Ctx::key(unsigned vk) const { return std::find(in.keys.begin(), in.keys.end(), vk) != in.keys.end(); }

bool Ctx::consumeKey(unsigned vk) {
    auto it = std::find(in.keys.begin(), in.keys.end(), vk);
    if (it == in.keys.end()) return false;
    in.keys.erase(it);
    return true;
}

void Ctx::pushClip(const Rect& r) { clips_.push_back(r); g.pushClip(r); }
void Ctx::popClip() { if (!clips_.empty()) clips_.pop_back(); g.popClip(); }

float Ctx::scrollBegin(Id id, const Rect& view, float contentH) {
    float maxOff = std::max(0.0f, contentH - view.h);
    float& tgt = scrollTarget_[id];
    tgt = std::clamp(tgt, 0.0f, maxOff);
    float& cur = scroll_[id];
    float k = 1.0f - std::exp(-22.0f * (float)dt);
    cur += (tgt - cur) * k;
    if (std::fabs(tgt - cur) < 0.3f) cur = tgt; else repaint = true;
    cur = std::clamp(cur, 0.0f, maxOff);
    scrollStack_.push_back({id, view, contentH});
    pushClip(view);
    return std::round(cur * g.scale()) / g.scale();
}

void Ctx::scrollEnd() {
    if (scrollStack_.empty()) return;
    ScrollFrame f = scrollStack_.back();
    scrollStack_.pop_back();
    popClip();
    float maxOff = std::max(0.0f, f.contentH - f.view.h);
    bool over = mouseOver(f.view);
    if (over && in.wheel != 0 && maxOff > 0) {
        scrollTarget_[f.id] = std::clamp(scrollTarget_[f.id] - in.wheel / 120.0f * 72.0f, 0.0f, maxOff);
        in.wheel = 0;
        repaint = true;
    }
    if (maxOff > 0) {
        float vis = anim(makeId(f.id, 77), over ? 1.0f : 0.0f, 10);
        if (vis > 0.01f) {
            float trackH = f.view.h - 8;
            float thumbH = std::max(28.0f, trackH * f.view.h / f.contentH);
            float t = scroll_[f.id] / maxOff;
            Rect thumb{f.view.r() - 6, f.view.y + 4 + (trackH - thumbH) * t, 3, thumbH};
            g.fillRound(thumb, 1.5f, snow3.withAlpha(0.5f * vis));
        }
    }
}

// ---------------------------------------------------------------- popups

void Ctx::openList(Id owner, const Rect& anchor, const std::vector<DropItem>& items, int sel, float width) {
    popupKind_ = PopupKind::List;
    popupOwner_ = owner;
    popupAnchor_ = anchor;
    popupItems_ = items;
    popupSel_ = sel;
    popupWidth_ = width > 0 ? width : anchor.w;
    popupOpenedAt_ = time;
    snap(makeId("popup-fade", 0), 0);
    repaint = true;
}

void Ctx::openCalendar(Id owner, const Rect& anchor, Date d, int fdow) {
    popupKind_ = PopupKind::Calendar;
    popupOwner_ = owner;
    popupAnchor_ = anchor;
    popupDate_ = d;
    popupMonth_ = {d.y, d.m, 1};
    popupFdow_ = fdow;
    popupOpenedAt_ = time;
    snap(makeId("popup-fade", 0), 0);
    repaint = true;
}

bool Ctx::takeListResult(Id owner, int& sel) {
    if (resultOwner_ != owner || listResult_ < 0) return false;
    sel = listResult_;
    listResult_ = -1;
    resultOwner_ = 0;
    return true;
}

bool Ctx::takeDateResult(Id owner, Date& d) {
    if (resultOwner_ != owner || !dateResult_) return false;
    d = dateValue_;
    dateResult_ = false;
    resultOwner_ = 0;
    return true;
}

void Ctx::drawPopups() {
    if (popupKind_ == PopupKind::None) return;
    setLayer(10);
    float fade = anim(makeId("popup-fade", 0), 1.0f, 26);
    float W = g.widthDip(), H = g.heightDip();
    Rect box;
    if (popupKind_ == PopupKind::List) {
        float rowH = 36;
        float h = std::min(rowH * (float)popupItems_.size() + 12, 340.0f);
        box = {popupAnchor_.x, popupAnchor_.b() + 6, popupWidth_, h};
        if (box.b() > H - 12) box.y = popupAnchor_.y - h - 6;
        if (box.r() > W - 12) box.x = W - 12 - box.w;
    } else {
        box = {popupAnchor_.x, popupAnchor_.b() + 6, 296, 316};
        if (box.b() > H - 12) box.y = popupAnchor_.y - box.h - 6;
        if (box.r() > W - 12) box.x = W - 12 - box.w;
    }
    bool pressOutside = in.pressed && !box.contains(in.mx, in.my) && (time - popupOpenedAt_) > 0.05;
    if (pressOutside || key(VK_ESCAPE)) {
        consumeKey(VK_ESCAPE);
        popupKind_ = PopupKind::None;
        consumeMouse();
        repaint = true;
        return;
    }
    g.pushOpacity(fade);
    g.pushTranslate(0, (1 - fade) * -4);
    g.shadow(box, 12, 22, 0.9f);
    g.fillRound(box, 12, night4);
    g.strokeRound(box, 12, hairlineStrong);

    if (popupKind_ == PopupKind::List) {
        Rect inner = box.inset(6);
        float rowH = 36;
        float off = scrollBegin(makeId("popup-scroll", popupOwner_), inner, rowH * (float)popupItems_.size());
        for (size_t i = 0; i < popupItems_.size(); ++i) {
            Rect row{inner.x, inner.y + rowH * (float)i - off, inner.w, rowH};
            if (row.b() < inner.y || row.y > inner.b()) continue;
            const DropItem& it = popupItems_[i];
            bool over = mouseOver(row);
            if (over || (int)i == popupSel_) g.fillRound(row, 8, over ? night3 : night3.withAlpha(0.6f));
            if (over) cursor = Cursor::Hand;
            float x = row.x + 12;
            if (it.icon != Icon::None) {
                Color ic = it.color.a > 0 ? it.color : snow2;
                if (it.color.a > 0) iconBadge(*this, {x - 2, row.y + 6, 24, 24}, it.icon, ic, 0.62f);
                else g.icon(it.icon, {x, row.y + 9, 18, 18}, ic);
                x += 30;
            } else if (it.color.a > 0) {
                g.circle(x + 5, row.cy(), 5, it.color);
                x += 20;
            }
            float hintW = it.hint.empty() ? 0 : g.measure(it.hint, Font::AmountSmall) + 12;
            g.text(it.text, {x, row.y, row.r() - x - 34 - hintW, rowH}, Font::Body, snow1);
            if (!it.hint.empty()) g.text(it.hint, {row.r() - 34 - hintW, row.y, hintW, rowH}, Font::AmountSmall, snow3, Align::Right);
            if ((int)i == popupSel_) g.icon(Icon::Check, {row.r() - 28, row.y + 9, 18, 18}, frost);
            if (over && in.released && row.contains(in.downX, in.downY)) {
                resultOwner_ = popupOwner_;
                listResult_ = (int)i;
                popupKind_ = PopupKind::None;
                consumeMouse();
                repaint = true;
            }
        }
        scrollEnd();
    } else {
        // calendar
        Rect head{box.x + 12, box.y + 10, box.w - 24, 34};
        g.text(monthName(popupMonth_.m, true) + L" " + std::to_wstring(popupMonth_.y), head.cutLeft(4), Font::BodyStrong, snow1);
        bool prev = false, next = false;
        Rect pb{head.r() - 68, head.y + 2, 30, 30}, nb{head.r() - 32, head.y + 2, 30, 30};
        if (iconButton(*this, makeId("cal-prev", 0), pb, Icon::ChevLeft, snow2, false, 16)) prev = true;
        if (iconButton(*this, makeId("cal-next", 0), nb, Icon::ChevRight, snow2, false, 16)) next = true;
        if (prev) popupMonth_ = popupMonth_.addMonths(-1);
        if (next) popupMonth_ = popupMonth_.addMonths(1);
        float cw = (box.w - 24) / 7.0f, ch = 34;
        float gy = head.b() + 6;
        for (int i = 0; i < 7; ++i)
            g.text(weekdayShort((i + popupFdow_) % 7), {box.x + 12 + cw * i, gy, cw, 22}, Font::Caption, snow3, Align::Center);
        gy += 24;
        Date first = popupMonth_;
        int lead = (first.weekday() - popupFdow_ + 7) % 7;
        Date start = first.addDays(-lead);
        Date today = Date::today();
        for (int k = 0; k < 42; ++k) {
            Date d = start.addDays(k);
            Rect cell{box.x + 12 + cw * (k % 7), gy + ch * (k / 7), cw, ch};
            Rect dot{cell.cx() - 15, cell.cy() - 15, 30, 30};
            bool inMonth = d.m == popupMonth_.m;
            bool sel = d == popupDate_;
            bool over = mouseOver(dot);
            if (sel) g.fillRound(dot, 15, frost);
            else if (over) g.fillRound(dot, 15, night3);
            else if (d == today) g.strokeRound(dot, 15, frost.withAlpha(0.6f));
            Color tc = sel ? night0 : (inMonth ? snow1 : snow3);
            g.text(std::to_wstring(d.d), dot, Font::Small, tc, Align::Center);
            if (over) cursor = Cursor::Hand;
            if (over && in.released && dot.contains(in.downX, in.downY)) {
                resultOwner_ = popupOwner_;
                dateResult_ = true;
                dateValue_ = d;
                popupKind_ = PopupKind::None;
                consumeMouse();
                repaint = true;
            }
        }
    }
    g.popTranslate();
    g.popOpacity();
}

// ---------------------------------------------------------------- widgets

bool Ctx::clicked(Id id, const Rect& r, bool over) {
    if (over && in.pressed) active = id;
    bool hit = over && in.released && (active == id || r.contains(in.downX, in.downY));
    if (hit) consumeMouse();
    return hit;
}

static bool clickLogic(Ctx& c, Id id, const Rect& r, bool over) { return c.clicked(id, r, over); }

bool button(Ctx& c, Id id, const Rect& r, std::wstring_view label, Btn style, Icon icon, bool enabled) {
    bool over = enabled && c.mouseOver(r);
    if (over) c.cursor = Cursor::Hand;
    bool clicked = enabled && clickLogic(c, id, r, over);
    float h = c.anim(makeId(id, 1), over ? 1.0f : 0.0f, 22);
    Color bg, fg;
    switch (style) {
        case Btn::Primary: bg = mix(frost, rgb(0xA3D3E0), h * 0.6f); fg = night0; break;
        case Btn::Danger: bg = mix(expense.withAlpha(0.16f), expense.withAlpha(0.26f), h); fg = expense; break;
        case Btn::Ghost: bg = night3.withAlpha(h); fg = snow1; break;
        case Btn::Subtle: bg = night3.withAlpha(0.5f + 0.5f * h); fg = snow1; break;
        default: bg = mix(night3, night4, h); fg = snow1; break;
    }
    if (!enabled) { bg = bg.mul(0.5f); fg = fg.mul(0.45f); }
    const Rect& rr = r;
    if (bg.a > 0.001f) c.g.fillRound(rr, radiusS + 2, bg);
    if (style == Btn::Secondary) c.g.strokeRound(rr, radiusS + 2, hairline);
    float tw = c.g.measure(label, Font::BodyStrong);
    float iw = icon != Icon::None ? 18 + (label.empty() ? 0 : 8) : 0;
    float x = r.cx() - (tw + iw) * 0.5f;
    if (icon != Icon::None) { c.g.icon(icon, {x, r.cy() - 9, 18, 18}, fg, 1.8f); x += iw; }
    if (!label.empty()) c.g.text(label, {x, r.y, tw + 2, r.h}, Font::BodyStrong, fg);
    return clicked;
}

bool iconButton(Ctx& c, Id id, const Rect& r, Icon icon, Color fg, bool selected, float iconSize, Color hoverBg) {
    bool over = c.mouseOver(r);
    if (over) c.cursor = Cursor::Hand;
    bool clicked = clickLogic(c, id, r, over);
    float h = c.anim(makeId(id, 1), over || selected ? 1.0f : 0.0f, 22);
    if (h > 0.001f) c.g.fillRound(r, std::min(r.w, r.h) * 0.3f, hoverBg.withAlpha(hoverBg.a * h));
    c.g.icon(icon, {r.cx() - iconSize / 2, r.cy() - iconSize / 2, iconSize, iconSize}, mix(fg, snow1, h * 0.35f));
    return clicked;
}

bool hoverArea(Ctx& c, Id id, const Rect& r, float radius, Color hoverBg, bool selected, Color selectedBg) {
    bool over = c.mouseOver(r);
    if (over) c.cursor = Cursor::Hand;
    bool clicked = clickLogic(c, id, r, over);
    float h = c.anim(makeId(id, 1), over ? 1.0f : 0.0f, 22);
    if (selected) c.g.fillRound(r, radius, selectedBg);
    else if (h > 0.001f) c.g.fillRound(r, radius, hoverBg.withAlpha(hoverBg.a * h));
    return clicked;
}

static void clipboardSet(HWND hwnd, const std::wstring& s) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (h) {
        void* p = GlobalLock(h);
        memcpy(p, s.c_str(), (s.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

static std::wstring clipboardGet(HWND hwnd) {
    std::wstring out;
    if (!OpenClipboard(hwnd)) return out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        auto* p = static_cast<const wchar_t*>(GlobalLock(h));
        if (p) out = p;
        GlobalUnlock(h);
    }
    CloseClipboard();
    return out;
}

FieldResult textField(Ctx& c, Id id, const Rect& r, std::wstring& text, std::wstring_view placeholder, const FieldOpts& o) {
    FieldResult res;
    c.focusables.push_back(id);
    EditState& st = c.edit(id);
    bool over = c.mouseOver(r);
    if (over) c.cursor = Cursor::IBeam;
    if (c.pendingFocus == id) {
        c.focus = id;
        c.pendingFocus = 0;
        st.anchor = 0;
        st.caret = (int)text.size();
    }
    float padL = 12 + (o.icon != Icon::None ? 26 : 0), padR = 12;
    Rect tr{r.x + padL, r.y, r.w - padL - padR, r.h};
    std::wstring shown = o.password ? std::wstring(text.size(), L'•') : text;
    float textW = c.g.measure(shown, o.font);
    float alignOff = 0;
    if (o.align == Align::Right && textW < tr.w) alignOff = tr.w - textW;
    if (o.align == Align::Center && textW < tr.w) alignOff = (tr.w - textW) * 0.5f;

    if (over && c.in.pressed) {
        bool wasFocused = c.focus == id;
        c.focus = id;
        c.active = id;
        int idx = c.g.hitTest(shown, o.font, c.in.mx - tr.x - alignOff + st.scroll);
        if (c.in.dbl) { st.anchor = 0; st.caret = (int)text.size(); }
        else {
            st.caret = idx;
            if (!c.in.shift || !wasFocused) st.anchor = idx;
        }
        st.dragging = true;
    }
    if (st.dragging && c.in.down && c.active == id) {
        st.caret = c.g.hitTest(shown, o.font, c.in.mx - tr.x - alignOff + st.scroll);
    }
    if (!c.in.down) st.dragging = false;
    if (c.in.pressed && !over && c.focus == id && c.interactive()) c.focus = 0;

    bool focused = c.focus == id;
    res.focused = focused;
    st.caret = std::clamp(st.caret, 0, (int)text.size());
    st.anchor = std::clamp(st.anchor, 0, (int)text.size());

    if (focused) {
        c.textFocused = true;
        auto delSel = [&]() -> bool {
            if (st.caret == st.anchor) return false;
            int a = std::min(st.caret, st.anchor), b = std::max(st.caret, st.anchor);
            text.erase(a, b - a);
            st.caret = st.anchor = a;
            return true;
        };
        for (wchar_t ch : c.in.chars) {
            if (ch < 32 || ch == 127) continue;
            if (o.digitsOnly && (ch < L'0' || ch > L'9')) continue;
            bool removed = delSel();
            if ((int)text.size() >= o.maxLen) { if (removed) res.changed = true; continue; }
            text.insert(text.begin() + st.caret, ch);
            st.caret++;
            st.anchor = st.caret;
            res.changed = true;
        }
        std::vector<unsigned> keep;
        for (unsigned k : c.in.keys) {
            bool used = true;
            bool shift = c.in.shift, ctrl = c.in.ctrl;
            auto moveTo = [&](int p) { st.caret = std::clamp(p, 0, (int)text.size()); if (!shift) st.anchor = st.caret; };
            auto wordLeft = [&]() { int p = st.caret; while (p > 0 && text[p - 1] == L' ') --p; while (p > 0 && text[p - 1] != L' ') --p; return p; };
            auto wordRight = [&]() { int p = st.caret, n = (int)text.size(); while (p < n && text[p] != L' ') ++p; while (p < n && text[p] == L' ') ++p; return p; };
            switch (k) {
                case VK_BACK:
                    if (!delSel() && st.caret > 0) {
                        int from = ctrl ? wordLeft() : st.caret - 1;
                        text.erase(from, st.caret - from);
                        st.caret = st.anchor = from;
                    }
                    res.changed = true;
                    break;
                case VK_DELETE:
                    if (!delSel() && st.caret < (int)text.size()) text.erase(st.caret, 1);
                    res.changed = true;
                    break;
                case VK_LEFT:
                    if (!shift && st.caret != st.anchor) moveTo(std::min(st.caret, st.anchor));
                    else moveTo(ctrl ? wordLeft() : st.caret - 1);
                    break;
                case VK_RIGHT:
                    if (!shift && st.caret != st.anchor) moveTo(std::max(st.caret, st.anchor));
                    else moveTo(ctrl ? wordRight() : st.caret + 1);
                    break;
                case VK_HOME: moveTo(0); break;
                case VK_END: moveTo((int)text.size()); break;
                case VK_RETURN: res.submitted = true; used = false; break;
                case 'A': if (ctrl) { st.anchor = 0; st.caret = (int)text.size(); } else used = false; break;
                case 'C':
                case 'X':
                    if (ctrl && !o.password && st.caret != st.anchor) {
                        int a = std::min(st.caret, st.anchor), b = std::max(st.caret, st.anchor);
                        clipboardSet(c.hwnd, text.substr(a, b - a));
                        if (k == 'X') { delSel(); res.changed = true; }
                    } else used = ctrl;
                    break;
                case 'V':
                    if (ctrl) {
                        std::wstring clip = clipboardGet(c.hwnd);
                        std::wstring clean;
                        for (wchar_t ch : clip) {
                            if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
                            if (o.digitsOnly && (ch < L'0' || ch > L'9')) continue;
                            if (ch >= 32) clean += ch;
                        }
                        delSel();
                        size_t room = (size_t)std::max(0, o.maxLen - (int)text.size());
                        clean = clean.substr(0, room);
                        text.insert(st.caret, clean);
                        st.caret += (int)clean.size();
                        st.anchor = st.caret;
                        res.changed = true;
                    } else used = false;
                    break;
                default: used = false; break;
            }
            if (!used) keep.push_back(k);
        }
        c.in.keys = keep;
        shown = o.password ? std::wstring(text.size(), L'•') : text;
        textW = c.g.measure(shown, o.font);
        alignOff = 0;
        if (o.align == Align::Right && textW < tr.w) alignOff = tr.w - textW;
        if (o.align == Align::Center && textW < tr.w) alignOff = (tr.w - textW) * 0.5f;
    }

    // keep caret visible
    float cx = c.g.caretX(shown, o.font, st.caret);
    if (cx - st.scroll > tr.w - 2) st.scroll = cx - tr.w + 2;
    if (cx - st.scroll < 0) st.scroll = cx;
    if (textW <= tr.w) st.scroll = 0;

    // draw
    float fh = c.anim(makeId(id, 2), focused ? 1.0f : (over ? 0.5f : 0.0f), 20);
    if (!o.flat) {
        c.g.fillRound(r, radiusS + 2, mix(night3, rgb(0x262D3B), fh));
        Color border = o.error ? expense.withAlpha(0.8f) : mix(hairline, frost.withAlpha(0.65f), focused ? fh : 0.0f);
        c.g.strokeRound(r, radiusS + 2, border, focused ? 1.5f : 1.0f);
    }
    if (o.icon != Icon::None) c.g.icon(o.icon, {r.x + 12, r.cy() - 9, 18, 18}, snow3);
    c.g.pushClip(tr);
    float lh = c.g.lineHeight(o.font);
    float ty = r.cy() - lh / 2;
    if (focused && st.caret != st.anchor) {
        float a = c.g.caretX(shown, o.font, std::min(st.caret, st.anchor));
        float b = c.g.caretX(shown, o.font, std::max(st.caret, st.anchor));
        c.g.fill({tr.x + alignOff + a - st.scroll, ty, b - a, lh}, frostDeep.withAlpha(0.55f));
    }
    if (text.empty()) {
        if (!placeholder.empty()) c.g.text(placeholder, {tr.x, r.y, tr.w, r.h}, o.font, snow3, o.align);
    } else {
        c.g.text(shown, {tr.x + alignOff - st.scroll, r.y, textW + 4, r.h}, o.font, o.textColor, Align::Left, false);
    }
    if (focused) {
        c.wantsCaret = true;
        double phase = std::fmod(c.time, 1.06);
        if (phase < 0.6) {
            float x = tr.x + alignOff + cx - st.scroll;
            c.g.fill({std::round(x * c.g.scale()) / c.g.scale(), ty + 1, 1.5f, lh - 2}, frost);
        }
    }
    c.g.popClip();
    return res;
}

bool toggle(Ctx& c, Id id, const Rect& r, bool& value) {
    Rect sw{r.r() - 40, r.cy() - 11, 40, 22};
    bool over = c.mouseOver(r);
    if (over) c.cursor = Cursor::Hand;
    bool clicked = clickLogic(c, id, r, over);
    if (clicked) value = !value;
    float t = c.anim(makeId(id, 3), value ? 1.0f : 0.0f, 20);
    c.g.fillRound(sw, 11, mix(night4, frost, t));
    c.g.strokeRound(sw, 11, hairline);
    float kx = sw.x + 11 + t * 18;
    c.g.circle(kx, sw.cy(), 8, mix(snow2, night0, t));
    return clicked;
}

int segmented(Ctx& c, Id id, const Rect& r, const std::vector<std::wstring>& items, int sel, Color accent) {
    if (items.empty()) return sel;
    c.g.fillRound(r, radiusS + 2, night3);
    float w = r.w / (float)items.size();
    float pos = c.anim(makeId(id, 4), (float)sel, 20);
    Rect pill{r.x + 3 + w * pos, r.y + 3, w - 6, r.h - 6};
    c.g.fillRound(pill, radiusS, night4);
    c.g.strokeRound(pill, radiusS, hairlineStrong);
    int out = sel;
    for (size_t i = 0; i < items.size(); ++i) {
        Rect cell{r.x + w * (float)i, r.y, w, r.h};
        bool over = c.mouseOver(cell);
        if (over) c.cursor = Cursor::Hand;
        if (clickLogic(c, makeId((int64_t)i, id), cell, over)) out = (int)i;
        bool on = (int)i == sel;
        c.g.text(items[i], cell.inset(6, 0), on ? Font::BodyStrong : Font::Body, on ? (accent.a > 0 ? mix(snow1, accent, 0.0f) : snow1) : (over ? snow1 : snow2), Align::Center);
    }
    return out;
}

bool swatch(Ctx& c, Id id, const Rect& r, Color col, bool selected) {
    bool over = c.mouseOver(r);
    if (over) c.cursor = Cursor::Hand;
    bool clicked = clickLogic(c, id, r, over);
    float rad = std::min(r.w, r.h) / 2;
    float s = c.anim(makeId(id, 5), selected ? 1.0f : (over ? 0.4f : 0.0f), 20);
    if (s > 0.01f) c.g.ring(r.cx(), r.cy(), rad - 1, col.withAlpha(0.35f + 0.65f * s), 2);
    c.g.circle(r.cx(), r.cy(), rad - 5, col);
    if (selected) c.g.icon(Icon::Check, {r.cx() - 7, r.cy() - 7, 14, 14}, night0, 2.4f);
    return clicked;
}

bool dropdown(Ctx& c, Id id, const Rect& r, const std::vector<DropItem>& items, int& sel, std::wstring_view placeholder) {
    bool changed = false;
    int res;
    if (c.takeListResult(id, res)) { if (res != sel) changed = true; sel = res; }
    bool over = c.mouseOver(r);
    if (over) c.cursor = Cursor::Hand;
    bool open = c.popupOpenFor(id);
    if (clickLogic(c, id, r, over) && !open) c.openList(id, r, items, sel);
    float h = c.anim(makeId(id, 6), over || open ? 1.0f : 0.0f, 20);
    c.g.fillRound(r, radiusS + 2, mix(night3, rgb(0x262D3B), h));
    c.g.strokeRound(r, radiusS + 2, open ? frost.withAlpha(0.6f) : hairline, open ? 1.5f : 1.0f);
    float x = r.x + 12;
    if (sel >= 0 && sel < (int)items.size()) {
        const DropItem& it = items[sel];
        if (it.icon != Icon::None) {
            if (it.color.a > 0) iconBadge(c, {x - 2, r.cy() - 12, 24, 24}, it.icon, it.color, 0.62f);
            else c.g.icon(it.icon, {x, r.cy() - 9, 18, 18}, snow2);
            x += 30;
        } else if (it.color.a > 0) {
            c.g.circle(x + 5, r.cy(), 5, it.color);
            x += 20;
        }
        c.g.text(it.text, {x, r.y, r.r() - x - 34, r.h}, Font::Body, snow1);
    } else {
        c.g.text(placeholder, {x, r.y, r.r() - x - 34, r.h}, Font::Body, snow3);
    }
    c.g.icon(open ? Icon::ChevUp : Icon::ChevDown, {r.r() - 30, r.cy() - 8, 16, 16}, snow3);
    return changed;
}

bool dateField(Ctx& c, Id id, const Rect& r, Date& d, int fdow) {
    bool changed = false;
    Date nd;
    if (c.takeDateResult(id, nd)) { changed = !(nd == d); d = nd; }
    bool over = c.mouseOver(r);
    if (over) c.cursor = Cursor::Hand;
    bool open = c.popupOpenFor(id);
    if (clickLogic(c, id, r, over) && !open) c.openCalendar(id, r, d, fdow);
    float h = c.anim(makeId(id, 6), over || open ? 1.0f : 0.0f, 20);
    c.g.fillRound(r, radiusS + 2, mix(night3, rgb(0x262D3B), h));
    c.g.strokeRound(r, radiusS + 2, open ? frost.withAlpha(0.6f) : hairline, open ? 1.5f : 1.0f);
    c.g.icon(Icon::Calendar, {r.x + 12, r.cy() - 9, 18, 18}, snow3);
    c.g.text(dateLabel(d), {r.x + 40, r.y, r.w - 52, r.h}, Font::Body, snow1);
    return changed;
}

void card(Ctx& c, const Rect& r, Color bg) {
    c.g.fillRound(r, radiusM, bg);
    c.g.strokeRound(r, radiusM, hairline);
}

void sectionLabel(Ctx& c, const Rect& r, std::wstring_view text) {
    std::wstring up(text);
    for (auto& ch : up) {
        if (ch >= L'a' && ch <= L'z') ch -= 32;
        else if (ch >= 0x430 && ch <= 0x44F) ch -= 32;
        else if (ch == 0x451) ch = 0x401;
    }
    c.g.text(up, r, Font::Label, snow3);
}

void iconBadge(Ctx& c, const Rect& r, Icon icon, Color col, float iconScale) {
    float d = std::min(r.w, r.h);
    c.g.circle(r.cx(), r.cy(), d / 2, col.withAlpha(0.16f));
    float is = d * iconScale;
    c.g.icon(icon, {r.cx() - is / 2, r.cy() - is / 2, is, is}, col, 1.8f);
}

}  // namespace fp::ui
