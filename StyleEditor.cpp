/**
 * @file StyleEditor.cpp
 * @brief Editor visual do ImGuiStyle com persistência delegada para App::SaveConfig().
 *
 * MUDANÇA DE PERSISTÊNCIA
 * ------------------------
 * Antes: StyleEditor::SaveToFile("imgui_style.json") / LoadFromFile() gerenciavam
 * um arquivo JSON próprio, separado de settings.json.
 *
 * Agora: Toda a persistência é centralizada em App::SaveConfig() / LoadConfig().
 *   - App::SyncStyleFromImGui() captura ImGuiStyle → AppSettings::style + color.
 *   - App::ApplyStyleToImGui() aplica AppSettings::style + color → ImGuiStyle.
 *   - StyleEditor não escreve mais em disco diretamente.
 *
 * CONSEQUÊNCIA PARA ESTA CLASSE:
 *   - SaveToFile() e LoadFromFile() agora DELEGAM para g_App->SaveConfig() e
 *     g_App->LoadConfig() respectivamente, em vez de operar em um arquivo separado.
 *   - Os botões "Save to JSON" e "Load from JSON" no Show() chamam esses helpers.
 *   - StyleEditor::StyleSettings ainda existe para compatibilidade interna (ApplyToStyle /
 *     LoadFromStyle usados pelo Show() para o fluxo Save Ref / Revert Ref), mas NÃO é
 *     mais a fonte de verdade para persistência em disco.
 *
 * FONTE DE VERDADE:
 *   Disco   → AppSettings::style + AppSettings::color em settings.json
 *   Runtime → ImGuiStyle (ImGui::GetStyle())
 *   StyleEditor apenas lê e escreve ImGuiStyle diretamente; App sincroniza com o disco.
 */

#include "pch.hpp"
#include "StyleEditor.hpp"
#include "App.hpp" // g_App — necessário para delegar Save/Load ao App

// =============================================================================
// StyleSettings::ApplyToStyle — sub-struct → ImGuiStyle
// =============================================================================

/**
 * @brief Copia os campos de StyleSettings para o ImGuiStyle fornecido.
 *
 * Usado pelo fluxo interno "Revert Ref" do Show():
 *   style = *ref_style → restaura o ImGuiStyle a partir de uma cópia salva.
 * NÃO é mais usado para persistência em disco.
 *
 * @param style  ImGuiStyle de destino (geralmente ImGui::GetStyle()).
 */
void StyleEditor::StyleSettings::ApplyToStyle(ImGuiStyle& style) const {
    style.FrameRounding   = frame_rounding;   // arredondamento dos frames/botões
    style.WindowRounding  = window_rounding;  // arredondamento das janelas
    style.PopupRounding   = popup_rounding;   // arredondamento dos popups
    style.TabRounding     = tab_rounding;     // arredondamento das abas
    style.GrabRounding    = grab_rounding;    // arredondamento dos sliders/scrollbars
    style.ChildRounding   = child_rounding;   // arredondamento das child windows

    style.FrameBorderSize  = frame_border_size;  // espessura da borda dos frames
    style.WindowBorderSize = window_border_size; // espessura da borda das janelas
    style.PopupBorderSize  = popup_border_size;  // espessura da borda dos popups
    style.ChildBorderSize  = child_border_size;  // espessura da borda das child windows
    style.TabBorderSize    = tab_border_size;    // espessura da borda das abas

    // ImVec2 reconstruído a partir dos dois floats separados
    style.WindowPadding     = ImVec2(window_padding_x,      window_padding_y);
    style.FramePadding      = ImVec2(frame_padding_x,       frame_padding_y);
    style.ItemSpacing       = ImVec2(item_spacing_x,        item_spacing_y);
    style.ItemInnerSpacing  = ImVec2(item_inner_spacing_x,  item_inner_spacing_y);
    style.TouchExtraPadding = ImVec2(touch_extra_padding_x, touch_extra_padding_y);

    style.IndentSpacing  = indent_spacing;   // recuo para itens filhos
    style.GrabMinSize    = grab_min_size;    // tamanho mínimo do grab dos sliders
    style.ScrollbarSize  = scrollbar_size;   // largura das scrollbars

    style.Alpha                      = alpha;                       // opacidade global
    style.DisabledAlpha              = disabled_alpha;              // opacidade dos desabilitados
    style.CurveTessellationTol       = curve_tessellation_tol;      // tolerância de curvas
    style.CircleTessellationMaxError = circle_tessellation_max_error; // erro máximo círculos

    style.AntiAliasedFill         = anti_aliased_fill;          // suavização de preenchimento
    style.AntiAliasedLines        = anti_aliased_lines;         // suavização de linhas
    style.AntiAliasedLinesUseTex  = anti_aliased_lines_use_tex; // suavização via textura
}

