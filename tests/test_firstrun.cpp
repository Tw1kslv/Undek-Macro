// Checks the first-start setup: folders, default files, the starter macro, and
// that a second start leaves everything alone.

#include "app/FirstRun.h"
#include "bindings/BindingConfig.h"
#include "core/HumanizationProfile.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    if (ok) {
        std::cout << "ok   " << what << "\n";
        return;
    }
    ++g_failures;
    std::cout << "FAIL " << what << "\n";
}

std::string readLine(const fs::path& file) {
    std::ifstream in(file);
    std::string line;
    std::getline(in, line);
    return line;
}

void writeText(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

} // namespace

int main() {
    const fs::path base = fs::temp_directory_path() / "undek_firstrun_test";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);

    // A folder that has never been started before
    {
        const auto report = undek::prepareFirstRun(base.string(), "1.0.0");
        check(report.fresh, "a folder with no marker counts as a first run");
        check(report.error.empty(), "the first run reports no error: " + report.error);
        check(fs::exists(base / "macros") && fs::exists(base / "logs"),
              "the working folders are created");
        check(fs::exists(base / "profiles" / "default.json"),
              "a default profile is written when there isn't one");
        check(fs::exists(base / "bindings" / "default.json"),
              "a default binding file is written when there isn't one");
        check(fs::exists(base / "macros" / "Welcome.undek"),
              "an empty macros folder gets the starter macro");
        check(readLine(base / ".undek-version") == "1.0.0", "the marker records the version");

        bool listed = false;
        for (const auto& item : report.created)
            listed = listed || item == "profiles/default.json";
        check(listed, "the report lists what it wrote");

        undek::HumanizationProfile profile;
        check(undek::HumanizationProfile::loadFromFile(
                  (base / "profiles" / "default.json").string(), profile),
              "the default profile it wrote loads back");
        undek::bindings::BindingConfig bindings;
        check(undek::bindings::BindingConfig::loadFromFile(
                  (base / "bindings" / "default.json").string(), bindings),
              "the default bindings it wrote load back");
    }

    // Second start: nothing to do, and it knows which version ran last
    {
        const auto report = undek::prepareFirstRun(base.string(), "1.1.0");
        check(!report.fresh, "the second start isn't a first run");
        check(report.previous_version == "1.0.0", "the previous version is read from the marker");
        check(report.created.empty(), "the second start creates nothing");
        check(readLine(base / ".undek-version") == "1.1.0",
              "the marker moves on to the new version");
    }

    // Existing work is never replaced
    {
        writeText(base / "macros" / "Welcome.undek", "// edited by hand\n");
        writeText(base / "profiles" / "default.json", "{\"owned\": true}\n");
        fs::remove(base / ".undek-version", ec);

        const auto report = undek::prepareFirstRun(base.string(), "1.0.0");
        check(report.fresh, "a lost marker still counts as a first run");
        check(readLine(base / "macros" / "Welcome.undek") == "// edited by hand",
              "the user's edits to the starter macro survive");
        check(readLine(base / "profiles" / "default.json") == "{\"owned\": true}",
              "an existing profile is never overwritten");
        check(!fs::exists(base / "macros" / "Welcome 2.undek"), "no second starter macro appears");
    }

    // A macros folder that already has work in it doesn't get a starter macro
    {
        const fs::path other = fs::temp_directory_path() / "undek_firstrun_busy";
        fs::remove_all(other, ec);
        fs::create_directories(other / "macros", ec);
        writeText(other / "macros" / "Mine.undek", "Log(\"hi\")\n");

        const auto report = undek::prepareFirstRun(other.string(), "1.0.0");
        check(report.fresh, "the busy folder is still a first start");
        check(!fs::exists(other / "macros" / "Welcome.undek"),
              "a folder with macros in it gets no starter macro");
        fs::remove_all(other, ec);
    }

    // A root that can't be prepared reports it instead of pretending
    {
        const fs::path broken = fs::temp_directory_path() / "undek_firstrun_broken";
        fs::remove_all(broken, ec);
        fs::create_directories(broken, ec);
        writeText(broken / "profiles", "not a folder\n");

        const auto report = undek::prepareFirstRun(broken.string(), "1.0.0");
        check(!report.error.empty(), "a profile path that can't be written is reported");
        fs::remove_all(broken, ec);
    }

    fs::remove_all(base, ec);

    if (g_failures == 0) {
        std::cout << "PASS\n";
        return 0;
    }
    std::cout << g_failures << " failure(s)\n";
    return 1;
}
