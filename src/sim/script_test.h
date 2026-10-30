#pragma once

// Golden tests written in HOI4 script (tests/script/*.txt). Each top-level `test_<name> = { ... }` runs as a
// top-level execution in country TST of a tiny fixed world:
//   countries TST (index 0, capital 1), OTH (index 1, capital 3)
//   states 1, 2 owned+controlled by TST (cores TST); state 3 owned by OTH
// Other top-level blocks are scripted effects; blocks inside `scripted_triggers = { }` are scripted triggers.
// Expectations are comments inside the test block:
//   # expect: <exact log line>          (all `log` output must match these lines, in order)
//   # expect-diag: <diag_kind>          (at least one diagnostic of that kind must be raised)

#include <string>
#include <vector>

namespace sim {

struct ScriptTestResult {
    std::string file;
    std::string name;
    bool pass = true;
    std::vector<std::string> messages;
};

std::vector<ScriptTestResult> run_script_test_file(const std::string& path);

} // namespace sim
