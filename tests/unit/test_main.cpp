#include "test.h"

#include <cstring>

namespace ut {

namespace {
int g_failures = 0;
const char* g_current = "";
} // namespace

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

void report_failure(const char* file, int line, const std::string& message) {
    ++g_failures;
    std::cout << "  FAIL [" << g_current << "] " << file << ":" << line << ": " << message << "\n";
}

} // namespace ut

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    int failed_cases = 0;
    for (const auto& c : ut::registry()) {
        if (filter && !std::strstr(c.name, filter)) {
            continue;
        }
        ut::g_current = c.name;
        const int before = ut::g_failures;
        c.fn();
        ++run;
        if (ut::g_failures != before) {
            ++failed_cases;
        }
    }
    std::cout << (failed_cases == 0 ? "OK" : "FAILED") << ": " << run << " test cases, " << failed_cases
              << " failed, " << ut::g_failures << " failed checks\n";
    return failed_cases == 0 ? 0 : 1;
}