// =============================================================================
// StyleSettings::LoadFromStyle — ImGuiStyle → sub-struct
// =============================================================================

/**
 * @brief Captura os campos do ImGuiStyle fornecido em StyleSettings.
 *
 * Usado pelo fluxo interno "Save Ref" do Show():
 *   m_RefSavedStyle = style → salva uma cópia local do estado atual.
 * NÃO persiste em disco — isso é responsabilidade de App::SaveConfig().
 *
 * @param style  ImGuiStyle de origem (geralmente ImGui::GetStyle()).
 */
void StyleEditor::StyleSettings::LoadFromStyle(const ImGuiStyle& style) {
    frame_rounding  = style.FrameRounding;  // captura arredondamento dos frames
    window_rounding = style.WindowRounding; // captura arredondamento das janelas
    popup_rounding  = style.PopupRounding;  // captura arredondamento dos popups
    tab_rounding    = style.TabRounding;    // captura arredondamento das abas
    grab_rounding   = style.GrabRounding;   // captura arredondamento dos grabs
    child_rounding  = style.ChildRounding;  // captura arredondamento das child windows

    frame_border_size  = style.FrameBorderSize;  // captura bordas dos frames
    window_border_size = style.WindowBorderSize; // captura bordas das janelas
    popup_border_size  = style.PopupBorderSize;  // captura bordas dos popups
    child_border_size  = style.ChildBorderSize;  // captura bordas das child windows
    tab_border_size    = style.TabBorderSize;    // captura bordas das abas

    // ImVec2 decomposto em dois floats (reflect-cpp não serializa ImVec2)
    window_padding_x      = style.WindowPadding.x;
    window_padding_y      = style.WindowPadding.y;
    frame_padding_x       = style.FramePadding.x;
    frame_padding_y       = style.FramePadding.y;
    item_spacing_x        = style.ItemSpacing.x;
    item_spacing_y        = style.ItemSpacing.y;
    item_inner_spacing_x  = style.ItemInnerSpacing.x;
    item_inner_spacing_y  = style.ItemInnerSpacing.y;
    touch_extra_padding_x = style.TouchExtraPadding.x;
    touch_extra_padding_y = style.TouchExtraPadding.y;

    indent_spacing  = style.IndentSpacing;  // captura recuo
    grab_min_size   = style.GrabMinSize;    // captura tamanho mínimo do grab
    scrollbar_size  = style.ScrollbarSize;  // captura largura das scrollbars

    alpha                       = style.Alpha;                       // captura opacidade global
    disabled_alpha              = style.DisabledAlpha;               // captura opacidade desabilitados
    curve_tessellation_tol      = style.CurveTessellationTol;        // captura tolerância de curvas
    circle_tessellation_max_error = style.CircleTessellationMaxError; // captura erro círculos

    anti_aliased_fill          = style.AntiAliasedFill;         // captura flag de suavização
    anti_aliased_lines         = style.AntiAliasedLines;        // captura flag de linhas
    anti_aliased_lines_use_tex = style.AntiAliasedLinesUseTex;  // captura flag via textura
}

