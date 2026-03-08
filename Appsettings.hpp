#pragma once
#include "pch.hpp"
#include "MicaTheme.h" ///< MicaTheme::ThemeConfig — serializado junto com AppSettings

/**
 * @file AppSettings.hpp
 * @brief Todas as configurações persistidas entre sessões via settings.json.
 *
 * ESTRUTURA
 * ----------
 *  WindowSettings           — flags booleanas de App (painéis, fullscreen…)
 *  FontSettings             — tamanho base, escala de fonte e flags FreeType
 *  FreeTypeSettings         — flags de carregamento/renderização de fontes FreeType
 *  StyleSettings            — dimensões do ImGuiStyle (rounding, padding…)
 *  ColorSettings            — as 54 cores do ImGuiStyle::Colors[]
 *  ViewportFlagSettings     — flags configuráveis de ImGuiViewportFlags_
 *  DockNodeSettings         — flags configuráveis de ImGuiDockNodeFlags_
 *  MicaTheme::ThemeConfig   — cores e dimensões do tema Windows 11 Mica
 *
 * CRITÉRIOS DE INCLUSÃO DAS FLAGS
 * ---------------------------------
 * Apenas flags "universais" (aplicáveis globalmente pelo usuário) são persistidas.
 *
 *  ImGuiViewportFlags — EXCLUÍDAS:
 *    • IsPlatformWindow / IsPlatformMonitor / OwnedByApp  → internas do backend
 *    • CanHostOtherWindows                                → interna do sistema
 *    • IsMinimized / IsFocused                            → saída de status (read-only)
 *    • TopMost                                            → exclusiva de tooltips
 *
 *  ImGuiDockNodeFlags — EXCLUÍDAS (private):
 *    • DockSpace / CentralNode                            → tipo de nó, não configuração
 *    • DockedWindowsInFocusRoute                          → roteamento interno de foco
 *    • NoDocking (máscara composta)                       → máscara, não flag atômica
 *    • *Mask_ / *TransferMask_ / *InheritMask_            → flags de máscara interna
 *
 * TEMA MICA E PERSISTÊNCIA
 * -------------------------
 * MicaTheme::ThemeConfig já é reflect-cpp compatível. O campo
 * AppSettings::use_mica_theme controla se o Mica é aplicado após restaurar
 * StyleSettings + ColorSettings em App::ApplyStyleToImGui().
 *
 * ORDEM DE APLICAÇÃO EM ApplyStyleToImGui():
 *  1. StyleSettings       → ImGuiStyle (dimensões)
 *  2. ColorSettings       → ImGuiStyle::Colors[] (paleta salva)
 *  3. Se use_mica_theme   → MicaTheme::ApplyMicaTheme(mica_theme) → sobrescreve
 *  4. ViewportFlagSettings → aplicado em ImGuiPlatformIO ou por viewport
 *  5. DockNodeSettings    → aplicado ao DockSpace raiz via ImGuiDockNodeFlags
 */

// ============================================================================
// Sub-struct: WindowSettings
// ============================================================================

/**
 * @brief Flags de visibilidade de janelas e modos de exibição.
 *
 * Mapeamento 1:1 com os membros públicos de App:
 *   show_console      ↔ g_Settings->window.show_console
 *   show_demo         ↔ g_ShowDemo
 *   show_style_editor ↔ g_ShowStyleEd
 *   is_fullscreen     ↔ g_IsFullscreen
 *   show_graph        ↔ g_grafico
 *   viewport_docking  ↔ bViewportDocking
 *   implot3d_*        ↔ bImPlot3d_*
 */
struct WindowSettings {
    bool show_window_controls  = true;  ///< Controles de janela (minimizar, fechar) visíveis
    bool show_console          = false; ///< Console ImGui interno visível
    bool show_demo             = true;  ///< ImGui Demo Window visível
    bool show_style_editor     = false; ///< Style Editor visível
    bool is_fullscreen         = false; ///< Janela em modo fullscreen
    bool show_graph            = false; ///< Gráfico ScrollingBuffer visível
    bool viewport_docking      = false; ///< Viewports flutuantes habilitados

