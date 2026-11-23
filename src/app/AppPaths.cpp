#include "app/AppPaths.h"

#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace undek {
namespace {

std::string g_root;

#ifdef _WIN32
// Only used by the Windows branch below, so it would be an unused-function
// warning everywhere else if it weren't behind the same guard.
bool hasProjectMarkers(const fs::path& dir) {
    return fs::exists(dir / "macros") || fs::exists(dir / "examples") ||
           fs::exists(dir / "bindings");
}
#endif

} // namespace

bool AppPaths::initialize() {
#ifdef _WIN32
    char buffer[MAX_PATH]{};
    const DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    fs::path exe_dir = (len > 0) ? fs::path(buffer).parent_path() : fs::current_path();

    const fs::path candidates[] = {
        fs::current_path(),
        exe_dir,
        exe_dir.parent_path().parent_path(),
        exe_dir.parent_path(),
    };

    for (const auto& c : candidates) {
        if (c.empty()) continue;
        std::error_code ec;
        const fs::path canon = fs::weakly_canonical(c, ec);
        const fs::path check = ec ? c : canon;
        if (hasProjectMarkers(check)) {
            g_root = check.string();
            fs::current_path(check);
            fs::create_directories(check / "macros");
            fs::create_directories(check / "bindings");
            return true;
        }
    }

    g_root = exe_dir.string();
    fs::current_path(exe_dir);
    fs::create_directories(exe_dir / "macros");
    fs::create_directories(exe_dir / "bindings");
    return true;
#else
    g_root = fs::current_path().string();
    return true;
#endif
}

const std::string& AppPaths::root() {
    return g_root;
}

std::string AppPaths::resolve(const std::string& relative_path) {
    if (relative_path.empty()) return g_root;
    fs::path p(relative_path);
    if (p.is_absolute()) return relative_path;
    return (fs::path(g_root) / p).lexically_normal().string();
}

std::string AppPaths::relativeToRoot(const std::string& absolute_path) {
    std::error_code ec;
    const fs::path rel = fs::relative(absolute_path, g_root, ec);
    if (!ec) {
        std::string s = rel.string();
        for (char& c : s)
            if (c == '\\') c = '/';
        return s;
    }
    return absolute_path;
}

std::string AppPaths::macrosDir() {
    return resolve("macros");
}
std::string AppPaths::examplesDir() {
    return resolve("examples");
}
std::string AppPaths::bindingsFile() {
    return resolve("bindings/default.json");
}
std::string AppPaths::profilesDir() {
    return resolve("profiles");
}

} // namespace undek
