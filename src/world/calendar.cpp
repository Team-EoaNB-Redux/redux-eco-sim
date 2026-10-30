#include "world/calendar.h"

#include <cstdlib>

namespace world {

namespace {
constexpr int kMonthDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
constexpr int kMonthStart[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
} // namespace

int Date::days_in_month(int month) {
    return (month >= 1 && month <= 12) ? kMonthDays[month - 1] : 30;
}

int64_t Date::ordinal() const {
    return static_cast<int64_t>(year - 1) * 365 + kMonthStart[(month >= 1 && month <= 12) ? month - 1 : 0] + (day - 1);
}

Date Date::from_ordinal(int64_t ord) {
    Date d;
    int64_t y = ord / 365;
    int64_t rem = ord % 365;
    if (rem < 0) {
        rem += 365;
        --y;
    }
    d.year = static_cast<int32_t>(y + 1);
    int m = 11;
    while (m > 0 && kMonthStart[m] > rem) {
        --m;
    }
    d.month = m + 1;
    d.day = static_cast<int32_t>(rem - kMonthStart[m] + 1);
    return d;
}

std::string Date::to_string() const {
    return std::to_string(year) + "." + std::to_string(month) + "." + std::to_string(day);
}

bool Date::parse(std::string_view text, Date& out) {
    int parts[3] = {0, 1, 1};
    int idx = 0;
    bool any = false;
    int cur = 0;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            cur = cur * 10 + (c - '0');
            any = true;
        } else if (c == '.') {
            if (!any || idx >= 2) {
                return false;
            }
            parts[idx++] = cur;
            cur = 0;
            any = false;
        } else {
            return false;
        }
    }
    if (!any || idx != 2) {
        return false;
    }
    parts[2] = cur;
    if (parts[1] < 1 || parts[1] > 12 || parts[2] < 1 || parts[2] > days_in_month(parts[1])) {
        return false;
    }
    out.year = parts[0];
    out.month = parts[1];
    out.day = parts[2];
    return true;
}

} // namespace world
