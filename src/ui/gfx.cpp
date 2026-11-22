#include "ui/gfx.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace fp::ui {

namespace {

constexpr float kPi = 3.14159265358979f;

D2D1_COLOR_F dcol(Color c) { return {c.r, c.g, c.b, c.a}; }
D2D1_RECT_F drect(const Rect& r) { return {r.x, r.y, r.x + r.w, r.y + r.h}; }
D2D1_MATRIX_3X2_F identity() { return {1, 0, 0, 1, 0, 0}; }
D2D1_MATRIX_3X2_F mulM(const D2D1_MATRIX_3X2_F& a, const D2D1_MATRIX_3X2_F& b) {
    return {a._11 * b._11 + a._12 * b._21,         a._11 * b._12 + a._12 * b._22,
            a._21 * b._11 + a._22 * b._21,         a._21 * b._12 + a._22 * b._22,
            a._31 * b._11 + a._32 * b._21 + b._31, a._31 * b._12 + a._32 * b._22 + b._32};
}

struct FontSpec {
    float size;
    DWRITE_FONT_WEIGHT weight;
    bool display;
    bool tnum;
    float spacing;
};

const FontSpec kFonts[(int)Font::Count] = {
    {11, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, false, 0.7f},  // Label
    {12, DWRITE_FONT_WEIGHT_NORMAL, false, false, 0},        // Caption
    {13, DWRITE_FONT_WEIGHT_NORMAL, false, false, 0},        // Small
    {14, DWRITE_FONT_WEIGHT_NORMAL, false, false, 0},        // Body
    {14, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, false, 0},     // BodyStrong
    {16, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, false, 0},     // Subtitle
    {22, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, false, 0},      // Title
    {14, DWRITE_FONT_WEIGHT_SEMI_BOLD, false, true, 0},      // Amount
    {12, DWRITE_FONT_WEIGHT_NORMAL, false, true, 0},         // AmountSmall
    {18, DWRITE_FONT_WEIGHT_SEMI_BOLD, true, true, 0},       // AmountMid
    {26, DWRITE_FONT_WEIGHT_LIGHT, true, true, 0},           // AmountLarge
    {40, DWRITE_FONT_WEIGHT_LIGHT, true, true, 0},           // AmountHuge
    {28, DWRITE_FONT_WEIGHT_LIGHT, true, false, 0},          // PinDigit
};

// 24x24 stroke icons. Syntax: SVG absolute M L H V C Q A Z, plus R x y w h r (rounded rect) and O cx cy r (circle).
const char* iconPath(Icon id) {
    switch (id) {
        case Icon::None: return "";
        case Icon::Plus: return "M12 5V19M5 12H19";
        case Icon::Minus: return "M5 12H19";
        case Icon::Close: return "M6.5 6.5L17.5 17.5M17.5 6.5L6.5 17.5";
        case Icon::Check: return "M5 12.5L10 17.5L19 7";
        case Icon::ChevLeft: return "M15 5.5L8.5 12L15 18.5";
        case Icon::ChevRight: return "M9 5.5L15.5 12L9 18.5";
        case Icon::ChevDown: return "M6 9L12 15L18 9";
        case Icon::ChevUp: return "M6 15L12 9L18 15";
        case Icon::Search: return "O11 11 6.5 M16 16L20 20";
        case Icon::Lock: return "R5 10.5 14 10 2.5 M8 10.5V7.5A4 4 0 0 1 16 7.5V10.5";
        case Icon::Unlock: return "R5 10.5 14 10 2.5 M8 10.5V7.5A4 4 0 0 1 15.6 5.8";
        case Icon::Sliders: return "M4 7H13M17 7H20M4 17H7M11 17H20 O15 7 2 O9 17 2";
        case Icon::Grid: return "R4 4 7 7 1.8 R13 4 7 7 1.8 R4 13 7 7 1.8 R13 13 7 7 1.8";
        case Icon::List: return "M9 6.5H20M9 12H20M9 17.5H20 O4.8 6.5 0.6 O4.8 12 0.6 O4.8 17.5 0.6";
        case Icon::Wallet: return "R3 6 18 14 2.5 M3 10H21 M16 14.5H17.5 M6 6L15.5 3.2A1.5 1.5 0 0 1 17.4 4.6V6";
        case Icon::Tag: return "M3.5 12.4V5A1.5 1.5 0 0 1 5 3.5H12.4L20.3 11.4A1.6 1.6 0 0 1 20.3 13.6L13.6 20.3A1.6 1.6 0 0 1 11.4 20.3Z O8.2 8.2 1.4";
        case Icon::Pie: return "M11 4A8.5 8.5 0 1 0 20 13H11Z M14 2.5A8 8 0 0 1 21.5 10H14Z";
        case Icon::Bars: return "M4 20.5H20 M7 16.5V11 M12 16.5V5.5 M17 16.5V8.5";
        case Icon::Calendar: return "R4 5 16 15 2.5 M4 10H20 M8.5 3V7 M15.5 3V7";
        case Icon::Trash: return "M4 7H20 M9.5 7V4.5H14.5V7 M6.5 7L7.4 19.2A1.5 1.5 0 0 0 8.9 20.5H15.1A1.5 1.5 0 0 0 16.6 19.2L17.5 7 M10 11V16.5 M14 11V16.5";
        case Icon::Pencil: return "M4 20L4.6 16.4L15.8 5.2A2.1 2.1 0 0 1 18.8 8.2L7.6 19.4Z M13.8 7.2L16.8 10.2";
        case Icon::Eye: return "M2.5 12C5 7.2 8.4 5 12 5C15.6 5 19 7.2 21.5 12C19 16.8 15.6 19 12 19C8.4 19 5 16.8 2.5 12Z O12 12 3";
        case Icon::EyeOff: return "M2.5 12C5 7.2 8.4 5 12 5C15.6 5 19 7.2 21.5 12C19 16.8 15.6 19 12 19C8.4 19 5 16.8 2.5 12Z O12 12 3 M4 4L20 20";
        case Icon::Upload: return "M12 15V4 M7.5 8.5L12 4L16.5 8.5 M4 14.5V18.5A1.5 1.5 0 0 0 5.5 20H18.5A1.5 1.5 0 0 0 20 18.5V14.5";
        case Icon::Download: return "M12 4V15 M7.5 10.5L12 15L16.5 10.5 M4 14.5V18.5A1.5 1.5 0 0 0 5.5 20H18.5A1.5 1.5 0 0 0 20 18.5V14.5";
        case Icon::ArrowUp: return "M12 19V5M6 11L12 5L18 11";
        case Icon::ArrowDown: return "M12 5V19M6 13L12 19L18 13";
        case Icon::Swap: return "M4 8H18M14 4L18 8L14 12 M20 16H6M10 12L6 16L10 20";
        case Icon::Dots: return "O6 12 1 O12 12 1 O18 12 1";
        case Icon::WinMin: return "M6.5 12H17.5";
        case Icon::WinMax: return "R6.5 6.5 11 11 1.5";
        case Icon::WinRestore: return "R6.5 9 8.5 8.5 1.2 M9.5 6.5H16A1.5 1.5 0 0 1 17.5 8V14.5";
        case Icon::WinClose: return "M7 7L17 17M17 7L7 17";
        case Icon::Shield: return "M12 3L19 6V11.5C19 16 16 19.4 12 21C8 19.4 5 16 5 11.5V6Z";
        case Icon::Pulse: return "M2.5 12H7L9.5 6.5L14 17.5L16.5 12H21.5";
        case Icon::Info: return "O12 12 9 M12 11V16.5 O12 7.8 0.4";
        case Icon::Folder: return "M3 6.5A1.5 1.5 0 0 1 4.5 5H9.5L11.5 7H19.5A1.5 1.5 0 0 1 21 8.5V17.5A1.5 1.5 0 0 1 19.5 19H4.5A1.5 1.5 0 0 1 3 17.5Z";
        case Icon::Key: return "O8 15.5 4.5 M11.2 12.3L20 3.5 M16 7.5L18.5 10 M14 9.5L16 11.5";
        case Icon::Clock: return "O12 12 9 M12 7V12L15.5 14";
        case Icon::Archive: return "R3 4 18 5 1.2 M5 9V18.5A1.5 1.5 0 0 0 6.5 20H17.5A1.5 1.5 0 0 0 19 18.5V9 M10 13H14";
        case Icon::ArrowLeft: return "M19 12H5M11 6L5 12L11 18";
        case Icon::Monitor: return "R3 4 18 12 1.5 M8 20H16 M12 16V20";
        case Icon::Cart: return "M3 4H5.5L8 15.5H18L20.5 8H6.6 O9.5 19.5 1.3 O17 19.5 1.3";
        case Icon::Food: return "M6 3V8A3 3 0 0 0 12 8V3 M9 11V21 M9 3V8 M17.5 21V3C15.5 5 15 8.5 15.5 12.5H17.5";
        case Icon::Coffee: return "M5 9H16V14.5A4.5 4.5 0 0 1 11.5 19H9.5A4.5 4.5 0 0 1 5 14.5Z M16 10.5H17.5A2.5 2.5 0 0 1 17.5 15.5H16 M8 3.5V6 M11 3.5V6 M14 3.5V6";
        case Icon::Bus: return "R5 3 14 15 2.5 M5 12H19 M5 7.5H19 M8 18V20.5 M16 18V20.5 O8.5 15 0.5 O15.5 15 0.5";
        case Icon::Car: return "M3.5 16.5V12L6 7H18L20.5 12V16.5Z M3.5 12H20.5 O7.5 16.5 1.8 O16.5 16.5 1.8";
        case Icon::Home: return "M4 10.5L12 4L20 10.5V19A1 1 0 0 1 19 20H5A1 1 0 0 1 4 19Z M9.5 20V14H14.5V20";
        case Icon::Bolt: return "M13 2.5L5 13.5H11.5L10.5 21.5L19 10H12.5Z";
        case Icon::Phone: return "R7 2.5 10 19 2.2 M11 18.5H13";
        case Icon::Wifi: return "M2.5 9A13.5 13.5 0 0 1 21.5 9 M5.5 12.5A9 9 0 0 1 18.5 12.5 M8.5 16A4.5 4.5 0 0 1 15.5 16 O12 19.5 0.5";
        case Icon::Heart: return "M12 20C5.5 15.6 3 12.2 3 8.8A4.5 4.5 0 0 1 12 6.8A4.5 4.5 0 0 1 21 8.8C21 12.2 18.5 15.6 12 20Z";
        case Icon::Shirt: return "M8 3.5L3.5 6L2.5 10L5.5 11.5L7 10.5V20.5H17V10.5L18.5 11.5L21.5 10L20.5 6L16 3.5C15 5 13.6 5.8 12 5.8C10.4 5.8 9 5 8 3.5Z";
        case Icon::Film: return "R3 4 18 16 2 M7.5 4V20 M16.5 4V20 M3 9H7.5 M3 15H7.5 M16.5 9H21 M16.5 15H21";
        case Icon::Dumbbell: return "M6.5 7V17 M17.5 7V17 M3.5 9.5V14.5 M20.5 9.5V14.5 M6.5 12H17.5";
        case Icon::Book: return "M12 6.5C10 5 7 4.5 3.5 5V18.5C7 18 10 18.5 12 20C14 18.5 17 18 20.5 18.5V5C17 4.5 14 5 12 6.5Z M12 6.5V20";
        case Icon::Gift: return "R3.5 8 17 4 1 M5 12V19A1 1 0 0 0 6 20H18A1 1 0 0 0 19 19V12 M12 8V20 M12 8C11 5 8 3.5 7 5.5C6.3 7 8 8 12 8C16 8 17.7 7 17 5.5C16 3.5 13 5 12 8";
        case Icon::Plane: return "M21 3L3 10.5L10 13.5L13.5 21Z M10 13.5L21 3";
        case Icon::Paw: return "O6.5 10.5 1.7 O10 6.5 1.7 O14 6.5 1.7 O17.5 10.5 1.7 M8 17.2C8 14.2 10 12.2 12 12.2C14 12.2 16 14.2 16 17.2C16 19.2 14 19.6 12 18.9C10 19.6 8 19.2 8 17.2Z";
        case Icon::Repeat: return "M17 2.5L20.5 6L17 9.5 M3.5 11.5V10A4 4 0 0 1 7.5 6H20.5 M7 21.5L3.5 18L7 14.5 M20.5 12.5V14A4 4 0 0 1 16.5 18H3.5";
        case Icon::Briefcase: return "R3 7 18 13 2 M8.5 7V5.5A1.5 1.5 0 0 1 10 4H14A1.5 1.5 0 0 1 15.5 5.5V7 M3 12.5H21";
        case Icon::Building: return "M4 21V5A1 1 0 0 1 5 4H13A1 1 0 0 1 14 5V21 M14 10H19A1 1 0 0 1 20 11V21 M2.5 21H21.5 M7.5 8H10.5 M7.5 12H10.5 M7.5 16H10.5";
        case Icon::Percent: return "M19 5L5 19 O7 7 2.2 O17 17 2.2";
        case Icon::Receipt: return "M6 3H18V21L15.5 19.5L13.5 21L12 19.5L10.5 21L8.5 19.5L6 21Z M9 8H15 M9 12H15 M9 15.5H12.5";
        case Icon::Box: return "M12 3L20.5 7.5V16.5L12 21L3.5 16.5V7.5Z M3.5 7.5L12 12L20.5 7.5 M12 12V21";
        case Icon::Users: return "O9 8 3.5 M2.5 20C2.5 16.5 5.5 14 9 14C12.5 14 15.5 16.5 15.5 20 M15.5 4.6A3.5 3.5 0 0 1 15.5 11.4 M18 14.3C20 15 21.5 17 21.5 20";
        case Icon::User: return "O12 8 4 M4.5 20.5C4.5 16.5 8 14 12 14C16 14 19.5 16.5 19.5 20.5";
        case Icon::Wrench: return "M15 3.5A5 5 0 0 0 10.2 10.3L3.8 16.7A2.1 2.1 0 0 0 7.3 20.2L13.7 13.8A5 5 0 0 0 20.5 9L17 10.5L13.5 7Z";
        case Icon::Fuel: return "M4 21V5A1.5 1.5 0 0 1 5.5 3.5H12.5A1.5 1.5 0 0 1 14 5V21 M3 21H15 M4 10H14 M14 8H16.5A1.5 1.5 0 0 1 18 9.5V16A1.5 1.5 0 0 0 21 16V8L18.5 5.5";
        case Icon::Laptop: return "R4.5 5 15 10.5 1.5 M2.5 19H21.5";
        case Icon::Bank: return "M3 9.5L12 4L21 9.5Z M5 9.5V17 M9.5 9.5V17 M14.5 9.5V17 M19 9.5V17 M3 20H21";
        case Icon::Coins: return "O9 9 5.5 M15.96 9.58A5.5 5.5 0 1 1 9.58 14.05";
        case Icon::Hammer: return "M13 3.5L20.5 11L18 13.5L15.5 11L6 20.5A1.8 1.8 0 0 1 3.5 18L13 8.5L10.5 6Z";
        case Icon::Truck: return "M2.5 6.5H14.5V17H2.5Z M14.5 10H18.5L21.5 13.5V17H14.5 O6.5 17.5 1.8 O17.5 17.5 1.8";
        case Icon::Megaphone: return "M3.5 10V14H7L15 18.5V5.5L7 10Z M7 14L8.5 19.5H11 M18.5 9.5A3 3 0 0 1 18.5 14.5";
        case Icon::Music: return "M9 18V5L20 3V16 O6.5 18 2.5 O17.5 16 2.5";
        case Icon::Globe: return "O12 12 9 M3 12H21 M12 3C9.5 6 8.5 9 8.5 12C8.5 15 9.5 18 12 21C14.5 18 15.5 15 15.5 12C15.5 9 14.5 6 12 3Z";
        case Icon::Cash: return "R2.5 6 19 12 2 O12 12 2.8 M6 9.5V14.5 M18 9.5V14.5";
        case Icon::Card: return "R2.5 5 19 14 2.2 M2.5 10H21.5 M6 15H9.5";
        case Icon::Star: return "M12 3L14.7 8.6L20.8 9.4L16.3 13.6L17.5 19.7L12 16.7L6.5 19.7L7.7 13.6L3.2 9.4L9.3 8.6Z";
        case Icon::Leaf: return "M5 19C5 10 10 5 20 4C19.5 14 14 19 5 19Z M5 19L13 11";
        case Icon::Sparkle: return "M12 3L13.8 10.2L21 12L13.8 13.8L12 21L10.2 13.8L3 12L10.2 10.2Z";
        case Icon::Target: return "O12 12 8.5 O12 12 5 O12 12 1.5";
        case Icon::Bed: return "M3 19V6 M3 15H21V19 M21 15V12A2.5 2.5 0 0 0 18.5 9.5H11V15 O7 11.5 1.8";
        case Icon::Pill: return "M4.17 14.17L14.17 4.17A4 4 0 0 1 19.83 9.83L9.83 19.83A4 4 0 0 1 4.17 14.17Z M9.17 9.17L14.83 14.83";
        case Icon::Baby: return "O12 12 8.5 O9.2 10.5 0.5 O14.8 10.5 0.5 M9.5 14.5C11 16 13 16 14.5 14.5";
        case Icon::Count: return "";
    }
    return "";
}

struct PathParser {
    const char* p;
    ID2D1GeometrySink* sink;
    bool open = false;
    float cx = 0, cy = 0;

