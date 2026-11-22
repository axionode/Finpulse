#include "core/money.h"

#include <cmath>
#include <cstdlib>
#include <numeric>

namespace fp {

const std::vector<Currency>& currencies() {
    static const std::vector<Currency> list = {
        {"CZK", L"Kč", 2, false},  {"EUR", L"€", 2, false},   {"USD", L"$", 2, true},    {"UAH", L"₴", 2, false},
        {"GBP", L"£", 2, true},    {"PLN", L"zł", 2, false},  {"CHF", L"CHF", 2, false}, {"HUF", L"Ft", 0, false},
        {"SEK", L"kr", 2, false},  {"NOK", L"kr", 2, false},  {"DKK", L"kr", 2, false},  {"CAD", L"C$", 2, true},
        {"AUD", L"A$", 2, true},   {"JPY", L"¥", 0, true},    {"CNY", L"¥", 2, true},    {"TRY", L"₺", 2, false},
        {"AED", L"AED", 2, false}, {"RON", L"lei", 2, false}, {"BGN", L"лв", 2, false},  {"GEL", L"₾", 2, false},
        {"KZT", L"₸", 2, false},   {"ILS", L"₪", 2, true},    {"INR", L"₹", 2, true},    {"MDL", L"L", 2, false},
    };
    return list;
}

const Currency& currencyByCode(std::string_view code) {
    for (auto& c : currencies())
        if (c.code == code) return c;
    static Currency generic{"XXX", L"", 2, false};
    return generic;
}

i64 pow10i(int n) {
    i64 r = 1;
    while (n-- > 0) r *= 10;
    return r;
}

AmountParts splitAmount(i64 minor, const Currency& c, const NumberFormat& nf, bool plusSign) {
    AmountParts p;
    p.symbol = c.symbol;
    p.symbolBefore = c.symbolBefore;
    bool neg = minor < 0;
    unsigned long long v = neg ? 0ULL - static_cast<unsigned long long>(minor) : static_cast<unsigned long long>(minor);
    unsigned long long scale = static_cast<unsigned long long>(pow10i(c.decimals));
    unsigned long long ip = v / scale, fp_ = v % scale;
    if (neg) p.sign = L"-";
    else if (plusSign && v) p.sign = L"+";
    std::wstring digits = std::to_wstring(ip);
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0 && digits.size() > 4) p.integer += nf.group;
        p.integer += digits[i];
    }
    if (c.decimals > 0) {
        std::wstring f = std::to_wstring(fp_);
        while ((int)f.size() < c.decimals) f = L"0" + f;
        p.fraction = std::wstring(1, nf.decimal) + f;
    }
    return p;
}

std::wstring formatAmount(i64 minor, const Currency& c, const NumberFormat& nf, bool withSymbol, bool plusSign) {
    AmountParts p = splitAmount(minor, c, nf, plusSign);
    std::wstring num = p.sign + p.integer + p.fraction;
    if (!withSymbol || p.symbol.empty()) return num;
    return p.symbolBefore ? p.sign + p.symbol + p.integer + p.fraction : num + L" " + p.symbol;
}

std::wstring formatPlain(i64 minor, int decimals, wchar_t dec) {
    bool neg = minor < 0;
    unsigned long long v = neg ? 0ULL - static_cast<unsigned long long>(minor) : static_cast<unsigned long long>(minor);
    unsigned long long scale = static_cast<unsigned long long>(pow10i(decimals));
    std::wstring s = (neg ? L"-" : L"") + std::to_wstring(v / scale);
    unsigned long long f = v % scale;
    if (decimals > 0 && f) {
        std::wstring fs = std::to_wstring(f);
        while ((int)fs.size() < decimals) fs = L"0" + fs;
        s += dec;
        s += fs;
    }
    return s;
}

// ---------------------------------------------------------------- exact rational calculator
namespace {

struct Rat {
    i64 n = 0, d = 1;
};

bool safeMul(i64 a, i64 b, i64& r) {
    long double x = static_cast<long double>(a) * static_cast<long double>(b);
    if (std::fabs(static_cast<double>(x)) > 9.0e18) return false;
    r = a * b;
    return true;
}

bool norm(Rat& r) {
    if (r.d == 0) return false;
    if (r.d < 0) { r.d = -r.d; r.n = -r.n; }
    i64 g = std::gcd(r.n < 0 ? -r.n : r.n, r.d);
    if (g > 1) { r.n /= g; r.d /= g; }
    return true;
}

bool add(Rat a, Rat b, Rat& out) {
    i64 x, y, z;
    if (!safeMul(a.n, b.d, x) || !safeMul(b.n, a.d, y) || !safeMul(a.d, b.d, z)) return false;
    long double s = static_cast<long double>(x) + y;
    if (std::fabs(static_cast<double>(s)) > 9.0e18) return false;
    out = {x + y, z};
    return norm(out);
}

bool mul(Rat a, Rat b, Rat& out) {
    // cross-reduce first to keep numbers small
    i64 g1 = std::gcd(a.n < 0 ? -a.n : a.n, b.d), g2 = std::gcd(b.n < 0 ? -b.n : b.n, a.d);
    if (g1 > 1) { a.n /= g1; b.d /= g1; }
    if (g2 > 1) { b.n /= g2; a.d /= g2; }
    if (!safeMul(a.n, b.n, out.n) || !safeMul(a.d, b.d, out.d)) return false;
    return norm(out);
}

struct Parser {
    std::wstring s;
    size_t i = 0;
    bool err = false;

