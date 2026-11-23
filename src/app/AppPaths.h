#pragma once

#include <string>

namespace undek {

class AppPaths {
public:
    static bool initialize();
    static const std::string& root();
    static std::string resolve(const std::string& relative_path);
    static std::string relativeToRoot(const std::string& absolute_path);
    static std::string macrosDir();
    static std::string examplesDir();
    static std::string bindingsFile();
    static std::string profilesDir();
};

} // namespace undek
