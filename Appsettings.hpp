#pragma once
#include "pch.hpp"
#include "MicaTheme.h" // MicaTheme::ThemeConfig — serializado junto com AppSettings

/**
 * @file AppSettings.hpp
 * @brief Todas as configurações persistidas entre sessões via settings.json.
 *
 * ESTRUTURA
 * ----------
 *  WindowSettings           — flags booleanas de App (painéis, fullscreen…)
 *  FontSettings             — tamanho base e escala de fonte
 *  StyleSettings            — dimensões do ImGuiStyle (rounding, padding…)
 *  ColorSettings            — as 54 cores do ImGuiStyle::Colors[]
 *  MicaTheme::ThemeConfig   — cores e dimensões do tema Windows 11 Mica
 *
 * TEMA MICA E PERSISTÊNCIA
 * -------------------------
 * MicaTheme::ThemeConfig já é reflect-cpp compatível (apenas floats e a struct
 * Color com floats) — nenhum código extra de serialização é necessário.
 * O campo AppSettings::use_mica_theme controla se o Mica é aplicado após
 * restaurar StyleSettings + ColorSettings em App::ApplyStyleToImGui().
 *
 * ORDEM DE APLICAÇÃO EM ApplyStyleToImGui():
 *  1. StyleSettings  → ImGuiStyle (dimensões)
 *  2. ColorSettings  → ImGuiStyle::Colors[] (paleta salva)
 *  3. Se use_mica_theme: MicaTheme::ApplyMicaTheme(mica_theme) → ImGuiStyle
 *     (sobrescreve tanto dimensões quanto cores com os valores do Mica)
 *
 * Portanto, quando Mica está ativo, ele sempre tem precedência sobre
 * StyleSettings e ColorSettings em caso de conflito — comportamento idêntico
 * ao que ocorria antes da unificação do JSON.
 *
 * CAMPO clear_color
 * ------------------
 * Inicializado com o cinza escuro do Mica (0.129) em vez do azul-acinzentado
 * anterior (0.45/0.55/0.60). Alinhado com a aparência do tema padrão.
 */

// ============================================================================
// Sub-struct: WindowSettings
// ============================================================================

/**
 * @brief Flags de visibilidade de janelas e modos de exibição.
 *
 * Mapeamento 1:1 com os membros públicos de App:
 *   show_console            ↔ g_Settings->window.show_console
 *   show_demo               ↔ g_ShowDemo
 *   show_style_editor       ↔ g_ShowStyleEd
 *   is_fullscreen           ↔ g_IsFullscreen
 *   show_graph              ↔ g_grafico
 *   viewport_docking        ↔ bViewportDocking
 *   implot3d_*              ↔ bImPlot3d_*
 */
struct WindowSettings {
	bool show_window_controls	   = true;	///< Controles de janela (minimizar, fechar) visíveis
    bool show_console              = false; ///< Console ImGui interno visível
    bool show_demo                 = true;  ///< ImGui Demo Window visível
    bool show_style_editor         = false; ///< Style Editor visível
    bool is_fullscreen             = false; ///< Janela em modo fullscreen
    bool show_graph                = false; ///< Gráfico ScrollingBuffer visível
    bool viewport_docking          = false; ///< Viewports flutuantes habilitados
    bool implot3d_realtime_plots   = false; ///< ImPlot3D RealtimePlots visível
    bool implot3d_quad_plots       = false; ///< ImPlot3D QuadPlots visível
    bool implot3d_tick_labels      = false; ///< ImPlot3D TickLabels visível
};

// ============================================================================
// Sub-struct: FontSettings
// ============================================================================

/**
 * @brief Tamanho base e multiplicador de escala da fonte ImGui.
 *
 * font_size_base  → ImGuiStyle::FontSizeBase  (pixels absolutos)
 * font_scale_main → ImGuiStyle::FontScaleMain (multiplicador; 1.0 = sem escala)
 *
 * FontSizeBase é alterado por Ctrl+Scroll (FontScale::ProcessEvent) e pelo
 * slider "Font Size Base" em Windows(). FontScaleMain é alterado pelo slider
 * "Font Scale". Ambos são persistidos em SaveConfig().
 */
struct FontSettings {
    float font_size_base  = 13.0f; ///< Pixels absolutos (ImGuiStyle::FontSizeBase)
    float font_scale_main = 1.0f;  ///< Multiplicador   (ImGuiStyle::FontScaleMain)
};

// ============================================================================
// Sub-struct: StyleSettings
// ============================================================================