// =============================================================================
// Construtor
// =============================================================================

/**
 * @brief Inicializa StyleEditor capturando o ImGuiStyle atual como referência.
 *
 * m_Settings.LoadFromStyle() armazena uma cópia local do estado inicial.
 * m_RefSavedStyle é a referência usada pelo botão "Revert Ref" do Show().
 */
StyleEditor::StyleEditor() {
    m_Settings.LoadFromStyle(ImGui::GetStyle()); // captura o estilo atual (pode já ter sido restaurado por App)
    m_RefSavedStyle = ImGui::GetStyle();          // cópia para o botão "Revert Ref"
}

// =============================================================================
// ApplySettings
// =============================================================================

/**
 * @brief Aplica m_Settings ao ImGuiStyle global atual.
 *
 * Usado internamente após Load / Reset para efetivar as mudanças.
 * NÃO persiste em disco — App::SaveConfig() faz isso.
 */
void StyleEditor::ApplySettings() {
    ImGuiStyle& style = ImGui::GetStyle(); // estilo global do contexto ImGui
    m_Settings.ApplyToStyle(style);        // copia campos de m_Settings → ImGuiStyle
}

// =============================================================================
// LoadFromFile — DELEGADO para App::LoadConfig()
// =============================================================================

/**
 * @brief Delega o carregamento para App::LoadConfig(), que lê settings.json.
 *
 * A assinatura foi mantida para compatibilidade com código existente que chame
 * StyleEditor::LoadFromFile(). O parâmetro filepath é ignorado — a fonte
 * de verdade é sempre settings.json gerenciado pelo App.
 *
 * @param filepath  Ignorado. Mantido por compatibilidade de assinatura.
 * @return          Cópia de m_Settings após a aplicação do estilo carregado.
 */
StyleEditor::StyleSettings StyleEditor::LoadFromFile(const std::string& filepath) {
    (void)filepath; // parâmetro não utilizado — persistence é via App

    // Delega para App::LoadConfig() que lê settings.json completo
    if(g_App)
        g_App->LoadConfig(); // restaura AppSettings e aplica ao ImGuiStyle

    // Sincroniza m_Settings com o ImGuiStyle recém-aplicado
    m_Settings.LoadFromStyle(ImGui::GetStyle());
    return m_Settings; // retorna cópia local atualizada
}

// =============================================================================
// SaveToFile — DELEGADO para App::SaveConfig()
// =============================================================================

/**
 * @brief Delega a gravação para App::SaveConfig(), que escreve settings.json.
 *
 * A assinatura foi mantida para compatibilidade. O parâmetro filepath é ignorado.
 *
 * @param filepath  Ignorado. Mantido por compatibilidade de assinatura.
 */
void StyleEditor::SaveToFile(const std::string& filepath) {
    (void)filepath; // parâmetro não utilizado — persistence é via App

    // Delega para App::SaveConfig() que:
    //  1. SyncFlagsToSettings()  → flags de App → AppSettings::window
    //  2. SyncStyleFromImGui()   → ImGuiStyle → AppSettings::style + color
    //  3. rfl::json::save()      → settings.json
    if(g_App)
        g_App->SaveConfig();
}

// =============================================================================
// ResetToDefault
// =============================================================================

/**
 * @brief Restaura o ImGuiStyle para o tema Dark padrão do ImGui.
 *
 * Após resetar, chama App::SaveConfig() para persistir o reset em disco.
 */
void StyleEditor::ResetToDefault() {
    ImGuiStyle default_style;               // construído com valores default do ImGui
    ImGui::StyleColorsDark(&default_style); // aplica o tema Dark (cores padrão)
    m_Settings.LoadFromStyle(default_style); // captura em m_Settings
    ApplySettings();                         // aplica ao ImGuiStyle global

    // Persiste o reset imediatamente
    if(g_App)
        g_App->SaveConfig();
}

