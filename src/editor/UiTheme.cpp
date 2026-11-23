#include "editor/UiTheme.h"

#include <imgui.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace undek::editor {
namespace {

void scaleStyle(ImGuiStyle& style, float dpi) {
    style.WindowPadding = ImVec2(12.0f * dpi, 12.0f * dpi);
    style.FramePadding = ImVec2(10.0f * dpi, 6.0f * dpi);
    style.ItemSpacing = ImVec2(10.0f * dpi, 8.0f * dpi);
    style.ItemInnerSpacing = ImVec2(8.0f * dpi, 6.0f * dpi);
    style.CellPadding = ImVec2(8.0f * dpi, 4.0f * dpi);
    style.IndentSpacing = 20.0f * dpi;
    style.ScrollbarSize = 14.0f * dpi;
    style.GrabMinSize = 12.0f * dpi;

    style.WindowRounding = 10.0f * dpi;
    style.ChildRounding = 8.0f * dpi;
    style.FrameRounding = 6.0f * dpi;
    style.PopupRounding = 8.0f * dpi;
    style.ScrollbarRounding = 8.0f * dpi;
    style.GrabRounding = 6.0f * dpi;
    style.TabRounding = 6.0f * dpi;
}

} // namespace

void ApplyModernTheme(float dpi_scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    c[ImGuiCol_Text] = ImVec4(0.93f, 0.94f, 0.97f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.52f, 0.58f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.13f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.11f, 0.14f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(0.20f, 0.22f, 0.28f, 0.55f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.18f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.24f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.22f, 0.30f, 1.0f);
    c[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.16f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.09f, 0.12f, 1.0f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.0f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.30f, 0.38f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36f, 0.38f, 0.48f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.44f, 0.46f, 0.58f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.45f, 0.95f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.38f, 0.85f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.58f, 0.48f, 0.98f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.22f, 0.20f, 0.32f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.32f, 0.28f, 0.48f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.42f, 0.36f, 0.62f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.24f, 0.22f, 0.34f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.32f, 0.28f, 0.46f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.34f, 0.58f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.22f, 0.24f, 0.30f, 0.65f);
    c[ImGuiCol_SeparatorHovered] = ImVec4(0.38f, 0.34f, 0.55f, 0.78f);
    c[ImGuiCol_SeparatorActive] = ImVec4(0.48f, 0.42f, 0.68f, 1.0f);
    c[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.28f, 0.40f, 0.4f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0.45f, 0.40f, 0.65f, 0.7f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(0.55f, 0.48f, 0.78f, 0.95f);
    c[ImGuiCol_Tab] = ImVec4(0.14f, 0.13f, 0.20f, 1.0f);
    c[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.24f, 0.40f, 1.0f);
    c[ImGuiCol_TabActive] = ImVec4(0.22f, 0.20f, 0.32f, 1.0f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.12f, 0.16f, 1.0f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.22f, 0.24f, 0.30f, 1.0f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.18f, 0.20f, 0.26f, 1.0f);

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    scaleStyle(style, dpi_scale);

    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = 1.0f;
}

bool LoadUiFonts(float dpi_scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float size = 16.0f * dpi_scale;
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\SegoeUI.ttf",
    };

    ImFontConfig cfg{};
    cfg.OversampleH = 3;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = true;

    for (const char* path : candidates) {
        if (fs::exists(path)) {
            io.Fonts->AddFontFromFileTTF(path, size, &cfg);
            return true;
        }
    }

    io.Fonts->AddFontDefault();
    return false;
}

} // namespace undek::editor