    wchar_t peek() const { return i < s.size() ? s[i] : 0; }

    bool number(Rat& r) {
        size_t st = i;
        while (i < s.size() && ((s[i] >= L'0' && s[i] <= L'9') || s[i] == L'.' || s[i] == L',')) ++i;
        std::wstring tok = s.substr(st, i - st);
        if (tok.empty()) return false;
        size_t lastDot = tok.rfind(L'.'), lastComma = tok.rfind(L',');
        size_t nDot = 0, nComma = 0;
        for (wchar_t c : tok) { nDot += c == L'.'; nComma += c == L','; }
        wchar_t decSep = 0;
        if (nDot && nComma) decSep = (lastDot > lastComma) ? L'.' : L',';
        else if (nDot == 1) decSep = L'.';
        else if (nComma == 1) decSep = L',';
        i64 n = 0, d = 1;
        bool frac = false;
        int digits = 0;
        for (wchar_t c : tok) {
            if (c == decSep) { if (frac) return false; frac = true; continue; }
            if (c == L'.' || c == L',') continue;  // group separator
            if (++digits > 17) return false;
            n = n * 10 + (c - L'0');
            if (frac) d *= 10;
        }
        r = {n, d};
        return norm(r);
    }

    bool factor(Rat& r) {
        wchar_t c = peek();
        if (c == L'-' || c == L'+') {
            ++i;
            if (!factor(r)) return false;
            if (c == L'-') r.n = -r.n;
            return true;
        }
        if (c == L'(') {
            ++i;
            if (!expr(r)) return false;
            if (peek() != L')') return false;
            ++i;
            return true;
        }
        return number(r);
    }

    bool term(Rat& r) {
        if (!factor(r)) return false;
        for (;;) {
            wchar_t c = peek();
            bool isMul = c == L'*' || c == 0xD7 || c == L'x' || c == L'X' || c == 0x445 /* кириллица х */;
            bool isDiv = c == L'/' || c == 0xF7 || c == L':';
            if (!isMul && !isDiv) return true;
            ++i;
            Rat b;
            if (!factor(b)) return false;
            if (isDiv) {
                if (b.n == 0) return false;
                b = {b.d, b.n};
                if (!norm(b)) return false;
            }
            if (!mul(r, b, r)) return false;
        }
    }

    bool expr(Rat& r) {
        if (!term(r)) return false;
        for (;;) {
            wchar_t c = peek();
            if (c != L'+' && c != L'-') return true;
            ++i;
            Rat b;
            if (!term(b)) return false;
            if (c == L'-') b.n = -b.n;
            if (!add(r, b, r)) return false;
        }
    }
};

}  // namespace

bool looksLikeExpression(std::wstring_view e) {
    for (size_t k = 1; k < e.size(); ++k) {
        wchar_t c = e[k];
        if (c == L'+' || c == L'-' || c == L'*' || c == L'/' || c == L'(' || c == 0xD7 || c == 0xF7) return true;
    }
    return false;
}

Result<i64> evalAmount(std::wstring_view expr, int decimals) {
    Parser p;
    for (wchar_t c : expr)
        if (c != L' ' && c != 0xA0 && c != 0x202F && c != L'\t') p.s += c;
    if (p.s.empty()) return Result<i64>::fail(Ec::Invalid, "empty");
    Rat r;
    if (!p.expr(r) || p.i != p.s.size()) return Result<i64>::fail(Ec::Invalid, "syntax");
    // value * 10^decimals, rounded half away from zero
    Rat scaled;
    if (!mul(r, {pow10i(decimals), 1}, scaled)) return Result<i64>::fail(Ec::Invalid, "overflow");
    i64 q = scaled.n / scaled.d, rem = scaled.n % scaled.d;
    if (rem != 0) {
        i64 twice = (rem < 0 ? -rem : rem) * 2;
        if (twice >= scaled.d) q += (scaled.n < 0 ? -1 : 1);
    }
    return Result<i64>::of(q);
}

}  // namespace fp
