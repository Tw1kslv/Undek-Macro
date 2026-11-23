#include "app/FirstRun.h"

#include "bindings/BindingConfig.h"
#include "core/HumanizationProfile.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace undek {
namespace {

constexpr const char* kMarker = ".undek-version";

const char* kWelcomeScript =
    "// Welcome to Undek Macro.\n"
    "// This is a script, not a recording: press F5 to run it, or F6 for a dry\n"
    "// run that only shows the timing in the Execution Log.\n"
    "Log(\"Undek Macro is working\")\n"
    "Wait(300ms, humanize=false)\n"
    "\n"
    "// Delete this macro once you have your own. The examples folder next to\n"
    "// the program has longer ones to borrow from.\n";

std::string readFirstLine(const fs::path& file) {
    std::ifstream in(file);
    std::string line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
    return line;
}

bool folderHasMacros(const fs::path& dir) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".undek") return true;
    }
    return false;
}

bool writeText(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    return static_cast<bool>(out);
}

} // namespace

FirstRunReport prepareFirstRun(const std::string& root, const std::string& version) {
    FirstRunReport report;
    const fs::path base(root);

    std::error_code ec;
    for (const char* name : {"macros", "profiles", "bindings", "logs"}) {
        const fs::path dir = base / name;
        if (fs::exists(dir, ec)) continue;
        fs::create_directories(dir, ec);
        if (ec) {
            report.error = std::string("can't create ") + name + ": " + ec.message();
            return report;
        }
        report.created.push_back(name);
    }

    report.previous_version = readFirstLine(base / kMarker);
    report.fresh = report.previous_version.empty();

    // A missing default file is the one case where writing something is better
    // than starting up with nothing; anything already there is left alone.
    const fs::path profile_file = base / "profiles" / "default.json";
    if (!fs::exists(profile_file, ec)) {
        if (HumanizationProfile::defaults().saveToFile(profile_file.string())) {
            report.created.push_back("profiles/default.json");
        } else {
            report.error = "can't write profiles/default.json";
        }
    }

    const fs::path bindings_file = base / "bindings" / "default.json";
    if (!fs::exists(bindings_file, ec)) {
        if (bindings::BindingConfig::defaults().saveToFile(bindings_file.string())) {
            report.created.push_back("bindings/default.json");
        } else if (report.error.empty()) {
            report.error = "can't write bindings/default.json";
        }
    }

    // Only a real first start gets the starter macro, and only if the folder is
    // empty - a dev tree with macros in it shouldn't collect another one.
    const fs::path macros = base / "macros";
    if (report.fresh && !folderHasMacros(macros) &&
        writeText(macros / "Welcome.undek", kWelcomeScript)) {
        report.created.push_back("macros/Welcome.undek");
    }

    if (!writeText(base / kMarker, version + "\n") && report.error.empty()) {
        report.error = std::string("can't write ") + kMarker;
    }
    return report;
}

} // namespace undek
