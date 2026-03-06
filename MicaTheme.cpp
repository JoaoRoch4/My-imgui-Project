/**
 * @file MicaTheme.cpp
 * @brief Implementação do tema Windows 11 Mica para ImGui.
 *
 * SOBRECARGA ApplyMicaTheme(ThemeConfig, ImGuiStyle&)
 * ----------------------------------------------------
 * Esta é a implementação real — recebe o ImGuiStyle como parâmetro para
 * poder ser chamada sem um contexto ImGui ativo (necessário em
 * ColorSettings::ColorSettings() que roda durante a construção de AppSettings,
 * potencialmente antes de ImGui::CreateContext()).
 *
 * A sobrecarga ApplyMicaTheme(ThemeConfig) delega para esta, passando
 * ImGui::GetStyle() — sem duplicação de lógica.
 */
#include "pch.hpp"
#include "MicaTheme.h"

namespace MicaTheme {

// ============================================================================
// GetDefaultTheme
// ============================================================================

/**
 * @brief Retorna um ThemeConfig construído com os defaults do struct.
 *
 * Os defaults já estão definidos nos inicializadores de membros de ThemeConfig,
 * portanto basta construir e retornar.
 */
ThemeConfig GetDefaultTheme() {
    return ThemeConfig{}; // construtor padrão aplica todos os defaults declarados
}

// ============================================================================
// LoadThemeFromFile
// ============================================================================

/**
 * @brief Carrega um ThemeConfig de um arquivo JSON via reflect-cpp.
 *
 * Em caso de falha (arquivo inexistente, JSON inválido), retorna GetDefaultTheme()
 * sem lançar exceção para o chamador — falha silenciosa é intencional.
 *
 * @param filepath  Caminho do JSON a ler.
 * @return          ThemeConfig carregado ou defaults em caso de erro.
 */
ThemeConfig LoadThemeFromFile(const std::string& filepath) {
    try {
        auto result = rfl::json::load<ThemeConfig>(filepath); // desserializa via reflect-cpp
        if(result)
            return *result; // retorna o ThemeConfig desserializado
    } catch(const std::exception& e) {
        // Falha silenciosa — log para stderr mas não propaga
        fprintf(stderr, "Warning: Failed to load theme from %s: %s\n",
            filepath.c_str(), e.what());
    }
    return GetDefaultTheme(); // fallback para os defaults
}

// ============================================================================
// SaveThemeToFile
// ============================================================================

/**
 * @brief Serializa um ThemeConfig em um arquivo JSON via reflect-cpp.
 *
 * @param config    ThemeConfig a salvar.
 * @param filepath  Caminho de destino.
 */
void SaveThemeToFile(const ThemeConfig& config, const std::string& filepath) {
    try {
        rfl::json::save(filepath, config); // serializa via reflect-cpp
    } catch(const std::exception& e) {
        fprintf(stderr, "Warning: Failed to save theme to %s: %s\n",
            filepath.c_str(), e.what());
    }
}

// ============================================================================
// ApplyMicaTheme — sobrecarga principal (ImGuiStyle& explícito)
// ============================================================================

/**
 * @brief Aplica o tema Mica ao ImGuiStyle fornecido.
 *
 * ESTA é a implementação real. A outra sobrecarga delega para cá.
 *
 * Pode ser chamada com qualquer ImGuiStyle — incluindo um temporário na stack,
 * sem precisar de contexto ImGui ativo. Isso é necessário em
 * ColorSettings::ColorSettings() que pode rodar antes de ImGui::CreateContext().
 *
 * ORGANIZAÇÃO:
 *  1. Cores de janela/fundo
 *  2. Cores de frame (inputs, sliders)
 *  3. Barra de título
 *  4. Menu bar
 *  5. Scrollbar
 *  6. Botões
 *  7. Headers (selectable, tree, collapsingheader)
 *  8. Separadores e resize grip
 *  9. Abas
 * 10. Texto
 * 11. Widgets de seleção (checkbox, slider, plotlines)
 * 12. Navegação e docking
 * 13. Dimensões de estilo (rounding, padding, etc.)
 *
 * @param theme  Cores e dimensões do tema a aplicar.
 * @param style  ImGuiStyle de destino (modificado in-place).
 */
void ApplyMicaTheme(const ThemeConfig& theme, ImGuiStyle& style) {
    ImVec4* colors = style.Colors; // ponteiro direto para o array de 54 cores

    // ---- 1. Janela e fundo -----------------------------------------------
    colors[ImGuiCol_WindowBg]    = theme.surface_primary.toImVec4();   // fundo da janela
    colors[ImGuiCol_ChildBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);    // child transparente
    colors[ImGuiCol_PopupBg]     = theme.surface_secondary.toImVec4(); // fundo de popup
    colors[ImGuiCol_Border]      = theme.border.toImVec4();            // bordas
    colors[ImGuiCol_BorderShadow]= ImVec4(0.0f, 0.0f, 0.0f, 0.0f);    // sem sombra

    // ---- 2. Frames (inputs, combos, sliders) -----------------------------
    colors[ImGuiCol_FrameBg]        = theme.frame_bg.toImVec4();         // fundo normal
    colors[ImGuiCol_FrameBgHovered] = theme.frame_bg_hovered.toImVec4(); // fundo em hover
    colors[ImGuiCol_FrameBgActive]  = theme.frame_bg_active.toImVec4();  // fundo ao clicar

    // ---- 3. Barra de título -----------------------------------------------
    colors[ImGuiCol_TitleBg]          = theme.surface_primary.toImVec4();   // título inativo
    colors[ImGuiCol_TitleBgActive]    = theme.surface_secondary.toImVec4(); // título ativo
    colors[ImGuiCol_TitleBgCollapsed] = theme.surface_primary.toImVec4();   // título colapsado

    // ---- 4. Menu bar ------------------------------------------------------
    colors[ImGuiCol_MenuBarBg] = theme.surface_secondary.toImVec4(); // fundo da menu bar

    // ---- 5. Scrollbar -----------------------------------------------------
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.02f, 0.02f, 0.02f, 0.53f); // trilho
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.31f, 0.31f, 0.31f, 1.0f);  // thumb
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.0f);  // thumb hover
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.51f, 0.51f, 0.51f, 1.0f);  // thumb ativo

    // ---- 6. Botões --------------------------------------------------------
    colors[ImGuiCol_Button]        = theme.frame_bg.toImVec4();        // botão normal
    colors[ImGuiCol_ButtonHovered] = theme.hover.toImVec4();           // botão em hover
    colors[ImGuiCol_ButtonActive]  = theme.frame_bg_active.toImVec4(); // botão clicado

    // ---- 7. Headers (Selectable, TreeNode, CollapsingHeader) -------------
    colors[ImGuiCol_Header]        = theme.frame_bg.toImVec4();        // item selecionável
    colors[ImGuiCol_HeaderHovered] = theme.hover.toImVec4();           // hover
    colors[ImGuiCol_HeaderActive]  = theme.frame_bg_active.toImVec4(); // ativo

    // ---- 8. Separador e resize grip --------------------------------------
    colors[ImGuiCol_Separator]        = theme.border.toImVec4();               // linha
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.45f, 0.45f, 0.45f, 0.5f);    // hover
    colors[ImGuiCol_SeparatorActive]  = theme.accent.toImVec4();               // ativo (accent)
    colors[ImGuiCol_ResizeGrip]       = ImVec4(0.289f, 0.289f, 0.289f, 0.45f); // grip normal
    colors[ImGuiCol_ResizeGripHovered]= theme.hover.toImVec4();                // hover
    colors[ImGuiCol_ResizeGripActive] = theme.accent.toImVec4();               // ativo (accent)

    // ---- 9. Abas ----------------------------------------------------------
    colors[ImGuiCol_Tab]          = ImVec4(0.132f, 0.132f, 0.132f, 0.863f); // aba inativa
    colors[ImGuiCol_TabHovered]   = theme.hover.toImVec4();                  // aba em hover
    colors[ImGuiCol_TabSelected]  = theme.accent.toImVec4();                 // aba ativa (accent)
    colors[ImGuiCol_TabDimmed]    = colors[ImGuiCol_Tab];                    // aba de viewport inativa

    // ---- 10. Texto --------------------------------------------------------
    colors[ImGuiCol_Text]         = theme.text_primary.toImVec4();   // texto principal
    colors[ImGuiCol_TextDisabled] = theme.text_secondary.toImVec4(); // texto desabilitado

    // ---- 11. Widgets de seleção e plots -----------------------------------
    colors[ImGuiCol_CheckMark]         = theme.accent.toImVec4(); // checkmark
    colors[ImGuiCol_SliderGrab]        = theme.accent.toImVec4(); // thumb do slider
    colors[ImGuiCol_SliderGrabActive]  = theme.accent.toImVec4(); // thumb ativo
    colors[ImGuiCol_PlotLines]         = ImVec4(0.61f, 0.61f, 0.61f, 1.0f); // linhas de plot
    colors[ImGuiCol_PlotLinesHovered]  = ImVec4(1.0f,  0.43f, 0.35f, 1.0f); // hover
    colors[ImGuiCol_PlotHistogram]     = theme.accent.toImVec4(); // barras
    colors[ImGuiCol_PlotHistogramHovered]= theme.accent.toImVec4(); // barras hover

    // ---- 12. Navegação, seleção e docking --------------------------------
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.067f, 0.341f, 0.608f, 0.35f); // seleção de texto
    colors[ImGuiCol_DragDropTarget]        = ImVec4(0.004f, 0.576f, 0.976f, 0.9f);  // alvo drag&drop
    colors[ImGuiCol_NavCursor]             = theme.accent.toImVec4();                // cursor de navegação
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);        // highlight de janela
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);        // dim de fundo
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.8f, 0.8f, 0.8f, 0.35f);       // dim de modal
    colors[ImGuiCol_DockingPreview]        = ImVec4(0.004f, 0.576f, 0.976f, 0.3f);  // preview de dock
    colors[ImGuiCol_DockingEmptyBg]        = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);        // área vazia de dock

    // ---- 13. Dimensões de estilo -----------------------------------------
    // Aplicadas ao ImGuiStyle recebido — não ao global (exceto na sobrecarga sem &)
    style.FrameRounding    = theme.frame_rounding;    // arredondamento de frames/botões
    style.GrabMinSize      = theme.grab_min_size;     // tamanho mínimo do grab
    style.GrabRounding     = theme.grab_rounding;     // arredondamento do grab
    style.WindowRounding   = theme.window_rounding;   // arredondamento de janelas
    style.PopupRounding    = theme.popup_rounding;    // arredondamento de popups
    style.TabRounding      = theme.tab_rounding;      // arredondamento de abas
    style.FrameBorderSize  = theme.frame_border_size; // espessura de borda de frame
    style.WindowBorderSize = theme.window_border_size;// espessura de borda de janela
    style.PopupBorderSize  = theme.popup_border_size; // espessura de borda de popup
    style.ScrollbarSize    = theme.scrollbar_size;    // largura das scrollbars
    style.Alpha            = 1.0f;                    // opacidade global sempre 1 no Mica
    style.AntiAliasedFill         = true;             // suavização de preenchimento
    style.AntiAliasedLines        = true;             // suavização de linhas
    style.AntiAliasedLinesUseTex  = true;             // suavização via textura
    style.WindowPadding    = ImVec2(10.0f, 10.0f);    // padding interno de janelas
    style.FramePadding     = ImVec2(8.0f,  6.0f);     // padding interno de frames
    style.ItemSpacing      = ImVec2(8.0f,  6.0f);     // espaçamento entre itens
    style.IndentSpacing    = 20.0f;                   // recuo de itens filhos
    style.LogSliderDeadzone= 4.0f;                    // zona morta do slider logarítmico
}

// ============================================================================
// ApplyMicaTheme — sobrecarga de conveniência (usa ImGui::GetStyle())
// ============================================================================

/**
 * @brief Aplica o tema Mica ao ImGuiStyle GLOBAL (ImGui::GetStyle()).
 *
 * Requer contexto ImGui ativo (GImGui != nullptr).
 * Delega para a sobrecarga principal com ImGuiStyle& para evitar duplicação.
 *
 * @param theme  Configuração do tema a aplicar.
 */
void ApplyMicaTheme(const ThemeConfig& theme) {
    ApplyMicaTheme(theme, ImGui::GetStyle()); // usa o estilo global do contexto
}

} // namespace MicaTheme