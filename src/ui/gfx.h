#pragma once
// ADR-0002: Direct3D 11 + DXGI flip swap chain + Direct2D device context + DirectWrite.
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/model.h"
#include "ui/theme.h"

namespace fp::ui {

using Microsoft::WRL::ComPtr;

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float r() const { return x + w; }
    float b() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect inset(float dx, float dy) const { return {x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    Rect offset(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
    Rect takeTop(float hh) const { return {x, y, w, hh}; }
    Rect takeLeft(float ww) const { return {x, y, ww, h}; }
    Rect takeRight(float ww) const { return {x + w - ww, y, ww, h}; }
    Rect takeBottom(float hh) const { return {x, y + h - hh, w, hh}; }
    Rect cutTop(float hh) const { return {x, y + hh, w, h - hh}; }
    Rect cutLeft(float ww) const { return {x + ww, y, w - ww, h}; }
    Rect cutRight(float ww) const { return {x, y, w - ww, h}; }
    Rect cutBottom(float hh) const { return {x, y, w, h - hh}; }
    bool intersects(const Rect& o) const { return x < o.r() && o.x < r() && y < o.b() && o.y < b(); }
};

enum class Font : int {
    Label,         // 11 semibold, spaced, for section captions
    Caption,       // 12
    Small,         // 13
    Body,          // 14
    BodyStrong,    // 14 semibold
    Subtitle,      // 16 semibold
    Title,         // 22 semibold
    Amount,        // 14 semibold tabular
    AmountSmall,   // 12 regular tabular
    AmountMid,     // 18 semibold tabular
    AmountLarge,   // 26 light tabular
    AmountHuge,    // 40 light tabular
    PinDigit,      // 28 light
    Count
};

enum class Align { Left, Center, Right };

struct Pt { float x, y; };

class Gfx {
public:
    bool init(HWND hwnd);
    void shutdown();
    void resize();
    void setDpi(float dpi);
    float dpi() const { return dpi_; }
    float scale() const { return dpi_ / 96.0f; }
    float widthDip() const { return w_ / scale(); }
    float heightDip() const { return h_ / scale(); }

    bool begin(Color clear);
    void end();

    // primitives
    void fill(const Rect& r, Color c);
    void fillRound(const Rect& r, float radius, Color c);
    void strokeRound(const Rect& r, float radius, Color c, float width = 1.0f);
    void gradientRound(const Rect& r, float radius, Color top, Color bottom);
    void line(float x1, float y1, float x2, float y2, Color c, float width = 1.0f);
    void circle(float cx, float cy, float radius, Color c);
    void ring(float cx, float cy, float radius, Color c, float width);
    void arc(float cx, float cy, float radius, float a0, float a1, Color c, float width, bool roundCaps = false);
    void polyline(const std::vector<Pt>& pts, Color c, float width);
    void area(const std::vector<Pt>& pts, float baseY, Color top, Color bottom);
    void shadow(const Rect& r, float radius, float spread, float alpha);

    // text
    void text(std::wstring_view s, const Rect& r, Font f, Color c, Align a = Align::Left, bool ellipsis = true);
    void textWrapped(std::wstring_view s, const Rect& r, Font f, Color c, Align a = Align::Left);
    float measure(std::wstring_view s, Font f);
    float wrappedHeight(std::wstring_view s, Font f, float width);
    float lineHeight(Font f);
    float baseline(Font f);  // distance from layout top to the text baseline
    int hitTest(std::wstring_view s, Font f, float x);
    float caretX(std::wstring_view s, Font f, int index);

    // icons
    void icon(Icon id, const Rect& r, Color c, float stroke = 1.7f);

    // state
    void pushClip(const Rect& r);
    void popClip();
    void pushOpacity(float a);
    void popOpacity();
    void pushTranslate(float dx, float dy);
    void popTranslate();

    ID2D1DeviceContext* dc() { return dc_.Get(); }

private:
    bool createDevice();
    void releaseDevice();
    bool createTarget();
    void createFormats();
    ID2D1SolidColorBrush* brush(Color c);
    IDWriteTextLayout* layout(std::wstring_view s, Font f, float maxW, bool wrap, Align a, bool ellipsis);
    ID2D1PathGeometry* iconGeometry(Icon id);

    HWND hwnd_ = nullptr;
    UINT w_ = 0, h_ = 0;
    float dpi_ = 96;

    ComPtr<ID2D1Factory1> factory_;
    ComPtr<IDWriteFactory1> dwrite_;
    ComPtr<ID3D11Device> d3d_;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> dc_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1StrokeStyle> roundStroke_;
    ComPtr<ID2D1StrokeStyle> flatStroke_;
    ComPtr<IDWriteTextFormat> formats_[(int)Font::Count];
    ComPtr<IDWriteTypography> tnum_;
    ComPtr<IDWriteInlineObject> ellipsis_[(int)Font::Count];
    std::unordered_map<std::wstring, ComPtr<IDWriteTextLayout>> layouts_;
    ComPtr<ID2D1PathGeometry> icons_[(int)Icon::Count];
    std::vector<D2D1_MATRIX_3X2_F> transforms_;
    int layerDepth_ = 0;
    bool drawing_ = false;
};

}  // namespace fp::ui
