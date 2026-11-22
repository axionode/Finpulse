#pragma once
// ADR-0006: money is int64 minor units, one currency per book, no floating point.
#include <string>
#include <string_view>
#include <vector>

#include "core/base.h"

namespace fp {

struct Currency {
    std::string code;        // ISO 4217
    std::wstring symbol;
    int decimals = 2;
    bool symbolBefore = false;
};

const std::vector<Currency>& currencies();               // most used first
const Currency& currencyByCode(std::string_view code);  // falls back to a generic 2-decimal currency

struct NumberFormat {
    wchar_t decimal = L',';
    wchar_t group = 0x202F;  // narrow no-break space
};

struct AmountParts {
    std::wstring sign;       // "-", "+" or empty
    std::wstring integer;    // grouped
    std::wstring fraction;   // with decimal separator, empty for 0-decimal currencies
    std::wstring symbol;
    bool symbolBefore = false;
};

AmountParts splitAmount(i64 minor, const Currency& c, const NumberFormat& nf, bool plusSign = false);
std::wstring formatAmount(i64 minor, const Currency& c, const NumberFormat& nf, bool withSymbol = true, bool plusSign = false);
std::wstring formatPlain(i64 minor, int decimals, wchar_t decimalSep);  // for editing: "1250,5" -> "1250,50"

// Calculator input: "1200/3+45", "12,50 * 2". Result is rounded to the currency precision.
Result<i64> evalAmount(std::wstring_view expr, int decimals);
bool looksLikeExpression(std::wstring_view expr);

i64 pow10i(int n);

}  // namespace fp
