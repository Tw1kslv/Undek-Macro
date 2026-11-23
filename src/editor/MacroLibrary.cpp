#include "editor/MacroLibrary.h"

#include "app/AppPaths.h"
#include "script/BuiltinFunctions.h"

#include <algorithm>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;

namespace undek::editor {
namespace {

bool fileExists(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

} // namespace

void MacroLibrary::scanDirectory(const std::string& dir) {
    // error_code overloads throughout: a folder that vanished or is locked used
    // to throw straight out of a menu click.
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) return;

    fs::directory_iterator it(dir, ec);
    if (ec) return;
    const fs::directory_iterator end;
    while (it != end) {
        const fs::directory_entry entry = *it;
        it.increment(ec);
        if (ec) break;

        std::error_code file_ec;
        if (!entry.is_regular_file(file_ec) || file_ec) continue;
        if (entry.path().extension() != ".undek") continue;

        MacroEntry me;
        me.name = entry.path().stem().string();
        me.path = AppPaths::relativeToRoot(entry.path().string());
        entries_.push_back(me);
    }
}

void MacroLibrary::refresh() {
    entries_.clear();
    try {
        scanDirectory(AppPaths::macrosDir());
        scanDirectory(AppPaths::examplesDir());
        std::sort(entries_.begin(), entries_.end(),
                  [](const MacroEntry& a, const MacroEntry& b) { return a.name < b.name; });
    } catch (const std::exception&) {
        // Keep whatever was scanned rather than taking the app down over a
        // folder that moved mid-listing.
    }
    ++generation_;
}

std::string MacroLibrary::defaultTemplate() {
    return R"(// New macro script
profile default_sigma=12

// KeyDown("E")
// Wait(100ms, humanize=true)
// KeyUp("E")
)";
}

std::string MacroLibrary::safeName(const std::string& name) {
    std::string safe = name;
    // Trim before mapping, so a name made of spaces stays empty instead of
    // turning into underscores.
    while (!safe.empty() && (safe.front() == ' ' || safe.front() == '\t'))
        safe.erase(safe.begin());
    while (!safe.empty() && (safe.back() == ' ' || safe.back() == '\t' || safe.back() == '.'))
        safe.pop_back();

    for (char& c : safe) {
        switch (c) {
            case ' ':
            case '/':
            case '\\':
            case ':':
            case '*':
            case '?':
            case '"':
            case '<':
            case '>':
            case '|':
                c = '_';
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) c = '_';
                break;
        }
    }
    return safe;
}

std::string MacroLibrary::createNew(const std::string& name) {
    std::string safe = safeName(name);
    if (safe.empty()) safe = "new_macro";

    std::string path =
        AppPaths::relativeToRoot((fs::path(AppPaths::macrosDir()) / (safe + ".undek")).string());
    int suffix = 1;
    while (fileExists(AppPaths::resolve(path))) {
        path = AppPaths::relativeToRoot(
            (fs::path(AppPaths::macrosDir()) / (safe + "_" + std::to_string(suffix) + ".undek"))
                .string());
        ++suffix;
    }

    save(path, defaultTemplate());
    refresh();
    return path;
}

bool MacroLibrary::save(const std::string& path, const std::string& content) {
    const std::string abs = AppPaths::resolve(path);
    fs::path p(abs);
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    return script::writeFile(abs, content);
}

bool MacroLibrary::deleteMacro(const std::string& path) {
    const std::string abs = AppPaths::resolve(path);
    if (!fileExists(abs)) return false;
    std::error_code ec;
    fs::remove(abs, ec);
    refresh();
    return !ec;
}

std::string MacroLibrary::duplicate(const std::string& path, const std::string& name) {
    const std::string abs = AppPaths::resolve(path);
    if (!fileExists(abs)) return "";

    std::string safe = safeName(name);
    if (safe.empty()) safe = "copy";

    std::string newPath =
        AppPaths::relativeToRoot((fs::path(AppPaths::macrosDir()) / (safe + ".undek")).string());
    int suffix = 1;
    while (fileExists(AppPaths::resolve(newPath))) {
        newPath = AppPaths::relativeToRoot(
            (fs::path(AppPaths::macrosDir()) / (safe + "_" + std::to_string(suffix) + ".undek"))
                .string());
        ++suffix;
    }

    save(newPath, script::readFile(abs));
    refresh();
    return newPath;
}

std::string MacroLibrary::rename(const std::string& oldPath, const std::string& newName) {
    const std::string oldAbs = AppPaths::resolve(oldPath);
    if (!fileExists(oldAbs)) return {};

    const std::string safe = safeName(newName);
    if (safe.empty()) return {};

    fs::path parent = fs::path(oldAbs).parent_path();
    std::string newAbs = (parent / (safe + ".undek")).string();
    if (newAbs == oldAbs) return oldPath;
    if (fileExists(newAbs)) return {};

    std::error_code ec;
    fs::rename(oldAbs, newAbs, ec);
    if (ec) return {};
    refresh();
    return AppPaths::relativeToRoot(newAbs);
}

} // namespace undek::editor
