#pragma once

// HOI4 calendar: 365-day years, no leap days, fixed month lengths. Dates print as Y.M.D.

#include <cstdint>
#include <string>
#include <string_view>

namespace world {

struct Date {
    int32_t year = 1857;
    int32_t month = 1; // 1..12
    int32_t day = 1;   // 1..days_in_month

    static int days_in_month(int month);
    // Days since 1.1.1 (year 1 day 1 = 0). Monotonic, so comparisons and differences are easy.
    int64_t ordinal() const;
    static Date from_ordinal(int64_t ord);
    Date plus_days(int64_t days) const { return from_ordinal(ordinal() + days); }
    std::string to_string() const; // "1857.1.1"
    static bool parse(std::string_view text, Date& out); // "1857.1.1" or "1857.01.01"

    friend bool operator==(const Date& a, const Date& b) { return a.ordinal() == b.ordinal(); }
    friend bool operator<(const Date& a, const Date& b) { return a.ordinal() < b.ordinal(); }
    friend bool operator<=(const Date& a, const Date& b) { return a.ordinal() <= b.ordinal(); }
    friend bool operator>(const Date& a, const Date& b) { return a.ordinal() > b.ordinal(); }
};

} // namespace world
