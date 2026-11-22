#pragma once
// Immediate-mode UI with retained per-widget state (ADR-0018). Frames run only on input or animation.
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/date.h"
#include "ui/gfx.h"

namespace fp::ui {

using Id = uint64_t;
Id makeId(std::wstring_view s, Id seed = 0);
Id makeId(int64_t v, Id seed);
inline Id makeId(const char* s, Id seed = 0) {
    Id h = seed ? seed : 1469598103934665603ULL;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; }
    return h;
}

struct Input {
    float mx = -1e5f, my = -1e5f;
    float downX = -1e5f, downY = -1e5f;  // where the left button last went down
    bool down = false, pressed = false, released = false, rpressed = false, dbl = false;
    float wheel = 0;
    std::wstring chars;
    std::vector<unsigned> keys;
    bool ctrl = false, shift = false, alt = false;
    void clearFrame() {
        pressed = released = rpressed = dbl = false;
        wheel = 0;
        chars.clear();
        keys.clear();
    }
};

enum class Cursor { Arrow, Hand, IBeam };

struct EditState {
    int caret = 0, anchor = 0;
    float scroll = 0;
    bool dragging = false;
};

struct DropItem {
    std::wstring text;
    Icon icon = Icon::None;
    Color color{0, 0, 0, 0};
    std::wstring hint;
};

enum class PopupKind { None, List, Calendar };

class Ctx {
public:
    explicit Ctx(Gfx& gfx) : g(gfx) {}
    Gfx& g;
    HWND hwnd = nullptr;
    Input in;
    double time = 0, dt = 0.016;
    Id active = 0, focus = 0, pendingFocus = 0;
    int layer = 0, topLayer = 0, topLayerPrev = 0;
    bool repaint = false;
    bool wantsCaret = false;
    bool textFocused = false;      // a text field has keyboard focus this frame
    Cursor cursor = Cursor::Arrow;
    std::vector<Rect> captionExclusions;

    void beginFrame(double now);
    void endFrame();
    void setLayer(int l) { layer = l; if (l > topLayer) topLayer = l; }
    bool interactive() const { return layer >= topLayerPrev; }
    bool mouseOver(const Rect& r) const;
    float anim(Id id, float target, float speed = 18.0f);
    void snap(Id id, float v) { anims_[id] = v; }
    bool key(unsigned vk) const;
    bool consumeKey(unsigned vk);
    void consumeMouse() { in.pressed = false; in.released = false; }
    // Click = press and release inside r. Uses the recorded press point, so it never depends on frame timing.
    bool clicked(Id id, const Rect& r, bool over);
    void requestFocus(Id id) { pendingFocus = id; }
    void pushClip(const Rect& r);
    void popClip();
    EditState& edit(Id id) { return edits_[id]; }

    // scrolling
    float scrollBegin(Id id, const Rect& view, float contentH);
    void scrollEnd();
    void scrollTo(Id id, float y) { scrollTarget_[id] = y; }

    // popups (drawn last by drawPopups)
    void openList(Id owner, const Rect& anchor, const std::vector<DropItem>& items, int sel, float width = 0);
    void openCalendar(Id owner, const Rect& anchor, Date d, int fdow);
    bool popupOpenFor(Id owner) const { return popupKind_ != PopupKind::None && popupOwner_ == owner; }
    bool popupOpen() const { return popupKind_ != PopupKind::None; }
    bool takeListResult(Id owner, int& sel);
    bool takeDateResult(Id owner, Date& d);
    void closePopup() { popupKind_ = PopupKind::None; }
    void drawPopups();

    std::vector<Id> focusables;

private:
    std::unordered_map<Id, float> anims_;
    std::unordered_map<Id, EditState> edits_;
    std::unordered_map<Id, float> scroll_, scrollTarget_;
    struct ScrollFrame { Id id; Rect view; float contentH; };
    std::vector<ScrollFrame> scrollStack_;
    std::vector<Rect> clips_;

    PopupKind popupKind_ = PopupKind::None;
    Id popupOwner_ = 0;
    Rect popupAnchor_{};
    float popupWidth_ = 0;
    std::vector<DropItem> popupItems_;
    int popupSel_ = -1;
    Date popupDate_{}, popupMonth_{};
    int popupFdow_ = 0;
    Id resultOwner_ = 0;
    int listResult_ = -1;
    bool dateResult_ = false;
    Date dateValue_{};
    double popupOpenedAt_ = 0;
};

// ---------------------------------------------------------------- widgets
enum class Btn { Primary, Secondary, Ghost, Danger, Subtle };

bool button(Ctx& c, Id id, const Rect& r, std::wstring_view label, Btn style = Btn::Secondary, Icon icon = Icon::None,
            bool enabled = true);
bool iconButton(Ctx& c, Id id, const Rect& r, Icon icon, Color fg, bool selected = false, float iconSize = 18,
                Color hoverBg = theme::night3);
bool hoverArea(Ctx& c, Id id, const Rect& r, float radius, Color hoverBg, bool selected = false, Color selectedBg = theme::night3);

struct FieldOpts {
    Font font = Font::Body;
    bool password = false;
    bool digitsOnly = false;
    int maxLen = 120;
    Align align = Align::Left;
    Icon icon = Icon::None;
    bool error = false;
    bool flat = false;   // no background (large inline fields)
    Color textColor = theme::snow1;
};
struct FieldResult {
    bool changed = false;
    bool submitted = false;
    bool focused = false;
};
FieldResult textField(Ctx& c, Id id, const Rect& r, std::wstring& text, std::wstring_view placeholder, const FieldOpts& o = {});

bool toggle(Ctx& c, Id id, const Rect& r, bool& value);
int segmented(Ctx& c, Id id, const Rect& r, const std::vector<std::wstring>& items, int sel, Color accent = theme::frost);
bool swatch(Ctx& c, Id id, const Rect& r, Color col, bool selected);
bool dropdown(Ctx& c, Id id, const Rect& r, const std::vector<DropItem>& items, int& sel, std::wstring_view placeholder = L"");
bool dateField(Ctx& c, Id id, const Rect& r, Date& d, int fdow);

// small drawing helpers shared by screens
void card(Ctx& c, const Rect& r, Color bg = theme::night2);
void sectionLabel(Ctx& c, const Rect& r, std::wstring_view text);
void iconBadge(Ctx& c, const Rect& r, Icon icon, Color col, float iconScale = 0.5f);

}  // namespace fp::ui
