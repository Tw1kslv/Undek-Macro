#include "util/AtomicFile.h"

#include <cstdio>
#include <fstream>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace undek {

namespace {

#ifdef _WIN32
std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 1) return {};
    std::wstring wide(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), len);
    wide.pop_back(); // the conversion wrote the terminator too
    return wide;
}
#endif

} // namespace

bool writeFileAtomically(const std::string& path, const std::string& data) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file) {
            file.close();
            std::remove(tmp.c_str());
            return false;
        }
    }

#ifdef _WIN32
    // MoveFileEx replaces the target in one step, so there is no window where
    // the config doesn't exist. Deleting first and renaming after used to lose
    // the file outright if the process died in between.
    const std::wstring wide_tmp = widen(tmp);
    const std::wstring wide_path = widen(path);
    if (!wide_tmp.empty() && !wide_path.empty() &&
        MoveFileExW(wide_tmp.c_str(), wide_path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
#else
    if (std::rename(tmp.c_str(), path.c_str()) == 0) return true;
#endif
    std::remove(tmp.c_str());
    return false;
}

} // namespace undek