    void skip() { while (*p == ' ' || *p == ',') ++p; }
    bool num(float& v) {
        skip();
        if (!*p || ((*p < '0' || *p > '9') && *p != '-' && *p != '.')) return false;
        char* end = nullptr;
        v = std::strtof(p, &end);
        p = end;
        return true;
    }
    void endFig(bool closed) {
        if (open) sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        open = false;
    }
    void begin(float x, float y) {
        endFig(false);
        sink->BeginFigure({x, y}, D2D1_FIGURE_BEGIN_HOLLOW);
        open = true;
        cx = x; cy = y;
    }
    void arcTo(float rx, float ry, float rot, bool large, bool sweep, float x, float y) {
        D2D1_ARC_SEGMENT a{{x, y}, {rx, ry}, rot, sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                           large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL};
        sink->AddArc(a);
        cx = x; cy = y;
    }
    void lineTo(float x, float y) { sink->AddLine({x, y}); cx = x; cy = y; }

    void run() {
        char cmd = 0;
        for (;;) {
            skip();
            if (!*p) break;
            if ((*p >= 'A' && *p <= 'Z')) cmd = *p++;
            float a, b, c, d, e, f, g;
            switch (cmd) {
                case 'M': if (!num(a) || !num(b)) return; begin(a, b); cmd = 'L'; break;
                case 'L': if (!num(a) || !num(b)) return; lineTo(a, b); break;
                case 'H': if (!num(a)) return; lineTo(a, cy); break;
                case 'V': if (!num(a)) return; lineTo(cx, a); break;
                case 'C':
                    if (!num(a) || !num(b) || !num(c) || !num(d) || !num(e) || !num(f)) return;
                    sink->AddBezier({{a, b}, {c, d}, {e, f}});
                    cx = e; cy = f;
                    break;
                case 'Q':
                    if (!num(a) || !num(b) || !num(c) || !num(d)) return;
                    sink->AddQuadraticBezier({{a, b}, {c, d}});
                    cx = c; cy = d;
                    break;
                case 'A':
                    if (!num(a) || !num(b) || !num(c) || !num(d) || !num(e) || !num(f) || !num(g)) return;
                    arcTo(a, b, c, d != 0, e != 0, f, g);
                    break;
                case 'Z': endFig(true); cmd = 0; break;
                case 'R': {
                    if (!num(a) || !num(b) || !num(c) || !num(d) || !num(e)) return;
                    float x = a, y = b, w = c, h = d, r = e;
                    begin(x + r, y);
                    lineTo(x + w - r, y);
                    arcTo(r, r, 0, false, true, x + w, y + r);
                    lineTo(x + w, y + h - r);
                    arcTo(r, r, 0, false, true, x + w - r, y + h);
                    lineTo(x + r, y + h);
                    arcTo(r, r, 0, false, true, x, y + h - r);
                    lineTo(x, y + r);
                    arcTo(r, r, 0, false, true, x + r, y);
                    endFig(true);
                    break;
                }
                case 'O': {
                    if (!num(a) || !num(b) || !num(c)) return;
                    begin(a - c, b);
                    arcTo(c, c, 0, false, true, a + c, b);
                    arcTo(c, c, 0, false, true, a - c, b);
                    endFig(true);
                    break;
                }
                default: return;
            }
        }
        endFig(false);
    }
};

}  // namespace

