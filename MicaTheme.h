#pragma once

#include "pch.hpp"

/**
 * Windows 11 Mica Theme for ImGui
 * JSON-based theme configuration with reflect-cpp
 * Allows runtime customization and persistence
 */
namespace MicaTheme {

    // Color representation for JSON serialization
    struct Color {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;

        ImVec4 toImVec4() const { return ImVec4(r, g, b, a); }
    };

    // Theme configuration structure (reflect-cpp compatible)
    struct ThemeConfig {
        // Surface colors
        Color surface_primary{0.129f, 0.129f, 0.129f, 0.78f};
        Color surface_secondary{0.157f, 0.157f, 0.157f, 0.85f};
        
        // Accent and UI colors
        Color accent{0.004f, 0.576f, 0.976f, 1.0f};
        Color text_primary{0.949f, 0.949f, 0.949f, 1.0f};
        Color text_secondary{0.698f, 0.698f, 0.702f, 1.0f};
        Color border{0.329f, 0.329f, 0.329f, 0.4f};
        Color hover{0.212f, 0.212f, 0.212f, 0.9f};
        Color active{0.004f, 0.576f, 0.976f, 0.9f};
        
        // Frame colors
        Color frame_bg{0.176f, 0.176f, 0.176f, 0.545f};
        Color frame_bg_hovered{0.212f, 0.212f, 0.212f, 0.9f};
        Color frame_bg_active{0.067f, 0.341f, 0.608f, 0.588f};
        
        // Style settings
        float frame_rounding = 4.0f;
        float window_rounding = 8.0f;
        float popup_rounding = 4.0f;
        float tab_rounding = 4.0f;
        float grab_rounding = 2.0f;
        float frame_border_size = 1.0f;
        float window_border_size = 1.0f;
        float popup_border_size = 1.0f;
        float scrollbar_size = 14.0f;
        float grab_min_size = 10.0f;
    };

    /**
     * Load theme from JSON file
     */
    ThemeConfig LoadThemeFromFile(const std::string& filepath);

    /**
     * Save theme to JSON file
     */
    void SaveThemeToFile(const ThemeConfig& config, const std::string& filepath);

    /**
     * Get default Mica theme
     */
    ThemeConfig GetDefaultTheme();

    /**
     * Apply theme to ImGui
     * Call this after ImGui::CreateContext() and before any rendering
     */
    void ApplyMicaTheme(const ThemeConfig& theme);

    /**
     * Apply theme (helper using default)
     * Call this after ImGui::CreateContext() and before any rendering
     */
    inline void ApplyMicaThemeDefault() {
        ApplyMicaTheme(GetDefaultTheme());
    }

} // namespace MicaTheme