    bool implot3d_realtime_plots = false; ///< ImPlot3D RealtimePlots visível
    bool implot3d_quad_plots     = false; ///< ImPlot3D QuadPlots visível
    bool implot3d_tick_labels    = false; ///< ImPlot3D TickLabels visível
};

// ============================================================================
// Sub-struct: FreeTypeSettings
// ============================================================================

/**
 * @brief Flags de carregamento e renderização de fontes via FreeType.
 *
 * Cada bool corresponde a um bit de ImGuiFreeTypeLoaderFlags_.
 * São aplicadas como default global em FontManager::BuildFontConfig() antes
 * de chamar AddFontFromFileTTF().
 *
 * CRITÉRIOS DE RENDERIZAÇÃO (mutuamente exclusivos na prática):
 *   • light_hinting + monochrome → texto 1-bit nítido (terminal/IDE)
 *   • light_hinting + load_color → emoji coloridos com suavização
 *   • force_auto_hint            → hinting automático (fontes sem hints nativos)
 *
 * NOTAS:
 *   • bold / oblique são efeitos sintéticos — prefira variantes de arquivo reais.
 *   • no_hinting e mono_hinting são mutuamente exclusivos: ative apenas um.
 *   • bitmap requer que a fonte contenha strikes de bitmap embutidos.
 */
struct FreeTypeSettings {

    // ---- Hinting ---------------------------------------------------------

    /// ImGuiFreeTypeLoaderFlags_NoHinting — desativa hinting completamente.
    /// Glifos ficam mais suaves/borrados em tamanhos pequenos.
    bool no_hinting     = false;

    /// ImGuiFreeTypeLoaderFlags_NoAutoHint — desativa o auto-hinter do FreeType.
    /// Usa apenas os hints nativos da fonte (se existirem).
    bool no_auto_hint   = false;

    /// ImGuiFreeTypeLoaderFlags_ForceAutoHint — força o auto-hinter mesmo quando
    /// a fonte possui hints nativos. Útil para fontes com hints ruins.
    bool force_auto_hint = false;

    /// ImGuiFreeTypeLoaderFlags_LightHinting — hinting leve (só eixo Y).
    /// Comportamento similar ao ClearType da Microsoft: preserva espaçamento
    /// horizontal e aproxima-se melhor do design original da fonte.
    bool light_hinting  = false;

    /// ImGuiFreeTypeLoaderFlags_MonoHinting — hinting forte para saída 1-bit.
    /// Use apenas com monochrome = true para melhor resultado.
    bool mono_hinting   = false;

    // ---- Estilo sintético ------------------------------------------------

    /// ImGuiFreeTypeLoaderFlags_Bold — emboldenamento sintético via FreeType.
    /// Prefira uma variante Bold real do arquivo de fonte quando disponível.
    bool bold    = false;

    /// ImGuiFreeTypeLoaderFlags_Oblique — inclinação sintética (pseudo-italic).
    /// Prefira uma variante Italic real do arquivo de fonte quando disponível.
    bool oblique = false;

    // ---- Modos de renderização -------------------------------------------

    /// ImGuiFreeTypeLoaderFlags_Monochrome — desativa anti-aliasing dos glifos.
    /// Combine com mono_hinting para texto 1-bit mais nítido.
    bool monochrome = false;

    /// ImGuiFreeTypeLoaderFlags_LoadColor — habilita glifos coloridos em camadas
    /// (emoji coloridos COLR/SVG). Requer fonte com tabelas de cor (ex.: Segoe UI Emoji).
    bool load_color = false;

    /// ImGuiFreeTypeLoaderFlags_Bitmap — usa strikes de bitmap embutidos na fonte
    /// quando disponíveis, em vez de renderizar os glifos vetoriais.
    bool bitmap = false;