// ---------------------------------------------------------------- lifecycle

bool Gfx::init(HWND hwnd) {
    hwnd_ = hwnd;
    D2D1_FACTORY_OPTIONS fo{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &fo,
                                 reinterpret_cast<void**>(factory_.GetAddressOf()))))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory1),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;
    D2D1_STROKE_STYLE_PROPERTIES sp{D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                    D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f};
    factory_->CreateStrokeStyle(&sp, nullptr, 0, &roundStroke_);
    D2D1_STROKE_STYLE_PROPERTIES fp{D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                    D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f};
    factory_->CreateStrokeStyle(&fp, nullptr, 0, &flatStroke_);
    createFormats();
    RECT rc;
    GetClientRect(hwnd_, &rc);
    w_ = (UINT)std::max<LONG>(1, rc.right - rc.left);
    h_ = (UINT)std::max<LONG>(1, rc.bottom - rc.top);
    return createDevice();
}

void Gfx::shutdown() {
    releaseDevice();
    layouts_.clear();
}

void Gfx::createFormats() {
    ComPtr<IDWriteFontCollection> sys;
    dwrite_->GetSystemFontCollection(&sys);
    auto has = [&](const wchar_t* name) {
        UINT32 idx;
        BOOL exists = FALSE;
        return sys && SUCCEEDED(sys->FindFamilyName(name, &idx, &exists)) && exists;
    };
    std::wstring text = has(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    std::wstring display = has(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : L"Segoe UI";
    dwrite_->CreateTypography(&tnum_);
    if (tnum_) tnum_->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
    for (int i = 0; i < (int)Font::Count; ++i) {
        const FontSpec& s = kFonts[i];
        dwrite_->CreateTextFormat((s.display ? display : text).c_str(), nullptr, s.weight, DWRITE_FONT_STYLE_NORMAL,
                                  DWRITE_FONT_STRETCH_NORMAL, s.size, L"", &formats_[i]);
        if (formats_[i]) {
            formats_[i]->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            dwrite_->CreateEllipsisTrimmingSign(formats_[i].Get(), &ellipsis_[i]);
        }
    }
}

bool Gfx::createDevice() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                  D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_1};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 6, D3D11_SDK_VERSION,
                                   &d3d_, nullptr, nullptr);
    if (hr == E_INVALIDARG)
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels + 1, 5, D3D11_SDK_VERSION,
                               &d3d_, nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels + 1, 5, D3D11_SDK_VERSION, &d3d_,
                               nullptr, nullptr);
    if (FAILED(hr)) return false;
    ComPtr<IDXGIDevice> dxgiDev;
    d3d_.As(&dxgiDev);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDev->GetAdapter(&adapter);
    ComPtr<IDXGIFactory2> fac;
    adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(fac.GetAddressOf()));
    if (!fac) return false;
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = w_;
    sd.Height = h_;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.Scaling = DXGI_SCALING_NONE;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    hr = fac->CreateSwapChainForHwnd(d3d_.Get(), hwnd_, &sd, nullptr, nullptr, &swap_);
    if (FAILED(hr)) {
        sd.Scaling = DXGI_SCALING_STRETCH;
        hr = fac->CreateSwapChainForHwnd(d3d_.Get(), hwnd_, &sd, nullptr, nullptr, &swap_);
    }
    if (FAILED(hr)) return false;
    fac->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(factory_->CreateDevice(dxgiDev.Get(), &d2dDevice_))) return false;
    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_))) return false;
    dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    dc_->CreateSolidColorBrush({1, 1, 1, 1}, &brush_);
    return createTarget();
}

