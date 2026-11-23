#include "editor/RecordedActions.h"

#include <cctype>
#include <map>

namespace undek::editor {
namespace {

// -1 when the line isn't a wait, otherwise the milliseconds in it.
int parseWaitMs(const std::string& line) {
    if (line.rfind("Wait(", 0) != 0) return -1;

    int ms = 0;
    bool saw_digit = false;
    for (size_t i = 5; i < line.size(); ++i) {
        const char c = line[i];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            ms = ms * 10 + (c - '0');
            saw_digit = true;
        } else if (saw_digit) {
            return ms;
        }
    }
    return saw_digit ? ms : -1;
}

bool startsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

} // namespace

std::vector<RecordedStep> parseRecordedSteps(const std::vector<std::string>& lines) {
    std::vector<RecordedStep> steps;
    int pending_wait = 0;
    for (const auto& line : lines) {
        if (line.empty()) continue;

        const int wait = parseWaitMs(line);
        if (wait >= 0) {
            pending_wait = wait;
            continue;
        }

        RecordedStep step;
        step.wait_ms = pending_wait;
        step.text = line;
        pending_wait = 0;
        steps.push_back(std::move(step));
    }

    // A left press with some movement before the release reads as a drag.
    int next_group = 0;
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].text != "MouseDown(\"left\")") continue;

        int end = -1;
        bool moved = false;
        for (size_t j = i + 1; j < steps.size(); ++j) {
            if (startsWith(steps[j].text, "MoveTo(")) moved = true;
            if (steps[j].text == "MouseUp(\"left\")") {
                end = static_cast<int>(j);
                break;
            }
            if (steps[j].text == "MouseDown(\"left\")") break;
        }
        if (end < 0 || !moved) continue;

        for (int k = static_cast<int>(i); k <= end; ++k)
            steps[k].drag_group = next_group;
        ++next_group;
        i = static_cast<size_t>(end);
    }

    return steps;
}

std::string formatRecordedSteps(const std::vector<RecordedStep>& steps) {
    std::string out;
    for (const auto& s : steps) {
        if (!s.enabled || s.text.empty()) continue;
        if (s.wait_ms > 0) {
            out += "Wait(" + std::to_string(s.wait_ms) + "ms, humanize=true)\n";
        }
        out += s.text;
        out += '\n';
    }
    return out;
}

std::string checkRecordingBalance(const std::vector<RecordedStep>& steps) {
    auto argument = [](const std::string& text, const char* prefix, std::string& out) {
        const std::string head(prefix);
        if (text.rfind(head, 0) != 0) return false;
        const size_t open = head.size(); // the prefix ends on the opening quote
        const size_t close = text.find('"', open);
        if (close == std::string::npos) return false;
        out = text.substr(open, close - open);
        return true;
    };

    std::map<std::string, int> keys;
    std::map<std::string, int> buttons;
    for (const auto& step : steps) {
        if (!step.enabled) continue;
        std::string name;
        if (argument(step.text, "KeyDown(\"", name))
            ++keys[name];
        else if (argument(step.text, "KeyUp(\"", name))
            --keys[name];
        else if (argument(step.text, "MouseDown(\"", name))
            ++buttons[name];
        else if (argument(step.text, "MouseUp(\"", name))
            --buttons[name];
    }

    for (const auto& entry : keys) {
        if (entry.second > 0) {
            return "the recording still holds key \"" + entry.first +
                   "\" - it has a KeyDown without a KeyUp";
        }
        if (entry.second < 0) {
            return "the recording releases key \"" + entry.first + "\" it never pressed";
        }
    }
    for (const auto& entry : buttons) {
        if (entry.second > 0) {
            return "the recording still holds the " + entry.first +
                   " mouse button - it has a MouseDown without a MouseUp";
        }
        if (entry.second < 0) {
            return "the recording releases the " + entry.first + " mouse button it never pressed";
        }
    }
    return {};
}

} // namespace undek::editor
