#include "app/ScriptRunner.h"

#include "core/HumanizationProfile.h"
#include "core/RunSlot.h"
#include "input/InputBackendFactory.h"
#include "input/NullInputBackend.h"
#include "script/BuiltinFunctions.h"
#include "script/Lexer.h"
#include "script/Parser.h"
#include "script/Vm.h"

#include <filesystem>
#include <iostream>
#include <memory>

namespace undek {

int ScriptRunner::runSource(const std::string& source, const std::string& profile_path,
                            bool dry_run, std::string* error_out, LogCallback log_cb,
                            script::DebugController* dbg, const std::string& script_dir) {
    if (source.empty()) {
        if (error_out) *error_out = "Script is empty";
        return 1;
    }

    if (!runs::tryBegin("script")) {
        if (error_out) *error_out = "Another macro is already running";
        return 1;
    }
    struct RunRelease {
        ~RunRelease() { runs::end(); }
    } run_release;

    HumanizationProfile profile = HumanizationProfile::defaults();
    if (!HumanizationProfile::loadFromFile(profile_path, profile)) {
        if (error_out) *error_out = "Warning: using default profile";
    }
    std::string verr;
    if (!profile.validate(verr)) {
        if (error_out) *error_out = "Invalid profile: " + verr;
        return 1;
    }

    try {
        script::Lexer lexer(source);
        const auto tokens = lexer.tokenize();
        script::Parser parser(tokens, 0, 0, script_dir);
        const auto program = parser.parse();

        // A dry run never sends anything, so there's no reason to load the
        // Interception driver or open a SendInput path just to throw it away.
        std::unique_ptr<IInputBackend> input;
        std::string input_name;
        if (dry_run) {
            input = std::make_unique<NullInputBackend>();
            input_name = "none (dry run)";
        } else {
            auto made = createInputBackend(profile);
            input = std::move(made.backend);
            input_name = made.name;
        }
        if (log_cb) {
            log_cb("[input] backend: " + input_name);
        } else {
            std::cout << "[input] backend: " << input_name << "\n";
        }
        script::Vm vm(program, profile, *input, dry_run, log_cb, dbg);
        vm.setScriptDir(script_dir);
        vm.run();
        return 0;
    } catch (const std::exception& ex) {
        if (error_out) *error_out = ex.what();
        return 1;
    }
}

int ScriptRunner::run(const std::string& script_path, const std::string& profile_path, bool dry_run,
                      LogCallback log_cb) {
    const std::string source = script::readFile(script_path);
    if (source.empty()) {
        std::cerr << "Failed to read script: " << script_path << "\n";
        return 1;
    }

    std::string err;
    const std::string script_dir = std::filesystem::path(script_path).parent_path().string();
    const int code = runSource(source, profile_path, dry_run, &err, log_cb, nullptr, script_dir);
    if (code != 0) {
        std::cerr << "Error running " << script_path << ": " << err << "\n";
        return code;
    }
    // A profile that had to fall back to the defaults still ran, so it belongs
    // in the output as a warning instead of disappearing.
    if (!err.empty()) {
        std::cerr << "Warning running " << script_path << ": " << err << "\n";
    }
    std::cout << (dry_run ? "[dry-run] " : "[run] ") << script_path << "\n";
    return code;
}

} // namespace undek
