#include "script/BuiltinFunctions.h"

#include <fstream>
#include <sstream>

namespace undek::script {

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return {};
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << content;
    return file.good();
}

} // namespace undek::script
