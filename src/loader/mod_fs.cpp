#include "loader/mod_fs.h"

#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace loader {

namespace {

bool is_mod_root(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p / "common" / "scripted_effects", ec);
}

} // namespace

std::string find_mod_root(const std::string& explicit_path, const std::string& start_dir) {
    std::error_code ec;
    if (!explicit_path.empty()) {
        fs::path p = fs::absolute(explicit_path, ec);
        if (is_mod_root(p)) {
            return p.lexically_normal().string();
        }
        if (is_mod_root(p / "toi")) {
            return (p / "toi").lexically_normal().string();
        }
        return {};
    }
    fs::path cur = fs::absolute(start_dir.empty() ? fs::current_path(ec) : fs::path(start_dir), ec);
    for (int i = 0; i < 8 && !cur.empty(); ++i) {
        if (is_mod_root(cur)) {
            return cur.lexically_normal().string();
        }
        if (is_mod_root(cur / "toi")) {
            return (cur / "toi").lexically_normal().string();
        }
        const fs::path parent = cur.parent_path();
        if (parent == cur) {
            break;
        }
        cur = parent;
    }
    return {};
}

std::vector<std::string> list_files(const std::string& root, const std::string& relative_dir,
                                    const std::string& extension, bool recursive) {
    std::vector<std::string> out;
    const fs::path dir = fs::path(root) / relative_dir;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return out;
    }
    auto consider = [&](const fs::directory_entry& e) {
        if (!e.is_regular_file(ec)) {
            return;
        }
        if (!extension.empty() && e.path().extension().string() != extension) {
            return;
        }
        out.push_back(e.path().string());
    };
    if (recursive) {
        for (const auto& e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
            consider(e);
        }
    } else {
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            consider(e);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string join(const std::string& a, const std::string& b) {
    return (fs::path(a) / b).string();
}

std::string relative_to(const std::string& path, const std::string& root) {
    std::error_code ec;
    const fs::path rel = fs::relative(path, root, ec);
    return ec ? path : rel.generic_string();
}

std::string executable_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) {
        return {};
    }
    return fs::path(buf).parent_path().string();
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec).parent_path().string();
#endif
}

} // namespace loader
