#include "pch.hpp"

#include "StyleEditor.hpp"

// Apply settings to ImGui style
void StyleEditor::StyleSettings::ApplyToStyle(ImGuiStyle& style) const {
    style.FrameRounding = frame_rounding;
    style.WindowRounding = window_rounding;
    style.PopupRounding = popup_rounding;
    style.TabRounding = tab_rounding;
    style.GrabRounding = grab_rounding;
    style.ChildRounding = child_rounding;
    
    style.FrameBorderSize = frame_border_size;
    style.WindowBorderSize = window_border_size;
    style.PopupBorderSize = popup_border_size;
    style.ChildBorderSize = child_border_size;
    style.TabBorderSize = tab_border_size;
    
    style.WindowPadding = ImVec2(window_padding_x, window_padding_y);
    style.FramePadding = ImVec2(frame_padding_x, frame_padding_y);
    style.ItemSpacing = ImVec2(item_spacing_x, item_spacing_y);
    style.ItemInnerSpacing = ImVec2(item_inner_spacing_x, item_inner_spacing_y);
    style.TouchExtraPadding = ImVec2(touch_extra_padding_x, touch_extra_padding_y);
    
    style.IndentSpacing = indent_spacing;
    style.GrabMinSize = grab_min_size;
    style.ScrollbarSize = scrollbar_size;
    
    style.Alpha = alpha;
    style.DisabledAlpha = disabled_alpha;
    style.CurveTessellationTol = curve_tessellation_tol;
    style.CircleTessellationMaxError = circle_tessellation_max_error;
    
    style.AntiAliasedFill = anti_aliased_fill;
    style.AntiAliasedLines = anti_aliased_lines;
    style.AntiAliasedLinesUseTex = anti_aliased_lines_use_tex;
}

// Load settings from ImGui style
void StyleEditor::StyleSettings::LoadFromStyle(const ImGuiStyle& style) {
    frame_rounding = style.FrameRounding;
    window_rounding = style.WindowRounding;
    popup_rounding = style.PopupRounding;
    tab_rounding = style.TabRounding;
    grab_rounding = style.GrabRounding;
    child_rounding = style.ChildRounding;
    
    frame_border_size = style.FrameBorderSize;
    window_border_size = style.WindowBorderSize;
    popup_border_size = style.PopupBorderSize;
    child_border_size = style.ChildBorderSize;
    tab_border_size = style.TabBorderSize;
    
    window_padding_x = style.WindowPadding.x;
    window_padding_y = style.WindowPadding.y;
    frame_padding_x = style.FramePadding.x;
    frame_padding_y = style.FramePadding.y;
    item_spacing_x = style.ItemSpacing.x;
    item_spacing_y = style.ItemSpacing.y;
    item_inner_spacing_x = style.ItemInnerSpacing.x;
    item_inner_spacing_y = style.ItemInnerSpacing.y;
    touch_extra_padding_x = style.TouchExtraPadding.x;
    touch_extra_padding_y = style.TouchExtraPadding.y;
    
    indent_spacing = style.IndentSpacing;
    grab_min_size = style.GrabMinSize;
    scrollbar_size = style.ScrollbarSize;
    
    alpha = style.Alpha;
    disabled_alpha = style.DisabledAlpha;
    curve_tessellation_tol = style.CurveTessellationTol;
    circle_tessellation_max_error = style.CircleTessellationMaxError;
    
    anti_aliased_fill = style.AntiAliasedFill;
    anti_aliased_lines = style.AntiAliasedLines;
    anti_aliased_lines_use_tex = style.AntiAliasedLinesUseTex;
}

StyleEditor::StyleEditor() {
    // Load settings from current ImGui style
    m_Settings.LoadFromStyle(ImGui::GetStyle());
    m_RefSavedStyle = ImGui::GetStyle();
}

void StyleEditor::ApplySettings() {
    ImGuiStyle& style = ImGui::GetStyle();
    m_Settings.ApplyToStyle(style);
}

StyleEditor::StyleSettings StyleEditor::LoadFromFile(const std::string& filepath) {
    try {
        auto result = rfl::json::load<StyleSettings>(filepath);
        if (result) {
            m_Settings = *result;
            ApplySettings();
            fprintf(stderr, "Style settings loaded from: %s\n", filepath.c_str());
            return m_Settings;
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "Warning: Failed to load style from %s: %s\n", filepath.c_str(), e.what());
    }
    return m_Settings;
}

void StyleEditor::SaveToFile(const std::string& filepath) {
    try {
        // Update settings from current ImGui style
        m_Settings.LoadFromStyle(ImGui::GetStyle());
        rfl::json::save(filepath, m_Settings);
        fprintf(stderr, "Style settings saved to: %s\n", filepath.c_str());
    } catch (const std::exception& e) {
        fprintf(stderr, "Warning: Failed to save style to %s: %s\n", filepath.c_str(), e.what());
    }
}

