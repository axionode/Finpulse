#pragma once
// Calendar dates without time zones (ADR-0004: a financial date is a calendar day).
#include <cstdint>

namespace fp {

struct Date {
    int y = 1970, m = 1, d = 1;

    int key() const { return y * 10000 + m * 100 + d; }            // 20261122, sortable, stored in SQLite
    static Date fromKey(int k) { return {k / 10000, (k / 100) % 100, k % 100}; }
    int days() const;                                               // days since 1970-01-01
    static Date fromDays(int z);
    int weekday() const;                                            // 0 = Monday ... 6 = Sunday
    Date addDays(int n) const { return fromDays(days() + n); }
    Date addMonths(int n) const;                                    // clamps the day
    bool operator==(const Date& o) const { return key() == o.key(); }
    bool operator<(const Date& o) const { return key() < o.key(); }
    bool operator<=(const Date& o) const { return key() <= o.key(); }
    bool valid() const;

    static Date today();
    static int daysInMonth(int y, int m);
};

enum class PeriodKind : int { Day = 0, Week, Month, Quarter, Year, All };

// Half-open range [start, end).
struct Period {
    PeriodKind kind = PeriodKind::Month;
    Date start, end;
    bool contains(const Date& d) const { return start <= d && d < end; }
    int lengthDays() const { return end.days() - start.days(); }
};

Period makePeriod(PeriodKind kind, Date anchor, int firstDayOfWeek /*0=Mon, 6=Sun*/);
Period shiftPeriod(const Period& p, int n, int firstDayOfWeek);

int64_t unixNow();  // seconds

}  // namespace fp
