#include "editor/MacroBuilder.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace undek::editor {
namespace {

std::string numberText(double value) {
    std::ostringstream oss;
    if (std::fabs(value - std::llround(value)) < 1e-9) {
        oss << std::llround(value);
    } else {
        oss << std::fixed << std::setprecision(1) << value;
    }
    return oss.str();
}

std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string boolText(bool value) {
    return value ? "true" : "false";
}

std::string indentLines(const std::string& text) {
    std::string out;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t nl = text.find('\n', start);
        const std::string line =
            text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!line.empty()) out += "  " + line;
        if (nl == std::string::npos) break;
        out.push_back('\n');
        start = nl + 1;
    }
    return out;
}

} // namespace

const char* builderActionName(BuilderAction action) {
    switch (action) {
        case BuilderAction::Wait:
            return "Wait";
        case BuilderAction::MoveTo:
            return "Move cursor";
        case BuilderAction::Click:
            return "Click";
        case BuilderAction::PressKey:
            return "Press key";
        case BuilderAction::KeyDown:
            return "Hold key down";
        case BuilderAction::KeyUp:
            return "Release key";
        case BuilderAction::MouseDown:
            return "Hold mouse button";
        case BuilderAction::MouseUp:
            return "Release mouse button";
        case BuilderAction::Scroll:
            return "Scroll wheel";
        case BuilderAction::Loop:
            return "Loop";
        case BuilderAction::IfWindow:
            return "If window";
        case BuilderAction::IfPixel:
            return "If pixel";
    }
    return "Step";
}

bool builderActionNeedsPoint(BuilderAction action) {
    return action == BuilderAction::MoveTo || action == BuilderAction::Click;
}

bool builderActionNeedsText(BuilderAction action) {
    return action == BuilderAction::PressKey || action == BuilderAction::KeyDown ||
           action == BuilderAction::KeyUp || action == BuilderAction::MouseDown ||
           action == BuilderAction::MouseUp;
}

bool builderActionIsButton(BuilderAction action) {
    return action == BuilderAction::MouseDown || action == BuilderAction::MouseUp;
}

bool builderActionIsBlock(BuilderAction action) {
    return action == BuilderAction::Loop || action == BuilderAction::IfWindow ||
           action == BuilderAction::IfPixel;
}

std::string formatBuilderStep(const BuilderStep& step) {
    switch (step.action) {
        case BuilderAction::Wait:
            return "Wait(" + numberText(step.wait_ms) + "ms, humanize=" + boolText(step.humanize) +
                   ")";
        case BuilderAction::MoveTo:
            return "MoveTo(Point(" + std::to_string(step.x) + ", " + std::to_string(step.y) +
                   "), humanize=" + boolText(step.humanize) + ")";
        case BuilderAction::Click:
            return "Click(Point(" + std::to_string(step.x) + ", " + std::to_string(step.y) +
                   "), humanize=" + boolText(step.humanize) + ")";
        case BuilderAction::PressKey:
            if (step.text.empty()) return {};
            return "PressKey(" + quoted(step.text) + ", humanize=" + boolText(step.humanize) + ")";
        case BuilderAction::KeyDown:
            if (step.text.empty()) return {};
            return "KeyDown(" + quoted(step.text) + ")";
        case BuilderAction::KeyUp:
            if (step.text.empty()) return {};
            return "KeyUp(" + quoted(step.text) + ")";
        case BuilderAction::MouseDown:
            if (step.text.empty()) return {};
            return "MouseDown(" + quoted(step.text) + ")";
        case BuilderAction::MouseUp:
            if (step.text.empty()) return {};
            return "MouseUp(" + quoted(step.text) + ")";
        case BuilderAction::Scroll:
            return "Scroll(" + std::to_string(step.amount) + ")";
        case BuilderAction::Loop: {
            const std::string body = formatBuilderScript(step.body);
            if (body.empty()) return {};
            return "loop " + std::to_string(step.loop_count) + " {\n" + indentLines(body) + "\n}";
        }
        case BuilderAction::IfWindow: {
            const std::string body = formatBuilderScript(step.body);
            if (body.empty() || step.window_title.empty()) return {};
            return "if WindowTitle(" + quoted(step.window_title) + ") {\n" + indentLines(body) +
                   "\n}";
        }
        case BuilderAction::IfPixel: {
            const std::string body = formatBuilderScript(step.body);
            if (body.empty()) return {};
            std::ostringstream oss;
            oss << "if PixelAt(" << step.pixel_x << ", " << step.pixel_y << ") == 0x"
                << std::uppercase << std::hex << std::setw(6) << std::setfill('0')
                << (step.color & 0xFFFFFFu) << std::setfill(' ');
            if (step.tolerance > 0) oss << " tolerance " << std::dec << step.tolerance;
            oss << " {\n" << indentLines(body) << "\n}";
            return oss.str();
        }
    }
    return {};
}

std::string builderStepSummary(const BuilderStep& step) {
    const std::string full = formatBuilderStep(step);
    const size_t nl = full.find('\n');
    return nl == std::string::npos ? full : full.substr(0, nl);
}

std::vector<BuilderStep>* builderTarget(std::vector<BuilderStep>& steps,
                                        const std::vector<int>& path) {
    std::vector<BuilderStep>* current = &steps;
    for (const int index : path) {
        if (index < 0 || index >= static_cast<int>(current->size())) return nullptr;
        BuilderStep& step = (*current)[index];
        if (!builderActionIsBlock(step.action)) return nullptr;
        current = &step.body;
    }
    return current;
}

bool wrapSelected(std::vector<BuilderStep>& steps, BuilderAction block_kind,
                  const std::string& window_title, int loop_count, int pixel_x, int pixel_y,
                  unsigned int color, int tolerance) {
    std::vector<BuilderStep> moved;
    for (const auto& step : steps) {
        if (step.selected) moved.push_back(step);
    }
    if (moved.empty()) return false;

    BuilderStep block;
    block.action = block_kind;
    block.window_title = window_title;
    block.loop_count = loop_count;
    block.pixel_x = pixel_x;
    block.pixel_y = pixel_y;
    block.color = color;
    block.tolerance = tolerance;
    block.body = std::move(moved);
    for (auto& inner : block.body)
        inner.selected = false;

    std::vector<BuilderStep> result;
    bool placed = false;
    for (const auto& step : steps) {
        if (step.selected) {
            if (!placed) {
                result.push_back(std::move(block));
                placed = true;
            }
            continue;
        }
        result.push_back(step);
    }
    steps = std::move(result);
    return true;
}

int wrapSelectedAnywhere(std::vector<BuilderStep>& steps, BuilderAction block_kind,
                         const std::string& window_title, int loop_count, int pixel_x, int pixel_y,
                         unsigned int color, int tolerance) {
    int made = 0;
    for (auto& step : steps) {
        if (!builderActionIsBlock(step.action)) continue;
        made += wrapSelectedAnywhere(step.body, block_kind, window_title, loop_count, pixel_x,
                                     pixel_y, color, tolerance);
    }
    if (wrapSelected(steps, block_kind, window_title, loop_count, pixel_x, pixel_y, color,
                     tolerance)) {
        ++made;
    }
    return made;
}

std::string formatBuilderScript(const std::vector<BuilderStep>& steps) {
    std::string out;
    for (const auto& step : steps) {
        const std::string line = formatBuilderStep(step);
        if (line.empty()) continue;
        if (!out.empty()) out.push_back('\n');
        out += line;
    }
    return out;
}

} // namespace undek::editor