void StyleEditor::ResetToDefault() {
    ImGuiStyle default_style;
    m_Settings.LoadFromStyle(default_style);
    ApplySettings();
}

bool StyleEditor::Show(ImGuiStyle* ref_style, bool* p_open) {
    ImGuiStyle& style = ImGui::GetStyle();
    
    if (!ImGui::Begin("Style Editor", p_open)) {
        ImGui::End();
        return false;
    }

    // Default to using internal storage as reference
    if (m_Init && ref_style == nullptr) {
        m_RefSavedStyle = style;
    }
    m_Init = false;
    if (ref_style == nullptr) {
        ref_style = &m_RefSavedStyle;
    }

    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.50f);

    // General
    ImGui::SeparatorText("General");
    
    if ((ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) == 0) {
        ImGui::BulletText("Warning: Font scaling will NOT be smooth, because\nImGuiBackendFlags_RendererHasTextures is not set!");
        ImGui::BulletText("For instructions, see:");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("docs/BACKENDS.md", "https://github.com/ocornut/imgui/blob/master/docs/BACKENDS.md");
    }

    // Style selector
    if (ImGui::ShowStyleSelector("Colors##Selector")) {
        m_RefSavedStyle = style;
        m_Settings.LoadFromStyle(style);
    }
    
    ImGui::ShowFontSelector("Fonts##Selector");
    
    if (ImGui::DragFloat("FontSizeBase", &style.FontSizeBase, 0.20f, 5.0f, 100.0f, "%.0f")) {
        style._NextFrameFontSizeBase = style.FontSizeBase;
    }
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::Text(" (out %.2f)", ImGui::GetFontSize());
    
    ImGui::DragFloat("FontScaleMain", &style.FontScaleMain, 0.02f, 0.5f, 4.0f);
    ImGui::DragFloat("FontScaleDpi", &style.FontScaleDpi, 0.02f, 0.5f, 4.0f);

    // Save/Load/Revert buttons
    ImGui::Separator();
    if (ImGui::Button("Save Ref")) {
        *ref_style = m_RefSavedStyle = style;
        m_Settings.LoadFromStyle(style);
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert Ref")) {
        style = *ref_style;
        m_Settings.LoadFromStyle(style);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Default")) {
        ResetToDefault();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save to JSON")) {
        SaveToFile("imgui_style.json");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load from JSON")) {
        LoadFromFile("imgui_style.json");
    }
    ImGui::SetItemTooltip(
        "Save/Revert in local non-persistent storage. Default Colors definition are not affected.\n"
        "Use 'Save to JSON' to persist settings to file.");

    ImGui::SeparatorText("Details");

    // Tabs for different settings
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Sizes")) {
            DrawSizeTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Colors")) {
            DrawColorsTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Fonts")) {
            DrawFontsTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Rendering")) {
            DrawRenderingTab();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::PopItemWidth();
    ImGui::End();

    return true;
}

void StyleEditor::DrawSizeTab() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    ImGui::SeparatorText("Main");
    ImGui::SliderFloat2("WindowPadding", (float*)&style.WindowPadding, 0.0f, 20.0f, "%.0f");
    ImGui::SliderFloat2("FramePadding", (float*)&style.FramePadding, 0.0f, 20.0f, "%.0f");
    ImGui::SliderFloat2("ItemSpacing", (float*)&style.ItemSpacing, 0.0f, 20.0f, "%.0f");
    ImGui::SliderFloat2("ItemInnerSpacing", (float*)&style.ItemInnerSpacing, 0.0f, 20.0f, "%.0f");
    ImGui::SliderFloat2("TouchExtraPadding", (float*)&style.TouchExtraPadding, 0.0f, 10.0f, "%.0f");
    ImGui::SliderFloat("IndentSpacing", &style.IndentSpacing, 0.0f, 30.0f, "%.0f");
    ImGui::SliderFloat("GrabMinSize", &style.GrabMinSize, 1.0f, 20.0f, "%.0f");

    ImGui::SeparatorText("Borders");
    ImGui::SliderFloat("WindowBorderSize", &style.WindowBorderSize, 0.0f, 1.0f, "%.0f");
    ImGui::SliderFloat("ChildBorderSize", &style.ChildBorderSize, 0.0f, 1.0f, "%.0f");
    ImGui::SliderFloat("PopupBorderSize", &style.PopupBorderSize, 0.0f, 1.0f, "%.0f");
    ImGui::SliderFloat("FrameBorderSize", &style.FrameBorderSize, 0.0f, 1.0f, "%.0f");

    ImGui::SeparatorText("Rounding");
    ImGui::SliderFloat("WindowRounding", &style.WindowRounding, 0.0f, 12.0f, "%.0f");
    ImGui::SliderFloat("ChildRounding", &style.ChildRounding, 0.0f, 12.0f, "%.0f");
    ImGui::SliderFloat("FrameRounding", &style.FrameRounding, 0.0f, 12.0f, "%.0f");
    ImGui::SliderFloat("PopupRounding", &style.PopupRounding, 0.0f, 12.0f, "%.0f");
    ImGui::SliderFloat("GrabRounding", &style.GrabRounding, 0.0f, 12.0f, "%.0f");
    ImGui::SliderFloat("TabRounding", &style.TabRounding, 0.0f, 12.0f, "%.0f");

    ImGui::SeparatorText("Scrollbar");
    ImGui::SliderFloat("ScrollbarSize", &style.ScrollbarSize, 1.0f, 20.0f, "%.0f");
    ImGui::SliderFloat("ScrollbarRounding", &style.ScrollbarRounding, 0.0f, 12.0f, "%.0f");
}

