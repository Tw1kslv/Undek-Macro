#pragma once

#include <string>

namespace undek::script {

std::string readFile(const std::string& path);
bool writeFile(const std::string& path, const std::string& content);

} // namespace undek::script
