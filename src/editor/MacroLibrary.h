#pragma once

#include <string>
#include <vector>

namespace undek::editor {

struct MacroEntry {
    std::string name;
    std::string path;
};

class MacroLibrary {
public:
    void refresh();
    const std::vector<MacroEntry>& entries() const { return entries_; }
    // Bumped by refresh(), so views can tell whether a cached list is stale.
    unsigned generation() const { return generation_; }

    std::string createNew(const std::string& name);
    bool save(const std::string& path, const std::string& content);
    bool deleteMacro(const std::string& path);
    std::string duplicate(const std::string& path, const std::string& name);
    std::string rename(const std::string& oldPath, const std::string& newName);
    static std::string defaultTemplate();

    // Turns a name someone typed into a file name that Windows will take.
    // Separators, characters the filesystem refuses, and trailing dots all get
    // folded away; UTF-8 is left alone. Empty result means nothing usable.
    static std::string safeName(const std::string& name);

private:
    void scanDirectory(const std::string& dir);

    std::vector<MacroEntry> entries_;
    unsigned generation_ = 0;
};

} // namespace undek::editor
