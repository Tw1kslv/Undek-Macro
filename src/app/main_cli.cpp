#include "app/FileLog.h"
#include "app/ScriptRunner.h"
#include "bindings/BindingConfig.h"
#include "bindings/HotkeyHost.h"
#include "script/BuiltinFunctions.h"
#include "script/Checker.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

#ifndef UNDEK_VERSION
#define UNDEK_VERSION "dev"
#endif

void printUsage() {
    std::cout << "Undek Macro " << UNDEK_VERSION << "\n"
              << "  undek run <script.undek> [--profile path.json]\n"
              << "  undek dry-run <script.undek> [--profile path.json]\n"
              << "  undek check <script.undek>\n"
              << "  undek listen [--bindings path.json]\n"
              << "  undek --version\n"
              << "\nRun and dry-run exit 0 on success, 1 on a bad script or profile.\n"
              << "Check exits 0 when there's nothing to report, 1 when something is.\n"
              << "Launch undek.exe with no arguments for the GUI.\n";
}

} // namespace

int runCli(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    const std::string cmd = argv[1];

    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        printUsage();
        return 0;
    }
    if (cmd == "version" || cmd == "--version" || cmd == "-v") {
        std::cout << "Undek Macro " << UNDEK_VERSION << "\n";
        return 0;
    }

    undek::FileLog::init("logs");

    if (cmd == "listen") {
        std::string bindings_path = "bindings/default.json";
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--bindings" && i + 1 < argc) {
                bindings_path = argv[++i];
            }
        }

        undek::bindings::BindingConfig config = undek::bindings::BindingConfig::defaults();
        if (!undek::bindings::BindingConfig::loadFromFile(bindings_path, config) ||
            config.bindings.empty()) {
            std::cerr << "Warning: could not load " << bindings_path
                      << ", using built-in defaults.\n";
            config = undek::bindings::BindingConfig::defaults();
        }

        std::cout << "Undek Macro keybindings:\n";
        undek::bindings::HotkeyHost host(config);
        return host.run();
    }

    if (argc < 3) {
        printUsage();
        return 1;
    }

    if (cmd == "check") {
        // Worth having without a screen: everything it looks at is in the file
        // and the folder around it, so it runs anywhere, including CI.
        const std::string path = argv[2];
        const std::string source = undek::script::readFile(path);
        if (source.empty()) {
            std::cerr << "Can't read " << path << "\n";
            return 1;
        }
        const auto problems =
            undek::script::checkScript(source, std::filesystem::path(path).parent_path().string());
        if (problems.empty()) {
            std::cout << path << ": nothing to report\n";
            return 0;
        }
        for (const auto& p : problems) {
            std::cout << path << ":" << p.line << ": " << p.message << "\n";
        }
        std::cout << problems.size() << " problem(s)\n";
        return 1;
    }

    const bool dry_run = (cmd == "dry-run");
    if (cmd != "run" && !dry_run) {
        printUsage();
        return 1;
    }

    std::string script_path = argv[2];
    std::string profile_path = "profiles/default.json";

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--profile" && i + 1 < argc) {
            profile_path = argv[++i];
        }
    }

    return undek::ScriptRunner::run(script_path, profile_path, dry_run);
}
