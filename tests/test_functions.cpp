// The contract a C++ function has to keep to be callable from a script: what
// add() accepts, how a call is measured against the declaration, and what the
// registry hands back while other threads are adding and removing entries.

#include "script/FunctionRegistry.h"

#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using undek::script::ArgumentInfo;
using undek::script::ContextScriptFunction;
using undek::script::FunctionContext;
using undek::script::FunctionInfo;
using undek::script::FunctionRegistry;
using undek::script::ScriptFunction;
using undek::script::ScriptValue;

int g_failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

bool reads(const std::string& error, const char* text) {
    return error.find(text) != std::string::npos;
}

FunctionInfo describe(const std::string& name) {
    FunctionInfo info;
    info.name = name;
    return info;
}

bool doesNothing(const std::vector<ScriptValue>&, const FunctionContext&, ScriptValue&,
                 std::string&) {
    return true;
}

} // namespace

int main() {
    {
        // A name has to be something the lexer could produce, or a script
        // could never call it
        FunctionRegistry registry;
        std::string error;
        check(!registry.add("", doesNothing, error) && reads(error, "needs a name"),
              "an empty name is refused");
        check(!registry.add("9lives", doesNothing, error) &&
                  reads(error, "can't start with a digit"),
              "a name starting with a digit is refused");
        check(!registry.add("has space", doesNothing, error) && reads(error, "letters, digits"),
              "a name with a space is refused");
        check(!registry.add("dot.name", doesNothing, error), "a name with punctuation is refused");
        check(registry.add("Under_score2", doesNothing, error),
              "a name with letters, digits and _ is accepted: " + error);
        check(registry.size() == 1, "only the accepted name is registered");
    }

    {
        FunctionRegistry registry;
        std::string error;
        check(registry.add("Twice", doesNothing, error), "the duplicate fixture registers");
        check(!registry.add("Twice", doesNothing, error) && reads(error, "already registered"),
              "registering the same name twice is refused");

        ContextScriptFunction empty;
        check(!registry.add(describe("NoBody"), empty, error) && reads(error, "no body"),
              "a declaration with no function behind it is refused");
        check(registry.size() == 1, "a refused registration doesn't take a slot");
    }

    {
        // Anything the host would have to guess at is refused up front rather
        // than quietly ignored when a call arrives
        FunctionRegistry registry;
        std::string error;

        FunctionInfo unknown_type = describe("UnknownType");
        unknown_type.arguments.push_back({"where", "vector", "", false});
        check(!registry.add(unknown_type, doesNothing, error) &&
                  reads(error, "unknown type 'vector'"),
              "an argument type the host doesn't have is refused");

        FunctionInfo unknown_return = describe("UnknownReturn");
        unknown_return.returns = "boolean";
        check(!registry.add(unknown_return, doesNothing, error) &&
                  reads(error, "unknown return type 'boolean'"),
              "a return type the host doesn't have is refused");

        FunctionInfo late_required = describe("LateRequired");
        late_required.arguments.push_back({"first", "number", "", true});
        late_required.arguments.push_back({"second", "number", "", false});
        check(!registry.add(late_required, doesNothing, error) &&
                  reads(error, "required argument after an optional one"),
              "a required argument after an optional one is refused");

        FunctionInfo repeated = describe("Repeated");
        repeated.arguments.push_back({"spot", "number", "", false});
        repeated.arguments.push_back({"spot", "text", "", false});
        check(!registry.add(repeated, doesNothing, error) &&
                  reads(error, "repeats the argument name 'spot'"),
              "two arguments with the same name are refused");

        FunctionInfo good = describe("Described");
        good.description = "Something a function browser can show";
        good.arguments.push_back({"value", "number", "", false});
        good.arguments.push_back({"label", "text", "", true});
        good.returns = "text";
        good.version = "1.0";
        check(registry.add(good, doesNothing, error), "a full declaration is accepted: " + error);

        FunctionInfo copy;
        check(registry.findInfo("Described", copy) && copy.description == good.description &&
                  copy.arguments.size() == 2 && copy.returns == "text" && copy.version == "1.0",
              "what a function declared comes back unchanged");
    }

    {
        // checkSignature is what the VM and the checker both call, so the
        // wording it produces is part of the contract
        FunctionInfo info = describe("Fit");
        info.arguments.push_back({"value", "number", "", false});
        info.arguments.push_back({"label", "text", "", true});
        std::string error;

        check(FunctionRegistry::checkSignature(info, {ScriptValue::num(1)}, error),
              "the required argument alone fits");
        check(FunctionRegistry::checkSignature(info, {ScriptValue::num(1), ScriptValue::str("x")},
                                               error),
              "the optional argument fits too");
        check(!FunctionRegistry::checkSignature(info, {}, error) &&
                  reads(error, "takes 1 to 2 arguments, got 0"),
              "too few arguments are refused with the range");
        check(!FunctionRegistry::checkSignature(
                  info, {ScriptValue::num(1), ScriptValue::str("x"), ScriptValue::num(2)}, error) &&
                  reads(error, "got 3"),
              "too many arguments are refused");
        check(!FunctionRegistry::checkSignature(info, {ScriptValue::str("x")}, error) &&
                  reads(error, "argument 1 has to be number ('value')"),
              "a text argument where a number is declared is refused by name");
        check(!FunctionRegistry::checkSignature(info, {ScriptValue::num(1), ScriptValue::num(2)},
                                                error) &&
                  reads(error, "argument 2 has to be text ('label')"),
              "a number where text is declared is refused");

        FunctionInfo anything = describe("Anything");
        check(
            FunctionRegistry::checkSignature(
                anything, {ScriptValue::num(1), ScriptValue::str("x"), ScriptValue::num(2)}, error),
            "a function that declared nothing takes any arguments");

        FunctionInfo one = describe("One");
        one.arguments.push_back({"only", "number", "", false});
        check(!FunctionRegistry::checkSignature(one, {ScriptValue::num(1), ScriptValue::num(2)},
                                                error) &&
                  reads(error, "takes 1 argument, got 2"),
              "a single required argument reads in the singular");

        FunctionInfo untyped = describe("Untyped");
        untyped.arguments.push_back({"first", "", "", false});
        check(FunctionRegistry::checkSignature(untyped, {ScriptValue::str("x")}, error),
              "an argument with no declared type takes either kind");
    }

    {
        FunctionRegistry registry;
        std::string error;
        check(registry.add(
                  "Count",
                  [](const std::vector<ScriptValue>&, ScriptValue& result, std::string&) {
                      result = ScriptValue::num(7);
                      return true;
                  },
                  error),
              "the plain fixture registers");

        const ScriptFunction* plain = registry.find("Count");
        check(plain && *plain, "the plain shape is found");
        ScriptValue result;
        std::string why;
        check((*plain)({}, result, why) && result.kind == ScriptValue::Kind::Number &&
                  result.number == 7,
              "the plain shape calls the function it was built from");
        check(registry.find("count") == nullptr, "lookups are case sensitive");

        FunctionInfo info;
        ContextScriptFunction fn;
        check(registry.findCall("Count", info, fn) && info.name == "Count" && fn,
              "findCall returns the function and its declaration together");
        check(!registry.findCall("Missing", info, fn), "findCall says no for a name nobody took");

        FunctionInfo copy;
        check(registry.findInfo("Count", copy) && copy.name == "Count", "findInfo finds it");
        check(!registry.findInfo("Missing", copy), "findInfo says no for an unknown name");
        check(registry.info("Count") != nullptr, "info() finds it");
        check(registry.info("Missing") == nullptr, "info() is null for an unknown name");

        check(registry.add("beta", doesNothing, error) && registry.add("Alpha", doesNothing, error),
              "the sorting fixtures register");
        const auto names = registry.names();
        check(names.size() == 3 && names[0] == "Alpha" && names[1] == "Count" && names[2] == "beta",
              "names() comes back sorted");
        const auto all = registry.all();
        check(all.size() == 3 && all[0].name == "Alpha" && all[2].name == "beta",
              "all() is sorted the same way");

        check(registry.remove("Count", error), "removing a registered function works");
        check(!registry.remove("Count", error) && reads(error, "no script function named 'Count'"),
              "removing it a second time says so");
        check(registry.find("Count") == nullptr && registry.size() == 2,
              "a removed function is gone");
        check(registry.add("Count", doesNothing, error), "the name can be used again afterwards");
    }

    {
        // The context shape is how a function stops on a cancelled run and
        // writes to the same log as everything else
        FunctionRegistry registry;
        std::string error;
        bool saw_cancel = false;
        std::string printed;
        check(registry.add(
                  "SeeContext",
                  [&](const std::vector<ScriptValue>&, const FunctionContext& context, ScriptValue&,
                      std::string&) {
                      saw_cancel = context.cancelled();
                      context.print("hello from a function");
                      return true;
                  },
                  error),
              "the context fixture registers: " + error);

        FunctionInfo info;
        ContextScriptFunction fn;
        check(registry.findCall("SeeContext", info, fn), "the context fixture is found");

        FunctionContext context;
        context.is_cancelled = [] {
            return true;
        };
        context.log = [&](const std::string& message) {
            printed = message;
        };
        ScriptValue out;
        std::string why;
        check(fn({}, context, out, why) && saw_cancel && printed == "hello from a function",
              "the function sees the host's cancel flag and log");

        // A plain registration must not hand the function a half-built context
        FunctionContext none;
        check(!none.cancelled() && none.elapsedMs() < 0 && none.remainingMs() < 0,
              "an empty context answers without calling anything");
        none.print("dropped");
    }

    {
        // Re-registering a name under a different declaration must not leave a
        // caller holding half of one and half of the other
        FunctionRegistry registry;
        std::string error;
        FunctionInfo first = describe("Moving");
        first.description = "first";
        first.arguments.push_back({"a", "number", "", false});
        check(registry.add(first, doesNothing, error), "the first declaration registers");
        FunctionInfo seen;
        ContextScriptFunction fn;
        check(registry.findCall("Moving", seen, fn) && seen.description == "first" &&
                  seen.arguments.size() == 1,
              "the call is paired with the declaration that was there");

        check(registry.remove("Moving", error), "the first declaration is removed");
        FunctionInfo second = describe("Moving");
        second.description = "second";
        check(registry.add(second, doesNothing, error), "the name is free again");
        check(registry.findCall("Moving", seen, fn) && seen.description == "second" &&
                  seen.arguments.empty(),
              "the new declaration replaces the old one whole");
    }

    {
        // One run can't stop another from registering, so a lookup has to stay
        // safe while entries are added and removed underneath it
        FunctionRegistry registry;
        std::string error;
        check(registry.add("Stable", doesNothing, error), "the churn fixture registers");
        std::atomic<bool> stop{false};
        std::thread churn([&] {
            int i = 0;
            std::string ignored;
            while (!stop.load()) {
                registry.add("Temp" + std::to_string(i % 8), doesNothing, ignored);
                registry.remove("Temp" + std::to_string((i + 7) % 8), ignored);
                ++i;
            }
        });
        for (int i = 0; i < 2000; ++i) {
            FunctionInfo info;
            ContextScriptFunction fn;
            if (registry.findCall("Stable", info, fn)) {
                check(info.name == "Stable", "a lookup under churn returns the name it asked for");
            }
            for (const auto& name : registry.names())
                check(!name.empty(), "names are never empty");
            registry.size();
        }
        stop = true;
        churn.join();
    }

    check(&FunctionRegistry::global() == &FunctionRegistry::global(),
          "global() is one registry, not a new one each call");

    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "PASS\n";
    return 0;
}