/**
 * @brief Cópia serializável dos campos dimensionais do ImGuiStyle.
 *
 * ImVec2 é decomposto em _x / _y porque reflect-cpp não serializa ImVec2.
 * As cores ficam em ColorSettings para separação de responsabilidade.
 *
 * Quando use_mica_theme == true, alguns destes campos são sobrescritos por
 * MicaTheme::ApplyMicaTheme() — que define seus próprios rounding/padding.
 * O StyleEditor ainda pode editá-los, mas o Mica os restaurará ao próximo boot.
 * Para persistir customizações de dimensão com Mica ativo, edite ThemeConfig.
 */
struct StyleSettings {
    // ---- Arredondamento --------------------------------------------------
    float frame_rounding   = 4.0f;  ///< ImGuiStyle::FrameRounding
    float window_rounding  = 8.0f;  ///< ImGuiStyle::WindowRounding
    float popup_rounding   = 4.0f;  ///< ImGuiStyle::PopupRounding
    float tab_rounding     = 4.0f;  ///< ImGuiStyle::TabRounding
    float grab_rounding    = 2.0f;  ///< ImGuiStyle::GrabRounding
    float child_rounding   = 0.0f;  ///< ImGuiStyle::ChildRounding

    // ---- Bordas ----------------------------------------------------------
    float frame_border_size  = 1.0f; ///< ImGuiStyle::FrameBorderSize
    float window_border_size = 1.0f; ///< ImGuiStyle::WindowBorderSize
    float popup_border_size  = 1.0f; ///< ImGuiStyle::PopupBorderSize
    float child_border_size  = 1.0f; ///< ImGuiStyle::ChildBorderSize
    float tab_border_size    = 1.0f; ///< ImGuiStyle::TabBorderSize

    // ---- Padding e espaçamento (ImVec2 → dois floats) --------------------
    float window_padding_x       = 10.0f; ///< ImGuiStyle::WindowPadding.x
    float window_padding_y       = 10.0f; ///< ImGuiStyle::WindowPadding.y
    float frame_padding_x        = 8.0f;  ///< ImGuiStyle::FramePadding.x
    float frame_padding_y        = 6.0f;  ///< ImGuiStyle::FramePadding.y
    float item_spacing_x         = 8.0f;  ///< ImGuiStyle::ItemSpacing.x
    float item_spacing_y         = 6.0f;  ///< ImGuiStyle::ItemSpacing.y
    float item_inner_spacing_x   = 4.0f;  ///< ImGuiStyle::ItemInnerSpacing.x
    float item_inner_spacing_y   = 4.0f;  ///< ImGuiStyle::ItemInnerSpacing.y
    float touch_extra_padding_x  = 0.0f;  ///< ImGuiStyle::TouchExtraPadding.x
    float touch_extra_padding_y  = 0.0f;  ///< ImGuiStyle::TouchExtraPadding.y

    // ---- Outros escalares ------------------------------------------------
    float indent_spacing              = 20.0f;  ///< ImGuiStyle::IndentSpacing
    float grab_min_size               = 10.0f;  ///< ImGuiStyle::GrabMinSize
    float scrollbar_size              = 14.0f;  ///< ImGuiStyle::ScrollbarSize
    float alpha                       = 1.0f;   ///< ImGuiStyle::Alpha
    float disabled_alpha              = 0.6f;   ///< ImGuiStyle::DisabledAlpha
    float curve_tessellation_tol      = 1.25f;  ///< ImGuiStyle::CurveTessellationTol
    float circle_tessellation_max_err = 1.6f;   ///< ImGuiStyle::CircleTessellationMaxError

    // ---- Anti-aliasing ---------------------------------------------------
    bool anti_aliased_fill          = true; ///< ImGuiStyle::AntiAliasedFill
    bool anti_aliased_lines         = true; ///< ImGuiStyle::AntiAliasedLines
    bool anti_aliased_lines_use_tex = true; ///< ImGuiStyle::AntiAliasedLinesUseTex
};

// ============================================================================
// Sub-struct: ImGuiColor — uma cor RGBA nomeada para JSON legível
// ============================================================================

/**
 * @brief Uma cor RGBA com campos nomeados para serialização legível.
 *
 * Usando esta struct em vez de std::array<float,4>, o settings.json exibe:
 *
 *   { "name": "Text", "r": 0.949, "g": 0.949, "b": 0.949, "a": 1.0 }
 *
 * Em vez do array anônimo anterior:
 *
 *   [ 0.949, 0.949, 0.949, 1.0 ]
 *
 * O campo "name" contém o nome do ImGuiCol_* correspondente
 * (ex.: "Text", "WindowBg", "Button") para que o JSON seja auto-documentado.
 * reflect-cpp serializa/desserializa a struct automaticamente pelo nome dos membros.
 */
