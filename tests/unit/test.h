#pragma once

// Minimal self-registering test framework (no third-party dependency).
//   TEST_CASE(fixed_mul_truncates) { CHECK_EQ(a, b); CHECK(cond); }
// Run: bin/ecosim_tests.exe [substring-filter]

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace ut {

struct Case {
    const char* name;
    const char* file;
    void (*fn)();
};

std::vector<Case>& registry();
void report_failure(const char* file, int line, const std::string& message);

struct Registrar {
    Registrar(const char* name, const char* file, void (*fn)()) { registry().push_back({name, file, fn}); }
};

template <typename T>
std::string show(const T& v) {
    std::ostringstream os;
    os << v;
    return os.str();
}
inline std::string show(const std::string& v) { return "\"" + v + "\""; }
inline std::string show(const char* v) { return std::string("\"") + v + "\""; }

} // namespace ut

#define TEST_CASE(name)                                                    \
    static void name();                                                    \
    static ::ut::Registrar ut_reg_##name(#name, __FILE__, &name);          \
    static void name()

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) ::ut::report_failure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(a, b)                                                     \
    do {                                                                   \
        const auto& ut_a = (a);                                            \
        const auto& ut_b = (b);                                            \
        if (!(ut_a == ut_b))                                               \
            ::ut::report_failure(__FILE__, __LINE__,                       \
                "CHECK_EQ(" #a ", " #b "): " + ::ut::show(ut_a) + " != " + ::ut::show(ut_b)); \
    } while (0)
