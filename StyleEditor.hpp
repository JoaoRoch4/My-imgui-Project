#pragma once

#include "pch.hpp"

/**
 * ImGui Style Editor with JSON persistence
 * Allows editing and saving ImGui style settings
 */
class StyleEditor {
public:
    /**
     * Style settings structure (reflect-cpp compatible)
     * Serializes ImGui style properties to JSON
     */
    struct StyleSettings {
        // Window and frame
        float frame_rounding = 4.0f;
        float window_rounding = 8.0f;
        float popup_rounding = 4.0f;
        float tab_rounding = 4.0f;
        float grab_rounding = 2.0f;
        float child_rounding = 0.0f;
        
        // Border sizes
        float frame_border_size = 1.0f;
        float window_border_size = 1.0f;
        float popup_border_size = 1.0f;
        float child_border_size = 1.0f;
        float tab_border_size = 1.0f;
        
        // Padding and spacing
        float window_padding_x = 10.0f;
        float window_padding_y = 10.0f;
        float frame_padding_x = 8.0f;
        float frame_padding_y = 6.0f;
        float item_spacing_x = 8.0f;
        float item_spacing_y = 6.0f;
        float item_inner_spacing_x = 4.0f;
        float item_inner_spacing_y = 4.0f;
        float touch_extra_padding_x = 0.0f;
        float touch_extra_padding_y = 0.0f;
        
        // Indentation and grab
        float indent_spacing = 20.0f;
        float grab_min_size = 10.0f;
        float scrollbar_size = 14.0f;
        
        // General style
        float alpha = 1.0f;
        float disabled_alpha = 0.6f;
        float curve_tessellation_tol = 1.25f;
        float circle_tessellation_max_error = 1.6f;
        
        // Anti-aliasing
        bool anti_aliased_fill = true;
        bool anti_aliased_lines = true;
        bool anti_aliased_lines_use_tex = true;
        
        // Saving helper
        void ApplyToStyle(ImGuiStyle& style) const;
        void LoadFromStyle(const ImGuiStyle& style);
    };

    StyleEditor();
    ~StyleEditor() = default;

    /**
     * Show the style editor window
     * Returns true if the window is still open
     */
    bool Show(ImGuiStyle* ref_style = nullptr, bool* p_open = nullptr);

    /**
     * Load style settings from JSON file
     */
    StyleSettings LoadFromFile(const std::string& filepath);

    /**
     * Save style settings to JSON file
     */
    void SaveToFile(const std::string& filepath);

    /**
     * Reset to default style
     */
    void ResetToDefault();

    /**
     * Get current style settings
     */
    const StyleSettings& GetAppSettings() const { return m_Settings; }

    /**
     * Set style settings
     */
    void SetSettings(const StyleSettings& settings) {
        m_Settings = settings;
        ApplySettings();
    }

private:
    StyleSettings m_Settings;
    ImGuiStyle m_RefSavedStyle;
    bool m_Init = true;
        class App* g_App;

    // Helper functions
    void ApplySettings();
    void DrawSizeTab();
    void DrawColorsTab();
    void DrawFontsTab();
    void DrawRenderingTab();
};
