#pragma once

#include <string>
#include <vector>

namespace undek::script {

struct ScriptProblem {
    int line = 0;
    std::string message;
};

// Reads a script the way a run would, without running it: parse errors, calls to
// functions nothing has registered or whose arguments don't fit what they
// declared, and pictures that aren't there or aren't readable. It can't judge
// keys or coordinates without a screen, so it doesn't.
//
// `base_dir` is where a relative picture path is looked for, normally the folder
// the script lives in. Running passes the same folder to resolvePicturePath(), so
// both look at the same file.
std::vector<ScriptProblem> checkScript(const std::string& source, const std::string& base_dir);

} // namespace undek::script