// =============================================================================
// Show — janela principal do editor de estilo
// =============================================================================

/**
 * @brief Renderiza a janela do Style Editor e delega persistência para App.
 *
 * BOTÕES E SUAS AÇÕES:
 *  "Save Ref"      → copia ImGuiStyle → m_RefSavedStyle (memória local)
 *  "Revert Ref"    → restaura ImGuiStyle ← m_RefSavedStyle (memória local)
 *  "Reset Default" → ResetToDefault() → ImGui Dark + App::SaveConfig()
 *  "Save to JSON"  → App::SaveConfig() (antes: escrevia imgui_style.json)
 *  "Load from JSON"→ App::LoadConfig() (antes: lia imgui_style.json)
 *
 * @param ref_style  Ponteiro externo para referência; nullptr usa m_RefSavedStyle.
 * @param p_open     Ponteiro para bool que controla visibilidade da janela.
 * @return           true se a janela está aberta.
 */
bool StyleEditor::Show(ImGuiStyle* ref_style, bool* p_open) {
    ImGuiStyle& style = ImGui::GetStyle(); // estilo global — lido e escrito diretamente

    if(!ImGui::Begin("Style Editor", p_open)) {
        ImGui::End();
        return false; // janela minimizada ou fechada
    }

    // Na primeira chamada, salva o estado inicial como referência local
    if(m_Init && ref_style == nullptr) {
        m_RefSavedStyle = style; // snapshot do estado inicial
    }
    m_Init = false;
    if(ref_style == nullptr)
        ref_style = &m_RefSavedStyle; // usa a referência local como padrão

    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.50f); // largura dos widgets

    // ---- Informações gerais -----------------------------------------------
    ImGui::SeparatorText("General");

    if((ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) == 0) {
        ImGui::BulletText(
            "Warning: Font scaling will NOT be smooth, because\n"
            "ImGuiBackendFlags_RendererHasTextures is not set!");
        ImGui::BulletText("For instructions, see:");
        ImGui::SameLine();
        ImGui::TextLinkOpenURL("docs/BACKENDS.md",
            "https://github.com/ocornut/imgui/blob/master/docs/BACKENDS.md");
    }

    // Seletor de tema de cores pré-definido (Dark / Light / Classic)
    if(ImGui::ShowStyleSelector("Colors##Selector")) {
        m_RefSavedStyle = style;             // atualiza a referência local
        m_Settings.LoadFromStyle(style);     // captura em m_Settings
        if(g_App) g_App->SaveConfig();       // persiste a mudança de tema
    }

    ImGui::ShowFontSelector("Fonts##Selector"); // seletor de fonte ativo

    // FontSizeBase: tamanho base em pixels — FontLoader re-rasteriza automaticamente
    if(ImGui::DragFloat("FontSizeBase", &style.FontSizeBase, 0.20f, 5.0f, 100.0f, "%.0f")) {
        style._NextFrameFontSizeBase = style.FontSizeBase; // aplica no próximo frame
        if(g_App) g_App->SaveConfig();                     // persiste o novo tamanho
    }
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::Text(" (out %.2f)", ImGui::GetFontSize()); // mostra o tamanho final calculado

    // FontScaleMain: multiplicador sobre FontSizeBase
    if(ImGui::DragFloat("FontScaleMain", &style.FontScaleMain, 0.02f, 0.5f, 4.0f)) {
        if(g_App) g_App->SaveConfig(); // persiste o multiplicador
    }

    // FontScaleDpi: escala por DPI (geralmente controlado pelo sistema)
    if(ImGui::DragFloat("FontScaleDpi", &style.FontScaleDpi, 0.02f, 0.5f, 4.0f)) {
        if(g_App) g_App->SaveConfig(); // persiste a escala DPI
    }

    // ---- Botões de persistência ------------------------------------------
    ImGui::Separator();

    if(ImGui::Button("Save Ref")) {
        // Salva o estado atual como referência local (para "Revert Ref")
        *ref_style = m_RefSavedStyle = style; // atualiza ambas as referências
        m_Settings.LoadFromStyle(style);       // captura em m_Settings
        // NÃO persiste em disco — "Save Ref" é apenas uma memória local
    }
    ImGui::SameLine();

    if(ImGui::Button("Revert Ref")) {
        // Restaura ImGuiStyle a partir da referência local (não do disco)
        style = *ref_style;                  // copia a referência → ImGuiStyle
        m_Settings.LoadFromStyle(style);     // atualiza m_Settings
        if(g_App) g_App->SaveConfig();       // persiste o estado revertido
    }
    ImGui::SameLine();

    if(ImGui::Button("Reset Default")) {
        ResetToDefault(); // tema Dark + App::SaveConfig() internamente
    }
    ImGui::SameLine();

    if(ImGui::Button("Save to JSON")) {
        // Delega para App::SaveConfig() — escreve settings.json (não imgui_style.json)
        if(g_App) g_App->SaveConfig();
    }
    ImGui::SameLine();

    if(ImGui::Button("Load from JSON")) {
        // Delega para App::LoadConfig() — lê settings.json e aplica ao ImGuiStyle
        if(g_App) g_App->LoadConfig();
        m_Settings.LoadFromStyle(ImGui::GetStyle()); // sincroniza m_Settings
    }

    ImGui::SetItemTooltip(
        "Save/Load operate on settings.json (unified config file).\n"
        "All app settings, style, colors and flags are saved together.");

    ImGui::SeparatorText("Details");

    // ---- Abas de configuração -----------------------------------------------
    if(ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None)) {

        if(ImGui::BeginTabItem("Sizes")) {
            DrawSizeTab(); // sliders de dimensões — cada alteração chama SaveConfig()
            ImGui::EndTabItem();
        }

        if(ImGui::BeginTabItem("Colors")) {
            DrawColorsTab(); // paleta de cores — SaveConfig() no color picker
            ImGui::EndTabItem();
        }

        if(ImGui::BeginTabItem("Fonts")) {
            DrawFontsTab(); // informações do atlas de fontes
            ImGui::EndTabItem();
        }

        if(ImGui::BeginTabItem("Rendering")) {
            DrawRenderingTab(); // checkboxes de anti-aliasing e tolerâncias
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::PopItemWidth();
    ImGui::End();

    return true; // janela ainda está aberta
}

// =============================================================================
// DrawSizeTab
// =============================================================================

/**
 * @brief Aba "Sizes" — sliders de padding, rounding e bordas.
 *
 * Cada slider que mudar persiste via App::SaveConfig() usando o padrão
 * if(ImGui::SliderFloat(...)) SaveConfig();
 */
void StyleEditor::DrawSizeTab() {
    ImGuiStyle& style = ImGui::GetStyle();

    ImGui::SeparatorText("Main");

    // Cada ImGui::SliderFloat* retorna true se o valor foi alterado neste frame.
    // Chamar SaveConfig() nessa condição garante persistência imediata.

    if(ImGui::SliderFloat2("WindowPadding",     (float*)&style.WindowPadding,    0.0f, 20.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat2("FramePadding",      (float*)&style.FramePadding,     0.0f, 20.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat2("ItemSpacing",       (float*)&style.ItemSpacing,      0.0f, 20.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat2("ItemInnerSpacing",  (float*)&style.ItemInnerSpacing, 0.0f, 20.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat2("TouchExtraPadding", (float*)&style.TouchExtraPadding,0.0f, 10.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat( "IndentSpacing",     &style.IndentSpacing,            0.0f, 30.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat( "GrabMinSize",       &style.GrabMinSize,              1.0f, 20.0f, "%.0f")) if(g_App) g_App->SaveConfig();

    ImGui::SeparatorText("Borders");

    if(ImGui::SliderFloat("WindowBorderSize", &style.WindowBorderSize, 0.0f, 1.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat("ChildBorderSize",  &style.ChildBorderSize,  0.0f, 1.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat("PopupBorderSize",  &style.PopupBorderSize,  0.0f, 1.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat("FrameBorderSize",  &style.FrameBorderSize,  0.0f, 1.0f, "%.0f")) if(g_App) g_App->SaveConfig();

    ImGui::SeparatorText("Rounding");

    if(ImGui::SliderFloat("WindowRounding",   &style.WindowRounding, 0.0f, 12.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat("ChildRounding",    &style.ChildRounding,  0.0f, 12.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat("FrameRounding",    &style.FrameRounding,  0.0f, 12.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat("PopupRounding",    &style.PopupRounding,  0.0f, 12.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat("GrabRounding",     &style.GrabRounding,   0.0f, 12.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat("TabRounding",      &style.TabRounding,    0.0f, 12.0f, "%.0f")) if(g_App) g_App->SaveConfig();

    ImGui::SeparatorText("Scrollbar");

    if(ImGui::SliderFloat("ScrollbarSize",      &style.ScrollbarSize,      1.0f, 20.0f, "%.0f")) if(g_App) g_App->SaveConfig();
    if(ImGui::SliderFloat("ScrollbarRounding",  &style.ScrollbarRounding,  0.0f, 12.0f, "%.0f")) if(g_App) g_App->SaveConfig();
}

// =============================================================================
// DrawColorsTab
// =============================================================================

/**
 * @brief Aba "Colors" — paleta de 54 cores do ImGuiStyle.
 *
 * Cada alteração de cor persiste via App::SaveConfig(). Como ColorEdit4
 * retorna true apenas quando o valor muda (não a cada frame), o custo
 * é mínimo — SaveConfig() é chamado raramente.
 */
void StyleEditor::DrawColorsTab() {
    ImGuiStyle& style = ImGui::GetStyle();

    static ImGuiTextFilter filter; // filtro de busca persistido entre frames (static)
    filter.Draw("Filter colors", ImGui::GetFontSize() * 16);

    static ImGuiColorEditFlags alpha_flags = 0; // modo de exibição do alpha (static)
    if(ImGui::RadioButton("Opaque", alpha_flags == ImGuiColorEditFlags_AlphaOpaque))
        alpha_flags = ImGuiColorEditFlags_AlphaOpaque;
    ImGui::SameLine();
    if(ImGui::RadioButton("Alpha", alpha_flags == ImGuiColorEditFlags_None))
        alpha_flags = ImGuiColorEditFlags_None;
    ImGui::SameLine();
    if(ImGui::RadioButton("Both", alpha_flags == ImGuiColorEditFlags_AlphaPreviewHalf))
        alpha_flags = ImGuiColorEditFlags_AlphaPreviewHalf;

    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 10),
        ImVec2(FLT_MAX, FLT_MAX));
    ImGui::BeginChild("##colors", ImVec2(0, 0),
        ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened,
        ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar);
    ImGui::PushItemWidth(ImGui::GetFontSize() * -12);

    for(int i = 0; i < ImGuiCol_COUNT; i++) {
        const char* name = ImGui::GetStyleColorName(i); // nome legível do slot de cor
        if(!filter.PassFilter(name))
            continue; // pula cores que não passam no filtro de busca

        ImGui::PushID(i); // garante IDs únicos para cada ColorEdit4
        if(ImGui::ColorEdit4("##color", (float*)&style.Colors[i],
            ImGuiColorEditFlags_AlphaBar | alpha_flags)) {
            if(g_App) g_App->SaveConfig(); // persiste a nova cor imediatamente
        }
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::TextUnformatted(name); // nome da cor ao lado do picker
        ImGui::PopID();
    }

    ImGui::PopItemWidth();
    ImGui::EndChild();
}

// =============================================================================
// DrawFontsTab
// =============================================================================

/**
 * @brief Aba "Fonts" — informações do atlas de fontes ImGui.
 *
 * Exibe metadados do atlas: textura, número de fontes, lista de slots.
 * Sem persistência — apenas leitura.
 */
void StyleEditor::DrawFontsTab() {
    ImGuiIO& io = ImGui::GetIO();          // ImGuiIO contém o atlas de fontes
    ImFontAtlas* atlas = io.Fonts;          // ponteiro para o atlas atual

    ImGui::Text("Font Atlas Information");
    ImGui::Separator();
    ImGui::Text("Texture ID: %p", atlas->TexRef);
    ImGui::Separator();
    ImGui::Text("Loaded Fonts: %d", atlas->Fonts.Size);

    ImGui::BeginChild("##fonts_list", ImVec2(0, 200), ImGuiChildFlags_Borders);
    for(int i = 0; i < atlas->Fonts.Size; i++) {
        ImFont* font = atlas->Fonts[i];
        ImGui::Text("Font %d: %p", i, font); // endereço do ImFont para debug
    }
    ImGui::EndChild();

    ImGui::SetItemTooltip("Shows information about loaded fonts in the font atlas.");
}

// =============================================================================
// DrawRenderingTab
// =============================================================================

/**
 * @brief Aba "Rendering" — checkboxes e sliders de qualidade de renderização.
 *
 * Alterações persistidas via App::SaveConfig().
 */
void StyleEditor::DrawRenderingTab() {
    ImGuiStyle& style = ImGui::GetStyle();

    if(ImGui::Checkbox("Anti-aliased lines", &style.AntiAliasedLines)) {
        if(g_App) g_App->SaveConfig(); // persiste a alteração de anti-aliasing
    }
    ImGui::SameLine();
    ImGui::SetItemTooltip(
        "When disabling anti-aliasing lines, you'll probably want to "
        "disable borders in your style as well.");

    if(ImGui::Checkbox("Anti-aliased lines use texture", &style.AntiAliasedLinesUseTex)) {
        if(g_App) g_App->SaveConfig();
    }
    ImGui::SameLine();
    ImGui::SetItemTooltip(
        "Faster lines using texture data. Require backend to render with "
        "bilinear filtering (not point/nearest filtering).");

    if(ImGui::Checkbox("Anti-aliased fill", &style.AntiAliasedFill)) {
        if(g_App) g_App->SaveConfig();
    }

    ImGui::PushItemWidth(ImGui::GetFontSize() * 8);

    if(ImGui::DragFloat("Curve Tessellation Tolerance",
        &style.CurveTessellationTol, 0.02f, 0.10f, 10.0f, "%.2f")) {
        if(style.CurveTessellationTol < 0.10f)
            style.CurveTessellationTol = 0.10f; // clamp manual ao mínimo razoável
        if(g_App) g_App->SaveConfig();
    }

    if(ImGui::DragFloat("Circle Tessellation Max Error",
        &style.CircleTessellationMaxError, 0.005f, 0.10f, 5.0f, "%.2f",
        ImGuiSliderFlags_AlwaysClamp)) {
        if(g_App) g_App->SaveConfig();
    }

    if(ImGui::DragFloat("Global Alpha", &style.Alpha, 0.005f, 0.20f, 1.0f, "%.2f")) {
        if(g_App) g_App->SaveConfig();
    }

    if(ImGui::DragFloat("Disabled Alpha", &style.DisabledAlpha,
        0.005f, 0.0f, 1.0f, "%.2f")) {
        if(g_App) g_App->SaveConfig();
    }
    ImGui::SameLine();
    ImGui::SetItemTooltip(
        "Additional alpha multiplier for disabled items "
        "(multiply over current value of Alpha).");

    ImGui::PopItemWidth();
}