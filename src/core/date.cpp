#include "core/date.h"

#include <ctime>

namespace fp {

// Howard Hinnant's civil date algorithms.
int Date::days() const {
    int yy = y - (m <= 2);
    int era = (yy >= 0 ? yy : yy - 399) / 400;
    unsigned yoe = unsigned(yy - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int(doe) - 719468;
}

Date Date::fromDays(int z) {
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = unsigned(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = int(yoe) + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    int dd = int(doy - (153 * mp + 2) / 5 + 1);
    int mm = int(mp < 10 ? mp + 3 : mp - 9);
    return {yy + (mm <= 2), mm, dd};
}

int Date::weekday() const {
    int z = days();  // 1970-01-01 was a Thursday (3 when Monday = 0)
    return ((z % 7) + 7 + 3) % 7;
}

int Date::daysInMonth(int y, int m) {
    static const int dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
    return dm[(m - 1) % 12];
}

Date Date::addMonths(int n) const {
    int total = y * 12 + (m - 1) + n;
    Date r{total / 12, total % 12 + 1, d};
    int dim = daysInMonth(r.y, r.m);
    if (r.d > dim) r.d = dim;
    return r;
}

bool Date::valid() const {
    return y >= 1900 && y <= 2999 && m >= 1 && m <= 12 && d >= 1 && d <= daysInMonth(y, m);
}

Date Date::today() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
}

int64_t unixNow() { return static_cast<int64_t>(std::time(nullptr)); }

Period makePeriod(PeriodKind kind, Date a, int fdow) {
    Period p;
    p.kind = kind;
    switch (kind) {
        case PeriodKind::Day: p.start = a; p.end = a.addDays(1); break;
        case PeriodKind::Week: {
            int back = (a.weekday() - fdow + 7) % 7;
            p.start = a.addDays(-back);
            p.end = p.start.addDays(7);
            break;
        }
        case PeriodKind::Month: p.start = {a.y, a.m, 1}; p.end = p.start.addMonths(1); break;
        case PeriodKind::Quarter: p.start = {a.y, ((a.m - 1) / 3) * 3 + 1, 1}; p.end = p.start.addMonths(3); break;
        case PeriodKind::Year: p.start = {a.y, 1, 1}; p.end = {a.y + 1, 1, 1}; break;
        case PeriodKind::All: p.start = {1900, 1, 1}; p.end = {3000, 1, 1}; break;
    }
    return p;
}

Period shiftPeriod(const Period& p, int n, int fdow) {
    switch (p.kind) {
        case PeriodKind::Day: return makePeriod(p.kind, p.start.addDays(n), fdow);
        case PeriodKind::Week: return makePeriod(p.kind, p.start.addDays(7 * n), fdow);
        case PeriodKind::Month: return makePeriod(p.kind, p.start.addMonths(n), fdow);
        case PeriodKind::Quarter: return makePeriod(p.kind, p.start.addMonths(3 * n), fdow);
        case PeriodKind::Year: return makePeriod(p.kind, p.start.addMonths(12 * n), fdow);
        case PeriodKind::All: return p;
    }
    return p;
}

}  // namespace fp