    /**
     * @brief Converte os bools para o bitmask ImGuiFreeTypeLoaderFlags_.
     *
     * Uso em FontManager:
     * @code
     *   cfg.FontLoaderFlags = g_Settings->font.freetype.ToFlags();
     * @endcode
     *
     * @return Bitmask int combinando todos os bits ativos.
     */
    [[nodiscard]] int ToFlags() const noexcept {
        int flags = 0;

        // Cada bool é OR-ado com seu bit correspondente usando o operador ternário.
        // static_cast<int> evita conversão implícita de bool → int sem aviso.
        flags |= no_hinting     ? ImGuiFreeTypeLoaderFlags_NoHinting     : 0;
        flags |= no_auto_hint   ? ImGuiFreeTypeLoaderFlags_NoAutoHint    : 0;
        flags |= force_auto_hint? ImGuiFreeTypeLoaderFlags_ForceAutoHint : 0;
        flags |= light_hinting  ? ImGuiFreeTypeLoaderFlags_LightHinting  : 0;
        flags |= mono_hinting   ? ImGuiFreeTypeLoaderFlags_MonoHinting   : 0;
        flags |= bold           ? ImGuiFreeTypeLoaderFlags_Bold          : 0;
        flags |= oblique        ? ImGuiFreeTypeLoaderFlags_Oblique       : 0;
        flags |= monochrome     ? ImGuiFreeTypeLoaderFlags_Monochrome    : 0;
        flags |= load_color     ? ImGuiFreeTypeLoaderFlags_LoadColor     : 0;
        flags |= bitmap         ? ImGuiFreeTypeLoaderFlags_Bitmap        : 0;

        return flags;
    }
};

// ============================================================================
// Sub-struct: FontSettings
// ============================================================================

/**
 * @brief Tamanho base, multiplicador de escala e configuração FreeType.
 *
 * font_size_base  → ImGuiStyle::FontSizeBase  (pixels absolutos)
 * font_scale_main → ImGuiStyle::FontScaleMain (multiplicador; 1.0 = sem escala)
 * freetype        → FreeTypeSettings com flags de loader (ToFlags())
 */
struct FontSettings {
    float font_size_base  = 13.0f; ///< Pixels absolutos (ImGuiStyle::FontSizeBase)
    float font_scale_main = 1.0f;  ///< Multiplicador   (ImGuiStyle::FontScaleMain)

    FreeTypeSettings freetype; ///< Flags de carregamento FreeType aplicadas globalmente
};

// ============================================================================
// Sub-struct: ViewportFlagSettings
// ============================================================================

/**
 * @brief Flags configuráveis de ImGuiViewportFlags_ para janelas de plataforma.
 *
 * Estas flags são aplicadas globalmente a todos os viewports secundários criados
 * pelo sistema multi-viewport. O viewport principal (MainViewport) é gerenciado
 * separadamente pelo backend SDL3.
 *
 * APLICAÇÃO EM App::ApplyViewportFlags():
 * @code
 *   ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
 *   // Itera todos os viewports e ativa/desativa flags conforme os bools.
 *   for (ImGuiViewport* vp : pio.Viewports) {
 *       if (g_Settings->viewport.no_decoration)
 *           vp->Flags |= ImGuiViewportFlags_NoDecoration;
 *       // ...
 *   }
 * @endcode
 *
 * Alternativamente, use ToFlags() para obter o bitmask e aplicar de uma vez:
 * @code
 *   vp->Flags = g_Settings->viewport.ToFlags();
 * @endcode
 *
 * FLAGS EXCLUÍDAS (ver cabeçalho do arquivo para justificativa):
 *   IsPlatformWindow, IsPlatformMonitor, OwnedByApp  — internas do backend
 *   CanHostOtherWindows                              — interna do sistema
 *   IsMinimized, IsFocused                           — saída de status (read-only)
 *   TopMost                                          — exclusiva de tooltips
 */
