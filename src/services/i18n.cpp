#include "services/i18n.h"

#include <windows.h>

namespace fp {

namespace {

struct Entry { const wchar_t* en; const wchar_t* ru; };

#define FP_TABLE_ITEM(id, en, ru) {en, ru},
const Entry kTable[] = { FP_STRINGS(FP_TABLE_ITEM) };
#undef FP_TABLE_ITEM
static_assert(sizeof(kTable) / sizeof(kTable[0]) == (size_t)S::Count, "string table out of sync");

Lang g_lang = Lang::En;

const wchar_t* kMonthsEn[] = {L"January", L"February", L"March", L"April", L"May", L"June", L"July",
                              L"August", L"September", L"October", L"November", L"December"};
const wchar_t* kMonthsRuNom[] = {L"Январь", L"Февраль", L"Март", L"Апрель", L"Май", L"Июнь", L"Июль",
                                 L"Август", L"Сентябрь", L"Октябрь", L"Ноябрь", L"Декабрь"};
const wchar_t* kMonthsRuGen[] = {L"января", L"февраля", L"марта", L"апреля", L"мая", L"июня", L"июля",
                                 L"августа", L"сентября", L"октября", L"ноября", L"декабря"};
const wchar_t* kMonthsShortEn[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
const wchar_t* kMonthsShortRu[] = {L"янв", L"фев", L"мар", L"апр", L"май", L"июн", L"июл", L"авг", L"сен", L"окт", L"ноя", L"дек"};
const wchar_t* kWdEn[] = {L"Mo", L"Tu", L"We", L"Th", L"Fr", L"Sa", L"Su"};
const wchar_t* kWdRu[] = {L"Пн", L"Вт", L"Ср", L"Чт", L"Пт", L"Сб", L"Вс"};

}  // namespace

void setLang(Lang l) { g_lang = l; }
Lang lang() { return g_lang; }

Lang systemLang() {
    LANGID id = GetUserDefaultUILanguage();
    WORD primary = PRIMARYLANGID(id);
    if (primary == LANG_RUSSIAN || primary == LANG_UKRAINIAN || primary == LANG_BELARUSIAN) return Lang::Ru;
    return Lang::En;
}

const wchar_t* trIn(S s, Lang l) {
    const Entry& e = kTable[(int)s];
    return l == Lang::Ru ? e.ru : e.en;
}

const wchar_t* tr(S s) { return trIn(s, g_lang); }

std::wstring plural(long long n, S one, S few, S many) {
    long long a = n < 0 ? -n : n;
    if (g_lang == Lang::En) return tr(a == 1 ? one : many);
    long long m10 = a % 10, m100 = a % 100;
    if (m10 == 1 && m100 != 11) return tr(one);
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return tr(few);
    return tr(many);
}

std::wstring monthName(int m, bool standalone) {
    m = (m - 1) % 12;
    if (g_lang == Lang::En) return kMonthsEn[m];
    return standalone ? kMonthsRuNom[m] : kMonthsRuGen[m];
}

std::wstring monthShort(int m) { return (g_lang == Lang::En ? kMonthsShortEn : kMonthsShortRu)[(m - 1) % 12]; }
std::wstring weekdayShort(int wd) { return (g_lang == Lang::En ? kWdEn : kWdRu)[wd % 7]; }

std::wstring dateShort(const Date& d) {
    if (g_lang == Lang::En) return monthShort(d.m) + L" " + std::to_wstring(d.d);
    return std::to_wstring(d.d) + L" " + monthShort(d.m);
}

std::wstring dateLabel(const Date& d, bool relative) {
    Date t = Date::today();
    if (relative) {
        if (d == t) return tr(S::Today);
        if (d == t.addDays(-1)) return tr(S::Yesterday);
    }
    std::wstring s;
    if (g_lang == Lang::En) s = monthName(d.m, true) + L" " + std::to_wstring(d.d);
    else s = std::to_wstring(d.d) + L" " + monthName(d.m, false);
    if (d.y != t.y) s += (g_lang == Lang::En ? L", " : L" ") + std::to_wstring(d.y);
    return s;
}

std::wstring periodLabel(const Period& p) {
    Date t = Date::today();
    switch (p.kind) {
        case PeriodKind::Day: return dateLabel(p.start);
        case PeriodKind::Week: {
            Date last = p.end.addDays(-1);
            std::wstring a = dateShort(p.start), b = dateShort(last);
            std::wstring s = a + L" – " + b;
            if (last.y != t.y) s += L" " + std::to_wstring(last.y);
            return s;
        }
        case PeriodKind::Month: {
            std::wstring s = monthName(p.start.m, true);
            if (p.start.y != t.y) s += L" " + std::to_wstring(p.start.y);
            return s;
        }
        case PeriodKind::Quarter:
            return std::wstring(tr(S::QuarterShort)) + (g_lang == Lang::En ? L"" : L" ") +
                   std::to_wstring((p.start.m - 1) / 3 + 1) + L" " + std::to_wstring(p.start.y);
        case PeriodKind::Year: return std::to_wstring(p.start.y);
        case PeriodKind::All: return tr(S::PeriodAll);
    }
    return {};
}

std::wstring kindName(BookKind k) {
    switch (k) {
        case BookKind::Personal: return tr(S::KindPersonal);
        case BookKind::Wallet: return tr(S::KindWallet);
        case BookKind::Business: return tr(S::KindBusiness);
        case BookKind::Project: return tr(S::KindProject);
        case BookKind::Other: return tr(S::KindOther);
    }
    return {};
}

std::wstring accountTypeName(AccountType t) {
    switch (t) {
        case AccountType::Cash: return tr(S::AccCash);
        case AccountType::Card: return tr(S::AccCard);
        case AccountType::Bank: return tr(S::AccBank);
        case AccountType::Savings: return tr(S::AccSavings);
        case AccountType::Credit: return tr(S::AccCredit);
        case AccountType::Debt: return tr(S::AccDebt);
    }
    return {};
}

std::wstring errorText(Ec e) {
    switch (e) {
        case Ec::Ok: return {};
        case Ec::Io: return tr(S::ErrIo);
        case Ec::Corrupt: return tr(S::ErrCorrupt);
        case Ec::WrongPin: return tr(S::ErrWrongPin);
        case Ec::Throttled: return tr(S::ErrThrottled);
        case Ec::WrongPassword: return tr(S::ErrWrongPassword);
        case Ec::TooNew: return tr(S::ErrTooNew);
        case Ec::NotFound: return tr(S::ErrNotFound);
        case Ec::Invalid: return tr(S::ErrInvalid);
        case Ec::Crypto: return tr(S::ErrCrypto);
    }
    return {};
}

// Palette indices refer to ui/theme.h kPalette (16 Nordic tones).
std::vector<SeedCategory> defaultCategories(BookKind k) {
    auto E = CatType::Expense;
    auto I = CatType::Income;
    bool business = (k == BookKind::Business || k == BookKind::Project);
    if (business) {
        return {
            {E, tr(S::CatMaterials), Icon::Box, 10},     {E, tr(S::CatSubcontractors), Icon::Users, 2},
            {E, tr(S::CatSalaries), Icon::User, 3},      {E, tr(S::CatRent), Icon::Building, 9},
            {E, tr(S::CatTools), Icon::Wrench, 5},       {E, tr(S::CatFuel), Icon::Fuel, 11},
            {E, tr(S::CatTaxes), Icon::Percent, 4},      {E, tr(S::CatAds), Icon::Megaphone, 8},
            {E, tr(S::CatSoftware), Icon::Laptop, 0},    {E, tr(S::CatOffice), Icon::Briefcase, 13},
            {E, tr(S::CatTravel), Icon::Plane, 1},       {E, tr(S::CatBankFees), Icon::Bank, 6},
            {E, tr(S::CatOther), Icon::Dots, 13},
            {I, tr(S::CatSales), Icon::Cart, 7},         {I, tr(S::CatServices), Icon::Wrench, 1},
            {I, tr(S::CatProjects), Icon::Hammer, 14},   {I, tr(S::CatOther), Icon::Dots, 13},
        };
    }
    return {
        {E, tr(S::CatGroceries), Icon::Cart, 7},      {E, tr(S::CatCafes), Icon::Food, 5},
        {E, tr(S::CatTransport), Icon::Bus, 2},       {E, tr(S::CatCar), Icon::Car, 3},
        {E, tr(S::CatHousing), Icon::Home, 10},       {E, tr(S::CatUtilities), Icon::Bolt, 6},
        {E, tr(S::CatPhone), Icon::Wifi, 11},         {E, tr(S::CatHealth), Icon::Heart, 4},
        {E, tr(S::CatClothes), Icon::Shirt, 8},       {E, tr(S::CatFun), Icon::Film, 15},
        {E, tr(S::CatSport), Icon::Dumbbell, 14},     {E, tr(S::CatEducation), Icon::Book, 9},
        {E, tr(S::CatGifts), Icon::Gift, 12},         {E, tr(S::CatTravel), Icon::Plane, 0},
        {E, tr(S::CatPets), Icon::Paw, 10},           {E, tr(S::CatSubscriptions), Icon::Repeat, 13},
        {E, tr(S::CatOther), Icon::Dots, 13},
        {I, tr(S::CatSalary), Icon::Briefcase, 7},    {I, tr(S::CatSideIncome), Icon::Laptop, 1},
        {I, tr(S::CatGifts), Icon::Gift, 12},         {I, tr(S::CatInterest), Icon::Percent, 6},
        {I, tr(S::CatRefunds), Icon::Receipt, 2},     {I, tr(S::CatOther), Icon::Dots, 13},
    };
}

}  // namespace fp
