#pragma once

#include <string>

namespace undek {

class FileLog {
public:
    static void init(const std::string& dir);
    static void setEnabled(bool enabled);
    static void write(const std::string& line);
};

} // namespace undek