struct ViewportFlagSettings {

    // ---- Decoração e barra de tarefas ------------------------------------

    /// ImGuiViewportFlags_NoDecoration — remove decorações da janela de plataforma
    /// (barra de título, bordas). Geralmente ativado em popups e tooltips, ou
    /// globalmente se ImGuiConfigFlags_ViewportsNoDecoration estiver ativo.
    bool no_decoration    = false;

    /// ImGuiViewportFlags_NoTaskBarIcon — oculta o ícone da janela na barra de tarefas
    /// do sistema operacional. Ativado automaticamente em popups/tooltips pelo ImGui,
    /// ou globalmente se ImGuiConfigFlags_ViewportsNoTaskBarIcon estiver ativo.
    bool no_task_bar_icon = false;

    // ---- Comportamento de foco ------------------------------------------

    /// ImGuiViewportFlags_NoFocusOnAppearing — a janela de plataforma não captura
    /// o foco quando aparece. Útil para notificações ou overlays passivos.
    bool no_focus_on_appearing = false;

    /// ImGuiViewportFlags_NoFocusOnClick — a janela de plataforma não captura
    /// o foco quando clicada. Permite clicar em janelas flutuantes sem desviar
    /// o foco da janela principal.
    bool no_focus_on_click     = false;

    // ---- Interação e renderização ----------------------------------------

    /// ImGuiViewportFlags_NoInputs — o mouse passa através da janela de plataforma
    /// (hit-test always miss). Permite arrastar janelas enquanto se vê através delas.
    /// Use com cuidado: impossibilita interação com widgets nesta janela.
    bool no_inputs          = false;

    /// ImGuiViewportFlags_NoRendererClear — o renderer não precisa limpar o
    /// framebuffer antes de renderizar (pois será preenchido completamente).
    /// Ative para viewports opacos onde a limpeza é redundante.
    bool no_renderer_clear  = false;

    /// ImGuiViewportFlags_NoAutoMerge — impede que este viewport seja fundido
    /// (merged) automaticamente em outro host window quando as janelas se sobrepõem.
    /// Útil para forçar janelas a permanecerem independentes.
    bool no_auto_merge      = false;

    /**
     * @brief Converte os bools para o bitmask ImGuiViewportFlags_.
     *
     * Uso em App::ApplyViewportFlags():
     * @code
     *   vp->Flags |= g_Settings->viewport.ToFlags();
     * @endcode
     *
     * @return Bitmask ImGuiViewportFlags combinando todos os bits ativos.
     */
    [[nodiscard]] ImGuiViewportFlags ToFlags() const noexcept {
        ImGuiViewportFlags flags = ImGuiViewportFlags_None;

        flags |= no_decoration        ? ImGuiViewportFlags_NoDecoration        : 0;
        flags |= no_task_bar_icon     ? ImGuiViewportFlags_NoTaskBarIcon       : 0;
        flags |= no_focus_on_appearing? ImGuiViewportFlags_NoFocusOnAppearing  : 0;
        flags |= no_focus_on_click    ? ImGuiViewportFlags_NoFocusOnClick      : 0;
        flags |= no_inputs            ? ImGuiViewportFlags_NoInputs            : 0;
        flags |= no_renderer_clear    ? ImGuiViewportFlags_NoRendererClear     : 0;
        flags |= no_auto_merge        ? ImGuiViewportFlags_NoAutoMerge         : 0;

        return flags;
    }
};

// ============================================================================
// Sub-struct: DockNodeSettings
// ============================================================================

