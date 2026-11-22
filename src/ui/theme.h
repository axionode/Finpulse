#pragma once
// ADR-0003: "Nordic Night" design tokens. All colors go through here.
#include <cstdint>

namespace fp::ui {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    Color withAlpha(float na) const { return {r, g, b, na}; }
    Color mul(float k) const { return {r, g, b, a * k}; }
};

constexpr Color rgb(uint32_t hex, float a = 1.0f) {
    return {((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, a};
}

inline Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

namespace theme {
// surfaces, deep to high
constexpr Color night0 = rgb(0x0F1217);
constexpr Color night1 = rgb(0x151920);
constexpr Color night2 = rgb(0x1B2029);
constexpr Color night3 = rgb(0x232937);
constexpr Color night4 = rgb(0x2C3444);
constexpr Color hairline = rgb(0xD8DEE9, 0.07f);
constexpr Color hairlineStrong = rgb(0xD8DEE9, 0.12f);
// text
constexpr Color snow1 = rgb(0xECEFF4);
constexpr Color snow2 = rgb(0xA9B2C3);
constexpr Color snow3 = rgb(0x6B7487);
// accents
constexpr Color frost = rgb(0x88C0D0);
constexpr Color frostDeep = rgb(0x5E81AC);
constexpr Color income = rgb(0xA3BE8C);
constexpr Color expense = rgb(0xD9828A);
constexpr Color transfer = rgb(0x81A1C1);
constexpr Color warning = rgb(0xEBCB8B);
constexpr Color scrim = rgb(0x07090C, 0.62f);

// 16 curated Nordic tones for books, accounts and categories
constexpr uint32_t kPalette[16] = {
    0x88C0D0, 0x8FBCBB, 0x81A1C1, 0x5E81AC, 0xBF616A, 0xD08770, 0xEBCB8B, 0xA3BE8C,
    0xB48EAD, 0x6FA3A8, 0xC9A26B, 0x7E9CC9, 0xD59AA6, 0x9AA9BC, 0x7FB59B, 0xA88FCB,
};
inline Color palette(int i) { return rgb(kPalette[((i % 16) + 16) % 16]); }

// metrics (DIPs)
constexpr float titleBarH = 44;
constexpr float sidebarW = 236;
constexpr float sidebarCompactW = 68;
constexpr float radiusS = 6;
constexpr float radiusM = 12;
constexpr float radiusL = 16;
constexpr float fieldH = 40;
constexpr float buttonH = 38;
}  // namespace theme

}  // namespace fp::ui