bool Gfx::createTarget() {
    ComPtr<IDXGISurface> surface;
    if (FAILED(swap_->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf())))) return false;
    D2D1_BITMAP_PROPERTIES1 bp{};
    bp.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE};
    bp.dpiX = dpi_;
    bp.dpiY = dpi_;
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    if (FAILED(dc_->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &target_))) return false;
    dc_->SetTarget(target_.Get());
    dc_->SetDpi(dpi_, dpi_);
    return true;
}

void Gfx::releaseDevice() {
    if (dc_) dc_->SetTarget(nullptr);
    target_.Reset();
    brush_.Reset();
    dc_.Reset();
    d2dDevice_.Reset();
    swap_.Reset();
    d3d_.Reset();
}

void Gfx::resize() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    UINT w = (UINT)std::max<LONG>(1, rc.right - rc.left), h = (UINT)std::max<LONG>(1, rc.bottom - rc.top);
    if (w == w_ && h == h_) return;
    w_ = w;
    h_ = h;
    if (!swap_) return;
    dc_->SetTarget(nullptr);
    target_.Reset();
    if (FAILED(swap_->ResizeBuffers(0, w_, h_, DXGI_FORMAT_UNKNOWN, 0))) { releaseDevice(); return; }
    createTarget();
}

void Gfx::setDpi(float dpi) {
    if (dpi == dpi_) return;
    dpi_ = dpi;
    if (dc_) {
        dc_->SetTarget(nullptr);
        target_.Reset();
        createTarget();
    }
}

