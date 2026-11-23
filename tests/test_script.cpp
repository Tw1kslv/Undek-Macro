#include "app/AppPaths.h"
#include "app/AppSettings.h"
#include "app/ScriptRunner.h"
#include "app/FileLog.h"
#include "app/SingleInstance.h"
#include "bindings/BindingConfig.h"
#include "bindings/MousePressQueue.h"
#include "core/ActionScheduler.h"
#include "core/Cancel.h"
#include "core/HumanizationProfile.h"
#include "core/HumanizationSession.h"
#include "core/Pause.h"
#include "core/PathGenerator.h"
#include "core/ProfileStats.h"
#include "core/RandomizationEngine.h"
#include "core/RunSlot.h"
#include "core/ScanKey.h"
#include "editor/MacroBuilder.h"
#include "editor/MacroLibrary.h"
#include "editor/RecordedActions.h"
#include "input/IInputBackend.h"
#include "input/InputBackendFactory.h"
#include "input/InputRecorder.h"
#include "input/InterceptionBackend.h"
#include "input/RecorderTape.h"
#include "input/ThrottledInputBackend.h"
#include "screen/ScreenSampler.h"
#include "script/BuiltinFunctions.h"
#include "script/Checker.h"
#include "script/DebugController.h"
#include "script/FunctionRegistry.h"
#include "script/Lexer.h"
#include "script/Parser.h"
#include "script/PicturePath.h"
#include "script/Vm.h"
#include "util/AtomicFile.h"
#include "vision/Image.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct NullBackend : undek::IInputBackend {
    std::vector<std::string> events;
    std::vector<std::string> moves;
    std::vector<int> wheels;
    int active_keys = 0;
    int active_buttons = 0;

    undek::Vec2 cursorPosition() const override { return {}; }
    undek::InputResult moveAbsolute(int x, int y) override {
        moves.push_back(std::to_string(x) + "," + std::to_string(y));
        return {};
    }
    undek::InputResult moveRelative(int, int) override { return {}; }
    undek::InputResult clickLeft() override {
        events.push_back("ClickLeft");
        return {};
    }
    undek::InputResult mouseDown(const std::string& b) override {
        ++active_buttons;
        events.push_back("MouseDown:" + b);
        return {};
    }
    undek::InputResult mouseUp(const std::string& b) override {
        --active_buttons;
        events.push_back("MouseUp:" + b);
        return {};
    }
    undek::InputResult mouseWheel(int delta) override {
        wheels.push_back(delta);
        return {};
    }
    undek::InputResult keyDown(const std::string& k) override {
        ++active_keys;
        events.push_back("KeyDown:" + k);
        return {};
    }
    undek::InputResult keyUp(const std::string& k) override {
        --active_keys;
        events.push_back("KeyUp:" + k);
        return {};
    }
};

struct PickyBackend : NullBackend {
    bool supportsKey(const std::string& k) const override { return k != "bogus"; }
};

int g_failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

std::vector<std::string> runDry(const std::string& src, std::string* error = nullptr,
                                int cap = 100000,
                                undek::script::Vm::PixelSampler sampler = nullptr) {
    std::vector<std::string> logs;
    try {
        undek::script::Lexer lexer(src);
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
        profile.log_to_file = false;
        profile.max_loop_iterations = cap;
        NullBackend backend;
        undek::script::Vm vm(program, profile, backend, true,
                             [&](const std::string& m) { logs.push_back(m); });
        // The desktop isn't part of these tests: every pixel reads as black
        // unless the test says otherwise, so a colour condition behaves the same
        // on any machine.
        vm.setPixelSampler(sampler
                               ? sampler
                               : undek::script::Vm::PixelSampler(
                                     [](int, int) -> std::optional<unsigned int> { return 0u; }));
        vm.run();
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
    }
    return logs;
}

size_t count(const std::vector<std::string>& logs, const std::string& needle) {
    size_t n = 0;
    for (const auto& l : logs)
        if (l.find(needle) != std::string::npos) ++n;
    return n;
}

