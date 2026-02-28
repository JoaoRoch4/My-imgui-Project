#include "pch.hpp"
#include "MicaTheme.h"
namespace MicaTheme {

    ThemeConfig GetDefaultTheme() {
        return ThemeConfig{};
    }

    ThemeConfig LoadThemeFromFile(const std::string& filepath) {
        try {
            auto result = rfl::json::load<ThemeConfig>(filepath);
            if(result) {
                return *result;
            }
        } catch(const std::exception& e) {
            // Log error but continue with defaults
            fprintf(stderr, "Warning: Failed to load theme from %s: %s\n", filepath.c_str(), e.what());
        }
        return GetDefaultTheme();
    }

    void SaveThemeToFile(const ThemeConfig& config, const std::string& filepath) {
        try {
            rfl::json::save(filepath, config);
        } catch(const std::exception& e) {
            fprintf(stderr, "Warning: Failed to save theme to %s: %s\n", filepath.c_str(), e.what());
        }
    }

    void ApplyMicaTheme(const ThemeConfig& theme) {
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;

        // Window & Frame backgrounds
        colors[ImGuiCol_WindowBg] = theme.surface_primary.toImVec4();
        colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        colors[ImGuiCol_PopupBg] = theme.surface_secondary.toImVec4();
        colors[ImGuiCol_Border] = theme.border.toImVec4();
        colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        // Frame backgrounds
        colors[ImGuiCol_FrameBg] = theme.frame_bg.toImVec4();
        colors[ImGuiCol_FrameBgHovered] = theme.frame_bg_hovered.toImVec4();
        colors[ImGuiCol_FrameBgActive] = theme.frame_bg_active.toImVec4();

        // Title bar
        colors[ImGuiCol_TitleBg] = theme.surface_primary.toImVec4();
        colors[ImGuiCol_TitleBgActive] = theme.surface_secondary.toImVec4();
        colors[ImGuiCol_TitleBgCollapsed] = theme.surface_primary.toImVec4();

        // Menu bar
        colors[ImGuiCol_MenuBarBg] = theme.surface_secondary.toImVec4();

        // Scroll bar
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.31f, 0.31f, 0.31f, 1.0f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.0f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.0f);

        // Buttons
        colors[ImGuiCol_Button] = theme.frame_bg.toImVec4();
        colors[ImGuiCol_ButtonHovered] = theme.hover.toImVec4();
        colors[ImGuiCol_ButtonActive] = theme.frame_bg_active.toImVec4();

        // Headers
        colors[ImGuiCol_Header] = theme.frame_bg.toImVec4();
        colors[ImGuiCol_HeaderHovered] = theme.hover.toImVec4();
        colors[ImGuiCol_HeaderActive] = theme.frame_bg_active.toImVec4();

        // Separator
        colors[ImGuiCol_Separator] = theme.border.toImVec4();
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.45f, 0.45f, 0.45f, 0.5f);
        colors[ImGuiCol_SeparatorActive] = theme.accent.toImVec4();

        // Resize grip
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.289f, 0.289f, 0.289f, 0.45f);
        colors[ImGuiCol_ResizeGripHovered] = theme.hover.toImVec4();
        colors[ImGuiCol_ResizeGripActive] = theme.accent.toImVec4();

        // Tabs
        colors[ImGuiCol_Tab] = ImVec4(0.132f, 0.132f, 0.132f, 0.863f);
        colors[ImGuiCol_TabHovered] = theme.hover.toImVec4();
        colors[ImGuiCol_TabSelected] = theme.accent.toImVec4();
        colors[ImGuiCol_TabDimmed] = colors[ImGuiCol_Tab];
        colors[ImGuiCol_NavCursor] = theme.accent.toImVec4();

        // Docking
        colors[ImGuiCol_DockingPreview] = ImVec4(0.004f, 0.576f, 0.976f, 0.3f);
        colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);

        // Text
        colors[ImGuiCol_Text] = theme.text_primary.toImVec4();
        colors[ImGuiCol_TextDisabled] = theme.text_secondary.toImVec4();

        // Checkmark, radio button, etc.
        colors[ImGuiCol_CheckMark] = theme.accent.toImVec4();
        colors[ImGuiCol_SliderGrab] = theme.accent.toImVec4();
        colors[ImGuiCol_SliderGrabActive] = theme.accent.toImVec4();

        // Plotlines
        colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.0f);
        colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.43f, 0.35f, 1.0f);
        colors[ImGuiCol_PlotHistogram] = theme.accent.toImVec4();
        colors[ImGuiCol_PlotHistogramHovered] = theme.accent.toImVec4();

        // Text selection
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.067f, 0.341f, 0.608f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = ImVec4(0.004f, 0.576f, 0.976f, 0.9f);

        // Navigation highlight
        colors[ImGuiCol_NavCursor] = theme.accent.toImVec4();
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.35f);

        // Style settings for Mica look
        style.FrameRounding = theme.frame_rounding;
        style.GrabMinSize = theme.grab_min_size;
        style.GrabRounding = theme.grab_rounding;
        style.WindowRounding = theme.window_rounding;
        style.PopupRounding = theme.popup_rounding;
        style.TabRounding = theme.tab_rounding;
        style.FrameBorderSize = theme.frame_border_size;
        style.WindowBorderSize = theme.window_border_size;
        style.PopupBorderSize = theme.popup_border_size;
        style.Alpha = 1.0f;
        style.AntiAliasedFill = true;
        style.AntiAliasedLines = true;
        style.AntiAliasedLinesUseTex = true;
        style.WindowPadding = ImVec2(10.0f, 10.0f);
        style.FramePadding = ImVec2(8.0f, 6.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);
        style.IndentSpacing = 20.0f;
        style.ScrollbarSize = theme.scrollbar_size;
        style.LogSliderDeadzone = 4.0f;
    }

} // namespace MicaTheme