bool Gfx::begin(Color clear) {
    if (!dc_ && !createDevice()) return false;
    if (!target_ && !createTarget()) return false;
    dc_->BeginDraw();
    drawing_ = true;
    transforms_.clear();
    dc_->SetTransform(identity());
    dc_->Clear(dcol(clear));
    if (layouts_.size() > 3000) layouts_.clear();
    return true;
}

void Gfx::end() {
    if (!drawing_) return;
    while (layerDepth_ > 0) popOpacity();
    drawing_ = false;
    HRESULT hr = dc_->EndDraw();
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) { releaseDevice(); return; }
    hr = swap_->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) releaseDevice();
}

// ---------------------------------------------------------------- primitives

ID2D1SolidColorBrush* Gfx::brush(Color c) {
    brush_->SetColor(dcol(c));
    return brush_.Get();
}

void Gfx::fill(const Rect& r, Color c) { dc_->FillRectangle(drect(r), brush(c)); }

void Gfx::fillRound(const Rect& r, float radius, Color c) {
    if (radius <= 0) { fill(r, c); return; }
    dc_->FillRoundedRectangle({drect(r), radius, radius}, brush(c));
}

void Gfx::strokeRound(const Rect& r, float radius, Color c, float width) {
    Rect q = r.inset(width * 0.5f);
    dc_->DrawRoundedRectangle({drect(q), radius, radius}, brush(c), width);
}

