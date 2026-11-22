#pragma once
#include <string>
#include <string_view>

namespace fp {

std::string toUtf8(std::wstring_view w);
std::wstring fromUtf8(std::string_view s);

std::wstring trim(std::wstring_view s);
std::wstring lower(std::wstring_view s);          // simple Latin + Cyrillic lowercase for search
bool containsCI(std::wstring_view hay, std::wstring_view needle);

}  // namespace fp