void StyleEditor::DrawColorsTab() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    static ImGuiTextFilter filter;
    filter.Draw("Filter colors", ImGui::GetFontSize() * 16);

    static ImGuiColorEditFlags alpha_flags = 0;
    if (ImGui::RadioButton("Opaque", alpha_flags == ImGuiColorEditFlags_AlphaOpaque)) {
        alpha_flags = ImGuiColorEditFlags_AlphaOpaque;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Alpha", alpha_flags == ImGuiColorEditFlags_None)) {
        alpha_flags = ImGuiColorEditFlags_None;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Both", alpha_flags == ImGuiColorEditFlags_AlphaPreviewHalf)) {
        alpha_flags = ImGuiColorEditFlags_AlphaPreviewHalf;
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 10), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::BeginChild("##colors", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar);
    ImGui::PushItemWidth(ImGui::GetFontSize() * -12);
    
    for (int i = 0; i < ImGuiCol_COUNT; i++) {
        const char* name = ImGui::GetStyleColorName(i);
        if (!filter.PassFilter(name))
            continue;
        ImGui::PushID(i);
        ImGui::ColorEdit4("##color", (float*)&style.Colors[i], ImGuiColorEditFlags_AlphaBar | alpha_flags);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(name);
        ImGui::PopID();
    }
    
    ImGui::PopItemWidth();
    ImGui::EndChild();
}

void StyleEditor::DrawFontsTab() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;

    ImGui::Text("Font Atlas Information");
    ImGui::Separator();

    ImGui::Text("Texture ID: %p", atlas->TexRef);

    ImGui::Separator();
    ImGui::Text("Loaded Fonts: %d", atlas->Fonts.Size);

    ImGui::BeginChild("##fonts_list", ImVec2(0, 200), ImGuiChildFlags_Borders);
    for (int i = 0; i < atlas->Fonts.Size; i++) {
        ImFont* font = atlas->Fonts[i];
        ImGui::Text("Font %d: %p", i, font);
    }
    ImGui::EndChild();

    ImGui::SetItemTooltip("Shows information about loaded fonts in the font atlas.");
}

void StyleEditor::DrawRenderingTab() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    ImGui::Checkbox("Anti-aliased lines", &style.AntiAliasedLines);
    ImGui::SameLine();
    ImGui::SetItemTooltip("When disabling anti-aliasing lines, you'll probably want to disable borders in your style as well.");

    ImGui::Checkbox("Anti-aliased lines use texture", &style.AntiAliasedLinesUseTex);
    ImGui::SameLine();
    ImGui::SetItemTooltip("Faster lines using texture data. Require backend to render with bilinear filtering (not point/nearest filtering).");

    ImGui::Checkbox("Anti-aliased fill", &style.AntiAliasedFill);
    ImGui::PushItemWidth(ImGui::GetFontSize() * 8);
    ImGui::DragFloat("Curve Tessellation Tolerance", &style.CurveTessellationTol, 0.02f, 0.10f, 10.0f, "%.2f");
    if (style.CurveTessellationTol < 0.10f)
        style.CurveTessellationTol = 0.10f;

    ImGui::DragFloat("Circle Tessellation Max Error", &style.CircleTessellationMaxError, 0.005f, 0.10f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloat("Global Alpha", &style.Alpha, 0.005f, 0.20f, 1.0f, "%.2f");
    ImGui::DragFloat("Disabled Alpha", &style.DisabledAlpha, 0.005f, 0.0f, 1.0f, "%.2f");
    ImGui::SameLine();
    ImGui::SetItemTooltip("Additional alpha multiplier for disabled items (multiply over current value of Alpha).");
    ImGui::PopItemWidth();
}