void Gfx::gradientRound(const Rect& r, float radius, Color top, Color bottom) {
    D2D1_GRADIENT_STOP stops[2] = {{0, dcol(top)}, {1, dcol(bottom)}};
    ComPtr<ID2D1GradientStopCollection> col;
    dc_->CreateGradientStopCollection(stops, 2, &col);
    ComPtr<ID2D1LinearGradientBrush> lg;
    dc_->CreateLinearGradientBrush({{r.x, r.y}, {r.x, r.b()}}, col.Get(), &lg);
    if (lg) dc_->FillRoundedRectangle({drect(r), radius, radius}, lg.Get());
}

void Gfx::line(float x1, float y1, float x2, float y2, Color c, float width) {
    dc_->DrawLine({x1, y1}, {x2, y2}, brush(c), width, roundStroke_.Get());
}

void Gfx::circle(float cx, float cy, float radius, Color c) {
    dc_->FillEllipse({{cx, cy}, radius, radius}, brush(c));
}

void Gfx::ring(float cx, float cy, float radius, Color c, float width) {
    dc_->DrawEllipse({{cx, cy}, radius, radius}, brush(c), width);
}

void Gfx::arc(float cx, float cy, float radius, float a0, float a1, Color c, float width, bool roundCaps) {
    if (a1 - a0 <= 0.0005f) return;
    if (a1 - a0 >= 2 * kPi - 0.0005f) { ring(cx, cy, radius, c, width); return; }
    ComPtr<ID2D1PathGeometry> g;
    factory_->CreatePathGeometry(&g);
    ComPtr<ID2D1GeometrySink> s;
    g->Open(&s);
    s->BeginFigure({cx + radius * std::sin(a0), cy - radius * std::cos(a0)}, D2D1_FIGURE_BEGIN_HOLLOW);
    s->AddArc({{cx + radius * std::sin(a1), cy - radius * std::cos(a1)}, {radius, radius}, 0,
               D2D1_SWEEP_DIRECTION_CLOCKWISE, (a1 - a0) > kPi ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL});
    s->EndFigure(D2D1_FIGURE_END_OPEN);
    s->Close();
    dc_->DrawGeometry(g.Get(), brush(c), width, roundCaps ? roundStroke_.Get() : flatStroke_.Get());
}

