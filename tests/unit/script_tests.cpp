#include "test.h"

#include "loader/mod_fs.h"
#include "sim/script_test.h"

#include <filesystem>

// Runs every tests/script/*.txt golden test file (located relative to the executable: bin/../tests/script).
TEST_CASE(script_golden_tests) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(loader::executable_dir()).parent_path() / "tests" / "script";
    const auto files = loader::list_files(dir.string(), "", ".txt", false);
    CHECK(!files.empty());
    for (const auto& file : files) {
        for (const auto& r : sim::run_script_test_file(file)) {
            if (!r.pass) {
                std::string msg = fs::path(r.file).filename().string() + " :: " + r.name;
                for (const auto& m : r.messages) msg += "\n      " + m;
                ::ut::report_failure(__FILE__, __LINE__, msg);
            }
        }
    }
}