/**
 * @brief Flags configuráveis de ImGuiDockNodeFlags_ para o DockSpace raiz.
 *
 * Inclui as flags públicas (imgui.h) e as flags privadas com uso prático
 * em aplicações (controle de aba, botões de UI e restrições de docking).
 *
 * APLICAÇÃO EM App::SetupDockSpace():
 * @code
 *   ImGui::DockSpace(
 *       ImGui::GetID("MainDockSpace"),
 *       ImVec2(0.0f, 0.0f),
 *       g_Settings->dock.ToFlags()
 *   );
 * @endcode
 *
 * FLAGS EXCLUÍDAS (ver cabeçalho do arquivo para justificativa):
 *   DockSpace / CentralNode                 — tipo de nó, não configuração de usuário
 *   DockedWindowsInFocusRoute               — roteamento interno de foco
 *   NoDocking (máscara composta)            — máscara, use os bools individuais
 *   *Mask_ / *TransferMask_ / *InheritMask_ — flags de máscara interna do ImGui
 *
 * GRUPOS FUNCIONAIS:
 *  • Redimensionamento    : no_resize, no_resize_x, no_resize_y
 *  • Undocking            : no_undocking
 *  • Splitting            : no_docking_split, no_docking_split_other
 *  • Docking sobre nós    : no_docking_over_me, no_docking_over_other,
 *                           no_docking_over_empty, no_docking_over_central_node
 *  • Barra de abas        : auto_hide_tab_bar, no_tab_bar, hidden_tab_bar
 *  • Chrome da janela     : no_window_menu_button, no_close_button
 *  • Layout               : passthru_central_node
 */
struct DockNodeSettings {

    // ---- Redimensionamento -----------------------------------------------

    /// ImGuiDockNodeFlags_NoResize — desabilita redimensionamento do nó pelo
    /// usuário (arrasto da borda entre painéis).
    bool no_resize   = false;

    /// ImGuiDockNodeFlags_NoResizeX — desabilita redimensionamento apenas no
    /// eixo horizontal. Permite redimensionar verticalmente.
    bool no_resize_x = false;

    /// ImGuiDockNodeFlags_NoResizeY — desabilita redimensionamento apenas no
    /// eixo vertical. Permite redimensionar horizontalmente.
    bool no_resize_y = false;

    // ---- Undocking -------------------------------------------------------

    /// ImGuiDockNodeFlags_NoUndocking — impede que janelas dockadas sejam
    /// arrastadas para fora do nó (undocking). O nó fica "preso".
    bool no_undocking = false;

    // ---- Splitting (divisão do nó) ---------------------------------------

    /// ImGuiDockNodeFlags_NoDockingSplit — impede que este nó seja dividido
    /// ao soltar uma janela sobre ele (não cria sub-nós).
    bool no_docking_split = false;

    /// ImGuiDockNodeFlags_NoDockingSplitOther [private] — impede que este nó
    /// divida outros nós/janelas ao ser arrastado sobre eles.
    bool no_docking_split_other = false;

    // ---- Restrições de docking sobre nós ---------------------------------

    /// ImGuiDockNodeFlags_NoDockingOverCentralNode — impede docking diretamente
    /// sobre o nó central (útil para preservar a área de conteúdo principal vazia).
    bool no_docking_over_central_node = false;

    /// ImGuiDockNodeFlags_NoDockingOverMe [private] — impede que outras janelas
    /// ou nós façam docking sobre este nó.
    bool no_docking_over_me = false;

    /// ImGuiDockNodeFlags_NoDockingOverOther [private] — impede que este nó
    /// faça docking sobre outra janela ou nó não-vazio.
    bool no_docking_over_other = false;

    /// ImGuiDockNodeFlags_NoDockingOverEmpty [private] — impede que este nó
    /// faça docking sobre um nó vazio (ex.: DockSpace sem janelas).
    bool no_docking_over_empty = false;

    // ---- Barra de abas ---------------------------------------------------

    /// ImGuiDockNodeFlags_AutoHideTabBar — esconde automaticamente a barra de abas
    /// quando o nó contém apenas uma janela, exibindo-a novamente ao adicionar outra.
    bool auto_hide_tab_bar = false;

    /// ImGuiDockNodeFlags_NoTabBar [private] — remove completamente a barra de abas
    /// do nó. Sem triângulo para reativá-la (diferente de hidden_tab_bar).
    bool no_tab_bar = false;