void Gfx::polyline(const std::vector<Pt>& pts, Color c, float width) {
    if (pts.size() < 2) return;
    ComPtr<ID2D1PathGeometry> g;
    factory_->CreatePathGeometry(&g);
    ComPtr<ID2D1GeometrySink> s;
    g->Open(&s);
    s->BeginFigure({pts[0].x, pts[0].y}, D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < pts.size(); ++i) s->AddLine({pts[i].x, pts[i].y});
    s->EndFigure(D2D1_FIGURE_END_OPEN);
    s->Close();
    dc_->DrawGeometry(g.Get(), brush(c), width, roundStroke_.Get());
}

void Gfx::area(const std::vector<Pt>& pts, float baseY, Color top, Color bottom) {
    if (pts.size() < 2) return;
    ComPtr<ID2D1PathGeometry> g;
    factory_->CreatePathGeometry(&g);
    ComPtr<ID2D1GeometrySink> s;
    g->Open(&s);
    s->BeginFigure({pts[0].x, baseY}, D2D1_FIGURE_BEGIN_FILLED);
    float minY = baseY;
    for (auto& p : pts) { s->AddLine({p.x, p.y}); minY = std::min(minY, p.y); }
    s->AddLine({pts.back().x, baseY});
    s->EndFigure(D2D1_FIGURE_END_CLOSED);
    s->Close();
    D2D1_GRADIENT_STOP stops[2] = {{0, dcol(top)}, {1, dcol(bottom)}};
    ComPtr<ID2D1GradientStopCollection> col;
    dc_->CreateGradientStopCollection(stops, 2, &col);
    ComPtr<ID2D1LinearGradientBrush> lg;
    dc_->CreateLinearGradientBrush({{0, minY}, {0, baseY}}, col.Get(), &lg);
    if (lg) dc_->FillGeometry(g.Get(), lg.Get());
}

void Gfx::shadow(const Rect& r, float radius, float spread, float alpha) {
    const int n = 10;
    for (int i = n; i >= 1; --i) {
        float t = (float)i / n;
        float grow = spread * t;
        float a = alpha * (1 - t) * (1 - t) * 0.35f;
        Rect q{r.x - grow, r.y - grow * 0.6f, r.w + grow * 2, r.h + grow * 2};
        fillRound(q, radius + grow, Color{0, 0, 0, a});
    }
}

// ---------------------------------------------------------------- text

