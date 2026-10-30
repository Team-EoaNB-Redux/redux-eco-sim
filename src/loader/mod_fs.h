#pragma once

// Locates the mod (`toi/`) and lists script files. The simulator reads the mod only: its replace_paths
// cover scripted effects/triggers, on_actions, ideas, technologies, dynamic modifiers, history and tags.

#include <string>
#include <vector>

namespace loader {

// Finds the mod root (the directory containing `common/scripted_effects`). Search order:
// explicit path (if non-empty) -> walk up from `start_dir` looking for `toi/` or a dir that is itself the
// mod root. Returns an empty string if nothing is found.
std::string find_mod_root(const std::string& explicit_path, const std::string& start_dir);

// All files with the given extension under `root/relative_dir` (recursive), sorted by relative path so
// load order is deterministic (HOI4 loads a folder's files in filename order).
std::vector<std::string> list_files(const std::string& root, const std::string& relative_dir,
                                    const std::string& extension = ".txt", bool recursive = true);

std::string join(const std::string& a, const std::string& b);
std::string relative_to(const std::string& path, const std::string& root);
std::string executable_dir();

} // namespace loader
