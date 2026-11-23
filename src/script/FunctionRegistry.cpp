#include "script/FunctionRegistry.h"

#include <algorithm>

namespace undek::script {

namespace {

bool validFunctionName(const std::string& name, std::string& error) {
    if (name.empty()) {
        error = "a script function needs a name";
        return false;
    }
    for (char c : name) {
        const bool ok =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            error = "function name '" + name + "' can only use letters, digits and _";
            return false;
        }
    }
    if (name[0] >= '0' && name[0] <= '9') {
        error = "function name '" + name + "' can't start with a digit";
        return false;
    }
    return true;
}

bool validType(const std::string& type) {
    return type == "number" || type == "text";
}

// A declaration the host can actually honour. Anything else is refused now
// rather than ignored when a call arrives.
bool validMetadata(const FunctionInfo& info, std::string& error) {
    bool seen_optional = false;
    for (size_t i = 0; i < info.arguments.size(); ++i) {
        const ArgumentInfo& arg = info.arguments[i];
        if (!arg.type.empty() && !validType(arg.type)) {
            error = info.name + " argument " + std::to_string(i + 1) +
                    " declares an unknown type '" + arg.type + "'";
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (!arg.name.empty() && info.arguments[j].name == arg.name) {
                error = info.name + " repeats the argument name '" + arg.name + "'";
                return false;
            }
        }
        if (arg.optional) {
            seen_optional = true;
        } else if (seen_optional) {
            error = info.name + " has a required argument after an optional one";
            return false;
        }
    }
    if (!info.returns.empty() && !validType(info.returns)) {
        error = info.name + " declares an unknown return type '" + info.returns + "'";
        return false;
    }
    return true;
}

} // namespace

FunctionRegistry& FunctionRegistry::global() {
    static FunctionRegistry registry;
    return registry;
}

bool FunctionRegistry::checkSignature(const FunctionInfo& info,
                                      const std::vector<ScriptValue>& args, std::string& error) {
    if (info.arguments.empty()) return true;

    size_t required = 0;
    for (const auto& arg : info.arguments) {
        if (!arg.optional) ++required;
    }
    if (args.size() < required || args.size() > info.arguments.size()) {
        error = info.name + " takes ";
        if (required == info.arguments.size()) {
            error += std::to_string(required) + (required == 1 ? " argument" : " arguments");
        } else {
            error += std::to_string(required) + " to " + std::to_string(info.arguments.size()) +
                     " arguments";
        }
        error += ", got " + std::to_string(args.size());
        return false;
    }

    for (size_t i = 0; i < args.size(); ++i) {
        const ArgumentInfo& declared = info.arguments[i];
        if (declared.type.empty()) continue;
        const bool wants_number = declared.type == "number";
        const bool wants_text = declared.type == "text";
        if (!wants_number && !wants_text) {
            error = info.name + " argument " + std::to_string(i + 1) + " has an unknown type";
            return false;
        }
        const bool wrong = (wants_number && args[i].kind != ScriptValue::Kind::Number) ||
                           (wants_text && args[i].kind != ScriptValue::Kind::Text);
        if (wrong) {
            error =
                info.name + " argument " + std::to_string(i + 1) + " has to be " + declared.type;
            if (!declared.name.empty()) error += " ('" + declared.name + "')";
            return false;
        }
    }
    return true;
}

bool FunctionRegistry::add(const FunctionInfo& info, ContextScriptFunction fn, std::string& error) {
    if (!validFunctionName(info.name, error)) return false;
    if (!validMetadata(info, error)) return false;
    if (!fn) {
        error = "function '" + info.name + "' has no body";
        return false;
    }
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& existing : functions_) {
        if (existing.info.name == info.name) {
            error = "function '" + info.name + "' is already registered";
            return false;
        }
    }

    Entry entry;
    entry.info = info;
    entry.fn = fn;
    entry.plain = [fn](const std::vector<ScriptValue>& args, ScriptValue& result,
                       std::string& why) {
        return fn(args, FunctionContext{}, result, why);
    };
    functions_.push_back(std::move(entry));
    error.clear();
    return true;
}

bool FunctionRegistry::add(const std::string& name, ScriptFunction fn, std::string& error) {
    if (!fn) {
        error = "function '" + name + "' has no body";
        return false;
    }
    return add(
        name,
        [inner = std::move(fn)](const std::vector<ScriptValue>& args, const FunctionContext&,
                                ScriptValue& result,
                                std::string& why) { return inner(args, result, why); },
        error);
}

bool FunctionRegistry::add(const std::string& name, ContextScriptFunction fn, std::string& error) {
    FunctionInfo info;
    info.name = name;
    return add(info, std::move(fn), error);
}

bool FunctionRegistry::remove(const std::string& name, std::string& error) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto it = functions_.begin(); it != functions_.end(); ++it) {
        if (it->info.name != name) continue;
        functions_.erase(it);
        error.clear();
        return true;
    }
    error = "no script function named '" + name + "'";
    return false;
}

const ScriptFunction* FunctionRegistry::find(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& entry : functions_) {
        if (entry.info.name == name) return &entry.plain;
    }
    return nullptr;
}

bool FunctionRegistry::findContext(const std::string& name, ContextScriptFunction& out) const {
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& entry : functions_) {
        if (entry.info.name == name) {
            out = entry.fn;
            return true;
        }
    }
    return false;
}

bool FunctionRegistry::findCall(const std::string& name, FunctionInfo& info,
                                ContextScriptFunction& out) const {
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& entry : functions_) {
        if (entry.info.name == name) {
            info = entry.info;
            out = entry.fn;
            return true;
        }
    }
    return false;
}

bool FunctionRegistry::findInfo(const std::string& name, FunctionInfo& out) const {
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& entry : functions_) {
        if (entry.info.name == name) {
            out = entry.info;
            return true;
        }
    }
    return false;
}

const FunctionInfo* FunctionRegistry::info(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& entry : functions_) {
        if (entry.info.name == name) return &entry.info;
    }
    return nullptr;
}

std::vector<FunctionInfo> FunctionRegistry::all() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<FunctionInfo> out;
    out.reserve(functions_.size());
    for (const auto& entry : functions_)
        out.push_back(entry.info);
    std::sort(out.begin(), out.end(),
              [](const FunctionInfo& a, const FunctionInfo& b) { return a.name < b.name; });
    return out;
}

size_t FunctionRegistry::size() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return functions_.size();
}

std::vector<std::string> FunctionRegistry::names() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::string> out;
    out.reserve(functions_.size());
    for (const auto& entry : functions_)
        out.push_back(entry.info.name);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace undek::script