IDWriteTextLayout* Gfx::layout(std::wstring_view s, Font f, float maxW, bool wrap, Align a, bool ellipsis) {
    std::wstring key;
    key.reserve(s.size() + 16);
    key += wchar_t('A' + (int)f);
    key += wchar_t('0' + (wrap ? 1 : 0) + (ellipsis ? 2 : 0) + (int)a * 4);
    key += std::to_wstring((int)(maxW * 4));
    key += L'|';
    key += s;
    auto it = layouts_.find(key);
    if (it != layouts_.end()) return it->second.Get();
    ComPtr<IDWriteTextLayout> l;
    IDWriteTextFormat* fmt = formats_[(int)f].Get();
    if (!fmt) return nullptr;
    if (FAILED(dwrite_->CreateTextLayout(s.data(), (UINT32)s.size(), fmt, std::max(1.0f, maxW), 10000.0f, &l))) return nullptr;
    DWRITE_TEXT_RANGE all{0, (UINT32)s.size()};
    l->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    l->SetTextAlignment(a == Align::Left ? DWRITE_TEXT_ALIGNMENT_LEADING
                                         : (a == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_TRAILING));
    if (ellipsis && !wrap) {
        DWRITE_TRIMMING tr{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        l->SetTrimming(&tr, ellipsis_[(int)f].Get());
    }
    if (kFonts[(int)f].tnum && tnum_) l->SetTypography(tnum_.Get(), all);
    if (kFonts[(int)f].spacing > 0) {
        ComPtr<IDWriteTextLayout1> l1;
        if (SUCCEEDED(l.As(&l1))) l1->SetCharacterSpacing(0, kFonts[(int)f].spacing, 0, all);
    }
    IDWriteTextLayout* raw = l.Get();
    layouts_.emplace(std::move(key), std::move(l));
    return raw;
}

void Gfx::text(std::wstring_view s, const Rect& r, Font f, Color c, Align a, bool ellipsis) {
    if (s.empty() || r.w <= 0) return;
    IDWriteTextLayout* l = layout(s, f, r.w, false, a, ellipsis);
    if (!l) return;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    float y = r.y + (r.h - m.height) * 0.5f;
    dc_->DrawTextLayout({r.x, std::round(y * scale()) / scale()}, l, brush(c), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

void Gfx::textWrapped(std::wstring_view s, const Rect& r, Font f, Color c, Align a) {
    if (s.empty() || r.w <= 0) return;
    IDWriteTextLayout* l = layout(s, f, r.w, true, a, false);
    if (l) dc_->DrawTextLayout({r.x, r.y}, l, brush(c), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

float Gfx::measure(std::wstring_view s, Font f) {
    if (s.empty()) return 0;
    IDWriteTextLayout* l = layout(s, f, 10000, false, Align::Left, false);
    if (!l) return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

float Gfx::wrappedHeight(std::wstring_view s, Font f, float width) {
    if (s.empty()) return 0;
    IDWriteTextLayout* l = layout(s, f, width, true, Align::Left, false);
    if (!l) return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return m.height;
}

float Gfx::lineHeight(Font f) {
    IDWriteTextLayout* l = layout(L"Hg", f, 1000, false, Align::Left, false);
    if (!l) return kFonts[(int)f].size * 1.3f;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return m.height;
}

float Gfx::baseline(Font f) {
    IDWriteTextLayout* l = layout(L"Hg", f, 1000, false, Align::Left, false);
    if (!l) return kFonts[(int)f].size;
    DWRITE_LINE_METRICS lm{};
    UINT32 n = 0;
    l->GetLineMetrics(&lm, 1, &n);
    return lm.baseline;
}

int Gfx::hitTest(std::wstring_view s, Font f, float x) {
    if (s.empty()) return 0;
    IDWriteTextLayout* l = layout(s, f, 10000, false, Align::Left, false);
    if (!l) return 0;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS hm{};
    l->HitTestPoint(x, 1, &trailing, &inside, &hm);
    int idx = (int)hm.textPosition + (trailing ? (int)hm.length : 0);
    return std::clamp(idx, 0, (int)s.size());
}

float Gfx::caretX(std::wstring_view s, Font f, int index) {
    if (s.empty() || index <= 0) return 0;
    IDWriteTextLayout* l = layout(s, f, 10000, false, Align::Left, false);
    if (!l) return 0;
    float px = 0, py = 0;
    DWRITE_HIT_TEST_METRICS hm{};
    l->HitTestTextPosition((UINT32)std::min<int>(index, (int)s.size()), FALSE, &px, &py, &hm);
    return px;
}

// ---------------------------------------------------------------- icons

ID2D1PathGeometry* Gfx::iconGeometry(Icon id) {
    int i = (int)id;
    if (i <= 0 || i >= (int)Icon::Count) return nullptr;
    if (!icons_[i]) {
        ComPtr<ID2D1PathGeometry> g;
        factory_->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> s;
        g->Open(&s);
        PathParser pp{iconPath(id), s.Get()};
        pp.run();
        s->Close();
        icons_[i] = g;
    }
    return icons_[i].Get();
}

void Gfx::icon(Icon id, const Rect& r, Color c, float stroke) {
    ID2D1PathGeometry* g = iconGeometry(id);
    if (!g) return;
    float k = std::min(r.w, r.h) / 24.0f;
    D2D1_MATRIX_3X2_F cur;
    dc_->GetTransform(&cur);
    D2D1_MATRIX_3X2_F m = {k, 0, 0, k, r.x + (r.w - 24 * k) * 0.5f, r.y + (r.h - 24 * k) * 0.5f};
    dc_->SetTransform(mulM(m, cur));
    dc_->DrawGeometry(g, brush(c), stroke, roundStroke_.Get());
    dc_->SetTransform(cur);
}

// ---------------------------------------------------------------- state

void Gfx::pushClip(const Rect& r) { dc_->PushAxisAlignedClip(drect(r), D2D1_ANTIALIAS_MODE_ALIASED); }
void Gfx::popClip() { dc_->PopAxisAlignedClip(); }

void Gfx::pushOpacity(float a) {
    D2D1_LAYER_PARAMETERS1 lp{};
    lp.contentBounds = {-1e6f, -1e6f, 1e6f, 1e6f};
    lp.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    lp.maskTransform = identity();
    lp.opacity = std::clamp(a, 0.0f, 1.0f);
    lp.layerOptions = D2D1_LAYER_OPTIONS1_NONE;
    dc_->PushLayer(&lp, nullptr);
    ++layerDepth_;
}

void Gfx::popOpacity() {
    if (layerDepth_ <= 0) return;
    dc_->PopLayer();
    --layerDepth_;
}

void Gfx::pushTranslate(float dx, float dy) {
    D2D1_MATRIX_3X2_F cur;
    dc_->GetTransform(&cur);
    transforms_.push_back(cur);
    D2D1_MATRIX_3X2_F t = {1, 0, 0, 1, dx, dy};
    dc_->SetTransform(mulM(t, cur));
}

void Gfx::popTranslate() {
    if (transforms_.empty()) return;
    dc_->SetTransform(transforms_.back());
    transforms_.pop_back();
}

}  // namespace fp::ui