    /// ImGuiDockNodeFlags_HiddenTabBar [private] — oculta a barra de abas mas mantém
    /// um triângulo no canto que permite ao usuário reexibi-la.
    bool hidden_tab_bar = false;

    // ---- Chrome da janela ------------------------------------------------

    /// ImGuiDockNodeFlags_NoWindowMenuButton [private] — remove o botão de menu
    /// da janela (aquele que aparece no lugar do botão de colapso em nós dockados).
    bool no_window_menu_button = false;

    /// ImGuiDockNodeFlags_NoCloseButton [private] — remove o botão de fechar
    /// das janelas dockadas neste nó.
    bool no_close_button = false;

    // ---- Layout ----------------------------------------------------------

    /// ImGuiDockNodeFlags_PassthruCentralNode — o nó central do DockSpace é
    /// transparente (sem fundo) e passa cliques/scroll para a janela de host.
    /// Use quando o DockSpace cobre toda a janela e você quer ver o background.
    bool passthru_central_node = false;

    /**
     * @brief Converte os bools para o bitmask ImGuiDockNodeFlags.
     *
     * Uso em App::SetupDockSpace():
     * @code
     *   ImGui::DockSpace(id, size, g_Settings->dock.ToFlags());
     * @endcode
     *
     * @return Bitmask ImGuiDockNodeFlags combinando todos os bits ativos.
     */
    [[nodiscard]] ImGuiDockNodeFlags ToFlags() const noexcept {
        ImGuiDockNodeFlags flags = ImGuiDockNodeFlags_None;

        // Redimensionamento
        flags |= no_resize                  ? ImGuiDockNodeFlags_NoResize                   : 0;
        flags |= no_resize_x                ? ImGuiDockNodeFlags_NoResizeX                  : 0;
        flags |= no_resize_y                ? ImGuiDockNodeFlags_NoResizeY                  : 0;

        // Undocking
        flags |= no_undocking               ? ImGuiDockNodeFlags_NoUndocking                : 0;

        // Splitting
        flags |= no_docking_split           ? ImGuiDockNodeFlags_NoDockingSplit              : 0;
        flags |= no_docking_split_other     ? ImGuiDockNodeFlags_NoDockingSplitOther         : 0;

        // Restrições de docking
        flags |= no_docking_over_central_node ? ImGuiDockNodeFlags_NoDockingOverCentralNode  : 0;
        flags |= no_docking_over_me           ? ImGuiDockNodeFlags_NoDockingOverMe            : 0;
        flags |= no_docking_over_other        ? ImGuiDockNodeFlags_NoDockingOverOther         : 0;
        flags |= no_docking_over_empty        ? ImGuiDockNodeFlags_NoDockingOverEmpty         : 0;

        // Barra de abas
        flags |= auto_hide_tab_bar          ? ImGuiDockNodeFlags_AutoHideTabBar              : 0;
        flags |= no_tab_bar                 ? ImGuiDockNodeFlags_NoTabBar                    : 0;
        flags |= hidden_tab_bar             ? ImGuiDockNodeFlags_HiddenTabBar                : 0;

        // Chrome
        flags |= no_window_menu_button      ? ImGuiDockNodeFlags_NoWindowMenuButton          : 0;
        flags |= no_close_button            ? ImGuiDockNodeFlags_NoCloseButton               : 0;

        // Layout
        flags |= passthru_central_node      ? ImGuiDockNodeFlags_PassthruCentralNode         : 0;

        return flags;
    }
};

// ============================================================================
// Sub-struct: StyleSettings
// ============================================================================

