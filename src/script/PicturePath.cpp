#include "script/PicturePath.h"

#include "app/AppPaths.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace undek::script {

std::string resolvePicturePath(const std::string& path, const std::string& script_dir) {
    if (path.empty()) return {};

    std::error_code ec;
    if (fs::exists(path, ec)) return path;

    const fs::path given(path);
    if (!script_dir.empty() && !given.is_absolute()) {
        const fs::path beside = fs::path(script_dir) / given;
        if (fs::exists(beside, ec)) return beside.string();
    }

    const std::string under_root = AppPaths::resolve(path);
    if (under_root != path && fs::exists(under_root, ec)) return under_root;

    return {};
}

} // namespace undek::script
