#pragma once

// Tiny argument parser: `--key value`, `--flag`, `--key=value`, plus positionals. Repeated keys accumulate.

#include <map>
#include <string>
#include <vector>

namespace cli {

struct Args {
    std::vector<std::string> positionals;
    std::map<std::string, std::vector<std::string>> options;

    bool has(const std::string& key) const { return options.count(key) != 0; }
    std::string get(const std::string& key, const std::string& fallback = {}) const {
        auto it = options.find(key);
        return it == options.end() || it->second.empty() ? fallback : it->second.back();
    }
    std::vector<std::string> all(const std::string& key) const {
        auto it = options.find(key);
        return it == options.end() ? std::vector<std::string>{} : it->second;
    }
};

// `flags` lists option names that never take a value (e.g. "parse-only").
inline Args parse_args(int argc, char** argv, int first, const std::vector<std::string>& flags) {
    Args a;
    for (int i = first; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0) {
            std::string key = s.substr(2);
            std::string value;
            const size_t eq = key.find('=');
            if (eq != std::string::npos) {
                value = key.substr(eq + 1);
                key = key.substr(0, eq);
                a.options[key].push_back(value);
                continue;
            }
            bool is_flag = false;
            for (const auto& f : flags) {
                is_flag |= f == key;
            }
            if (!is_flag && i + 1 < argc) {
                a.options[key].push_back(argv[++i]);
            } else {
                a.options[key].push_back("");
            }
        } else {
            a.positionals.push_back(s);
        }
    }
    return a;
}

} // namespace cli
