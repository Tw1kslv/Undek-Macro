#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace undek::script {

// Bumped whenever this API or the meaning of FunctionInfo changes, so a plugin
// can say which host it was written for.
constexpr int kFunctionApiVersion = 1;

// Arguments and results for functions registered from C++. Everything that
// isn't a number is text, which is enough for the call host and easy to test.
struct ScriptValue {
    enum class Kind { Number, Text };
    Kind kind = Kind::Number;
    double number = 0.0;
    std::string text;

    static ScriptValue num(double v) {
        ScriptValue s;
        s.kind = Kind::Number;
        s.number = v;
        return s;
    }
    static ScriptValue str(std::string v) {
        ScriptValue s;
        s.kind = Kind::Text;
        s.text = std::move(v);
        return s;
    }
};

struct ArgumentInfo {
    std::string name;
    std::string type; // "number" or "text"
    std::string description;
    bool optional = false;
};

// What a registered function says about itself. checkSignature() holds calls to
// it, and a plugin listing, the docs or a function browser read the same thing.
struct FunctionInfo {
    std::string name;
    std::string description;
    std::vector<ArgumentInfo> arguments;
    std::string returns; // "number", "text" or empty for none
    std::string version;
};

// What a function may touch outside its arguments. Deliberately small: a
// cancelled run should stop, and anything a function prints belongs in the
// same log as everything else.
struct FunctionContext {
    std::function<bool()> is_cancelled;
    std::function<double()> elapsed_ms;   // -1 when unknown
    std::function<double()> remaining_ms; // -1 when there's no runtime limit
    std::function<void(const std::string&)> log;

    bool cancelled() const { return is_cancelled && is_cancelled(); }
    double elapsedMs() const { return elapsed_ms ? elapsed_ms() : -1.0; }
    double remainingMs() const { return remaining_ms ? remaining_ms() : -1.0; }
    void print(const std::string& message) const {
        if (log) log(message);
    }
};

// Returning false with `error` set shows up in the Execution Log and leaves
// the script running, so a failing helper shouldn't need to kill a macro.
using ContextScriptFunction =
    std::function<bool(const std::vector<ScriptValue>& args, const FunctionContext& context,
                       ScriptValue& result, std::string& error)>;

// The original shape, kept so existing registrations don't have to change.
using ScriptFunction = std::function<bool(const std::vector<ScriptValue>& args, ScriptValue& result,
                                          std::string& error)>;

// Rules the VM relies on:
//   - Every public method locks, so any thread can register, look up or remove.
//     A copy taken out of the registry stays valid even if another thread
//     removes that entry, and no lookup ever sees half an entry.
//   - Register before the run that calls the function. Every lookup is locked,
//     so adding from another thread is safe, but a call that already started
//     keeps the copy it took.
//   - An exception thrown by a function is caught and logged as a failed call,
//     the same as returning false, so a plugin can't take the whole run down.
//   - remove() is for teardown: a run that already copied the function finishes
//     with it rather than hitting a half-removed entry.
//   - add() checks the metadata, so a declaration can't promise something the
//     host would otherwise quietly ignore: argument types are number or text,
//     optional arguments come last, and returns is number, text or empty.
class FunctionRegistry {
public:
    // Functions registered here are available to every VM in the process.
    static FunctionRegistry& global();

    // Checks a call against what the function declared: how many arguments it
    // wants and, for the ones with a type, what kind each has to be. Functions
    // that declared nothing are left alone, so old registrations keep working.
    static bool checkSignature(const FunctionInfo& info, const std::vector<ScriptValue>& args,
                               std::string& error);

    bool add(const FunctionInfo& info, ContextScriptFunction fn, std::string& error);
    bool add(const std::string& name, ScriptFunction fn, std::string& error);
    bool add(const std::string& name, ContextScriptFunction fn, std::string& error);
    // Takes a registration out. Returns false when nothing had that name.
    bool remove(const std::string& name, std::string& error);

    // The plain shape, for existence checks and callers that don't need a
    // context. The VM uses findCall().
    const ScriptFunction* find(const std::string& name) const;
    bool findContext(const std::string& name, ContextScriptFunction& out) const;
    // The function and what it declared, copied under one lock. The VM uses this
    // so a registration changing underneath can't hand it mismatched halves.
    bool findCall(const std::string& name, FunctionInfo& info, ContextScriptFunction& out) const;
    // Copies what the function declared, so the caller can keep it while other
    // threads register and remove things.
    bool findInfo(const std::string& name, FunctionInfo& out) const;
    // Null when the name is unknown. The pointer is only good until the next
    // add()/remove(), so prefer findInfo() outside a single-threaded setup.
    const FunctionInfo* info(const std::string& name) const;
    std::vector<FunctionInfo> all() const;

    std::vector<std::string> names() const;
    size_t size() const;

private:
    struct Entry {
        FunctionInfo info;
        ContextScriptFunction fn;
        ScriptFunction plain; // fn with an empty context, built on add
    };
    std::vector<Entry> functions_;
    mutable std::mutex mtx_;
};

} // namespace undek::script