struct ImGuiColor {
    std::string name = "Unknown"; ///< Nome do slot ImGuiCol_* (ex.: "Text", "Button")
    float r = 1.0f; ///< Componente vermelho [0, 1]
    float g = 1.0f; ///< Componente verde    [0, 1]
    float b = 1.0f; ///< Componente azul     [0, 1]
    float a = 1.0f; ///< Componente alpha    [0, 1]
};

// ============================================================================
// Sub-struct: ColorSettings
// ============================================================================

/**
 * @brief Armazena as 54 cores do ImGuiStyle::Colors[] com nome de cada slot.
 *
 * Cada entrada é um ImGuiColor com os campos name, r, g, b, a.
 * O índice dentro de colors[] corresponde ao ImGuiCol_* enum.
 *
 * JSON RESULTANTE (trecho):
 * @code
 * "color": {
 *   "colors": [
 *     { "name": "Text",            "r": 0.949, "g": 0.949, "b": 0.949, "a": 1.0 },
 *     { "name": "TextDisabled",    "r": 0.698, "g": 0.698, "b": 0.702, "a": 1.0 },
 *     { "name": "WindowBg",        "r": 0.129, "g": 0.129, "b": 0.129, "a": 0.78 },
 *     ...
 *   ]
 * }
 * @endcode
 *
 * DEFAULT COM MICA:
 * O construtor aplica ImGui::StyleColorsDark (preenche todos os slots) e depois
 * MicaTheme::ApplyMicaTheme (sobrescreve com as cores Mica) em um ImGuiStyle
 * temporário. Assim, o primeiro boot sem settings.json já exibe o tema Mica.
 *
 * LEITURA EM ApplyStyleToImGui():
 * O campo name é ignorado na leitura — o índice do vetor determina o slot.
 * O name existe apenas para tornar o JSON legível por humanos.
 */
struct ColorSettings {
    std::vector<ImGuiColor> colors; ///< colors[i] corresponde a ImGuiCol_(i)

    /**
     * @brief Constrói com as cores do tema Mica padrão, nomeadas por ImGuiCol_*.
     *
     * 1. Cria ImGuiStyle temporário e aplica Dark (garante todos os 54 slots).
     * 2. Aplica ApplyMicaTheme → sobrescreve com as cores Mica.
     * 3. Captura em colors[], preenchendo o campo name com ImGui::GetStyleColorName().
     */
    ColorSettings() {
        ImGuiStyle tmp{};                                     // zero-inicializado
        ImGui::StyleColorsDark(&tmp);                         // base: preenche todos os slots
        MicaTheme::ApplyMicaTheme(MicaTheme::GetDefaultTheme(), tmp); // sobrescreve com Mica

        colors.resize(static_cast<std::size_t>(ImGuiCol_COUNT)); // exatamente 54 entradas

        for(int i = 0; i < ImGuiCol_COUNT; ++i) {
            ImGuiColor& c  = colors[static_cast<std::size_t>(i)];
            c.name = ImGui::GetStyleColorName(i); // ex.: "Text", "WindowBg", "Button"
            c.r    = tmp.Colors[i].x;             // componente vermelho
            c.g    = tmp.Colors[i].y;             // componente verde
            c.b    = tmp.Colors[i].z;             // componente azul
            c.a    = tmp.Colors[i].w;             // componente alpha
        }
    }
};

// ============================================================================
// AppSettings — struct raiz serializada em settings.json
// ============================================================================

/**
 * @brief Struct raiz: único tipo passado para rfl::json::save / rfl::json::load.
 *
 * CAMPOS:
 *  clear_color  — cor de fundo do framebuffer Vulkan (RGBA)
 *  window       — flags booleanas de App
 *  font         — tamanho e escala de fonte
 *  style        — dimensões do ImGuiStyle
 *  color        — paleta de 54 cores do ImGuiStyle
 *  mica_theme   — configuração completa do tema Mica (cores + dimensões)
 *  use_mica_theme — liga/desliga o tema Mica; quando false, usa style+color puros
 */
struct AppSettings {
    /// Cor de limpeza do framebuffer — cinza escuro Mica por padrão
    std::vector<float> clear_color = { 0.129f, 0.129f, 0.129f, 1.00f };

    WindowSettings         window;      ///< Flags booleanas de App
    FontSettings           font;        ///< Tamanho e escala de fonte
    StyleSettings          style;       ///< Dimensões do ImGuiStyle (sem cores)
    ColorSettings          color;       ///< Paleta de 54 cores do ImGuiStyle
    MicaTheme::ThemeConfig mica_theme;  ///< Tema Mica: cores + rounding + padding

    /// true  → ApplyStyleToImGui() chama ApplyMicaTheme(mica_theme) no final.
    /// false → apenas style + color são aplicados (tema totalmente customizado).
    bool use_mica_theme = true; ///< Ativa/desativa o tema Mica
};