bool waitForState(undek::script::DebugController& dbg, undek::script::DebugController::State st,
                  int timeout_ms = 3000) {
    for (int i = 0; i < timeout_ms / 5; ++i) {
        if (dbg.state() == st) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

// State stays Paused across step() boundaries, so wait for the paused line.
bool waitForLine(undek::script::DebugController& dbg, int line, int timeout_ms = 3000) {
    for (int i = 0; i < timeout_ms / 5; ++i) {
        if (dbg.state() == undek::script::DebugController::State::Paused &&
            dbg.snapshot().current_line == line)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

} // namespace

int main() {
    std::cout.setstate(std::ios::failbit); // Vm echoes logs to stdout

    {
        auto logs = runDry("loop 3 { Log(\"a\") break }");
        check(count(logs, "[log] a") == 1, "break exits loop after first iteration");
    }
    {
        // A literal past the int range used to narrow to a negative count, so the
        // loop ran zero times instead of the cap deciding.
        const auto parsedCount = [](const std::string& src) {
            undek::script::Lexer lexer(src);
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            return static_cast<const undek::script::LoopStmt&>(*program.statements.front()).count;
        };
        check(parsedCount("loop 1000000000 { }") == 1000000000,
              "a loop count at the shared limit is kept as written");
        check(parsedCount("loop 2147483647 { }") == 1000000000,
              "a count past the limit is clamped, not narrowed");
        check(parsedCount("loop 2147483648 { }") == 1000000000,
              "a count past INT_MAX stays positive instead of wrapping");
        check(parsedCount("loop 99999999999 { }") == 1000000000,
              "a much larger count is clamped the same way");

        auto logs = runDry("loop 2147483648 { Log(\"big\") }", nullptr, 3);
        check(count(logs, "[log] big") == 3,
              "a huge literal count still stops at max_loop_iterations");
        check(count(logs, "capped at 3") == 1, "and the cap is reported");

        logs = runDry("loop 0 - 2147483649 { Log(\"neg\") }");
        check(count(logs, "[log] neg") == 0, "a huge negative count runs zero times");
    }
    {
        auto logs = runDry("while PixelAt(0,0) == 0x123456 tolerance 255 { Log(\"w\") }");
        check(count(logs, "[log] w") == 5, "while is capped at 5 iterations in dry-run");
    }
    {
        auto logs =
            runDry("while PixelAt(0,0) == 0x123456 tolerance 255 { Log(\"w\") }", nullptr, 2);
        check(count(logs, "[log] w") == 2, "while honors max_loop_iterations");
    }
    {
        auto logs = runDry("repeat_until PixelAt(0,0) == 0x000000 tolerance 255 { Log(\"r\") }");
        check(count(logs, "[log] r") == 1, "repeat_until runs once and stops when condition holds");
    }
    {
        // A pixel nobody could read isn't black, and the log says which it was
        const auto logs = runDry(
            "if PixelAt(1, 2) == 0x000000 tolerance 255 { Log(\"hit\") } else { Log(\"miss\") }",
            nullptr, 100000, [](int, int) -> std::optional<unsigned int> { return std::nullopt; });
        check(count(logs, "unreadable") == 1, "an unreadable pixel is logged as such");
        check(count(logs, "[log] miss") == 1, "and the colour condition is false, not black");
        check(count(logs, "[log] hit") == 0, "so nothing matching black runs");
    }
#ifndef _WIN32
    {
        check(!undek::ScreenSampler::pixelColor(0, 0).has_value(),
              "a screen that isn't there reads as unreadable, not as black");
    }
#endif
    {
        auto logs = runDry("if not WindowTitle(\"zzz-no-such-window-zzz\") { Log(\"neg\") } else { "
                           "Log(\"pos\") }");
        check(count(logs, "[log] neg") == 1 && count(logs, "[log] pos") == 0, "not WindowTitle");
    }
    {
        auto logs = runDry("RandomWait(10, 20) Log(\"done\")");
        check(count(logs, "[random_wait]") == 1 && count(logs, "[log] done") == 1,
              "RandomWait parses and runs");
    }
    {
        // 1 followed by 308 zeroes is a valid double, but the seconds suffix
        // multiplied it to infinity, and that used to become the wait.
        std::string huge(1, '1');
        huge.append(308, '0');

        std::string err;
        runDry("Wait(" + huge + "s)", &err);
        check(err.find("too large") != std::string::npos,
              "a seconds value that overflows on the way to ms is a lexer error (got '" + err +
                  "')");
        check(err.find("line") != std::string::npos, "and the overflow error names the line");

        err.clear();
        runDry("Wait(" + huge + ")", &err);
        check(err.find("86400000 ms limit") != std::string::npos,
              "a huge literal wait is refused (got '" + err + "')");

        err.clear();
        runDry("Wait(3600000s)", &err);
        check(err.find("86400000 ms limit") != std::string::npos,
              "a wait past a day is refused (got '" + err + "')");

        // A computed value never passes the literal check, so the VM has to
        // turn it away before it sleeps.
        const auto refusedAtRuntime = [](const std::string& src) {
            undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
            p.log_to_file = false;
            undek::script::Lexer lexer(src);
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            NullBackend backend;
            undek::script::Vm vm(program, p, backend, false);
            std::string message;
            try {
                vm.run();
            } catch (const std::exception& e) {
                message = e.what();
            }
            return message;
        };
        const std::string computed = refusedAtRuntime("Wait(86400000 + 86400000)");
        check(computed.find("86400000 ms limit") != std::string::npos,
              "a computed wait past the limit is refused before it sleeps (got '" + computed +
                  "')");
        const std::string random = refusedAtRuntime("RandomWait(100000000000, 100000000000)");
        check(random.find("86400000 ms limit") != std::string::npos,
              "a RandomWait past the limit is refused too (got '" + random + "')");
    }
    {
        std::string err;
        runDry("include \"does/not/exist.undek\"", &err);
        check(err.find("Include not found") != std::string::npos, "missing include reports error");
    }
    {
        // A file kept beside the script is found the way a picture is, instead of
        // only when the working folder happens to be that folder.
        const auto original_cwd = std::filesystem::current_path();
        const auto sandbox = original_cwd / "test_include_beside";
        std::filesystem::remove_all(sandbox);
        std::filesystem::create_directories(sandbox / "macros");
        undek::script::writeFile((sandbox / "macros" / "shared.undek").string(),
                                 "Log(\"from include\")\n");
        const std::string profile_path = (sandbox / "profile.json").string();
        check(undek::HumanizationProfile::defaults().saveToFile(profile_path),
              "the include test can write its profile");

        std::vector<std::string> logs;
        std::string err;
        const int code = undek::ScriptRunner::runSource(
            "include \"shared.undek\"\n", profile_path, true, &err,
            [&](const std::string& m) { logs.push_back(m); }, nullptr,
            (sandbox / "macros").string());
        check(code == 0, "an include beside the script is found (got '" + err + "')");
        check(count(logs, "[log] from include") == 1, "and the included body runs");
        std::filesystem::remove_all(sandbox);
    }
    {
        std::string err;
        runDry("if PixelAt(0,0) == 16777216 { Log(\"x\") }", &err);
        check(err.find("0xFFFFFF") != std::string::npos,
              "a colour past 24 bits is refused instead of cast (got '" + err + "')");
        err.clear();
        runDry("if PixelAt(0,0) == 0xFF0000 { Log(\"x\") }", &err);
        check(err.empty(), "a normal 24-bit colour still parses (got '" + err + "')");
    }
    {
        std::string err;
        runDry("loop 1 { Log(\"x\") ", &err);
        check(!err.empty(), "unterminated block is an error");
    }
    {
        undek::cancel::request();
        std::string err;
        runDry("Log(\"never\")", &err);
        check(err == "Cancelled", "cancel flag aborts the script");
        undek::cancel::clear();
    }
    {
        std::string err;
        auto logs = runDry("Log(\"First\") @ Log(\"Second\")", &err);
        check(err.find("unexpected character") != std::string::npos,
              "unknown char produces a lexer error instead of silent truncation");
        check(err.find("line") != std::string::npos, "lexer error names the line");
        check(count(logs, "[log] First") == 0, "script with lexer error does not execute");
    }
    {
        std::string err;
        runDry("Log(\"never closed)", &err);
        check(err.find("unterminated string") != std::string::npos,
              "unterminated string is an error");
    }
    {
        auto logs = runDry("loop 50 { Log(\"x\") }");
        check(count(logs, "[log] x") == 5, "loop is capped at 5 iterations in dry-run");
    }
    {
        std::string err;
        runDry("loop 10 {}", &err);
        check(err.empty(), "empty loop body completes without error");
    }
    {
        auto logs =
            runDry("WaitUntil(PixelAt(0,0) == 0x000000 tolerance 255, timeout=100) Log(\"after\")");
        check(count(logs, "[wait_until]") == 1 && count(logs, "[log] after") == 1,
              "WaitUntil parses, evaluates, and continues");
    }
    {
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        p.delay_min_ms = 50.0;
        p.session_drift_enabled = true;
        p.session_drift_min = 0.5;
        p.session_drift_max = 1.5;
        undek::RandomizationEngine rng(p);
        bool exact = true;
        for (int i = 0; i < 20; ++i) {
            if (rng.sampleDelayMs(1.0, 12.0, false) != 1.0) exact = false;
        }
        check(exact, "humanize=false returns the exact requested delay");
    }
    check(undek::ScreenSampler::colorMatches(0x102030, 0x112232, 2), "tolerance within range");
    check(!undek::ScreenSampler::colorMatches(0x102030, 0x142030, 2), "tolerance out of range");
    check(undek::ScreenSampler::colorMatches(0xABCDEF, 0xABCDEF, 0), "exact color");

    // Test rarely missed input simulation: skips KeyDown+KeyUp pair without leaving key held
    {
        const std::string src = "loop 20 { KeyDown(\"4\") Wait(1ms, humanize=true) KeyUp(\"4\") }";
        undek::script::Lexer lexer(src);
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
        profile.log_to_file = false;
        profile.miss_input_enabled = true;
        profile.miss_probability = 0.5;
        profile.key_overlap_enabled = false;
        NullBackend backend;
        std::vector<std::string> logs;
        undek::script::Vm vm(program, profile, backend, false,
                             [&](const std::string& m) { logs.push_back(m); });
        vm.run();
        check(backend.active_keys == 0, "missed input simulation never leaves a key held down");
        check(count(logs, "missed - simulated input skip") > 0,
              "missed input simulation skips some KeyDown events");
        check(count(logs, "skipped - paired with missed keydown") ==
                  count(logs, "missed - simulated input skip"),
              "every missed KeyDown skips its paired KeyUp");
    }

    // Test finger overlap: MouseDown executes before KeyUp("4") when overlap triggers
    {
        const std::string src = "KeyDown(\"4\") Wait(1ms, humanize=true) KeyUp(\"4\") "
                                "MouseDown(\"right\") Wait(1ms, humanize=true) MouseUp(\"right\")";
        undek::script::Lexer lexer(src);
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
        profile.log_to_file = false;
        profile.miss_input_enabled = false;
        profile.key_overlap_enabled = true;
        profile.key_overlap_probability = 1.0;
        profile.key_overlap_min_ms = 1.0;
        profile.key_overlap_max_ms = 2.0;
        NullBackend backend;
        std::vector<std::string> logs;
        undek::script::Vm vm(program, profile, backend, false,
                             [&](const std::string& m) { logs.push_back(m); });
        vm.run();
        check(backend.active_keys == 0 && backend.active_buttons == 0,
              "overlap releases all keys and buttons");
        check(backend.events.size() == 4, "overlap executes all 4 input events");
        if (backend.events.size() == 4) {
            check(backend.events[0] == "KeyDown:4" && backend.events[1] == "MouseDown:right" &&
                      backend.events[2] == "KeyUp:4" && backend.events[3] == "MouseUp:right",
                  "MouseDown happens before KeyUp(4) during finger overlap");
        }
    }

    // Statements carry their source line for breakpoints and error reporting
    {
        undek::script::Lexer lexer("Log(\"a\")\n\nClick(Point(1, 2))\nWait(5ms)");
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        check(program.statements.size() == 3, "three statements parsed");
        check(program.statements[0]->line == 1 && program.statements[1]->line == 3 &&
                  program.statements[2]->line == 4 && program.statements[0]->source_id == 0,
              "statements stamped with source line and main source_id");
    }

    // Comments can pile up, including a bare "//" line
    {
        undek::script::Lexer lexer("// one\n// two\n//\nLog(\"a\")\n// no newline at the end");
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        check(program.statements.size() == 1 && program.statements[0]->line == 4,
              "back-to-back comments, a bare // and a comment at EOF all parse");
    }

    // All errors in one report instead of only the first
    {
        std::string err;
        auto logs = runDry("Log(\"a\")\n@\nLog(\"b\")\nBadToken\nWait(1ms)", &err);
        check(err.find("line 2") != std::string::npos, "multi-error: lexer error line reported");
        check(err.find("line 4") != std::string::npos,
              "multi-error: later parse error also reported");
        check(count(logs, "[log]") == 0, "script with errors does not execute");
    }
    {
        std::string err;
        runDry("Log(\"a\")\nBogus\nLog(\"b\")\nAlsoBogus", &err);
        check(err.find("line 2") != std::string::npos && err.find("line 4") != std::string::npos,
              "multi-error: both bad statements reported");
        check(err.rfind("line ", 0) == 0, "multi-error: first error keeps 'line N' prefix");
    }

    // DebugController: breakpoint pause, snapshot contents, step, continue
    {
        const std::string src = "var spot = Point(10, 20)\n"
                                "KeyDown(\"e\")\n"
                                "Log(\"bp\")\n"
                                "KeyUp(\"e\")\n"
                                "Log(\"done\")";
        undek::script::Lexer lexer(src);
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
        profile.log_to_file = false;
        // The defaults drop the odd press, and this test is about the key it
        // expects to be held when the breakpoint hits.
        profile.miss_input_enabled = false;
        NullBackend backend;
        std::vector<std::string> logs;
        undek::script::DebugController dbg(false);
        dbg.setBreakpoints({3});
        std::thread worker([&] {
            try {
                undek::script::Vm vm(
                    program, profile, backend, true,
                    [&](const std::string& m) { logs.push_back(m); }, &dbg);
                vm.run();
            } catch (...) {
            }
        });

        check(waitForLine(dbg, 3), "debugger pauses at breakpoint line");
        {
            const auto snap = dbg.snapshot();
            check(snap.current_line == 3, "paused at breakpoint line 3");
            check(snap.vars.size() == 1 && snap.vars[0].first == "spot" &&
                      snap.vars[0].second.x == 10.0 && snap.vars[0].second.y == 20.0,
                  "snapshot exposes script variables");
            check(snap.held_inputs.size() == 1 && snap.held_inputs[0] == "e",
                  "snapshot exposes held inputs");
        }
        dbg.step();
        check(waitForLine(dbg, 4), "step lands on line 4");
        dbg.resume();
        worker.join();
        check(dbg.state() == undek::script::DebugController::State::Finished,
              "continue runs to completion");
        check(count(logs, "[log] done") == 1, "script completed after continue");
        const auto hist = dbg.history();
        check(!hist.empty() && hist.back().text == "Log(\"done\")",
              "history records executed statements");
        double timeline = 0.0;
        for (const auto& h : hist)
            timeline += h.elapsed_ms;
        check(timeline >= 0.0, "history carries per-statement timings");
    }

    // DebugController: break on entry then stop while paused
    {
        undek::script::Lexer lexer("Log(\"a\")\nLog(\"b\")");
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
        profile.log_to_file = false;
        NullBackend backend;
        undek::script::DebugController dbg(true);
        bool cancelled = false;
        std::thread worker([&] {
            try {
                undek::script::Vm vm(program, profile, backend, true, nullptr, &dbg);
                vm.run();
            } catch (const undek::cancel::Cancelled&) {
                cancelled = true;
            } catch (...) {
            }
        });

        check(waitForState(dbg, undek::script::DebugController::State::Paused),
              "pause-on-entry stops before first statement");
        check(dbg.snapshot().current_line == 1, "paused at entry on line 1");
        dbg.stop();
        worker.join();
        undek::cancel::clear();
        check(cancelled, "stop while paused cancels the run");
    }

    // A breakpoint inside a block pauses on every pass, and stepping stays there
    {
        const std::string src = "loop 2 {\n"
                                "  KeyDown(\"a\")\n"
                                "  KeyUp(\"a\")\n"
                                "}\n"
                                "Log(\"done\")\n";
        undek::script::Lexer lexer(src);
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
        profile.log_to_file = false;
        profile.miss_input_enabled = false;
        NullBackend backend;
        undek::script::DebugController dbg(false);
        dbg.setBreakpoints({2});
        std::thread worker([&] {
            try {
                undek::script::Vm vm(program, profile, backend, false, nullptr, &dbg);
                vm.run();
            } catch (...) {
            }
        });

        check(waitForLine(dbg, 2), "a breakpoint in a loop body pauses there");
        {
            const auto snap = dbg.snapshot();
            check(snap.loop_iteration == 0, "the first pass reports iteration 0");
            check(snap.current_stmt == "KeyDown(\"a\")",
                  "the paused statement is described for the panel (got '" + snap.current_stmt +
                      "')");
        }

        dbg.step();
        check(waitForLine(dbg, 3), "stepping walks to the next line in the body");
        check(dbg.snapshot().held_inputs.size() == 1 && dbg.snapshot().held_inputs[0] == "a",
              "the snapshot shows the key held at the pause");

        dbg.resume();
        check(waitForLine(dbg, 2), "the same breakpoint pauses again on the next pass");
        check(dbg.snapshot().loop_iteration == 1, "the second pass reports iteration 1");

        // Breakpoints can be cleared while the run sits paused
        dbg.setBreakpoints({});
        dbg.resume();
        worker.join();
        check(dbg.state() == undek::script::DebugController::State::Finished,
              "clearing the breakpoint lets the rest of the run finish");

        const auto hist = dbg.history();
        check(hist.size() >= 6 && hist[0].text == "loop 2",
              "history starts with the loop statement (got " + std::to_string(hist.size()) +
                  " entries)");
        check(hist.size() >= 6 && hist[1].line == 2 && hist[1].text == "KeyDown(\"a\")",
              "history keeps the executed statements in order");
    }

    // HumanizationSession: engines sharing a session share the fatigue counter;
    // separate sessions count independently
    {
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        p.fatigue_focus_enabled = true;
        p.fatigue_cycle_actions = 10;
        p.fatigue_cycle_min_sec = 1.0e9;
        p.fatigue_cycle_max_sec = 1.0e9;

        undek::HumanizationSession shared;
        undek::RandomizationEngine e1(p, 1, &shared);
        undek::RandomizationEngine e2(p, 2, &shared);
        e1.resetSessionClock();
        e2.resetSessionClock();
        int advances = 0;
        for (int i = 0; i < 5; ++i)
            if (e1.checkFatigueCycle()) ++advances;
        for (int i = 0; i < 5; ++i)
            if (e2.checkFatigueCycle()) ++advances;
        check(advances == 1, "shared session counts actions across engines");

        undek::HumanizationSession sa, sb;
        undek::RandomizationEngine e3(p, 3, &sa);
        undek::RandomizationEngine e4(p, 4, &sb);
        e3.resetSessionClock();
        e4.resetSessionClock();
        advances = 0;
        for (int i = 0; i < 5; ++i)
            if (e3.checkFatigueCycle()) ++advances;
        for (int i = 0; i < 5; ++i)
            if (e4.checkFatigueCycle()) ++advances;
        check(advances == 0, "separate sessions count actions independently");
    }

    // Settings for disabled effects are not validated; enabled ones are
    {
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        std::string err;
        p.noise_enabled = false;
        p.noise_interval_actions = 0;
        check(p.validate(err), "disabled noise settings are not validated");
        p.noise_enabled = true;
        check(!p.validate(err), "enabled noise settings are still validated");

        undek::HumanizationProfile q = undek::HumanizationProfile::defaults();
        q.hold_variability_enabled = false;
        q.hold_scale_min = 0.0;
        q.hold_scale_max = 0.0;
        q.key_overlap_enabled = false;
        q.key_overlap_min_ms = 50.0;
        q.key_overlap_max_ms = 10.0;
        check(q.validate(err), "disabled hold/overlap settings are not validated");
        q.hold_variability_enabled = true;
        check(!q.validate(err), "enabled hold settings are still validated");
    }

    // Unsupported key names are reported instead of being silently dropped
    {
        undek::script::Lexer lexer("KeyDown(\"bogus\") KeyUp(\"bogus\") PressKey(\"bogus\")");
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
        profile.log_to_file = false;
        PickyBackend backend;
        std::vector<std::string> logs;
        undek::script::Vm vm(program, profile, backend, false,
                             [&](const std::string& m) { logs.push_back(m); });
        vm.run();
        check(count(logs, "unsupported key name") == 3,
              "every unsupported key statement is reported");
        check(backend.events.empty(), "unsupported keys inject nothing");
        check(backend.active_keys == 0, "unsupported keys leave nothing held");
    }

    // MoveTo and Scroll reach the backend
    {
        undek::script::Lexer lexer("MoveTo(Point(120, 240), humanize=false)\n"
                                   "Scroll(-120)\n"
                                   "var p = Point(30, 40)\n"
                                   "MoveTo(p)");
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
        profile.log_to_file = false;
        NullBackend backend;
        undek::script::Vm vm(program, profile, backend, false, nullptr);
        vm.run();
        check(backend.moves.size() == 2 && backend.moves[0] == "120,240" &&
                  backend.moves[1] == "30,40",
              "MoveTo accepts a point and a stored variable");
        check(backend.wheels.size() == 1 && backend.wheels[0] == -120,
              "Scroll sends the wheel amount to the backend");
    }

    // Profiles survive a save/load round-trip
    {
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        p.delay_sigma_ms = 21.5;
        p.steps_per_100px = 33;
        p.miss_probability = 0.02;
        p.session_drift_enabled = false;
        p.require_window = "Notepad";
        p.input_backend = "interception";

        const std::string path = "test_profile_roundtrip.json";
        check(p.saveToFile(path), "profile saves to disk");

        undek::HumanizationProfile loaded = undek::HumanizationProfile::defaults();
        check(undek::HumanizationProfile::loadFromFile(path, loaded), "saved profile loads back");
        check(loaded.delay_sigma_ms == 21.5 && loaded.steps_per_100px == 33 &&
                  loaded.miss_probability == 0.02 && !loaded.session_drift_enabled &&
                  loaded.require_window == "Notepad" && loaded.input_backend == "interception",
              "profile round-trips its values");
        std::remove(path.c_str());
    }

    // Bad values are rejected when the effect is in use
    {
        auto rejected = [](undek::HumanizationProfile p) {
            std::string err;
            return !p.validate(err) && !err.empty();
        };
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();

        p = undek::HumanizationProfile::defaults();
        p.delay_min_ms = 200.0;
        p.delay_max_ms = 100.0;
        check(rejected(p), "rejects an inverted delay range");

        p = undek::HumanizationProfile::defaults();
        p.steps_per_100px = 0;
        check(rejected(p), "rejects zero steps per 100px");

        p = undek::HumanizationProfile::defaults();
        p.overshoot_probability = 1.4;
        check(rejected(p), "rejects a probability above 1");

        p = undek::HumanizationProfile::defaults();
        p.key_overlap_min_ms = 40.0;
        p.key_overlap_max_ms = 10.0;
        check(rejected(p), "rejects an inverted key overlap range");

        p = undek::HumanizationProfile::defaults();
        p.fatigue_sigma_widen_max = 0.5;
        check(rejected(p), "rejects a fatigue widen factor below 1");

        p = undek::HumanizationProfile::defaults();
        p.max_loop_iterations = 0;
        check(rejected(p), "rejects a zero loop cap");

        p = undek::HumanizationProfile::defaults();
        p.random_seed = -1;
        check(rejected(p), "rejects a negative seed");
    }

    // Bindings keep their entries and window rules
    {
        undek::bindings::BindingConfig cfg;
        cfg.default_profile = "profiles/dev.json";
        cfg.bindings.push_back({"Mouse4", "macros/one.undek", "profiles/dev.json", false});
        cfg.window_profiles.push_back({"Minecraft", "profiles/game.json"});

        const std::string path = "test_bindings_roundtrip.json";
        check(cfg.saveToFile(path), "bindings save to disk");

        undek::bindings::BindingConfig loaded = undek::bindings::BindingConfig::defaults();
        check(undek::bindings::BindingConfig::loadFromFile(path, loaded), "bindings load back");
        check(loaded.bindings.size() == 1 && loaded.bindings[0].hotkey == "Mouse4" &&
                  loaded.bindings[0].script == "macros/one.undek" && !loaded.bindings[0].enabled,
              "binding entries round-trip");
        check(loaded.bindings.size() == 1 && loaded.bindings[0].profile == "profiles/dev.json",
              "binding profile filled in from the default");
        check(loaded.window_profiles.size() == 1 &&
                  loaded.window_profiles[0].window_title == "Minecraft" &&
                  loaded.window_profiles[0].profile == "profiles/game.json",
              "window profile rules round-trip");
        std::remove(path.c_str());
    }

    // A silly comparison size can't explode the path
    {
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        p.steps_per_100px = 100000;
        undek::RandomizationEngine rng(p, 7);
        undek::PathGenerator paths(p, rng);
        const auto steps = paths.generatePath({0, 0}, {60000, 60000});
        check(steps.size() >= 2 && steps.size() <= 2010,
              "path density is capped for very long moves");
    }

    // Recording takes split into editable steps, with left drags grouped
    {
        const std::vector<std::string> lines = {
            "Wait(120ms, humanize=true)", "MoveTo(Point(100, 100))",
            "Wait(8ms, humanize=true)",   "MoveTo(Point(140, 120))",
            "MouseDown(\"left\")",        "Wait(15ms, humanize=true)",
            "MoveTo(Point(200, 200))",    "MouseUp(\"left\")",
            "Wait(40ms, humanize=true)",  "KeyDown(\"e\")",
        };
        auto steps = undek::editor::parseRecordedSteps(lines);
        check(steps.size() == 6, "recording splits into one step per action");
        check(steps[0].wait_ms == 120 && steps[0].text == "MoveTo(Point(100, 100))",
              "each step keeps the wait recorded before it");
        check(steps[2].text == "MouseDown(\"left\")" && steps[2].drag_group >= 0,
              "left press with movement starts a drag group");
        check(steps[4].text == "MouseUp(\"left\")" && steps[4].drag_group == steps[2].drag_group,
              "drag group runs to the release");

        steps[3].enabled = false;
        const std::string out = undek::editor::formatRecordedSteps(steps);
        check(out.find("MoveTo(Point(200, 200))") == std::string::npos,
              "unticked steps are left out of the script");
        check(out.find("Wait(120ms, humanize=true)") != std::string::npos &&
                  out.find("KeyDown(\"e\")") != std::string::npos,
              "kept steps and their waits are written back out");
    }

    // Things the v0.7.0 review asked for.
    {
        std::string err;
        runDry("MouseDown(\"something\")", &err);
        check(err.find("unknown mouse button") != std::string::npos,
              "an unknown mouse button is an error, not a right click");

        err.clear();
        runDry("if PixelAt(1, 2) { Log(\"x\") }", &err);
        check(err.find("colour comparison") != std::string::npos, "a bare PixelAt is rejected");

        err.clear();
        runDry("if PixelAt(1, 2) = 0xFF0000 { Log(\"x\") }", &err);
        check(!err.empty(), "a single = is not a colour comparison");

        err.clear();
        runDry("if PixelAt(1, 2) == 0xFF0000 tolerance 400 { Log(\"x\") }", &err);
        check(err.find("tolerance") != std::string::npos, "out of range tolerance is reported");

        err.clear();
        runDry("Wait(1.2.3)", &err);
        check(err.find("malformed number") != std::string::npos,
              "a malformed number is a lexer error");

        err.clear();
        runDry("Wait(0x)", &err);
        check(err.find("malformed hex") != std::string::npos,
              "a hex literal without digits is a lexer error");
    }

    {
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        p.log_to_file = false;
        p.max_total_instructions = 3;
        undek::script::Lexer lexer("Log(\"a\") Log(\"b\") Log(\"c\") Log(\"d\")");
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        NullBackend backend;
        undek::script::Vm vm(program, p, backend, true);
        std::string err;
        try {
            vm.run();
        } catch (const std::exception& e) {
            err = e.what();
        }
        check(err.find("Instruction budget") != std::string::npos,
              "the instruction budget stops a runaway script");
    }

    {
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        undek::RandomizationEngine eng(p, 9);
        undek::PathGenerator gen(p, eng);
        NullBackend backend;
        undek::ActionScheduler scheduler(backend, eng, gen);
        scheduler.setDeadline(std::chrono::steady_clock::now() - std::chrono::milliseconds(1));
        bool gave_up = false;
        try {
            scheduler.sleepMs(200.0);
        } catch (const std::exception&) {
            gave_up = true;
        }
        check(gave_up, "a wait gives up once the runtime limit is reached");
    }

    // A humanized move plans a curve instead of going through the pixel guard,
    // so it has to be held to the same limit itself
    {
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        p.log_to_file = false;
        undek::RandomizationEngine eng(p, 3);
        undek::PathGenerator gen(p, eng);
        NullBackend backend;
        undek::ActionScheduler scheduler(backend, eng, gen);

        check(!scheduler.moveToHumanized(undek::Vec2(2000000.0, 0.0)),
              "a humanized move to nowhere is refused");
        check(backend.moves.empty(), "and the cursor is never sent there");

        // Far enough that rounding it into an int would be undefined, which is
        // what the guard is for.
        const std::vector<undek::PathStep> off_range = {{undek::Vec2(0.0, 5000000000.0), 0.0}};
        check(!scheduler.moveAlongPath(off_range), "a path step out of range stops the move");
        check(backend.moves.empty(), "without moving the cursor");

        const std::vector<undek::PathStep> edge = {{undek::Vec2(1000000.0, 0.0), 0.0}};
        check(scheduler.moveAlongPath(edge, false), "the limit itself is allowed");
        check(!backend.moves.empty(), "and does move the cursor");

        check(scheduler.clickAt(undek::Vec2(2000000.0, 0.0)) ==
                  undek::ActionScheduler::ClickOutcome::TargetOutOfRange,
              "a click that would land out of range reports why it was skipped");
    }

    {
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        p.log_to_file = false;
        NullBackend backend;
        auto runScript = [&](const std::string& src) {
            undek::script::Lexer lexer(src);
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::script::Vm vm(program, p, backend, false);
            try {
                vm.run();
            } catch (const std::exception& ex) {
                return std::string(ex.what());
            }
            return std::string();
        };

        check(runScript("MoveTo(Point(2000000, 0), humanize=true)").find("out of range") !=
                  std::string::npos,
              "a humanized move past the limit is refused like a plain one");
        check(backend.moves.empty(), "and nothing reaches the backend");
        check(runScript("Click(Point(0, 2500000), humanize=true)").find("out of range") !=
                  std::string::npos,
              "a humanized click past the limit is refused too");
        check(backend.events.empty(), "without clicking anywhere");
    }

    // Profile values that turn into integers on the way out need a ceiling
    {
        std::string err;
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        check(p.validate(err), "the defaults validate: " + err);

        p.steps_per_100px = 1000000;
        check(!p.validate(err) && err.find("steps_per_100px") != std::string::npos,
              "an absurd step density is refused");

        p = undek::HumanizationProfile::defaults();
        p.max_runtime_sec = 1e12;
        check(!p.validate(err) && err.find("max_runtime_sec") != std::string::npos,
              "a runtime past a day is refused");

        p = undek::HumanizationProfile::defaults();
        p.min_packet_delay_ms = std::numeric_limits<double>::quiet_NaN();
        check(!p.validate(err) && err.find("min_packet_delay_ms") != std::string::npos,
              "a NaN packet delay is refused instead of slipping past every comparison");

        p = undek::HumanizationProfile::defaults();
        p.path_fitts_ms = std::numeric_limits<double>::infinity();
        check(!p.validate(err) && err.find("path_fitts_ms") != std::string::npos,
              "an infinite path timing is refused");

        // Even one that got past validate can't produce a broken path: the
        // counts are clamped before they're rounded.
        undek::HumanizationProfile silly = undek::HumanizationProfile::defaults();
        silly.steps_per_100px = 1000000000;
        undek::RandomizationEngine gen_eng(silly, 1);
        undek::PathGenerator gen(silly, gen_eng);
        const auto path = gen.generatePath({0.0, 0.0}, {1000.0, 1000.0});
        check(!path.empty() && path.size() <= 2000, "a silly step density still makes a sane path");
        bool every_step_reachable = true;
        for (const auto& step : path)
            if (!undek::pixelInRange(step.position)) every_step_reachable = false;
        check(every_step_reachable, "and every step is somewhere it can go");
    }

    // Only one macro at a time, however it was started
    {
        undek::cancel::clear();
        check(undek::runs::tryBegin("first"), "the run slot starts free");
        check(!undek::runs::tryBegin("second"), "a second run is refused while one is active");
        check(undek::runs::owner() == "first", "the run slot remembers its owner");

        undek::runs::end();
        undek::cancel::request();
        check(undek::runs::tryBegin("second"), "the slot frees up after a run ends");
        check(!undek::cancel::requested(), "starting a run clears a stale cancel request");

        undek::runs::end();
        check(!undek::runs::active(), "the slot is free again after ending");
    }

    // The visual builder writes plain script lines
    {
        using undek::editor::BuilderAction;
        undek::editor::BuilderStep wait_step;
        wait_step.action = BuilderAction::Wait;
        wait_step.wait_ms = 250;
        check(undek::editor::formatBuilderStep(wait_step) == "Wait(250ms, humanize=true)",
              "builder writes a wait step");

        undek::editor::BuilderStep click_step;
        click_step.action = BuilderAction::Click;
        click_step.x = 640;
        click_step.y = 480;
        click_step.humanize = false;
        check(undek::editor::formatBuilderStep(click_step) ==
                  "Click(Point(640, 480), humanize=false)",
              "builder writes a click step");

        undek::editor::BuilderStep key_step;
        key_step.action = BuilderAction::KeyDown;
        key_step.text = "e";
        check(undek::editor::formatBuilderStep(key_step) == "KeyDown(\"e\")",
              "builder writes a key step");

        undek::editor::BuilderStep empty_key;
        empty_key.action = BuilderAction::PressKey;
        check(undek::editor::formatBuilderStep(empty_key).empty(),
              "a key step with no key writes nothing");

        undek::editor::BuilderStep scroll_step;
        scroll_step.action = BuilderAction::Scroll;
        scroll_step.amount = 240;
        check(undek::editor::formatBuilderStep(scroll_step) == "Scroll(240)",
              "builder writes a scroll step");

        undek::editor::BuilderStep loop;
        loop.action = BuilderAction::Loop;
        loop.loop_count = 3;
        loop.body.push_back(wait_step);
        loop.body.push_back(click_step);
        check(undek::editor::formatBuilderStep(loop) == "loop 3 {\n  Wait(250ms, humanize=true)\n  "
                                                        "Click(Point(640, 480), humanize=false)\n}",
              "a loop block indents its body");

        undek::editor::BuilderStep if_window;
        if_window.action = BuilderAction::IfWindow;
        if_window.window_title = "Notepad";
        if_window.body.push_back(key_step);
        check(undek::editor::formatBuilderStep(if_window) ==
                  "if WindowTitle(\"Notepad\") {\n  KeyDown(\"e\")\n}",
              "a window check wraps its body");

        undek::editor::BuilderStep if_pixel;
        if_pixel.action = BuilderAction::IfPixel;
        if_pixel.pixel_x = 10;
        if_pixel.pixel_y = 20;
        if_pixel.color = 0x00FF00;
        if_pixel.tolerance = 8;
        if_pixel.body.push_back(scroll_step);
        check(undek::editor::formatBuilderStep(if_pixel) ==
                  "if PixelAt(10, 20) == 0x00FF00 tolerance 8 {\n  Scroll(240)\n}",
              "a pixel check writes its colour and tolerance");

        undek::editor::BuilderStep empty_loop;
        empty_loop.action = BuilderAction::Loop;
        check(undek::editor::formatBuilderStep(empty_loop).empty(),
              "an empty block writes nothing");

        std::vector<undek::editor::BuilderStep> to_wrap = {wait_step, click_step, scroll_step};
        to_wrap[1].selected = true;
        check(undek::editor::wrapSelected(to_wrap, BuilderAction::Loop, {}, 4) &&
                  to_wrap.size() == 3 && to_wrap[1].action == BuilderAction::Loop &&
                  to_wrap[1].loop_count == 4 && to_wrap[1].body.size() == 1 &&
                  to_wrap[1].body[0].action == BuilderAction::Click &&
                  to_wrap[2].action == BuilderAction::Scroll,
              "wrapSelected moves ticked steps into a block where they were");

        check(!undek::editor::wrapSelected(to_wrap, BuilderAction::Loop),
              "wrapping nothing does nothing");

        check(undek::editor::builderStepSummary(loop) == "loop 3 {",
              "a block's summary is just its opening line");

        std::vector<undek::editor::BuilderStep> deep = {loop, scroll_step};
        deep[0].body[1].selected = true;
        const int wrapped_deep =
            undek::editor::wrapSelectedAnywhere(deep, BuilderAction::Loop, {}, 2);
        check(wrapped_deep == 1 && deep[0].body.size() == 2 &&
                  deep[0].body[0].action == BuilderAction::Wait &&
                  deep[0].body[1].action == BuilderAction::Loop &&
                  deep[0].body[1].loop_count == 2 && deep[0].body[1].body.size() == 1 &&
                  deep[0].body[1].body[0].action == BuilderAction::Click,
              "a ticked step inside a block is wrapped where it lives");
        check(undek::editor::wrapSelectedAnywhere(deep, BuilderAction::Loop) == 0,
              "wrapping nothing at any depth does nothing");

        std::vector<undek::editor::BuilderStep> nested = {wait_step, loop};
        check(undek::editor::builderTarget(nested, {}) == &nested,
              "an empty builder path lands on the top level");
        check(undek::editor::builderTarget(nested, {1}) == &nested[1].body,
              "a block index lands on its body");
        check(undek::editor::builderTarget(nested, {0}) == nullptr,
              "a path through a normal step goes nowhere");
        check(undek::editor::builderTarget(nested, {9}) == nullptr,
              "an out of range builder path goes nowhere");

        check(undek::editor::formatBuilderScript({}).empty(),
              "an empty builder produces no script");
        check(undek::editor::formatBuilderScript({wait_step, click_step}) ==
                  "Wait(250ms, humanize=true)\nClick(Point(640, 480), humanize=false)",
              "builder joins steps in order");
        check(undek::editor::formatBuilderScript({loop}) ==
                  "loop 3 {\n  Wait(250ms, humanize=true)\n"
                  "  Click(Point(640, 480), humanize=false)\n}",
              "blocks work as script steps too");
    }

    // Numbers and arithmetic inside Point(...)
    {
        auto runMoves = [](const std::string& src, std::vector<std::string>& moves,
                           std::string& err) {
            try {
                undek::script::Lexer lexer(src);
                undek::script::Parser parser(lexer.tokenize());
                const auto program = parser.parse();
                undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
                p.log_to_file = false;
                NullBackend backend;
                undek::script::Vm vm(program, p, backend, false);
                vm.run();
                moves = backend.moves;
            } catch (const std::exception& e) {
                err = e.what();
            }
        };

        std::vector<std::string> moves;
        std::string err;
        runMoves("var x = 500\nvar y = 300\nvar spot = Point(x + 20, y - 10)\nMoveTo(spot)", moves,
                 err);
        check(err.empty() && moves.size() == 1 && moves[0] == "520,290",
              "numeric variables work inside Point(...)");

        moves.clear();
        err.clear();
        runMoves("var n = 2 + 3 * 4\nMoveTo(Point(n, 0))", moves, err);
        check(err.empty() && moves.size() == 1 && moves[0] == "14,0",
              "multiplication binds tighter than addition");

        moves.clear();
        err.clear();
        runMoves("var n = (2 + 3) * 4\nMoveTo(Point(n, -5))", moves, err);
        check(err.empty() && moves.size() == 1 && moves[0] == "20,-5",
              "parentheses and unary minus work in Point(...)");

        moves.clear();
        err.clear();
        runMoves("MoveTo(Point(missing + 1, 0))", moves, err);
        check(err.find("Unknown variable") != std::string::npos,
              "an unknown variable in Point(...) is an error");

        moves.clear();
        err.clear();
        runMoves("MoveTo(Point(1 / 0, 0))", moves, err);
        check(err.find("Division by zero") != std::string::npos, "dividing by zero is reported");
    }

    // With humanize=false a click lands on the point, noise settings or not
    {
        auto clickWithNoise = [](unsigned seed, std::vector<std::string>& moves) {
            undek::script::Lexer lexer("Click(Point(640, 480), humanize=false)");
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
            p.log_to_file = false;
            p.noise_enabled = true;
            p.noise_interval_actions = 1;
            p.noise_max_pixels = 25;
            p.random_seed = static_cast<int>(seed);
            NullBackend backend;
            undek::script::Vm vm(program, p, backend, false);
            vm.run();
            moves = backend.moves;
        };

        std::vector<std::string> moves;
        bool all_exact = true;
        for (unsigned seed = 1; seed <= 8; ++seed) {
            moves.clear();
            clickWithNoise(seed, moves);
            all_exact = all_exact && moves.size() == 1 && moves[0] == "640,480";
        }
        check(all_exact, "humanize=false ignores the profile's cursor noise");
    }

    // The same expressions work in Wait, Scroll and loop counts
    {
        undek::script::Lexer lexer("var d = 40\nvar n = 2\nloop n + 1 { Log(\"x\") }\n"
                                   "Scroll(-(n * 60))\nWait(d, humanize=false)");
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        p.log_to_file = false;
        NullBackend backend;
        std::vector<std::string> logs;
        undek::script::Vm vm(program, p, backend, false,
                             [&](const std::string& m) { logs.push_back(m); });
        vm.run();

        size_t iterations = 0;
        for (const auto& l : logs) {
            if (l.find("[log] x") != std::string::npos) ++iterations;
        }
        check(iterations == 3, "loop takes an expression as its count");
        check(backend.wheels.size() == 1 && backend.wheels[0] == -120,
              "Scroll takes an expression");
    }

    // Bindings follow a macro when it's renamed or deleted
    {
        undek::bindings::BindingConfig sync = undek::bindings::BindingConfig::defaults();
        undek::bindings::Binding one;
        one.hotkey = "Mouse4";
        one.script = "macros\\one.undek";
        undek::bindings::Binding two;
        two.hotkey = "F8";
        two.script = "macros/two.undek";
        sync.bindings = {one, two};

        check(sync.renameScript("macros/one.undek", "macros/renamed.undek") == 1 &&
                  sync.bindings[0].script == "macros/renamed.undek",
              "renaming a macro updates the binding that used it");
        check(sync.forgetScript("macros/two.undek") == 1 && sync.bindings.size() == 1,
              "deleting a macro removes its bindings");
        check(sync.renameScript("macros/other.undek", "x") == 0,
              "renaming an unbound macro touches nothing");
        check(sync.forgetScript("/somewhere/else/two.undek") == 0,
              "deleting a different file with the same name touches nothing");
    }

    // Raw scancodes, which is what the recorder writes for unnamed keys
    {
        undek::ScanKey scan{};
        check(undek::parseScanKey("scan:0x1e", scan) && scan.code == 0x1E && !scan.extended,
              "a plain scan code parses");
        check(undek::parseScanKey("scan_e:0x4b", scan) && scan.code == 0x4B && scan.extended,
              "an E0 scan code parses as extended");
        check(!undek::parseScanKey("scan:zz", scan), "garbage scan codes are rejected");
        check(!undek::parseScanKey("e", scan), "normal key names are not scan codes");
        check(!undek::parseScanKey("scan:0x", scan), "a scan code needs digits");
        check(undek::formatScanKey(0x1E, false) == "scan:0x1e",
              "scan codes round-trip through formatScanKey");
        undek::ScanKey again{};
        check(undek::parseScanKey(undek::formatScanKey(0x4B, true), again) && again.extended &&
                  again.code == 0x4B,
              "formatted extended codes parse back the same way");
    }

    // Config files go through a temp file and are type-checked on load
    {
        const std::string path = "test_profile_atomic.json";
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        check(p.saveToFile(path), "profile saves to disk");
        check(!std::filesystem::exists(path + ".tmp"), "no temp file is left behind after saving");
        std::remove(path.c_str());

        {
            std::ofstream out(path);
            out << "{\"delay_sigma_ms\": \"fast\"}";
        }
        undek::HumanizationProfile loaded = undek::HumanizationProfile::defaults();
        check(!undek::HumanizationProfile::loadFromFile(path, loaded),
              "a profile with the wrong type is rejected");
        std::remove(path.c_str());

        // A rejected profile must not leave the live one half-updated
        {
            std::ofstream out(path);
            out << "{\"delay_sigma_ms\": 50, \"steps_per_100px\": \"many\"}";
        }
        undek::HumanizationProfile kept = undek::HumanizationProfile::defaults();
        kept.delay_sigma_ms = 33.0;
        check(!undek::HumanizationProfile::loadFromFile(path, kept),
              "a partly-broken profile is rejected");
        check(kept.delay_sigma_ms == 33.0,
              "a rejected profile leaves the previous values alone (got " +
                  std::to_string(kept.delay_sigma_ms) + ")");
        std::remove(path.c_str());

        // Integer settings are range checked before the conversion, not after
        {
            const char* bad[] = {
                "{\"steps_per_100px\": 24.5}",
                "{\"steps_per_100px\": 4294967296}",
                "{\"max_loop_iterations\": 2147483648}",
                "{\"random_seed\": -2147483649}",
            };
            for (const char* body : bad) {
                {
                    std::ofstream out(path);
                    out << body;
                }
                undek::HumanizationProfile rejected = undek::HumanizationProfile::defaults();
                check(!undek::HumanizationProfile::loadFromFile(path, rejected),
                      std::string("a fractional or out of range integer is rejected: ") + body);
            }
            std::remove(path.c_str());
        }

        // A file can load cleanly and still fail validation, which is the case
        // the editor has to check before it swaps the profile in.
        {
            std::ofstream out(path);
            out << "{\"bezier_curve_strength_min\": 40, \"bezier_curve_strength_max\": 10}";
        }
        undek::HumanizationProfile unvalidated = undek::HumanizationProfile::defaults();
        check(undek::HumanizationProfile::loadFromFile(path, unvalidated),
              "a profile with a broken range still loads");
        std::string perr;
        check(!unvalidated.validate(perr) && !perr.empty(),
              "but it does not validate, so the editor won't apply or save it");
        std::remove(path.c_str());

        // Writing over an existing config replaces it instead of appending
        undek::writeFileAtomically(path, "first");
        check(undek::writeFileAtomically(path, "second"), "an overwrite succeeds");
        {
            std::ifstream in(path);
            const std::string content((std::istreambuf_iterator<char>(in)),
                                      std::istreambuf_iterator<char>());
            check(content == "second",
                  "the overwritten file holds only the new content (got '" + content + "')");
        }
        std::remove(path.c_str());

        // Reloading bindings doesn't append a second copy of everything
        const std::string bindings_path = "test_bindings_reload.json";
        {
            std::ofstream out(bindings_path);
            out << "{\"stop_hotkey\":\"ctrl+shift+s\",\"bindings\":[{\"hotkey\":\"ctrl+1\","
                   "\"script\":\"a.undek\"}]}";
        }
        undek::bindings::BindingConfig config = undek::bindings::BindingConfig::defaults();
        check(undek::bindings::BindingConfig::loadFromFile(bindings_path, config),
              "bindings load from a file");
        const size_t first_load = config.bindings.size();
        check(first_load == 1, "the file's single binding is the only one");
        check(undek::bindings::BindingConfig::loadFromFile(bindings_path, config),
              "bindings load a second time");
        check(config.bindings.size() == first_load, "a reload doesn't duplicate bindings");
        check(!undek::bindings::BindingConfig::loadFromFile("no_such_bindings.json", config),
              "a missing bindings file is an error");
        check(config.bindings.size() == first_load,
              "a failed bindings load leaves the loaded ones alone");
        std::remove(bindings_path.c_str());
    }

    // Presets
    {
        std::string err;
        const undek::HumanizationProfile base = undek::HumanizationProfile::defaults();
        const undek::HumanizationProfile balanced =
            undek::HumanizationProfile::preset(undek::ProfilePreset::Balanced);
        check(balanced.delay_sigma_ms == base.delay_sigma_ms &&
                  balanced.steps_per_100px == base.steps_per_100px &&
                  balanced.micro_stall_chance == base.micro_stall_chance &&
                  balanced.hold_scale_max == base.hold_scale_max,
              "balanced preset is the built-in defaults");
        check(balanced.miss_input_enabled, "the defaults drop the odd input");

        // A first run loads the packed profile rather than the struct, so the
        // two have to say the same thing.
        undek::HumanizationProfile shipped;
        const bool shipped_loaded = undek::HumanizationProfile::loadFromFile(
            std::string(UNDEK_PROFILES_DIR) + "/default.json", shipped);
        check(shipped_loaded, "the packed default profile loads");
        check(shipped.miss_input_enabled == base.miss_input_enabled,
              "the packed profile agrees with the code about missed inputs");
        check(std::fabs(shipped.miss_probability - base.miss_probability) < 1e-9,
              "and about how often they happen");

        undek::HumanizationProfile off =
            undek::HumanizationProfile::preset(undek::ProfilePreset::Off);
        check(!off.session_drift_enabled && !off.miss_input_enabled && !off.fatigue_focus_enabled &&
                  !off.micro_stall_enabled && !off.hold_variability_enabled &&
                  !off.key_overlap_enabled && !off.noise_enabled && off.delay_sigma_ms == 0.0 &&
                  off.overshoot_probability == 0.0,
              "off preset turns humanization extras off");
        check(off.validate(err), "off preset validates");

        undek::HumanizationProfile safe =
            undek::HumanizationProfile::preset(undek::ProfilePreset::Safe);
        undek::HumanizationProfile hot =
            undek::HumanizationProfile::preset(undek::ProfilePreset::Aggressive);
        check(safe.validate(err), "safe preset validates");
        check(hot.validate(err), "aggressive preset validates");
        check(safe.delay_sigma_ms < base.delay_sigma_ms && hot.delay_sigma_ms > base.delay_sigma_ms,
              "presets spread delays differently");
        check(safe.overshoot_probability < hot.overshoot_probability,
              "presets differ in overshoot");

        undek::HumanizationProfile kept = base;
        kept.max_runtime_sec = 90.0;
        kept.max_loop_iterations = 77;
        kept.require_window = "Game";
        kept.input_backend = "interception";
        kept.log_to_file = false;
        kept.applyPreset(undek::ProfilePreset::Aggressive);
        check(kept.delay_sigma_ms == hot.delay_sigma_ms, "applyPreset copies the preset values");
        check(kept.max_runtime_sec == 90.0 && kept.max_loop_iterations == 77 &&
                  kept.require_window == "Game" && kept.input_backend == "interception" &&
                  !kept.log_to_file,
              "applyPreset leaves limits, window and backend alone");
        check(std::string(undek::HumanizationProfile::presetName(undek::ProfilePreset::Safe)) ==
                  "Safe",
              "presets have names for the UI");
    }

    // Delay diagnostics
    {
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        const undek::DelayStats s = undek::sampleDelays(p, 40.0, 400);
        check(s.samples == 400, "delay sampling reports its sample count");
        check(s.min_ms <= s.p10_ms && s.p10_ms <= s.p50_ms && s.p50_ms <= s.p90_ms &&
                  s.p90_ms <= s.max_ms,
              "delay percentiles are ordered");
        check(s.min_ms >= 0.0 && s.max_ms <= p.delay_max_ms + 1e-9,
              "sampled delays stay inside the profile range");
        check(s.p90_ms - s.p10_ms > 0.0, "humanized waits actually spread out");

        undek::HumanizationProfile off =
            undek::HumanizationProfile::preset(undek::ProfilePreset::Off);
        const undek::DelayStats flat = undek::sampleDelays(off, 40.0, 200);
        check(flat.min_ms == flat.max_ms && flat.p50_ms == 40.0,
              "off preset produces no delay spread");
        check(undek::describeDelays(s).find("median") != std::string::npos,
              "delay summary is readable");
    }

    // ImageExists conditions
    {
        undek::ImageFrame screen;
        screen.width = 40;
        screen.height = 30;
        screen.pixels.assign(1200, 0x223344);
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 4; ++x) {
                screen.pixels[static_cast<size_t>(8 + y) * 40 + (11 + x)] = 0xFF8800;
            }
        }

        undek::ImageFrame patch;
        patch.width = 4;
        patch.height = 3;
        patch.pixels.assign(12, 0xFF8800);
        const std::string tpl = "test_ui_patch.bmp";
        std::string werr;
        check(undek::saveBmp(tpl, patch, werr), "image template saves: " + werr);

        auto grab = [&](undek::ImageFrame& out, std::string& err) {
            out = screen;
            err.clear();
            return true;
        };

        auto runWithGrab = [&](const std::string& src, std::string* error = nullptr) {
            std::vector<std::string> logs;
            try {
                undek::script::Lexer lexer(src);
                undek::script::Parser parser(lexer.tokenize());
                const auto program = parser.parse();
                undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
                profile.log_to_file = false;
                NullBackend backend;
                undek::script::Vm vm(program, profile, backend, true,
                                     [&](const std::string& m) { logs.push_back(m); });
                vm.setScreenGrabber(grab);
                vm.run();
            } catch (const std::exception& ex) {
                if (error) *error = ex.what();
            }
            return logs;
        };

        {
            const auto logs = runWithGrab("if ImageExists(\"test_ui_patch.bmp\") { Log(\"saw ui\") "
                                          "} else { Log(\"no ui\") }");
            check(count(logs, "[log] saw ui") == 1, "ImageExists finds the template on screen");
            check(count(logs, "found at (11, 8)") == 1, "the match reports where it was found");
        }

        {
            const auto logs = runWithGrab("if ImageExists(\"not_on_disk.bmp\") { Log(\"bogus\") }");
            check(count(logs, "[log] bogus") == 0, "a missing template is false, not a crash");
            check(count(logs, "template not found") == 1, "a missing template says so");
        }

        {
            // One patch pixel six off in green: rejected at tolerance 4, taken at 8
            undek::ImageFrame strict_frame = screen;
            strict_frame.pixels[static_cast<size_t>(8) * 40 + 11] = 0xFF8E03;
            auto strictGrab = [&](undek::ImageFrame& out, std::string& err) {
                out = strict_frame;
                err.clear();
                return true;
            };
            auto runStrict = [&](const std::string& src) {
                std::vector<std::string> logs;
                undek::script::Lexer lexer(src);
                undek::script::Parser parser(lexer.tokenize());
                const auto program = parser.parse();
                undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
                profile.log_to_file = false;
                NullBackend backend;
                undek::script::Vm vm(program, profile, backend, true,
                                     [&](const std::string& m) { logs.push_back(m); });
                vm.setScreenGrabber(strictGrab);
                vm.run();
                return logs;
            };
            const auto tight = runStrict(
                "if ImageExists(\"test_ui_patch.bmp\", tolerance=4) { Log(\"matched\") }");
            check(count(tight, "[log] matched") == 0, "tolerance 4 rejects the faded patch");
            const auto loose = runStrict(
                "if ImageExists(\"test_ui_patch.bmp\", tolerance=8) { Log(\"matched\") }");
            check(count(loose, "[log] matched") == 1, "tolerance 8 accepts the faded patch");
        }

        {
            // Looking at the screen twice in a row shouldn't capture it twice
            int grabs = 0;
            auto counting = [&](undek::ImageFrame& out, std::string& err) {
                ++grabs;
                out = screen;
                err.clear();
                return true;
            };
            auto runCounting = [&](const std::string& src) {
                grabs = 0;
                undek::script::Lexer lexer(src);
                undek::script::Parser parser(lexer.tokenize());
                const auto program = parser.parse();
                undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
                profile.log_to_file = false;
                NullBackend backend;
                undek::script::Vm vm(program, profile, backend, false, [](const std::string&) {});
                vm.setScreenGrabber(counting);
                vm.run();
                return grabs;
            };

            const int back_to_back = runCounting("if ImageExists(\"test_ui_patch.bmp\") { }\n"
                                                 "if ImageExists(\"test_ui_patch.bmp\") { }");
            check(back_to_back == 1, "neighbouring checks share one capture (took " +
                                         std::to_string(back_to_back) + ")");

            const int after_wait = runCounting("if ImageExists(\"test_ui_patch.bmp\") { }\n"
                                               "Wait(40)\n"
                                               "if ImageExists(\"test_ui_patch.bmp\") { }");
            check(after_wait >= 2, "a wait means the next check looks again (took " +
                                       std::to_string(after_wait) + ")");

            const int in_loop =
                runCounting("loop 50 { if ImageExists(\"test_ui_patch.bmp\") { } }");
            check(in_loop < 50, "a tight loop can't capture once per iteration (took " +
                                    std::to_string(in_loop) + ")");
            check(in_loop >= 1, "and it still captures something");
        }

        {
            const auto inside = runWithGrab(
                "if ImageExists(\"test_ui_patch.bmp\", region=Rect(8, 5, 10, 10)) { Log(\"in\") } "
                "else { Log(\"out\") }");
            check(count(inside, "[log] in") == 1, "a region around the patch matches");

            const auto outside = runWithGrab(
                "if ImageExists(\"test_ui_patch.bmp\", region=Rect(0, 0, 6, 6)) { Log(\"in\") } "
                "else { Log(\"out\") }");
            check(count(outside, "[log] out") == 1, "a region that misses it doesn't match");

            std::string err;
            runWithGrab("if ImageExists(\"test_ui_patch.bmp\", region=Point(1, 2)) { }", &err);
            check(err.find("Rect") != std::string::npos,
                  "region wants a Rect, not a Point: " + err);

            err.clear();
            runWithGrab("if ImageExists(\"test_ui_patch.bmp\", region=Rect(1, 2, 0, 5)) { }", &err);
            check(err.find("width and height") != std::string::npos,
                  "a zero-width region is refused: " + err);

            err.clear();
            runWithGrab("var p = FindImage(\"test_ui_patch.bmp\", region=Rect(8, 5, 10, 10))\n"
                        "MoveTo(p)",
                        &err);
            check(err.empty(), "FindImage takes a region too: " + err);
        }

        {
            // FindImage hands back the centre of the match, ready for Click/MoveTo
            std::vector<std::string> logs;
            undek::script::Lexer lexer("var p = FindImage(\"test_ui_patch.bmp\") MoveTo(p)");
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            NullBackend backend;
            // dry_run = false so MoveTo actually reaches the (null) backend
            undek::script::Vm vm(program, profile, backend, false,
                                 [&](const std::string& m) { logs.push_back(m); });
            vm.setScreenGrabber(grab);
            vm.run();
            check(backend.moves.size() == 1 && backend.moves[0] == "13,9",
                  "FindImage gives the centre of the picture (got " +
                      (backend.moves.empty() ? std::string("nothing") : backend.moves[0]) + ")");
            check(count(logs, "from image") == 1, "FindImage logs where it landed");
        }

        {
            // A capture that starts left of the primary monitor still clicks the
            // right desktop point
            undek::ImageFrame shifted = screen;
            shifted.origin_x = -1920;
            shifted.origin_y = 0;
            auto shiftedGrab = [&](undek::ImageFrame& out, std::string& err) {
                out = shifted;
                err.clear();
                return true;
            };

            std::vector<std::string> logs;
            undek::script::Lexer lexer("var p = FindImage(\"test_ui_patch.bmp\") MoveTo(p)");
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            NullBackend backend;
            undek::script::Vm vm(program, profile, backend, false,
                                 [&](const std::string& m) { logs.push_back(m); });
            vm.setScreenGrabber(shiftedGrab);
            vm.run();
            check(backend.moves.size() == 1 && backend.moves[0] == "-1907,9",
                  "a capture with an origin maps to desktop coordinates (got " +
                      (backend.moves.empty() ? std::string("nothing") : backend.moves[0]) + ")");
        }

        {
            // A file that exists but isn't a picture says so, instead of looking
            // like a missing template
            {
                std::ofstream out("test_bad_template.bmp", std::ios::binary);
                out << "this is not a bitmap";
            }
            const auto logs =
                runWithGrab("if ImageExists(\"test_bad_template.bmp\") { Log(\"x\") }");
            check(count(logs, "invalid image") == 1, "a corrupt template is reported as invalid");
            check(count(logs, "template not found") == 0,
                  "a corrupt template isn't reported as missing");
            std::remove("test_bad_template.bmp");
        }

        {
            std::string err;
            runWithGrab("var p = FindImage(\"not_on_disk.bmp\")", &err);
            check(err.find("FindImage") != std::string::npos &&
                      err.find("not_on_disk.bmp") != std::string::npos,
                  "a missing FindImage target stops the run with the file name");
        }

        {
            // The template is cached, so repeating the check doesn't reread the file
            const auto logs =
                runWithGrab("loop 2 { if ImageExists(\"test_ui_patch.bmp\") { Log(\"tick\") } }");
            check(count(logs, "[log] tick") == 2, "the same template can be checked repeatedly");
        }

        std::remove(tpl.c_str());

        {
            bool threw = false;
            try {
                undek::script::Lexer lexer("if ImageExists(\"a.bmp\", tolerance=300) { }");
                undek::script::Parser parser(lexer.tokenize());
                parser.parse();
            } catch (const std::exception&) {
                threw = true;
            }
            check(threw, "an out-of-range image tolerance is a parse error");
        }
    }

    // Script functions registered from C++
    {
        undek::script::FunctionRegistry registry;
        std::string reg_error;
        int calls = 0;
        double last_number = 0;
        std::string last_text;

        check(registry.add(
                  "TestAdd",
                  [&](const std::vector<undek::script::ScriptValue>& args,
                      undek::script::ScriptValue& result, std::string&) {
                      ++calls;
                      double sum = 0;
                      for (const auto& a : args) {
                          if (a.kind == undek::script::ScriptValue::Kind::Number)
                              sum += a.number;
                          else
                              last_text = a.text;
                      }
                      last_number = args.empty() ? 0 : args[0].number;
                      result = undek::script::ScriptValue::num(sum);
                      return true;
                  },
                  reg_error),
              "a function registers: " + reg_error);

        check(!registry.add(
                  "TestAdd", [](const auto&, auto&, auto&) { return true; }, reg_error),
              "registering the same name twice is refused");
        check(!registry.add(
                  "2fast", [](const auto&, auto&, auto&) { return true; }, reg_error),
              "a name starting with a digit is refused");
        check(!registry.add(
                  "has space", [](const auto&, auto&, auto&) { return true; }, reg_error),
              "a name with a space is refused");
        check(!registry.add("Empty", undek::script::ScriptFunction{}, reg_error),
              "an empty function is refused");
        check(registry.names().size() == 1 && registry.names()[0] == "TestAdd",
              "registered names are listed");

        auto runCall = [&](const std::string& src, std::string* error = nullptr) {
            std::vector<std::string> logs;
            try {
                undek::script::Lexer lexer(src);
                undek::script::Parser parser(lexer.tokenize());
                const auto program = parser.parse();
                undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
                profile.log_to_file = false;
                NullBackend backend;
                undek::script::Vm vm(program, profile, backend, true,
                                     [&](const std::string& m) { logs.push_back(m); });
                vm.setFunctionRegistry(&registry);
                vm.run();
            } catch (const std::exception& ex) {
                if (error) *error = ex.what();
            }
            return logs;
        };

        {
            const auto logs = runCall("Call(\"TestAdd\", 2, 3.5, \"note\")");
            check(calls == 1, "Call runs the registered function once");
            check(last_number == 2.0, "numbers arrive in order");
            check(last_text == "note", "strings arrive as strings");
            check(count(logs, "[call] TestAdd -> 5.5") == 1, "the result is logged");
        }

        {
            // Numeric arguments can use variables and arithmetic
            registry.add(
                "TestDouble",
                [&](const std::vector<undek::script::ScriptValue>& args,
                    undek::script::ScriptValue& result, std::string&) {
                    result = undek::script::ScriptValue::num(args.at(0).number * 2.0);
                    return true;
                },
                reg_error);
            const auto logs = runCall("var n = 4 Call(\"TestDouble\", n + 1)");
            check(count(logs, "[call] TestDouble -> 10") == 1,
                  "Call arguments go through the expression evaluator");
        }

        {
            registry.add(
                "TestFail",
                [](const std::vector<undek::script::ScriptValue>&, undek::script::ScriptValue&,
                   std::string& error) {
                    error = "nope";
                    return false;
                },
                reg_error);
            const auto logs = runCall("Call(\"TestFail\") Log(\"still running\")");
            check(count(logs, "[call] TestFail failed: nope") == 1,
                  "a failing call explains itself");
            check(count(logs, "[log] still running") == 1,
                  "a failing call doesn't stop the script");
        }

        {
            std::string err;
            runCall("Call(\"NeverRegistered\")", &err);
            check(err.find("NeverRegistered") != std::string::npos,
                  "calling an unknown function is an error naming it");
        }

        {
            registry.add(
                "TestThrows",
                [](const std::vector<undek::script::ScriptValue>&, undek::script::ScriptValue&,
                   std::string&) -> bool { throw std::runtime_error("boom"); },
                reg_error);
            const auto logs = runCall("Call(\"TestThrows\") Log(\"still here\")");
            check(count(logs, "[call] TestThrows failed: boom") == 1,
                  "a function that throws is reported as a failed call");
            check(count(logs, "[log] still here") == 1, "and the script carries on after it");
        }

        {
            undek::script::FunctionInfo sized;
            sized.name = "TestSized";
            sized.arguments = {{"count", "number", "how many", false},
                               {"label", "text", "what to call it", true}};
            registry.add(
                sized,
                [](const std::vector<undek::script::ScriptValue>& args,
                   const undek::script::FunctionContext&, undek::script::ScriptValue& result,
                   std::string&) {
                    result = undek::script::ScriptValue::num(static_cast<double>(args.size()));
                    return true;
                },
                reg_error);

            const auto one = runCall("Call(\"TestSized\", 1)");
            check(count(one, "[call] TestSized -> 1") == 1, "an optional argument can be left out");
            const auto none = runCall("Call(\"TestSized\")");
            check(count(none, "TestSized takes 1 to 2 arguments, got 0") == 1,
                  "a missing required argument is refused");
            const auto wrong = runCall("Call(\"TestSized\", 1, 2)");
            check(count(wrong, "TestSized argument 2 has to be text") == 1,
                  "a wrong argument type is refused by name and position");

            undek::script::FunctionInfo copy;
            check(registry.findInfo("TestSized", copy) && copy.arguments.size() == 2,
                  "a declaration can be copied out, not pointed at");

            std::string err;
            check(registry.remove("TestSized", err), "a registration can be removed: " + err);
            check(!registry.remove("TestSized", err),
                  "removing it twice says there's nothing there");
            err.clear();
            runCall("Call(\"TestSized\", 1)", &err);
            check(err.find("TestSized") != std::string::npos,
                  "calling a removed function is the same as never registering it");
        }

        // A declaration has to be something the host can keep
        {
            std::string err;
            auto empty_body = [](const std::vector<undek::script::ScriptValue>&,
                                 const undek::script::FunctionContext&, undek::script::ScriptValue&,
                                 std::string&) {
                return true;
            };

            undek::script::FunctionInfo unknown_type;
            unknown_type.name = "TestUnknownType";
            unknown_type.arguments = {{"thing", "blob", "", false}};
            check(!registry.add(unknown_type, empty_body, err) &&
                      err.find("unknown type") != std::string::npos,
                  "an argument type the host can't check is refused at registration: " + err);

            undek::script::FunctionInfo late_required;
            late_required.name = "TestLateRequired";
            late_required.arguments = {{"first", "number", "", true},
                                       {"second", "number", "", false}};
            check(!registry.add(late_required, empty_body, err) &&
                      err.find("required argument after an optional one") != std::string::npos,
                  "a required argument after an optional one is refused: " + err);

            undek::script::FunctionInfo repeated;
            repeated.name = "TestRepeatedName";
            repeated.arguments = {{"a", "number", "", false}, {"a", "number", "", false}};
            check(!registry.add(repeated, empty_body, err) &&
                      err.find("repeats the argument name") != std::string::npos,
                  "two arguments can't share a name: " + err);

            undek::script::FunctionInfo odd_return;
            odd_return.name = "TestOddReturn";
            odd_return.returns = "blobby";
            check(!registry.add(odd_return, empty_body, err) &&
                      err.find("unknown return type") != std::string::npos,
                  "an unknown return type is refused: " + err);

            // A function that promises a number and hands back text is a bug in
            // the function, and the call fails instead of the value being used.
            undek::script::FunctionInfo liar;
            liar.name = "TestLiar";
            liar.returns = "number";
            check(registry.add(
                      liar,
                      [](const std::vector<undek::script::ScriptValue>&,
                         const undek::script::FunctionContext&, undek::script::ScriptValue& result,
                         std::string&) {
                          result = undek::script::ScriptValue::str("not a number");
                          return true;
                      },
                      err),
                  "a function can declare its return type: " + err);
            const auto lied = runCall("Call(\"TestLiar\") Log(\"after\")");
            check(count(lied, "declared number but returned text") == 1,
                  "the mismatch is reported as a failed call");
            check(count(lied, "[log] after") == 1, "and the script keeps going");

            undek::script::FunctionInfo copied;
            undek::script::ContextScriptFunction taken;
            check(registry.findCall("TestLiar", copied, taken) && copied.name == "TestLiar" &&
                      taken,
                  "one lookup hands back the function and what it declared");

            registry.remove("TestLiar", err);
        }

        // A function can describe itself and see the run's context
        {
            undek::script::FunctionInfo info;
            info.name = "TestContext";
            info.description = "reports what it can see";
            info.arguments.push_back({"amount", "number", "how much", false});
            info.arguments.push_back({"label", "text", "what to call it", true});
            info.returns = "text";
            info.version = "1";

            bool saw_running = false;
            bool saw_unlimited = false;
            bool saw_cancel_flag = false;
            check(registry.add(
                      info,
                      [&](const std::vector<undek::script::ScriptValue>& args,
                          const undek::script::FunctionContext& ctx,
                          undek::script::ScriptValue& result, std::string&) {
                          saw_running = ctx.elapsedMs() >= 0.0;
                          saw_unlimited = ctx.remainingMs() == -1.0;
                          saw_cancel_flag = !ctx.cancelled();
                          ctx.print("hello from the function");
                          result = undek::script::ScriptValue::str(
                              args.size() > 0 &&
                                      args[0].kind == undek::script::ScriptValue::Kind::Number
                                  ? "ok"
                                  : "bad");
                          return true;
                      },
                      reg_error),
                  "a function with metadata registers: " + reg_error);

            const auto* stored = registry.info("TestContext");
            check(stored && stored->description == "reports what it can see",
                  "the description is kept");
            check(stored && stored->arguments.size() == 2 && stored->arguments[1].name == "label" &&
                      stored->arguments[1].optional,
                  "argument metadata is kept");
            check(stored && stored->version == "1", "the version is kept");

            const auto list = registry.all();
            bool listed = false;
            for (const auto& f : list)
                listed = listed || f.name == "TestContext";
            check(listed, "registered functions are listed for a browser to read");

            const auto logs = runCall("Call(\"TestContext\", 3)");
            check(saw_running && saw_unlimited && saw_cancel_flag,
                  "the call context reports elapsed time, no limit and not cancelled");
            check(count(logs, "[call] hello from the function") == 1,
                  "a function's own log line lands in the Execution Log");
            check(count(logs, "[call] TestContext -> ok") == 1, "the text result is logged");
        }

        // What a function declares about its arguments is checked before it runs
        {
            undek::script::FunctionInfo info;
            info.name = "TestSignature";
            info.arguments.push_back({"count", "number", "how many", false});
            info.arguments.push_back({"label", "text", "what to call it", false});
            int runs = 0;
            check(registry.add(
                      info,
                      [&](const std::vector<undek::script::ScriptValue>&,
                          const undek::script::FunctionContext&, undek::script::ScriptValue& result,
                          std::string&) {
                          ++runs;
                          result = undek::script::ScriptValue::num(1);
                          return true;
                      },
                      reg_error),
                  "a function with a declared signature registers: " + reg_error);

            auto logs = runCall("Call(\"TestSignature\", 1)");
            check(runs == 0 && count(logs, "failed: TestSignature takes 2 arguments, got 1") == 1,
                  "too few arguments never reach the function");
            logs = runCall("Call(\"TestSignature\", 1, \"a\", 3)");
            check(runs == 0 && count(logs, "takes 2 arguments, got 3") == 1,
                  "too many arguments never reach the function");
            logs = runCall("Call(\"TestSignature\", \"a\", \"b\")");
            check(runs == 0 && count(logs, "has to be number") == 1,
                  "an argument of the wrong type never reaches the function");
            logs = runCall("Call(\"TestSignature\", 7, \"a\")");
            check(runs == 1 && count(logs, "TestSignature -> 1") == 1, "a matching call runs");
        }
    }

    // Input reliability: releasing has to win over throttling and stopping
    {
        auto owned = std::make_unique<NullBackend>();
        NullBackend* raw = owned.get();
        undek::ThrottledInputBackend throttled(std::move(owned), 400.0, 0.0);
        check(throttled.keyDown("a").ok, "a throttled keyDown still goes through");

        undek::cancel::request();
        const auto start = std::chrono::steady_clock::now();
        const auto up = throttled.keyUp("a");
        const double took_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        undek::cancel::clear();

        check(up.ok, "keyUp succeeds with a cancel request pending");
        check(took_ms < 150.0,
              "keyUp doesn't wait out the 400 ms gap (took " + std::to_string(took_ms) + " ms)");
        check(raw->events.size() == 2 && raw->events[1] == "KeyUp:a",
              "the release still reaches the backend");
    }

    // A throttled click still retries a release that didn't land
    {
        struct FlakyUpBackend : NullBackend {
            int failures_left = 1;
            undek::InputResult mouseUp(const std::string& b) override {
                if (b == "left" && failures_left > 0) {
                    --failures_left;
                    return undek::InputResult::fail("release refused");
                }
                return NullBackend::mouseUp(b);
            }
        };

        auto owned = std::make_unique<FlakyUpBackend>();
        FlakyUpBackend* raw = owned.get();
        undek::ThrottledInputBackend throttled(std::move(owned), 0.0, 0.0);
        check(throttled.clickLeft().ok, "a throttled click recovers from one failed release");
        check(raw->active_buttons == 0, "the retried release leaves no button held");
        check(raw->events.size() == 2 && raw->events[0] == "MouseDown:left" &&
                  raw->events[1] == "MouseUp:left",
              "the click pressed once and the retry released it");

        owned = std::make_unique<FlakyUpBackend>();
        raw = owned.get();
        raw->failures_left = 2;
        undek::ThrottledInputBackend hopeless(std::move(owned), 0.0, 0.0);
        check(!hopeless.clickLeft().ok, "a release that keeps failing is still reported");
    }

    // A release that fails or throws must not strand the other held keys
    {
        struct FlakyReleaseBackend : NullBackend {
            undek::InputResult keyUp(const std::string& k) override {
                if (k == "a") throw std::runtime_error("release refused");
                return NullBackend::keyUp(k);
            }
        };

        std::vector<std::string> logs;
        undek::script::Lexer lexer("KeyDown(\"a\") KeyDown(\"b\")");
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
        profile.log_to_file = false;
        profile.miss_input_enabled = false;
        FlakyReleaseBackend backend;
        undek::script::Vm vm(program, profile, backend, false,
                             [&](const std::string& m) { logs.push_back(m); });
        vm.run();

        check(count(logs, "[input] keyUp(a) threw: release refused") == 1,
              "a throwing release is reported");
        bool released_b = false;
        for (const auto& e : backend.events)
            released_b = released_b || e == "KeyUp:b";
        check(released_b, "the other held key is still released");
    }

    // A release that fails once is retried before anyone hears about it
    {
        struct OnceFlakyReleaseBackend : NullBackend {
            int failures_left = 1;
            undek::InputResult keyUp(const std::string& k) override {
                if (k == "a" && failures_left > 0) {
                    --failures_left;
                    return undek::InputResult::fail("release refused");
                }
                return NullBackend::keyUp(k);
            }
        };

        auto run = [](const std::string& src, NullBackend& backend, std::string* error = nullptr) {
            std::vector<std::string> logs;
            undek::script::Lexer lexer(src);
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            profile.miss_input_enabled = false;
            undek::script::Vm vm(program, profile, backend, false,
                                 [&](const std::string& m) { logs.push_back(m); });
            try {
                vm.run();
            } catch (const std::exception& ex) {
                if (!error) throw;
                *error = ex.what();
            }
            return logs;
        };

        {
            OnceFlakyReleaseBackend backend;
            const auto logs = run("KeyDown(\"a\") KeyUp(\"a\")", backend);
            check(backend.active_keys == 0, "a retried KeyUp leaves nothing held");
            check(count(logs, "keyUp failed") == 0, "a release that works on retry isn't reported");
        }

        {
            struct AlwaysFailsUp : NullBackend {
                undek::InputResult keyUp(const std::string&) override {
                    return undek::InputResult::fail("release refused");
                }
            };
            AlwaysFailsUp backend;
            std::string error;
            const auto logs = run("KeyDown(\"a\") KeyUp(\"a\")", backend, &error);
            check(count(logs, "retry failed") > 0,
                  "a release that keeps failing says the retry failed too");
            check(error.find("keyUp(a) failed") != std::string::npos,
                  "a release that keeps failing stops the macro (got '" + error + "')");
        }
    }

    // A PressKey's key is tracked while it is down, so a release the backend
    // refuses is still cleaned up
    {
        auto runPress = [](const std::string& policy, NullBackend& backend) {
            std::vector<std::string> logs;
            undek::script::Lexer lexer("PressKey(\"w\", hold_min=1, hold_max=1, humanize=false)");
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            profile.miss_input_enabled = false;
            profile.input_failure_policy = policy;
            undek::script::Vm vm(program, profile, backend, false,
                                 [&](const std::string& m) { logs.push_back(m); });
            try {
                vm.run();
            } catch (const std::exception&) {
            }
            return logs;
        };

        {
            struct OnceStubborn : NullBackend {
                int refusals = 1;
                undek::InputResult keyUp(const std::string& k) override {
                    if (k == "w" && refusals > 0) {
                        --refusals;
                        return undek::InputResult::fail("release refused");
                    }
                    return NullBackend::keyUp(k);
                }
            };
            OnceStubborn backend;
            const auto logs = runPress("abort", backend);
            check(backend.active_keys == 0,
                  "a PressKey whose release needed the retry leaves nothing held");
            check(count(logs, "keyUp(w) failed") == 0,
                  "the retry inside a PressKey isn't reported as a failure");
        }

        {
            struct TwiceStubborn : NullBackend {
                int refusals = 2;
                undek::InputResult keyUp(const std::string& k) override {
                    if (k == "w" && refusals > 0) {
                        --refusals;
                        return undek::InputResult::fail("release refused");
                    }
                    return NullBackend::keyUp(k);
                }
            };
            TwiceStubborn backend;
            const auto logs = runPress("abort", backend);
            check(count(logs, "keyUp(w) failed") == 1,
                  "a PressKey release that keeps failing stops the macro");
            check(backend.active_keys == 0,
                  "cleanup releases a PressKey the press itself couldn't");
        }

        {
            struct RefusedPress : NullBackend {
                undek::InputResult keyDown(const std::string&) override {
                    return undek::InputResult::fail("blocked by the desktop");
                }
            };
            RefusedPress backend;
            const auto logs = runPress("abort", backend);
            check(count(logs, "keyDown(w) failed") == 1,
                  "a PressKey the desktop refused is reported");
            int releases = 0;
            for (const auto& e : backend.events)
                releases += (e == "KeyUp:w") ? 1 : 0;
            check(releases == 1, "a refused PressKey is let go of once, right away");
        }

        {
            // The backend refused the press but left the key down anyway.
            // Releasing it there and then has to stop cleanup from letting go
            // of it a second time.
            struct HalfLandedPress : NullBackend {
                undek::InputResult keyDown(const std::string& k) override {
                    ++active_keys;
                    events.push_back("KeyDown:" + k);
                    return undek::InputResult::fail("timed out");
                }
            };
            HalfLandedPress backend;
            runPress("abort", backend);
            int releases = 0;
            for (const auto& e : backend.events)
                releases += (e == "KeyUp:w") ? 1 : 0;
            check(releases == 1, "a press that landed despite the refusal isn't released twice");
            check(backend.active_keys == 0, "the key doesn't stay held after a refused press");
        }

        {
            struct CancelledPress : NullBackend {
                undek::InputResult keyDown(const std::string& k) override {
                    const undek::InputResult result = NullBackend::keyDown(k);
                    // Stop requested while the key is down, so the hold itself
                    // is what throws.
                    undek::cancel::request();
                    return result;
                }
            };
            CancelledPress backend;
            runPress("abort", backend);
            undek::cancel::clear();
            check(backend.active_keys == 0, "a PressKey cancelled mid-hold still lets go");
        }
    }

    // Failures from humanized input reach the log
    {
        struct NoClickBackend : NullBackend {
            undek::InputResult clickLeft() override {
                return undek::InputResult::fail("blocked by the desktop");
            }
        };

        std::vector<std::string> logs;
        undek::script::Lexer lexer("Click(Point(5, 5), humanize=true)");
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
        profile.log_to_file = false;
        NoClickBackend backend;
        undek::script::Vm vm(program, profile, backend, false,
                             [&](const std::string& m) { logs.push_back(m); });
        std::string error;
        try {
            vm.run();
        } catch (const std::exception& ex) {
            error = ex.what();
        }
        check(count(logs, "[input] click failed: blocked by the desktop") == 1,
              "a humanized click reports a failed injection");
        check(error.find("click failed: blocked by the desktop") != std::string::npos,
              "a click Windows refused stops the macro (got '" + error + "')");
    }

    // A click that pressed but couldn't let go leaves the button to cleanup
    {
        struct DoomedClick : NullBackend {
            undek::InputResult clickLeft() override {
                ++active_buttons;
                events.push_back("ClickLeft");
                return undek::InputResult::uncertain("press landed, release refused");
            }
            undek::InputResult mouseUp(const std::string& b) override {
                --active_buttons;
                events.push_back("MouseUp:" + b);
                return {};
            }
        };

        auto runClick = [](const std::string& src, DoomedClick& backend,
                           std::vector<std::string>& logs) {
            undek::script::Lexer lexer(src);
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            profile.miss_input_enabled = false;
            undek::script::Vm vm(program, profile, backend, false,
                                 [&](const std::string& m) { logs.push_back(m); });
            std::string error;
            try {
                vm.run();
            } catch (const std::exception& ex) {
                error = ex.what();
            }
            return error;
        };

        for (const bool humanize : {false, true}) {
            const std::string src = humanize ? "Click(Point(5, 5), humanize=true)"
                                             : "Click(Point(5, 5), humanize=false)";
            DoomedClick backend;
            std::vector<std::string> logs;
            const std::string error = runClick(src, backend, logs);
            check(error.find("click failed") != std::string::npos,
                  "a click that couldn't let go still stops the macro (humanize=" +
                      std::to_string(humanize) + ")");
            int releases = 0;
            for (const auto& e : backend.events)
                releases += (e == "MouseUp:left") ? 1 : 0;
            check(releases == 1, "cleanup releases a button the click couldn't let go (humanize=" +
                                     std::to_string(humanize) + ")");
            check(backend.active_buttons == 0,
                  "nothing stays held after a click that couldn't let go (humanize=" +
                      std::to_string(humanize) + ")");
        }
    }

    // A press that overlaps a held key shares it instead of ending the hold
    {
        // A key is either down or it isn't, however many presses asked for it.
        struct KeyStateBackend : NullBackend {
            bool w_down = false;
            undek::InputResult keyDown(const std::string& k) override {
                if (k == "w") w_down = true;
                return NullBackend::keyDown(k);
            }
            undek::InputResult keyUp(const std::string& k) override {
                if (k == "w") w_down = false;
                return NullBackend::keyUp(k);
            }
        };

        auto runKeys = [](const std::string& src, KeyStateBackend& backend,
                          std::vector<std::string>& logs) {
            undek::script::Lexer lexer(src);
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            profile.miss_input_enabled = false;
            undek::script::Vm vm(program, profile, backend, false,
                                 [&](const std::string& m) { logs.push_back(m); });
            vm.run();
        };

        auto tally = [](const NullBackend& backend, int& downs, int& ups) {
            downs = 0;
            ups = 0;
            for (const auto& e : backend.events) {
                downs += (e == "KeyDown:w") ? 1 : 0;
                ups += (e == "KeyUp:w") ? 1 : 0;
            }
        };

        {
            KeyStateBackend backend;
            std::vector<std::string> logs;
            runKeys("KeyDown(\"w\") PressKey(\"w\", hold_min=1, hold_max=1, humanize=false)",
                    backend, logs);
            int downs = 0;
            int ups = 0;
            tally(backend, downs, ups);
            check(downs == 2, "the overlapping press still sends its own press");
            check(ups == 1, "a press on a held key doesn't let go of the hold");
            check(!backend.w_down, "the held key is still released when the run ends");
        }

        {
            // Two holds and two releases: the first release only gives up its
            // claim, so the key goes up once, when the second one arrives.
            KeyStateBackend backend;
            std::vector<std::string> logs;
            runKeys("KeyDown(\"w\") KeyDown(\"w\") KeyUp(\"w\") KeyUp(\"w\")", backend, logs);
            int downs = 0;
            int ups = 0;
            tally(backend, downs, ups);
            check(downs == 2, "both holds pressed the key");
            check(ups == 1, "only the last release lets go of the key");
            check(count(logs, "another statement still holds it") == 1,
                  "the log says why the release didn't happen");
            check(!backend.w_down, "cleanup releases the last hold");
        }
    }

    // Buttons are claimed the same way, so one release between two holds keeps
    // the button down
    {
        struct ButtonStateBackend : NullBackend {
            bool left_down = false;
            undek::InputResult mouseDown(const std::string& b) override {
                if (b == "left") left_down = true;
                return NullBackend::mouseDown(b);
            }
            undek::InputResult mouseUp(const std::string& b) override {
                if (b == "left") left_down = false;
                return NullBackend::mouseUp(b);
            }
        };

        ButtonStateBackend backend;
        std::vector<std::string> logs;
        undek::script::Lexer lexer(
            "MouseDown(\"left\") MouseDown(\"left\") MouseUp(\"left\") MouseUp(\"left\")");
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
        profile.log_to_file = false;
        profile.miss_input_enabled = false;
        undek::script::Vm vm(program, profile, backend, false,
                             [&](const std::string& m) { logs.push_back(m); });
        vm.run();

        int downs = 0;
        int ups = 0;
        for (const auto& e : backend.events) {
            downs += (e == "MouseDown:left") ? 1 : 0;
            ups += (e == "MouseUp:left") ? 1 : 0;
        }
        check(downs == 2, "both holds pressed the button");
        check(ups == 1, "only the last release lets go of the button");
        check(count(logs, "another statement still holds it") == 1,
              "the log says why the button release didn't happen");
        check(!backend.left_down, "the button ends up up when the last hold lets go");
    }

    // A key that is both confirmed held and possibly held is let go of once
    {
        struct RefusesSecondDown : NullBackend {
            int downs = 0;
            undek::InputResult keyDown(const std::string& k) override {
                if (++downs == 2) return undek::InputResult::fail("refused");
                return NullBackend::keyDown(k);
            }
        };
        struct RefusesSecondButton : NullBackend {
            int downs = 0;
            undek::InputResult mouseDown(const std::string& b) override {
                if (++downs == 2) return undek::InputResult::fail("refused");
                return NullBackend::mouseDown(b);
            }
        };

        auto run = [](const std::string& src, undek::IInputBackend& backend) {
            undek::script::Lexer lexer(src);
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            profile.miss_input_enabled = false;
            profile.input_failure_policy = "continue";
            undek::script::Vm vm(program, profile, backend, false, [](const std::string&) {});
            vm.run();
        };

        auto tally = [](const NullBackend& backend, const std::string& event) {
            int n = 0;
            for (const auto& e : backend.events)
                n += (e == event) ? 1 : 0;
            return n;
        };

        {
            RefusesSecondDown backend;
            run("KeyDown(\"w\") KeyDown(\"w\")", backend);
            check(tally(backend, "KeyUp:w") == 1,
                  "a key in both the held and the uncertain lists is released once");
        }
        {
            RefusesSecondButton backend;
            run("MouseDown(\"left\") MouseDown(\"left\")", backend);
            check(tally(backend, "MouseUp:left") == 1, "a button in both lists is released once");
        }
    }

    // A failed PressKey never gives up another statement's hold
    {
        struct RefusesPress : NullBackend {
            int downs = 0;
            undek::InputResult keyDown(const std::string& k) override {
                if (++downs == 2) return undek::InputResult::fail("refused");
                return NullBackend::keyDown(k);
            }
        };

        RefusesPress backend;
        undek::script::Lexer lexer(
            "KeyDown(\"w\") PressKey(\"w\", hold_min=1, hold_max=1, humanize=false) "
            "KeyUp(\"w\")");
        undek::script::Parser parser(lexer.tokenize());
        const auto program = parser.parse();
        undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
        profile.log_to_file = false;
        profile.miss_input_enabled = false;
        profile.input_failure_policy = "continue";
        undek::script::Vm vm(program, profile, backend, false, [](const std::string&) {});
        vm.run();

        int ups = 0;
        for (const auto& e : backend.events)
            ups += (e == "KeyUp:w") ? 1 : 0;
        check(ups == 1, "a refused PressKey leaves the other statement's hold alone");
        check(backend.active_keys == 0, "and the key is still released by its own KeyUp");
    }

    // A click won't end a button hold that belongs to another statement
    {
        for (const bool humanize : {false, true}) {
            NullBackend backend;
            std::vector<std::string> logs;
            const std::string src = "MouseDown(\"left\") Click(Point(5, 5), humanize=" +
                                    std::string(humanize ? "true" : "false") +
                                    ") MouseUp(\"left\")";
            undek::script::Lexer lexer(src);
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            profile.miss_input_enabled = false;
            undek::script::Vm vm(program, profile, backend, false,
                                 [&](const std::string& m) { logs.push_back(m); });
            vm.run();

            int clicks = 0;
            int downs = 0;
            int ups = 0;
            for (const auto& e : backend.events) {
                clicks += (e == "ClickLeft") ? 1 : 0;
                downs += (e == "MouseDown:left") ? 1 : 0;
                ups += (e == "MouseUp:left") ? 1 : 0;
            }
            const std::string how = humanize ? "humanized" : "plain";
            check(clicks == 0, "a " + how + " click waits while the left button is held");
            check(count(logs, "[click] skipped (the left button is held by another statement)") ==
                      1,
                  "and the log says why the " + how + " click didn't run");
            check(downs == 1 && ups == 1,
                  "the hold still presses and releases the button once (" + how + " click)");
        }
    }

    // The profile can't turn an accepted hold into one past the hold ceiling
    {
        undek::HumanizationProfile p = undek::HumanizationProfile::defaults();
        p.hold_scale_min = 1.0;
        p.hold_scale_max = 50.0;
        std::string err;
        check(!p.validate(err) && err.find("hold_scale_max") != std::string::npos,
              "a hold scale that would blow past the hold limit is refused");

        undek::HumanizationProfile q = undek::HumanizationProfile::defaults();
        q.hold_variability_enabled = true;
        q.hold_scale_min = 9.0;
        q.hold_scale_max = 9.0;
        q.hold_panic_chance = 1.0;
        q.hold_panic_mult_min = 9.0;
        q.hold_panic_mult_max = 9.0;
        undek::RandomizationEngine engine(q, 12345u);
        bool too_long = false;
        for (int i = 0; i < 200; ++i)
            too_long = too_long || engine.sampleHoldMs(600000.0, 600000.0, true) > 600000.0;
        check(!too_long, "humanization can't push a hold past the 600000 ms ceiling");
    }

    // A refused press stops the macro instead of letting it run as if it landed
    {
        struct DeadKeyBackend : NullBackend {
            undek::InputResult keyDown(const std::string&) override {
                return undek::InputResult::fail("blocked by the desktop");
            }
            undek::InputResult keyUp(const std::string& k) override {
                events.push_back("KeyUp:" + k);
                return {};
            }
        };

        auto run = [](const std::string& policy, DeadKeyBackend& backend,
                      std::vector<std::string>& logs) {
            undek::script::Lexer lexer(
                "KeyDown(\"w\") MoveTo(Point(4, 4), humanize=false) KeyUp(\"w\")");
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            profile.miss_input_enabled = false;
            profile.input_failure_policy = policy;
            undek::script::Vm vm(program, profile, backend, false,
                                 [&](const std::string& m) { logs.push_back(m); });
            std::string error;
            try {
                vm.run();
            } catch (const std::exception& ex) {
                error = ex.what();
            }
            return error;
        };

        {
            DeadKeyBackend backend;
            std::vector<std::string> logs;
            const std::string error = run("abort", backend, logs);
            check(error.find("keyDown(w) failed") != std::string::npos,
                  "a key that never went down stops the macro (got '" + error + "')");
            check(backend.moves.empty(), "the statements after a failed press don't run");
            bool released = false;
            for (const auto& e : backend.events)
                released = released || e == "KeyUp:w";
            check(released, "an uncertain press is still released on the way out");
            check(count(logs, "stopping the macro") == 1, "the log says the macro is stopping");
        }

        {
            DeadKeyBackend backend;
            std::vector<std::string> logs;
            const std::string error = run("continue", backend, logs);
            check(error.empty(), "a profile set to continue doesn't stop the macro");
            check(backend.moves.size() == 1, "the script carries on to the next statement");
            check(count(logs, "keyDown(w) failed") == 1, "the failure is still logged");
            check(count(logs, "stopping the macro") == 0, "nothing claims to be stopping");
        }
    }

    // A click never fires when the cursor didn't get there
    {
        struct NoHumanizedMoveBackend : NullBackend {
            undek::InputResult moveRelative(int, int) override {
                return undek::InputResult::fail("blocked by the desktop");
            }
        };
        struct NoMoveBackend : NullBackend {
            undek::InputResult moveAbsolute(int, int) override {
                return undek::InputResult::fail("blocked by the desktop");
            }
        };

        auto runClick = [](const std::string& src, NullBackend& backend, std::string* error) {
            std::vector<std::string> logs;
            undek::script::Lexer lexer(src);
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            profile.miss_input_enabled = false;
            undek::script::Vm vm(program, profile, backend, false,
                                 [&](const std::string& m) { logs.push_back(m); });
            try {
                vm.run();
            } catch (const std::exception& ex) {
                *error = ex.what();
            }
            return logs;
        };

        {
            NoHumanizedMoveBackend backend;
            std::string error;
            auto logs = runClick("Click(Point(40, 40), humanize=true)", backend, &error);
            bool clicked = false;
            for (const auto& e : backend.events)
                clicked = clicked || e == "ClickLeft";
            check(!clicked, "a blocked humanized move doesn't click anyway");
            check(error.find("move along path failed") != std::string::npos,
                  "a blocked humanized move stops the macro (got '" + error + "')");
            check(count(logs, "move along path failed") == 1, "the blocked move is reported");
        }

        {
            NoMoveBackend backend;
            std::string error;
            auto logs = runClick("Click(Point(40, 40), humanize=false)", backend, &error);
            bool clicked = false;
            for (const auto& e : backend.events)
                clicked = clicked || e == "ClickLeft";
            check(!clicked, "a blocked exact move doesn't click anyway");
            check(count(logs, "move to click target failed") == 1, "the blocked move is reported");
            check(error.find("move to click target failed") != std::string::npos,
                  "the skipped click stops the macro (got '" + error + "')");
        }
    }

    // Recordings must not leave anything held
    {
        using undek::editor::RecordedStep;
        auto step = [](const std::string& text, bool enabled = true) {
            RecordedStep s;
            s.text = text;
            s.enabled = enabled;
            return s;
        };

        check(undek::editor::checkRecordingBalance({step("KeyDown(\"w\")"), step("KeyUp(\"w\")")})
                  .empty(),
              "a balanced recording passes");
        check(undek::editor::checkRecordingBalance(
                  {step("MouseDown(\"right\")"), step("MouseUp(\"right\")")})
                  .empty(),
              "a balanced mouse recording passes");

        const std::string held_key = undek::editor::checkRecordingBalance({step("KeyDown(\"w\")")});
        check(!held_key.empty() && held_key.find("w") != std::string::npos,
              "a dangling KeyDown is reported with the key name (got '" + held_key + "')");

        const std::string stray_release =
            undek::editor::checkRecordingBalance({step("KeyUp(\"q\")")});
        check(!stray_release.empty() && stray_release.find("never pressed") != std::string::npos,
              "a KeyUp without a KeyDown is reported (got '" + stray_release + "')");

        const std::string held_button =
            undek::editor::checkRecordingBalance({step("MouseDown(\"left\")")});
        check(!held_button.empty() && held_button.find("left") != std::string::npos,
              "a dangling MouseDown names the button (got '" + held_button + "')");

        check(undek::editor::checkRecordingBalance({step("KeyDown(\"w\")", false)}).empty(),
              "an unticked step isn't counted towards balance");

        // Recorded lines turn into steps with their waits attached, and back
        const std::vector<std::string> lines = {
            "Wait(120ms, humanize=true)",
            "KeyDown(\"w\")",
            "Wait(40ms, humanize=true)",
            "KeyUp(\"w\")",
            "",
            "Wait(15ms, humanize=true)",
            "MoveTo(Point(10, 20))",
        };
        const auto steps = undek::editor::parseRecordedSteps(lines);
        check(steps.size() == 3,
              "empty lines don't become steps (got " + std::to_string(steps.size()) + ")");
        check(steps.size() == 3 && steps[0].wait_ms == 120 && steps[0].text == "KeyDown(\"w\")",
              "a wait belongs to the step after it");
        check(steps.size() == 3 && steps[1].wait_ms == 40 && steps[2].wait_ms == 15,
              "each step keeps its own wait");

        const std::string text = undek::editor::formatRecordedSteps(steps);
        check(text.find("Wait(120ms, humanize=true)\nKeyDown(\"w\")") != std::string::npos,
              "writing steps back puts the wait before its action (got '" + text + "')");

        auto kept = steps;
        kept[1].enabled = false; // the KeyUp
        const std::string dropped = undek::editor::formatRecordedSteps(kept);
        check(dropped.find("KeyUp") == std::string::npos,
              "an unticked step is left out of the generated script");
        check(!undek::editor::checkRecordingBalance(kept).empty(),
              "dropping only the release is caught as an unbalanced recording");

        // A left press with movement before the release is one drag, and a
        // press/release with nothing in between stays a plain click
        const auto drag = undek::editor::parseRecordedSteps(
            {"MouseDown(\"left\")", "MoveTo(Point(5, 5))", "MouseUp(\"left\")"});
        check(drag.size() == 3 && drag[0].drag_group == 0 && drag[1].drag_group == 0 &&
                  drag[2].drag_group == 0,
              "a press, a move and the release group into one drag");
        const auto click =
            undek::editor::parseRecordedSteps({"MouseDown(\"left\")", "MouseUp(\"left\")"});
        check(click.size() == 2 && click[0].drag_group == -1 && click[1].drag_group == -1,
              "a press and release with no movement stay a click");
    }

    // What the recorder does with what the hook sees, driven straight instead
    // of through a low-level hook.
    {
        undek::RecorderTape tape;
        tape.reset(0);
        tape.key(0x10, "shift", true, 0);
        tape.key(0xA0, "shift", true, 0);
        check(tape.count() == 1, "both Shift keys write one KeyDown");
        tape.key(0xA0, "shift", false, 100);
        check(tape.count() == 1, "releasing one Shift doesn't write a KeyUp");
        tape.key(0x10, "shift", false, 120);
        check(tape.count() == 3, "releasing the second one does, behind its wait");
        check(tape.actions()[1].rfind("Wait(", 0) == 0, "a gap in the take becomes a Wait");
        check(tape.actions()[2] == "KeyUp(\"shift\")", "and the KeyUp names the key once");
    }

    {
        // The other order: the key that wrote the KeyDown goes up first, while
        // its twin is still down. That used to close the modifier early.
        undek::RecorderTape tape;
        tape.reset(0);
        tape.key(0xA0, "shift", true, 0);
        tape.key(0xA1, "shift", true, 10);
        tape.key(0xA0, "shift", false, 20);
        check(count(tape.actions(), "KeyUp(\"shift\")") == 0,
              "releasing the recorded Shift first keeps the modifier down");
        tape.key(0xA1, "shift", false, 30);
        check(count(tape.actions(), "KeyUp(\"shift\")") == 1,
              "and the other one's release writes the single KeyUp");
        check(tape.count() == 3, "one KeyDown, a wait and the KeyUp");
    }

    {
        undek::RecorderTape tape;
        tape.reset(0);
        tape.key(0xA0, "shift", true, 0);
        tape.key(0xA1, "shift", true, 10);
        tape.setPaused(true);
        tape.key(0xA0, "shift", false, 20);
        tape.setPaused(false);
        tape.closeOut(true, 30);
        check(count(tape.actions(), "KeyUp(\"shift\")") == 0,
              "resuming doesn't close a modifier the other key still holds");
        tape.key(0xA1, "shift", false, 40);
        check(count(tape.actions(), "KeyUp(\"shift\")") == 1,
              "and its release still writes the KeyUp");
    }

    {
        undek::RecorderTape tape;
        tape.reset(0);
        tape.key(0xA0, "shift", true, 0);
        tape.key(0xA1, "shift", true, 10);
        tape.setPaused(true);
        tape.key(0xA1, "shift", false, 20);
        tape.key(0xA0, "shift", false, 30);
        tape.setPaused(false);
        tape.closeOut(true, 40);
        check(count(tape.actions(), "KeyUp(\"shift\")") == 1,
              "both going up while paused writes exactly one KeyUp");
    }

    {
        undek::RecorderTape tape;
        tape.reset(0);
        tape.key(0x41, "a", true, 0);
        tape.setPaused(true);
        tape.key(0x41, "a", false, 50);
        check(tape.count() == 1, "a release while paused isn't written into the take");
        tape.setPaused(false);
        tape.closeOut(true, 60);
        check(tape.actions().back() == "KeyUp(\"a\")",
              "resuming closes out the key that went up meanwhile");
    }

    {
        undek::RecorderTape tape;
        tape.reset(0);
        tape.key(0x41, "a", true, 0);
        tape.setPaused(true);
        tape.setPaused(false);
        tape.closeOut(true, 30);
        check(tape.count() == 1, "a key held through the whole pause stays held");
        tape.closeOut(false, 40);
        check(tape.actions().back() == "KeyUp(\"a\")", "stopping closes everything out");
    }

    {
        undek::RecorderTape tape;
        tape.reset(0);
        tape.button("left", true, 0);
        tape.setPaused(true);
        tape.button("left", false, 10);
        tape.setPaused(false);
        tape.closeOut(true, 20);
        check(count(tape.actions(), "MouseDown(\"left\")") == 1 &&
                  count(tape.actions(), "MouseUp(\"left\")") == 1,
              "a button works the same way as a key");

        undek::RecorderTape again;
        again.reset(0);
        again.button("left", true, 0);
        again.button("left", true, 10);
        check(count(again.actions(), "MouseDown(\"left\")") == 1,
              "a repeated press doesn't write a second MouseDown");
    }

    {
        undek::RecorderTape tape;
        tape.reset(0);
        tape.move(100, 100, 0);
        tape.move(105, 103, 30);
        check(tape.count() == 1, "a few pixels of jitter aren't worth a MoveTo");
        tape.move(300, 300, 200);
        check(tape.actions().back() == "MoveTo(Point(300, 300))", "a real move is kept");
        check(tape.count() == 3, "and the time it took is written as a Wait");

        tape.setPaused(true);
        tape.move(800, 800, 240);
        tape.wheel(120, 250);
        check(tape.count() == 3, "nothing lands in the take while paused");
        tape.setPaused(false);
        tape.wheel(120, 300);
        check(tape.actions().back() == "Scroll(120)", "a wheel notch is one Scroll");
        tape.wheel(0, 320);
        check(tape.actions().back() == "Scroll(120)", "a zero delta writes nothing");

        tape.reset(1000);
        check(tape.count() == 0, "starting a new take clears the old one");
    }

    {
        // The two ends of the coordinate range: the gap squared doesn't fit in
        // a long long, so this used to be signed overflow.
        undek::RecorderTape tape;
        tape.reset(0);
        tape.move(std::numeric_limits<int>::min(), 0, 0);
        tape.move(std::numeric_limits<int>::max(), 0, 1);
        check(tape.count() == 2, "moves at the ends of the coordinate range are both kept");
    }

#ifndef _WIN32
    // A recorder with no hooks to install says so instead of pretending
    {
        std::string err;
        check(!undek::InputRecorder::start(err) && err.find("Windows") != std::string::npos,
              "a recorder that can't hook explains why");
        check(!undek::InputRecorder::isRecording(), "and isn't left claiming to record");
        check(undek::InputRecorder::takeActions().empty(), "with nothing in the take");
    }
#endif

    // What the mouse watcher decides before it wakes anyone
    {
        using namespace undek::bindings;
        unsigned int vk = 0;
        check(MousePressQueue::buttonFor(mouse_message::kLeftDown, false, vk) &&
                  vk == mouse_key::kLeft,
              "a left press maps to the left button");
        check(MousePressQueue::buttonFor(mouse_message::kRightDblClk, false, vk) &&
                  vk == mouse_key::kRight,
              "the double-click spelling still counts as a press");
        check(MousePressQueue::buttonFor(mouse_message::kXButtonDblClk, true, vk) &&
                  vk == mouse_key::kX2,
              "the second side button is told apart from the first");
        check(!MousePressQueue::buttonFor(0x0200, false, vk), "a plain move isn't a press");

        MousePressQueue queue;
        queue.setButtons({{1, mouse_key::kX1, 0}, {2, mouse_key::kLeft, modifier_bit::kShift}});
        check(queue.match(mouse_key::kX1, 0) == 1,
              "an unmodified binding matches with nothing held");
        check(queue.match(mouse_key::kX1, modifier_bit::kShift) == 0,
              "and doesn't match while a modifier is down");
        check(queue.match(mouse_key::kLeft, modifier_bit::kShift) == 2,
              "a shift binding needs shift");
        check(queue.match(mouse_key::kLeft, 0) == 0, "and doesn't fire without it");
        check(queue.match(mouse_key::kMiddle, 0) == 0, "an unwatched button matches nothing");

        queue.push(2);
        check(queue.take() == std::vector<int>{2}, "a press comes back once");
        check(queue.size() == 0, "and taking it empties the queue");

        for (int i = 1; i <= 20; ++i)
            queue.push(i);
        const auto backlog = queue.take();
        check(backlog.size() == MousePressQueue::kBacklog, "the backlog is capped");
        check(backlog.front() == 5 && backlog.back() == 20,
              "the oldest presses are the ones dropped");

        queue.setButtons({{7, mouse_key::kX2, 0}});
        queue.clear();
        check(queue.buttons().empty() && queue.size() == 0, "clearing forgets the bindings too");
    }

    // Argument and string validation
    {
        auto parseText = [](const std::string& src) {
            undek::script::Lexer lexer(src);
            undek::script::Parser parser(lexer.tokenize());
            return parser.parse();
        };
        auto parseError = [&](const std::string& src) {
            try {
                parseText(src);
            } catch (const std::exception& ex) {
                return std::string(ex.what());
            }
            return std::string();
        };

        {
            // The builder escapes quotes and backslashes; the lexer has to undo it
            const auto program = parseText("KeyDown(\"a\\\"b\\\\c\")");
            const auto* kd =
                static_cast<const undek::script::KeyDownStmt*>(program.statements.front().get());
            check(kd->key == "a\"b\\c",
                  "quotes and backslashes round-trip (got '" + kd->key + "')");
        }

        {
            // Windows paths keep their backslashes
            const auto program = parseText("Log(\"C:\\temp\\ui.bmp\")");
            const auto* log =
                static_cast<const undek::script::LogStmt*>(program.statements.front().get());
            check(log->message == "C:\\temp\\ui.bmp",
                  "a path with backslashes is left alone (got '" + log->message + "')");
        }

        const std::string typo = parseError("Click(Point(1, 1), radious=3)");
        check(typo.find("unknown argument 'radious'") != std::string::npos,
              "a typo'd argument is an error (got '" + typo + "')");
        check(typo.find("line 1, column") != std::string::npos,
              "parse errors say where they are, not just which line (got '" + typo + "')");

        check(parseError("Click(Point(1, 1), radius=-2)").find("radius") != std::string::npos,
              "a negative radius is rejected");
        check(parseError("Click(Point(1, 1), box_w=-4)").find("box_w") != std::string::npos,
              "a negative box width is rejected");
        check(parseError("PressKey(\"a\", hold_min=-1)").find("hold_min") != std::string::npos,
              "a negative hold is rejected");
        check(parseError("PressKey(\"a\", hold_min=200, hold_max=50)").find("hold_max") !=
                  std::string::npos,
              "a hold range that runs backwards is rejected");
        check(parseError("PressKey(\"a\", hold_min=700000)").find("limit") != std::string::npos,
              "a hold past the limit is rejected");
        check(parseError("PressKey(\"a\", hold_max=700000)").find("limit") != std::string::npos,
              "a hold_max past the limit is rejected");
        check(parseError("Click(Point(1, 1), humanize=1)").find("true or false") !=
                  std::string::npos,
              "humanize has to be a bool");
        check(parseError("WaitUntil(WindowTitle(\"x\"), nonsense=5)").find("unknown argument") !=
                  std::string::npos,
              "WaitUntil rejects unknown arguments");
        check(parseError("WaitUntil(WindowTitle(\"x\"), timeout=-5)").find("timeout") !=
                  std::string::npos,
              "a negative WaitUntil timeout is rejected");

        // Numbers that don't fit, and settings that don't exist
        check(parseError("if PixelAt(1000000000000, 0) == 0xFFFFFF { }").find("out of range") !=
                  std::string::npos,
              "a pixel coordinate out of range is rejected");
        check(parseError("Scroll(1000000000000)").find("out of range") != std::string::npos,
              "a wheel amount out of range is rejected");
        check(parseError("profile default_sgma=12").find("unknown profile setting") !=
                  std::string::npos,
              "a typo'd profile setting is an error");

        {
            std::string err;
            // 1e24 squared again and again runs off the end of a double
            undek::script::Lexer lexer("var a = 1000000000000000000000000 "
                                       "var b = a * a "
                                       "var c = b * b "
                                       "var d = c * c "
                                       "var e = d * d");
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            NullBackend backend;
            try {
                undek::script::Vm vm(program, profile, backend, false, [](const std::string&) {});
                vm.run();
            } catch (const std::exception& ex) {
                err = ex.what();
            }
            check(err.find("out of range") != std::string::npos,
                  "an expression that overflows is a script error (got '" + err + "')");
        }

        {
            std::string err;
            undek::script::Lexer lexer(
                "MoveTo(Point(1000000000000000000000000, 0), humanize=false)");
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            NullBackend backend;
            try {
                undek::script::Vm vm(program, profile, backend, false, [](const std::string&) {});
                vm.run();
            } catch (const std::exception& ex) {
                err = ex.what();
            }
            check(err.find("out of range") != std::string::npos,
                  "a coordinate too far away is refused before the cast (got '" + err + "')");
        }

        {
            undek::HumanizationProfile backend_profile = undek::HumanizationProfile::defaults();
            backend_profile.input_backend = "nonsense";
            std::string err;
            check(!backend_profile.validate(err) && err.find("input_backend") != std::string::npos,
                  "an unknown input backend is refused");
            backend_profile.input_backend = "INTERCEPTION";
            check(backend_profile.validate(err), "a known input backend is accepted");
        }
    }

    // The macro library owns files the user can lose, so walk the whole cycle
    {
        const auto original_cwd = std::filesystem::current_path();
        const auto sandbox = original_cwd / "test_macro_library";
        std::filesystem::remove_all(sandbox);
        std::filesystem::create_directories(sandbox);
        std::filesystem::current_path(sandbox);
        undek::AppPaths::initialize();

        undek::editor::MacroLibrary lib;
        lib.refresh();
        check(lib.generation() > 0, "refresh bumps the generation");

        // The library also lists examples, and where its root lands depends on
        // the platform, so the checks go by name rather than by count.
        auto find = [](const undek::editor::MacroLibrary& library, const std::string& name) {
            for (const auto& entry : library.entries())
                if (entry.name == name) return true;
            return false;
        };
        const std::string macro_name = "Test_Lib_Marker";
        check(!find(lib, macro_name), "a fresh library has no macro by that name");

        const unsigned before_new = lib.generation();
        const std::string path = lib.createNew("Test Lib Marker");
        check(!path.empty() && std::filesystem::exists(undek::AppPaths::resolve(path)),
              "a new macro exists on disk");
        check(path.find(' ') == std::string::npos, "a space in the name becomes an underscore");
        check(find(lib, macro_name), "the new macro is listed under its file name");
        check(lib.generation() > before_new, "creating a macro bumps the generation");
        check(
            undek::script::readFile(undek::AppPaths::resolve(path)).find("profile default_sigma") !=
                std::string::npos,
            "a new macro starts from the template");

        const std::string second = lib.createNew("Test Lib Marker");
        check(second != path && std::filesystem::exists(undek::AppPaths::resolve(second)),
              "the same name twice doesn't overwrite the first macro");
        check(find(lib, macro_name + "_1"), "the second macro is listed next to the first");

        check(lib.save(path, "Log(\"hi\")\n"), "save writes through the library");
        check(undek::script::readFile(undek::AppPaths::resolve(path)) == "Log(\"hi\")\n",
              "the saved text comes back unchanged");

        const std::string copy = lib.duplicate(path, "Copy");
        check(!copy.empty() &&
                  undek::script::readFile(undek::AppPaths::resolve(copy)) == "Log(\"hi\")\n",
              "a duplicate carries the same text");

        const std::string renamed = lib.rename(copy, "Renamed");
        check(!renamed.empty() && std::filesystem::exists(undek::AppPaths::resolve(renamed)) &&
                  !std::filesystem::exists(undek::AppPaths::resolve(copy)),
              "rename moves the file instead of copying it");
        check(lib.rename(renamed, macro_name).empty(),
              "renaming onto an existing macro is refused");

        check(lib.deleteMacro(renamed), "delete removes a macro");
        check(!std::filesystem::exists(undek::AppPaths::resolve(renamed)),
              "the deleted file is gone from disk");
        check(!lib.deleteMacro(renamed), "deleting the same macro twice reports it as nothing");
        check(std::filesystem::exists(undek::AppPaths::resolve(path)),
              "deleting one macro leaves its neighbour alone");
        check(!find(lib, "Renamed"), "the list is current after a delete");

        check(lib.rename("macros/not_there.undek", "x").empty(),
              "renaming a missing macro fails without creating one");
        check(lib.duplicate("macros/not_there.undek", "x").empty(),
              "duplicating a missing macro fails without creating one");

        lib.deleteMacro(path);
        lib.deleteMacro(second);

        std::filesystem::current_path(original_cwd);
        std::filesystem::remove_all(sandbox);
    }

    // Settings survive a round trip, and rubbish in the file can't half-apply
    {
        const std::string path = "test_settings.json";
        std::remove(path.c_str());

        undek::AppSettings settings;
        settings.start_minimized = true;
        settings.minimize_to_tray = false;
        settings.show_hotkey = "Ctrl+F5";
        settings.toggle_sound = false;
        settings.toggle_sound_volume = 42.0;
        check(settings.save(path), "settings save");

        undek::AppSettings loaded;
        check(loaded.load(path), "settings load back");
        check(loaded.start_minimized && !loaded.minimize_to_tray,
              "switches come back as they went in");
        check(loaded.show_hotkey == "Ctrl+F5", "the hotkey text comes back unchanged");
        check(!loaded.toggle_sound && loaded.toggle_sound_volume == 42.0,
              "the sound settings come back");
        check(loaded.stealth_hotkey == "Ctrl+Shift+X",
              "a setting the file never mentioned keeps its default");

        auto writeSettings = [&](const std::string& text) {
            std::ofstream out(path, std::ios::trunc);
            out << text;
        };

        writeSettings("{\"toggle_sound_volume\": 900}");
        undek::AppSettings loud;
        check(loud.load(path) && loud.toggle_sound_volume == 100.0, "a volume past 100 is clamped");

        writeSettings("{\"toggle_sound_volume\": -20}");
        undek::AppSettings quiet;
        check(quiet.load(path) && quiet.toggle_sound_volume == 0.0, "a negative volume is clamped");

        writeSettings("{\"show_hotkey\": \"   \"}");
        undek::AppSettings blank;
        check(blank.load(path) && blank.show_hotkey == "Ctrl+Shift+H",
              "a blank hotkey keeps its default");

        writeSettings("not json at all");
        undek::AppSettings untouched;
        untouched.show_hotkey = "Ctrl+Alt+Z";
        check(!untouched.load(path) && untouched.show_hotkey == "Ctrl+Alt+Z",
              "a broken settings file changes nothing");

        std::remove(path.c_str());
    }

    // Which backend a profile actually ends up with
    {
        auto nameFor = [](const undek::HumanizationProfile& profile) {
            return undek::createInputBackend(profile).name;
        };

        undek::HumanizationProfile asked = undek::HumanizationProfile::defaults();
        asked.input_backend = "sendinput";
        asked.min_packet_delay_enabled = false;
        check(nameFor(asked) == "SendInput", "asking for sendinput gets sendinput");

        undek::HumanizationProfile auto_pick = undek::HumanizationProfile::defaults();
        auto_pick.input_backend = "auto";
        auto_pick.min_packet_delay_enabled = false;
        const std::string auto_name = nameFor(auto_pick);
        check(auto_name.find("SendInput") != std::string::npos,
              "auto falls back to sendinput when interception isn't there");
        check(auto_name.find("unavailable") != std::string::npos,
              "the fallback name says why it fell back");

        undek::HumanizationProfile spaced = undek::HumanizationProfile::defaults();
        spaced.input_backend = "sendinput";
        spaced.min_packet_delay_enabled = true;
        spaced.min_packet_delay_ms = 12.0;
        spaced.min_packet_delay_jitter_ms = 3.0;
        const std::string spaced_name = nameFor(spaced);
        check(spaced_name.find("packet spacing 12-15ms") != std::string::npos,
              "the packet spacing is part of the backend name (got '" + spaced_name + "')");
        check(spaced_name.find("SendInput") == 0,
              "and the name still starts with the backend it wraps");

        undek::HumanizationProfile no_spacing = spaced;
        no_spacing.min_packet_delay_enabled = false;
        check(nameFor(no_spacing).find("packet spacing") == std::string::npos,
              "switching the spacing off drops it from the name");

#ifndef _WIN32
        undek::InterceptionBackend interceptor;
        check(!interceptor.available(), "interception is unavailable off Windows");
        check(std::string(interceptor.statusName()) == "unsupported platform",
              "and says so instead of pretending");
        const undek::InputResult refused = interceptor.keyDown("a");
        check(!refused.ok && refused.error.find("only supported on Windows") != std::string::npos,
              "a press through the stub reports why it can't (got '" + refused.error + "')");
#endif
    }

    // The host a hotkey actually goes through
    {
        const std::string profile_path = "test_runner_profile.json";
        {
            std::ofstream out(profile_path, std::ios::trunc);
            out << "{\"log_to_file\": false}";
        }

        std::vector<std::string> logs;
        auto log_cb = [&](const std::string& m) {
            logs.push_back(m);
        };

        std::string err;
        int code = undek::ScriptRunner::runSource("", profile_path, true, &err, log_cb);
        check(code != 0 && err == "Script is empty", "an empty script is refused");

        err.clear();
        logs.clear();
        code = undek::ScriptRunner::runSource("Log(\"hi\")\n", profile_path, true, &err, log_cb);
        check(code == 0, "a dry run of a valid script succeeds: " + err);
        check(count(logs, "[input] backend: none (dry run)") == 1,
              "a dry run doesn't open a real input backend");
        check(count(logs, "[log] hi") == 1, "the script's own log comes back");

        err.clear();
        code = undek::ScriptRunner::runSource("loop {\n", profile_path, true, &err, nullptr);
        check(code != 0 && !err.empty(), "a broken script stops with a parse error");

        err.clear();
        code = undek::ScriptRunner::runSource("Log(\"hi\")\n", "no_such_profile.json", true, &err,
                                              nullptr);
        check(code == 0 && err.find("default profile") != std::string::npos,
              "a missing profile still runs and says it fell back");

        {
            const std::string bad_profile = "test_runner_bad_profile.json";
            {
                std::ofstream out(bad_profile, std::ios::trunc);
                out << "{\"delay_sigma_ms\": -5}";
            }
            err.clear();
            code =
                undek::ScriptRunner::runSource("Log(\"hi\")\n", bad_profile, true, &err, nullptr);
            check(code != 0 && err.find("Invalid profile") != std::string::npos,
                  "an invalid profile stops the run and names the field");
            std::remove(bad_profile.c_str());
        }

        check(undek::runs::tryBegin("test"), "the test can take the run slot");
        err.clear();
        code = undek::ScriptRunner::runSource("Log(\"hi\")\n", profile_path, true, &err, nullptr);
        check(code != 0 && err == "Another macro is already running",
              "a second run is refused while one holds the slot");
        undek::runs::end();

        check(undek::ScriptRunner::run("no_such_script.undek", profile_path, true) != 0,
              "running a script that isn't there reports a failure");

        std::remove(profile_path.c_str());
    }

    // The execution log rolls over instead of filling the disk
    {
        const auto original_cwd = std::filesystem::current_path();
        const auto dir = original_cwd / "test_file_log";
        std::filesystem::remove_all(dir);

        undek::FileLog::setEnabled(true);
        undek::FileLog::init(dir.string());
        undek::FileLog::write("first line");

        auto logFiles = [&] {
            std::vector<std::filesystem::path> out;
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
                out.push_back(entry.path());
            std::sort(out.begin(), out.end());
            return out;
        };

        check(logFiles().size() == 1, "one line makes one log file");
        if (!logFiles().empty()) {
            check(undek::script::readFile(logFiles().front().string()).find("first line") !=
                      std::string::npos,
                  "the line lands in the file");
        }

        const std::string filler(1000, 'x');
        for (int i = 0; i < 5000; ++i)
            undek::FileLog::write(filler);

        const auto after = logFiles();
        check(after.size() == 2, "past the cap the log rolls over into one older file");
        if (after.size() == 2) {
            check(after[1].extension() == ".1", "the older file keeps its date with a .1 suffix");
            check(undek::script::readFile(after[1].string()).find("first line") !=
                      std::string::npos,
                  "the rolled-over file keeps the earlier lines");
            check(std::filesystem::file_size(after[0]) <= 4u * 1024u * 1024u,
                  "the log being written stays under the cap");
        }

        undek::FileLog::setEnabled(false);
        const auto current = logFiles().front();
        const auto quiet_size = std::filesystem::file_size(current);
        undek::FileLog::write("while switched off");
        check(std::filesystem::file_size(current) == quiet_size, "a disabled log doesn't grow");
        check(undek::script::readFile(current.string()).find("while switched off") ==
                  std::string::npos,
              "and the line isn't in there either");

        undek::FileLog::init("");
        std::filesystem::remove_all(dir);
    }

    // The bundled examples have to keep working, not just read well
    {
        std::error_code ec;
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::directory_iterator(UNDEK_EXAMPLES_DIR, ec)) {
            if (entry.path().extension() == ".undek") files.push_back(entry.path().string());
        }
        std::sort(files.begin(), files.end());
        check(files.size() >= 3,
              "the examples folder is there (" + std::string(UNDEK_EXAMPLES_DIR) + ")");

        for (const auto& path : files) {
            std::ifstream in(path, std::ios::binary);
            std::stringstream buffer;
            buffer << in.rdbuf();
            const std::string name = std::filesystem::path(path).filename().string();

            std::string err;
            bool ran = false;
            try {
                undek::script::Lexer lexer(buffer.str());
                undek::script::Parser parser(lexer.tokenize());
                const auto program = parser.parse();
                check(!program.statements.empty(), name + " has something in it");
                undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
                profile.log_to_file = false;
                profile.max_runtime_sec = 5.0;
                NullBackend backend;
                undek::script::Vm vm(program, profile, backend, true);
                vm.run();
                ran = true;
            } catch (const std::exception& ex) {
                err = ex.what();
            }
            check(ran, name + " dry-runs to the end: " + err);
        }
    }

    // A hold can't sample down to a length the target sleeps through
    {
        auto runWait = [](const std::string& src, double min_press,
                          std::vector<std::string>& logs) {
            undek::script::Lexer lexer(src);
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            profile.miss_input_enabled = false;
            profile.delay_sigma_ms = 0.0;
            profile.delay_min_ms = 0.0;
            profile.hold_variability_enabled = false;
            profile.pause_probability = 0.0;
            profile.micro_stall_enabled = false;
            profile.min_press_ms = min_press;
            NullBackend backend;
            undek::script::Vm vm(program, profile, backend, false,
                                 [&](const std::string& m) { logs.push_back(m); });
            vm.run();
        };

        auto anyLog = [](const std::vector<std::string>& logs, const std::string& needle) {
            for (const auto& line : logs)
                if (line.find(needle) != std::string::npos) return true;
            return false;
        };

        std::vector<std::string> held;
        runWait("KeyDown(\"a\")\nWait(5ms, humanize=true)\nKeyUp(\"a\")", 30.0, held);
        check(anyLog(held, "[wait] 30.0 ms (press floor)"),
              "a 5 ms hold is raised to the press floor");
        check(!anyLog(held, "[wait] 5."), "the raised hold isn't also logged at its old time");

        std::vector<std::string> idle;
        runWait("Wait(5ms, humanize=true)", 30.0, idle);
        check(anyLog(idle, "[wait] 5.") && !anyLog(idle, "press floor"),
              "a wait with nothing held keeps the time the script asked for");

        std::vector<std::string> off;
        runWait("KeyDown(\"a\")\nWait(5ms, humanize=true)\nKeyUp(\"a\")", 0.0, off);
        check(anyLog(off, "[wait] 5.") && !anyLog(off, "press floor"),
              "the press floor can be switched off");

        std::vector<std::string> exact;
        runWait("MouseDown(\"x2\")\nWait(5ms, humanize=false)\nMouseUp(\"x2\")", 30.0, exact);
        check(anyLog(exact, "[wait] 5.") && !anyLog(exact, "press floor"),
              "an unhumanized hold keeps its time, floor or no floor");
    }

    // The visual builder only writes script text, so whatever it writes has to
    // come back through the real parser meaning the same thing.
    {
        auto parses = [](const std::string& src, std::string& err) {
            try {
                undek::script::Lexer lexer(src);
                undek::script::Parser parser(lexer.tokenize());
                parser.parse();
                return true;
            } catch (const std::exception& ex) {
                err = ex.what();
                return false;
            }
        };

        using undek::editor::BuilderAction;
        using undek::editor::BuilderStep;

        BuilderStep wait;
        wait.action = BuilderAction::Wait;
        wait.wait_ms = 250.0;
        wait.humanize = false;

        BuilderStep move;
        move.action = BuilderAction::MoveTo;
        move.x = 10;
        move.y = -20;

        BuilderStep click;
        click.action = BuilderAction::Click;
        click.x = 30;
        click.y = 40;

        BuilderStep press;
        press.action = BuilderAction::PressKey;
        press.text = "4";

        BuilderStep hold;
        hold.action = BuilderAction::KeyDown;
        hold.text = "shift";

        BuilderStep release;
        release.action = BuilderAction::KeyUp;
        release.text = "shift";

        BuilderStep buttonDown;
        buttonDown.action = BuilderAction::MouseDown;
        buttonDown.text = "x2";

        BuilderStep buttonUp;
        buttonUp.action = BuilderAction::MouseUp;
        buttonUp.text = "x2";

        BuilderStep scroll;
        scroll.action = BuilderAction::Scroll;
        scroll.amount = 240;

        BuilderStep loop;
        loop.action = BuilderAction::Loop;
        loop.loop_count = 5;
        loop.body = {press, wait};

        BuilderStep window;
        window.action = BuilderAction::IfWindow;
        window.window_title = "Notepad";
        window.body = {click};

        BuilderStep pixel;
        pixel.action = BuilderAction::IfPixel;
        pixel.pixel_x = 5;
        pixel.pixel_y = 6;
        pixel.color = 0x00FF00u;
        pixel.tolerance = 12;
        pixel.body = {move};

        const std::vector<BuilderStep> steps = {wait,   move,    click,      press,
                                                hold,   release, buttonDown, buttonUp,
                                                scroll, loop,    window,     pixel};

        const std::string script = undek::editor::formatBuilderScript(steps);
        std::string err;
        check(parses(script, err), "the builder's script parses: " + err);

        auto has = [&](const std::string& needle, const std::string& what) {
            check(script.find(needle) != std::string::npos,
                  what + " (script was:\n" + script + ")");
        };
        has("Wait(250ms, humanize=false)", "a wait keeps its time and its humanize flag");
        has("MoveTo(Point(10, -20), humanize=true)", "a move writes its point");
        has("Click(Point(30, 40), humanize=true)", "a click writes its point");
        has("PressKey(\"4\", humanize=true)", "a key press is quoted");
        has("KeyDown(\"shift\")", "holding a key is written out");
        has("KeyUp(\"shift\")", "releasing a key is written out");
        has("MouseDown(\"x2\")", "a side button can be held from the builder");
        has("MouseUp(\"x2\")", "a side button can be released from the builder");
        has("Scroll(240)", "a scroll keeps its amount");
        has("loop 5 {\n  PressKey(\"4\", humanize=true)\n  Wait(250ms, humanize=false)\n}",
            "a loop indents its body");
        has("if WindowTitle(\"Notepad\") {\n  Click(Point(30, 40), humanize=true)\n}",
            "an if-window indents its body");
        has("if PixelAt(5, 6) == 0x00FF00 tolerance 12 {\n  MoveTo(Point(10, -20), "
            "humanize=true)\n}",
            "an if-pixel writes its colour and tolerance");

        // Steps that can't be written are skipped rather than left half-done
        BuilderStep keyless;
        keyless.action = BuilderAction::KeyUp;
        check(undek::editor::formatBuilderStep(keyless).empty(),
              "a key step with no key writes nothing");
        BuilderStep emptyLoop;
        emptyLoop.action = BuilderAction::Loop;
        check(undek::editor::formatBuilderStep(emptyLoop).empty(),
              "a loop with no steps writes nothing");
        BuilderStep untitled;
        untitled.action = BuilderAction::IfWindow;
        untitled.body = {click};
        check(undek::editor::formatBuilderStep(untitled).empty(),
              "an if-window with no title writes nothing");
        check(undek::editor::formatBuilderScript({click, keyless, click}).find('\n') !=
                  std::string::npos,
              "a skipped step leaves the ones around it in place");

        BuilderStep tricky;
        tricky.action = BuilderAction::PressKey;
        tricky.text = "a\"b\\c";
        std::string tricky_err;
        check(parses(undek::editor::formatBuilderStep(tricky), tricky_err),
              "a key name with a quote or a backslash still parses: " + tricky_err);

        check(undek::editor::builderStepSummary(loop) == "loop 5 {",
              "a block's row shows only its first line");
        check(undek::editor::builderStepSummary(keyless).empty(),
              "a skipped step has no summary either");

        std::vector<BuilderStep> tree = {click, loop};
        check(undek::editor::builderTarget(tree, {}) == &tree, "the root path is the top level");
        check(undek::editor::builderTarget(tree, {1}) == &tree[1].body,
              "a path into a loop finds its body");
        check(undek::editor::builderTarget(tree, {0}) == nullptr,
              "a path through a plain step finds nothing");
        check(undek::editor::builderTarget(tree, {9}) == nullptr,
              "a path past the end finds nothing");

        std::vector<BuilderStep> selected = {click, press, scroll};
        selected[0].selected = true;
        selected[2].selected = true;
        check(undek::editor::wrapSelected(selected, BuilderAction::Loop, {}, 2),
              "ticked steps wrap into a loop");
        check(selected.size() == 2 && selected[0].action == BuilderAction::Loop &&
                  selected[0].body.size() == 2 &&
                  selected[0].body[0].action == BuilderAction::Click &&
                  selected[0].body[1].action == BuilderAction::Scroll,
              "the block takes the place of the first ticked step, in order");
        check(selected[1].action == BuilderAction::PressKey, "unticked steps stay where they were");
        check(!selected[0].body[0].selected && !selected[0].body[1].selected,
              "wrapped steps are unticked");
        check(!undek::editor::wrapSelected(selected, BuilderAction::Loop),
              "nothing ticked means no block");

        std::vector<BuilderStep> deep = {loop, press};
        deep[0].body[0].selected = true;
        check(undek::editor::wrapSelectedAnywhere(deep, BuilderAction::IfPixel) == 1,
              "a ticked step inside a loop wraps where it sits");
        check(deep.size() == 2 && deep[0].action == BuilderAction::Loop &&
                  deep[0].body.size() == 2 && deep[0].body[0].action == BuilderAction::IfPixel &&
                  deep[1].action == BuilderAction::PressKey,
              "the inner block lands inside the loop, not at the top");
        check(parses(undek::editor::formatBuilderScript(deep), err),
              "the wrapped script still parses: " + err);
    }

    {
        using undek::script::checkScript;
        const std::string pic = "check_shot.bmp";
        undek::ImageFrame frame;
        frame.width = 2;
        frame.height = 2;
        frame.pixels.assign(4, 0x101010);
        std::string werr;
        check(undek::saveBmp(pic, frame, werr), "checker fixture saves: " + werr);

        const auto clean = checkScript("if ImageExists(\"check_shot.bmp\") { Log(\"hi\") }", ".");
        check(clean.empty(), "a script that's fine reports nothing");

        const auto missing = checkScript("if ImageExists(\"no_such_shot.bmp\") { }", ".");
        check(missing.size() == 1 &&
                  missing[0].message.find("picture not found") != std::string::npos,
              "a picture that isn't there is reported");

        std::ofstream("check_junk.bmp") << "not a bmp";
        const auto junk = checkScript("if ImageExists(\"check_junk.bmp\") { }", ".");
        check(junk.size() == 1 && junk[0].message.find("can't be read") != std::string::npos,
              "a picture that isn't a picture is reported as such");

        const auto call = checkScript("Call(\"NothingRegistersThis\")", ".");
        check(call.size() == 1 && call[0].message.find("no script function") != std::string::npos,
              "a call to nothing is reported");

        const auto nested = checkScript("loop 3 {\n  if ImageExists(\"gone.bmp\") { }\n}", ".");
        check(nested.size() == 1 && nested[0].line == 2,
              "a check inside a loop reports the line it sits on");

        const auto broken = checkScript("if PixelAt(1, 2)", ".");
        check(broken.size() == 1 && broken[0].line == 1, "a parse error keeps its line number");

        const auto empty = checkScript("", ".");
        check(empty.size() == 1, "an empty script is a problem, not a crash");

        std::remove(pic.c_str());
        std::remove("check_junk.bmp");
    }

    {
        // A call is checked against what the function declared, not just
        // against whether the name exists
        using undek::script::checkScript;
        using undek::script::FunctionInfo;
        using undek::script::FunctionRegistry;
        using undek::script::ScriptValue;
        std::string err;

        FunctionInfo info;
        info.name = "SetValue";
        info.arguments.push_back({"value", "number", "Value to set", false});
        check(FunctionRegistry::global().add(
                  info,
                  [](const std::vector<ScriptValue>&, const undek::script::FunctionContext&,
                     ScriptValue&, std::string&) { return true; },
                  err),
              "the checker fixture function registers: " + err);

        const auto tooMany = checkScript("Call(\"SetValue\", 1, 2)", ".");
        check(tooMany.size() == 1 &&
                  tooMany[0].message.find("takes 1 argument, got 2") != std::string::npos,
              "a call with the wrong number of arguments is reported");

        const auto wrongType = checkScript("Call(\"SetValue\", \"hello\")", ".");
        check(wrongType.size() == 1 &&
                  wrongType[0].message.find("argument 1 has to be number") != std::string::npos,
              "a call with the wrong kind of argument is reported");

        const auto ok = checkScript("Call(\"SetValue\", 1 + 2)", ".");
        check(ok.empty(), "a call that fits the declaration passes checking");

        check(FunctionRegistry::global().remove("SetValue", err),
              "the checker fixture function unregisters: " + err);
    }

    {
        // A picture beside the script is looked up the same way by the checker
        // and by a run, because both go through resolvePicturePath()
        using undek::script::resolvePicturePath;
        const auto dir = std::filesystem::temp_directory_path() / "undek_check_side";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);

        undek::ImageFrame frame;
        frame.width = 2;
        frame.height = 2;
        frame.pixels.assign(4, 0x202020);
        std::string werr;
        check(undek::saveBmp((dir / "aside.bmp").string(), frame, werr),
              "the beside-the-script fixture saves: " + werr);

        const auto found = resolvePicturePath("aside.bmp", dir.string());
        check(!found.empty() && std::filesystem::path(found).parent_path() == dir,
              "a picture beside the script resolves to that file");
        check(resolvePicturePath("no_such_aside.bmp", dir.string()).empty(),
              "a picture that's nowhere resolves to nothing");

        const auto clean =
            undek::script::checkScript("if ImageExists(\"aside.bmp\") { }", dir.string());
        check(clean.empty(), "the checker finds a picture beside the script");

        std::filesystem::remove_all(dir);
    }

    {
        // A pause holds the run where it is; a stop still gets out
        std::mutex guard;
        std::vector<std::string> logs;
        undek::pause::clear();

        auto runLoggingScript = [&](const std::string& src) {
            undek::script::Lexer lexer(src);
            undek::script::Parser parser(lexer.tokenize());
            const auto program = parser.parse();
            undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
            profile.log_to_file = false;
            NullBackend backend;
            undek::script::Vm vm(program, profile, backend, true, [&](const std::string& m) {
                std::lock_guard<std::mutex> lock(guard);
                logs.push_back(m);
            });
            vm.run();
        };

        undek::pause::request();
        std::thread paused([&] {
            try {
                runLoggingScript("Log(\"first\")\nLog(\"second\")");
            } catch (const std::exception&) {
            }
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        {
            std::lock_guard<std::mutex> lock(guard);
            check(logs.empty(), "a paused run doesn't take the next step");
        }
        undek::pause::clear();
        paused.join();
        {
            std::lock_guard<std::mutex> lock(guard);
            check(logs.size() == 2, "resuming finishes the script");
        }

        undek::pause::request();
        std::atomic<bool> finished{false};
        std::thread stopped([&] {
            try {
                runLoggingScript("Log(\"third\")");
            } catch (const std::exception&) {
            }
            finished.store(true);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        undek::cancel::request();
        for (int i = 0; i < 200 && !finished.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(finished.load(), "stopping a paused run gets out instead of hanging");
        stopped.join();
        undek::pause::clear();
        undek::cancel::clear();
    }

    {
        // A pause pressed just as Run is hit mustn't be wiped by the startup
        // handshake; it belongs to the run, which takes it with it when it ends
        undek::pause::request();
        check(undek::runs::tryBegin("pause test"), "a run can start while a pause is pending");
        check(undek::pause::pending(), "a pause pressed as the run starts survives tryBegin");
        undek::runs::end();
        check(!undek::pause::pending(), "a finished run takes its pause with it");
    }

    {
        // A pause pressed after the run ended belongs to nothing, and mustn't be
        // waiting for the run after it.
        check(undek::runs::tryBegin("pausable"), "a run takes the slot");
        check(undek::runs::requestPause(), "a running macro can be paused");
        check(undek::pause::pending(), "and the pause is set");
        undek::runs::clearPause();
        check(!undek::pause::pending(), "clearing the pause resumes it");
        undek::runs::end();

        check(!undek::runs::requestPause(), "a pause with no run behind it is refused");
        check(!undek::pause::pending(), "and leaves nothing set");
        check(undek::runs::tryBegin("next run"), "the next run takes the slot");
        check(!undek::pause::pending(), "so the next run starts unpaused");
        undek::runs::end();
        undek::pause::clear();
    }

    {
        // A long Wait hears the pause hotkey instead of running to its end first
        std::mutex guard;
        std::vector<std::string> logs;
        std::atomic<bool> finished{false};
        undek::pause::clear();

        std::thread run([&] {
            try {
                undek::script::Lexer lexer("Wait(1200)\nLog(\"after wait\")");
                undek::script::Parser parser(lexer.tokenize());
                const auto program = parser.parse();
                undek::HumanizationProfile profile = undek::HumanizationProfile::defaults();
                profile.log_to_file = false;
                NullBackend backend;
                undek::script::Vm vm(program, profile, backend, false, [&](const std::string& m) {
                    std::lock_guard<std::mutex> lock(guard);
                    logs.push_back(m);
                });
                vm.run();
            } catch (const std::exception&) {
            }
            finished.store(true);
        });

        // Wait for the wait itself to show up, then pause in the middle of it
        for (int i = 0; i < 200; ++i) {
            {
                std::lock_guard<std::mutex> lock(guard);
                if (!logs.empty()) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        undek::pause::request();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        {
            std::lock_guard<std::mutex> lock(guard);
            check(count(logs, "[log] after wait") == 0,
                  "a pause lands in the middle of a long Wait");
        }

        undek::pause::clear();
        run.join();
        {
            std::lock_guard<std::mutex> lock(guard);
            check(count(logs, "[log] after wait") == 1,
                  "the rest of the Wait finishes after the resume");
        }
        check(finished.load(), "the run ends normally after a pause mid-wait");
    }

    {
        std::string err;
        check(undek::claimEditorInstance(err), "the editor claims the machine");
        check(undek::claimEditorInstance(err), "asking again in the same process is fine");
    }
    {
        using undek::editor::MacroLibrary;
        check(MacroLibrary::safeName("Snipe Combo") == "Snipe_Combo", "spaces become underscores");
        check(MacroLibrary::safeName("a/b\\c") == "a_b_c", "path separators can't climb out");
        check(MacroLibrary::safeName("what?") == "what_", "characters NTFS refuses are replaced");
        check(MacroLibrary::safeName("a:b*c\"d<e>f|g") == "a_b_c_d_e_f_g",
              "every refused character is replaced, not just the first");
        check(MacroLibrary::safeName("run.") == "run",
              "a trailing dot goes, because Windows drops it");
        check(MacroLibrary::safeName("  ") == "", "a name with nothing in it is refused");
        check(MacroLibrary::safeName("Caf\xc3\xa9") == "Caf\xc3\xa9",
              "letters outside ASCII survive");
        check(MacroLibrary::safeName("tab\there") == "tab_here", "control characters are replaced");
    }

    std::cout.clear();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "PASS\n";
    return 0;
}