/**
 * @brief Cópia serializável dos campos dimensionais do ImGuiStyle.
 *
 * ImVec2 é decomposto em _x / _y porque reflect-cpp não serializa ImVec2.
 * As cores ficam em ColorSettings para separação de responsabilidade.
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
 * JSON resultante:
 *   { "name": "Text", "r": 0.949, "g": 0.949, "b": 0.949, "a": 1.0 }
 *
 * O campo "name" contém o nome do ImGuiCol_* correspondente para que o JSON
 * seja auto-documentado. O índice do vetor determina o slot na leitura.
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
 * O campo name é ignorado na leitura — existe apenas para legibilidade do JSON.
 */
struct ColorSettings {
    std::vector<ImGuiColor> colors; ///< colors[i] corresponde a ImGuiCol_(i)

    /**
     * @brief Constrói com as cores do tema Mica padrão, nomeadas por ImGuiCol_*.
     *
     * 1. Cria ImGuiStyle temporário e aplica Dark (garante todos os 54 slots).
     * 2. Aplica ApplyMicaTheme → sobrescreve com as cores Mica.
     * 3. Captura em colors[], preenchendo o campo name com GetStyleColorName().
     */
    ColorSettings() {
        ImGuiStyle tmp{};
        ImGui::StyleColorsDark(&tmp);
        MicaTheme::ApplyMicaTheme(MicaTheme::GetDefaultTheme(), tmp);

        colors.resize(static_cast<std::size_t>(ImGuiCol_COUNT));

        for(int i = 0; i < ImGuiCol_COUNT; ++i) {
            ImGuiColor& c = colors[static_cast<std::size_t>(i)];
            c.name = ImGui::GetStyleColorName(i);
            c.r    = tmp.Colors[i].x;
            c.g    = tmp.Colors[i].y;
            c.b    = tmp.Colors[i].z;
            c.a    = tmp.Colors[i].w;
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
 *  clear_color    — cor de fundo do framebuffer Vulkan (RGBA)
 *  window         — flags booleanas de App
 *  font           — tamanho, escala e flags FreeType de fonte
 *  style          — dimensões do ImGuiStyle
 *  color          — paleta de 54 cores do ImGuiStyle
 *  viewport       — flags de ImGuiViewportFlags_ para viewports secundários
 *  dock           — flags de ImGuiDockNodeFlags_ para o DockSpace raiz
 *  mica_theme     — configuração completa do tema Mica (cores + dimensões)
 *  use_mica_theme — liga/desliga o tema Mica
 *
 * ORDEM DE APLICAÇÃO EM ApplyStyleToImGui():
 *  1. StyleSettings         → ImGuiStyle (dimensões)
 *  2. ColorSettings         → ImGuiStyle::Colors[] (paleta)
 *  3. Se use_mica_theme     → MicaTheme::ApplyMicaTheme(mica_theme)
 *  4. ViewportFlagSettings  → viewports via ImGuiPlatformIO
 *  5. DockNodeSettings      → DockSpace raiz via ImGui::DockSpace()
 *  6. FreeTypeSettings      → ImFontConfig::FontLoaderFlags em FontManager
 */
struct AppSettings {
    /// Cor de limpeza do framebuffer — cinza escuro Mica por padrão
    std::vector<float> clear_color = { 0.129f, 0.129f, 0.129f, 1.00f };

    WindowSettings         window;    ///< Flags booleanas de App
    FontSettings           font;      ///< Tamanho, escala e flags FreeType de fonte
    StyleSettings          style;     ///< Dimensões do ImGuiStyle (sem cores)
    ColorSettings          color;     ///< Paleta de 54 cores do ImGuiStyle
    ViewportFlagSettings   viewport;  ///< Flags de viewport (multi-viewport)
    DockNodeSettings       dock;      ///< Flags do DockSpace raiz
    MicaTheme::ThemeConfig mica_theme;///< Tema Mica: cores + rounding + padding

    /// true  → ApplyStyleToImGui() chama ApplyMicaTheme(mica_theme) no final.
    /// false → apenas style + color são aplicados (tema totalmente customizado).
    bool use_mica_theme = true; ///< Ativa/desativa o tema Mica
};