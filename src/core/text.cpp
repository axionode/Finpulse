#include "core/text.h"

namespace fp {

// wchar_t is UTF-16 on Windows and UTF-32 elsewhere (tests on Linux); handle both.
std::string toUtf8(std::wstring_view w) {
    std::string out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        char32_t c = static_cast<char32_t>(w[i]);
        if (sizeof(wchar_t) == 2 && c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size()) {
            char32_t lo = static_cast<char32_t>(w[i + 1]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); ++i; }
        }
        if (c < 0x80) out += char(c);
        else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
        else { out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
    }
    return out;
}

std::wstring fromUtf8(std::string_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        char32_t c; int n;
        if (b < 0x80) { c = b; n = 1; }
        else if ((b >> 5) == 6) { c = b & 0x1F; n = 2; }
        else if ((b >> 4) == 14) { c = b & 0x0F; n = 3; }
        else if ((b >> 3) == 30) { c = b & 0x07; n = 4; }
        else { out += wchar_t(0xFFFD); ++i; continue; }
        if (i + n > s.size()) { out += wchar_t(0xFFFD); break; }
        for (int k = 1; k < n; ++k) c = (c << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += n;
        if (sizeof(wchar_t) == 2 && c >= 0x10000) {
            c -= 0x10000;
            out += wchar_t(0xD800 + (c >> 10));
            out += wchar_t(0xDC00 + (c & 0x3FF));
        } else {
            out += wchar_t(c);
        }
    }
    return out;
}

std::wstring trim(std::wstring_view s) {
    size_t a = 0, b = s.size();
    auto ws = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0xA0 || c == 0x202F; };
    while (a < b && ws(s[a])) ++a;
    while (b > a && ws(s[b - 1])) --b;
    return std::wstring(s.substr(a, b - a));
}

static wchar_t lowerCh(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return c + 32;
    if (c >= 0x0410 && c <= 0x042F) return c + 32;   // А-Я
    if (c >= 0x0400 && c <= 0x040F) return c + 80;   // Ѐ-Џ (incl. Ё)
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return c + 32;
    return c;
}

std::wstring lower(std::wstring_view s) {
    std::wstring r(s);
    for (auto& c : r) c = lowerCh(c);
    return r;
}

bool containsCI(std::wstring_view hay, std::wstring_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        size_t k = 0;
        while (k < needle.size() && lowerCh(hay[i + k]) == lowerCh(needle[k])) ++k;
        if (k == needle.size()) return true;
    }
    return false;
}

}  // namespace fp
