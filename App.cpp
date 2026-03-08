/**
 * @file App.cpp
 * @brief Implementação de App — ciclo de vida completo da aplicação.
 *
 * PERSISTÊNCIA UNIFICADA EM settings.json
 * ----------------------------------------
 * SaveConfig() / LoadConfig() persistem em um único arquivo:
 *  - AppSettings::window      — flags booleanas
 *  - AppSettings::font        — tamanho e escala de fonte
 *  - AppSettings::style       — dimensões do ImGuiStyle
 *  - AppSettings::color       — 54 cores do ImGuiStyle
 *  - AppSettings::mica_theme  — ThemeConfig completo do tema Mica
 *  - AppSettings::use_mica_theme — liga/desliga o Mica
 *
 * ORDEM DE APLICAÇÃO EM ApplyStyleToImGui():
 *  1. FontSettings  → ImGuiStyle::FontSizeBase + FontScaleMain
 *  2. StyleSettings → ImGuiStyle (dimensões: rounding, padding…)
 *  3. ColorSettings → ImGuiStyle::Colors[] (54 cores salvas)
 *  4. Se use_mica_theme: MicaTheme::ApplyMicaTheme(mica_theme)
 *     sobrescreve tanto cores quanto dimensões com os valores do Mica.
 *
 * Portanto, quando Mica está ativo ele sempre tem a última palavra.
 * As cores salvas em ColorSettings refletem o estado pós-Mica, então
 * num round-trip save→load o resultado é idêntico.
 */

#include "pch.hpp"
#include "App.hpp"
#include "memory.hpp"
#include "MenuBar.hpp"
#include "Console.hpp"
#include "WindowsConsole.hpp"
#include "StyleEditor.hpp"
#include "VulkanContext_Wrapper.hpp"
#include "ImGuiContext_Wrapper.hpp"
#include "SystemInfo.hpp"
#include "MicaTheme.h"  // MicaTheme::ApplyMicaTheme — chamado em ApplyStyleToImGui()
#include "FontScale.hpp" // FontScale::ProcessEvent, SetSize, ResetToDefault
#include "MyWindows.hpp"
#include "MyResult.hpp"
#include "Commandregistry.hpp"

// =============================================================================
// Definição do ponteiro global
// =============================================================================

/**
 * @brief Instância global de App — única no processo.
 *
 * Declarada como "extern App* g_App" em App.hpp.
 * Atribuída em Startup() via Memory::Get()->GetApp().
 * Anulada em Close().
 */
App* g_App = nullptr;

// =============================================================================
// Construtor
// =============================================================================

/**
 * @brief Inicializa todos os membros com valores neutros/nulos.
 *
 * Valores reais dos flags booleanos e do estilo são restaurados em
 * LoadConfig() → SyncFlagsFromSettings() / ApplyStyleToImGui(),
 * chamados dentro de AllocGlobals(). Aqui apenas garantimos que
 * nenhum membro fica com lixo de memória antes disso.
 */
App::App()
    : started(false)
    , bViewportDocking(false)             // sobrescrito por SyncFlagsFromSettings()
    , g_Done(false)                       // nunca persistido — sempre false ao iniciar
    , g_ShowDemo(true)                    // sobrescrito por SyncFlagsFromSettings()
    , g_ShowStyleEd(false)               // sobrescrito por SyncFlagsFromSettings()
    , g_IsFullscreen(false)              // sobrescrito por SyncFlagsFromSettings()
    , g_grafico(false)                   // sobrescrito por SyncFlagsFromSettings()
    , bImPlot3d_DemoRealtimePlots(false) // sobrescrito por SyncFlagsFromSettings()
    , bImPlot3d_DemoQuadPlots(false)    // sobrescrito por SyncFlagsFromSettings()
    , bImPlot3d_DemoTickLabels(false)   // sobrescrito por SyncFlagsFromSettings()
    , g_window_opacity(1.0f)             // não persistido — sempre 1.0 ao iniciar
    , g_color_ptr(nullptr)              // apontado para g_Settings->clear_color[0] em MainLoop()
    , g_io(nullptr)                     // atribuído em MainLoop()
    , g_Vulkan(nullptr)                 // alias — preenchido em AllocGlobals()
    , g_ImGui(nullptr)                  // alias — preenchido em AllocGlobals()
    , g_Console(nullptr)                // alias — preenchido em AllocGlobals()
    , g_Style(nullptr)                  // alias — preenchido em AllocGlobals()
    , g_MenuBar(nullptr)                // alias — preenchido em AllocGlobals()
    , g_Settings(nullptr)               // alias — preenchido em AllocGlobals()
    , g_Window(nullptr)                 // preenchido em run() após SDL_CreateWindow
    , g_MyWindows(nullptr)                // alias — preenchido em AllocGlobals()
    , m_ConfigFile("settings.json")     // caminho fixo do arquivo de configuração
{
}

// =============================================================================
// Startup
// =============================================================================

/**
 * @brief Expõe a instância via g_App e conecta ao RenderDoc se disponível.
 *
 * Deve ser chamado por wWinMain APÓS Memory::Get()->AllocApp().
 */
void App::Startup() {
    g_App = Memory::Get()->GetApp(); // expõe a instância do Memory globalmente
    started = true;
}

// =============================================================================
// run()
// =============================================================================

/**
 * @brief Inicializa SDL, cria a janela, aloca recursos e roda o loop principal.
 */
MyResult App::run() {
    if(!started)
        return MR_MSGBOX_ERR_END_LOC("App::Startup() must be called before run().");

    // ---- 1. SDL_Init -----------------------------------------------------
    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        return MR_MSGBOX_ERR_END_LOC(
            "Failed to initialize SDL: " + StrToWStr(SDL_GetError()));

    // ---- 2. Escala DPI do monitor primário (para tamanho inicial da janela)
    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

    // ---- 3. Resolução do desktop -----------------------------------------
    int desktop_w = 0;
    int desktop_h = 0;
    if(!MR_IS_OK(GetDesktopResolution(desktop_w, desktop_h)) ||
        desktop_w <= 0 || desktop_h <= 0) {
        SDL_Quit();
        return MR_MSGBOX_ERR_END_LOC("Failed to get desktop resolution.");
    }

    // ---- 4. Cria a janela SDL --------------------------------------------
    constexpr SDL_WindowFlags flags =
        SDL_WINDOW_VULKAN             |  // habilita VkSurfaceKHR via SDL
        SDL_WINDOW_RESIZABLE          |  // usuário pode redimensionar
        SDL_WINDOW_HIDDEN             |  // oculta até SDL_ShowWindow()
        SDL_WINDOW_HIGH_PIXEL_DENSITY |  // pixels físicos em monitores HiDPI
        SDL_WINDOW_MAXIMIZED;            // maximizada ao ser exibida

    g_Window = SDL_CreateWindow(
        "Dear ImGui SDL3+Vulkan",
        static_cast<int>(desktop_w * scale), // largura inicial em pixels físicos
        static_cast<int>(desktop_h * scale), // altura inicial em pixels físicos
        flags);

    if(!g_Window) {
        SDL_Quit();
        return MR_MSGBOX_ERR_END_LOC(
            "Failed to create SDL window: " + StrToWStr(SDL_GetError()));
    }

    // ---- 5. Aloca Vulkan, ImGui, fontes, console, style via Memory -------
    if(!MR_IS_OK(Memory::Get()->AllocAll(g_Window)))
        return MR_MSGBOX_ERR_END_LOC("Memory::AllocAll falhou.");

    // ---- 6. Aliases + LoadConfig + AllocImages ---------------------------
    if(!MR_IS_OK(AllocGlobals()))
        return MR_MSGBOX_ERR_END_LOC("AllocGlobals falhou.");

    // ---- 7. Exibe a janela -----------------------------------------------
    SDL_SetWindowPosition(g_Window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(g_Window);

    // ---- 8. Restaura fullscreen se estava ativo -------------------------
    // Feito APÓS SDL_ShowWindow para que o SDL aplique o modo corretamente.
    if(g_IsFullscreen)
        SDL_SetWindowFullscreen(g_Window, true);

    // ---- 9. Registra comandos do console ---------------------------------
    if(!MR_IS_OK(RegisterCommands()))
        return MR_MSGBOX_ERR_END_LOC("RegisterCommands falhou.");

    // ---- 10. Loop principal ----------------------------------------------
    const MyResult result = MainLoop();


    return result;
}

// =============================================================================
// AllocGlobals
// =============================================================================

/**
 * @brief Extrai aliases do Memory, carrega a configuração e aplica ao ImGui.
 *
 * ORDEM OBRIGATÓRIA:
 *  1. Atribuir aliases (g_Vulkan … g_Settings, g_MenuBar)
 *  2. Validar aliases
 *  3. LoadConfig() → SyncFlagsFromSettings() + ApplyStyleToImGui()
 *     (inclui MicaTheme::ApplyMicaTheme se use_mica_theme == true)
 *  4. AllocImages()
 */
MyResult App::AllocGlobals() {

    // ---- 1. Aliases sem posse — dono é o Memory -------------------------
    g_Vulkan   = Memory::Get()->GetVulkan();
    g_ImGui    = Memory::Get()->GetImGui();
    g_Console  = Memory::Get()->GetConsole();
    g_Style    = Memory::Get()->GetStyleEditor();
    g_MenuBar  = Memory::Get()->GetMenuBar();

    g_Settings = Memory::Get()->GetAppSettings();
	g_MyWindows = Memory::Get()->GetMyWindows();
	g_MyWindows->Init();

    // ---- 2. Validação ---------------------------------------------------
    if(!g_Vulkan)
        return MR_MSGBOX_ERR_END_LOC("g_Vulkan nulo após AllocAll.");
    if(!g_ImGui)
        return MR_MSGBOX_ERR_END_LOC("g_ImGui nulo após AllocAll.");
    if(!g_Console)
        return MR_MSGBOX_ERR_END_LOC("g_Console nulo após AllocAll.");
    if(!g_Style)
        return MR_MSGBOX_ERR_END_LOC("g_Style nulo após AllocAll.");
    if(!g_Settings)
        return MR_MSGBOX_ERR_END_LOC("g_Settings nulo após AllocAll.");
    if(!g_MenuBar)
        return MR_MSGBOX_ERR_END_LOC("g_MenuBar nulo após AllocAll.");

        if (!g_MyWindows) return MR_MSGBOX_ERR_END_LOC("g_MyWindows nulo após AllocAll.");

    // ---- 3. Carrega configuração do disco --------------------------------
    // LoadConfig() chama internamente:
    //   SyncFlagsFromSettings() → g_ShowDemo, g_grafico, etc.
    //   ApplyStyleToImGui()     → ImGuiStyle (inclui Mica se habilitado)
      if(g_Settings)
        LoadConfig();

    // ---- 4. Imagens (falha não é fatal) ----------------------------------
    if(!MR_IS_OK(AllocImages()))
        return MR_CLS_WARN_LOC("AllocImages falhou — continuando sem imagens.");

    return MyResult::ok;
}

// =============================================================================
// AllocImages
// =============================================================================

/**
 * @brief Carrega as imagens da aplicação. Falhas não são fatais.
 */
MyResult App::AllocImages() {
    if(!g_Logo.Load("assets/logo.png"))
        g_Console->AddLog(L"[Aviso] Logo nao carregou (assets/logo.png)");
    else
        g_Console->AddLog(L"[OK] Logo carregada (%dx%d)",
            g_Logo.GetWidth(), g_Logo.GetHeight());

    if(!g_IconSettings.Load("assets/icon_settings.png"))
        g_Console->AddLog(L"[Aviso] IconSettings nao carregou");
    else
        g_Console->AddLog(L"[OK] IconSettings carregado (%dx%d)",
            g_IconSettings.GetWidth(), g_IconSettings.GetHeight());

    return MyResult::ok;
}

// =============================================================================
// Close
// =============================================================================

/**
 * @brief Executa o ciclo de destruição na ordem obrigatória.
 *
 * ORDEM:
 *  1. SaveConfig()          → persiste o estado final (incluindo tema Mica atual)
 *  2. vkDeviceWaitIdle      → GPU termina todos os frames em voo
 *  3. Imagens Unload()      → usa VkDevice — antes de DestroyAll()
 *  4. Anula aliases de App  → g_MenuBar, g_Settings
 *  5. Memory::DestroyAll()  → destrói todos os subsistemas na ordem inversa
 *  6. Anula aliases de recurso → g_Style, g_Console, g_ImGui, g_Vulkan
 *  7. SDL_DestroyWindow + SDL_Quit
 *  8. g_App = nullptr
 */
MyResult App::Close() {
    // ---- 1. Salva o estado final ----------------------------------------
    // Captura o tema Mica atual (pode ter sido editado no StyleEditor)
    // junto com todos os outros campos de AppSettings.
    SaveConfig();

    // ---- 2. GPU idle -----------------------------------------------------
    if(g_Vulkan && g_Vulkan->GetDevice() != VK_NULL_HANDLE) {
        const VkResult err = vkDeviceWaitIdle(g_Vulkan->GetDevice());
        VulkanContext::CheckVkResult(err);
    }

    // ---- 3. Imagens — ANTES de DestroyAll --------------------------------
    g_Logo.Unload();
    g_IconSettings.Unload();

    // ---- 4. Anula aliases de posse de App --------------------------------
    g_MenuBar  = nullptr; // dono é Memory — não fazemos delete
    g_Settings = nullptr; // idem

    // ---- 5. Memory::DestroyAll -------------------------------------------
    Memory::Get()->DestroyAll();

    // ---- 6. Anula aliases de recurso ------------------------------------
    g_Style   = nullptr;
    g_Console = nullptr;
    g_ImGui   = nullptr;
    g_Vulkan  = nullptr;

    // ---- 7. SDL ----------------------------------------------------------
    if(g_Window) {
        SDL_DestroyWindow(g_Window); // surface Vulkan já destruída pelo Memory
        g_Window = nullptr;
    }
    SDL_Quit();

    // ---- 8. Anula ponteiro global ----------------------------------------
    g_App = nullptr;

    ImPlot::DestroyContext();

    return MR_OK;
}

// =============================================================================
// SaveConfig
// =============================================================================

/**
 * @brief Captura o estado atual e serializa em settings.json.
 *
 * SEQUÊNCIA:
 *  1. SyncFlagsToSettings()  → membros públicos de App → AppSettings::window
 *  2. SyncStyleFromImGui()   → ImGuiStyle              → AppSettings::style + color + font
 *                              (captura o estado pós-Mica se Mica estiver ativo)
 *  3. rfl::json::save()      → grava settings.json com AppSettings completo
 *                              (inclui AppSettings::mica_theme e use_mica_theme)
 */
void App::SaveConfig() {
    if(!g_Settings)
        return; // chamado antes de AllocGlobals() — ignora silenciosamente

    SyncFlagsToSettings(); // flags de App → AppSettings::window
    SyncStyleFromImGui();  // ImGuiStyle   → AppSettings::style + color + font
    rfl::json::save(m_ConfigFile, *g_Settings); // serializa tudo
}

// =============================================================================
// LoadConfig
// =============================================================================

/**
 * @brief Carrega settings.json em *g_Settings e aplica ao estado de App.
 *
 * SEQUÊNCIA:
 *  1. rfl::json::load()        → desserializa settings.json
 *  2. *g_Settings = carregado  → substitui os defaults dos sub-structs
 *  3. SyncFlagsFromSettings()  → AppSettings::window → membros públicos de App
 *  4. ApplyStyleToImGui()      → AppSettings::style + color → ImGuiStyle
 *                                + MicaTheme::ApplyMicaTheme() se use_mica_theme
 *
 * Se o arquivo não existir, g_Settings mantém os defaults (Mica ativo).
 */
void App::LoadConfig() {
    if(!g_Settings)
        return;

    auto r = rfl::json::load<AppSettings>(m_ConfigFile); // tenta desserializar
    if(r)
        *g_Settings = *r; // substitui defaults pelos valores do disco

    SyncFlagsFromSettings(); // AppSettings::window → membros públicos
    ApplyStyleToImGui();     // AppSettings → ImGuiStyle (inclui Mica)
}

// =============================================================================
// SyncFlagsToSettings
// =============================================================================

/**
 * @brief Copia membros públicos de App → AppSettings::window.
 *
 * g_Done nunca é persistido (app sempre inicia com g_Done=false).
 */
void App::SyncFlagsToSettings() {
    if(!g_Settings) return;

    WindowSettings& w = g_Settings->window; // referência para encurtar o código
    w.show_demo                = g_ShowDemo;
    w.show_style_editor        = g_ShowStyleEd;
    w.is_fullscreen            = g_IsFullscreen;
    w.show_graph               = g_grafico;
    w.viewport_docking         = bViewportDocking;
    w.implot3d_realtime_plots  = bImPlot3d_DemoRealtimePlots;
    w.implot3d_quad_plots      = bImPlot3d_DemoQuadPlots;
    w.implot3d_tick_labels     = bImPlot3d_DemoTickLabels;
    // show_console é atualizado diretamente via &g_Settings->window.show_console
}

// =============================================================================
// SyncFlagsFromSettings
// =============================================================================

/**
 * @brief Copia AppSettings::window → membros públicos de App.
 *
 * Chamado em LoadConfig() para que o primeiro frame use os valores salvos.
 */
void App::SyncFlagsFromSettings() {
    if(!g_Settings) return;

    const WindowSettings& w = g_Settings->window;
    g_ShowDemo                  = w.show_demo;
    g_ShowStyleEd               = w.show_style_editor;
    g_IsFullscreen              = w.is_fullscreen;
    g_grafico                   = w.show_graph;
    bViewportDocking            = w.viewport_docking;
    bImPlot3d_DemoRealtimePlots = w.implot3d_realtime_plots;
    bImPlot3d_DemoQuadPlots     = w.implot3d_quad_plots;
    bImPlot3d_DemoTickLabels    = w.implot3d_tick_labels;
}

// =============================================================================
// SyncStyleFromImGui
// =============================================================================

/**
 * @brief Captura ImGuiStyle → AppSettings::font + style + color.
 *
 * Chamado em SaveConfig(). Quando Mica está ativo, o ImGuiStyle já contém
 * as cores e dimensões do Mica aplicadas — portanto ColorSettings reflete
 * o estado real visível ao usuário, não os defaults pré-Mica.
 */
void App::SyncStyleFromImGui() {
    if(!g_Settings) return;

    const ImGuiStyle& s = ImGui::GetStyle(); // leitura do estilo global atual
    StyleSettings& ss   = g_Settings->style;
    ColorSettings& cs   = g_Settings->color;

    // ---- Fonte -----------------------------------------------------------
    g_Settings->font.font_size_base  = s.FontSizeBase;
    g_Settings->font.font_scale_main = s.FontScaleMain;

    // ---- Arredondamento --------------------------------------------------
    ss.frame_rounding   = s.FrameRounding;
    ss.window_rounding  = s.WindowRounding;
    ss.popup_rounding   = s.PopupRounding;
    ss.tab_rounding     = s.TabRounding;
    ss.grab_rounding    = s.GrabRounding;
    ss.child_rounding   = s.ChildRounding;

    // ---- Bordas ----------------------------------------------------------
    ss.frame_border_size  = s.FrameBorderSize;
    ss.window_border_size = s.WindowBorderSize;
    ss.popup_border_size  = s.PopupBorderSize;
    ss.child_border_size  = s.ChildBorderSize;
    ss.tab_border_size    = s.TabBorderSize;

    // ---- Padding e espaçamento -------------------------------------------
    ss.window_padding_x      = s.WindowPadding.x;
    ss.window_padding_y      = s.WindowPadding.y;
    ss.frame_padding_x       = s.FramePadding.x;
    ss.frame_padding_y       = s.FramePadding.y;
    ss.item_spacing_x        = s.ItemSpacing.x;
    ss.item_spacing_y        = s.ItemSpacing.y;
    ss.item_inner_spacing_x  = s.ItemInnerSpacing.x;
    ss.item_inner_spacing_y  = s.ItemInnerSpacing.y;
    ss.touch_extra_padding_x = s.TouchExtraPadding.x;
    ss.touch_extra_padding_y = s.TouchExtraPadding.y;

    // ---- Escalares -------------------------------------------------------
    ss.indent_spacing              = s.IndentSpacing;
    ss.grab_min_size               = s.GrabMinSize;
    ss.scrollbar_size              = s.ScrollbarSize;
    ss.alpha                       = s.Alpha;
    ss.disabled_alpha              = s.DisabledAlpha;
    ss.curve_tessellation_tol      = s.CurveTessellationTol;
    ss.circle_tessellation_max_err = s.CircleTessellationMaxError;

    // ---- Anti-aliasing ---------------------------------------------------
    ss.anti_aliased_fill          = s.AntiAliasedFill;
    ss.anti_aliased_lines         = s.AntiAliasedLines;
    ss.anti_aliased_lines_use_tex = s.AntiAliasedLinesUseTex;

    // ---- Cores (ImVec4 → ImGuiColor com name) ---------------------------
    // O campo name usa ImGui::GetStyleColorName() para que o settings.json
    // seja auto-documentado: { "name":"WindowBg", "r":0.13, "g":0.13, ... }
    // em vez do array anônimo anterior: [ 0.13, 0.13, 0.13, 0.78 ]
    cs.colors.resize(static_cast<std::size_t>(ImGuiCol_COUNT));
    for(int i = 0; i < ImGuiCol_COUNT; ++i) {
        ImGuiColor& c  = cs.colors[static_cast<std::size_t>(i)];
        c.name = ImGui::GetStyleColorName(i); // "Text", "WindowBg", "Button"...
        c.r    = s.Colors[i].x;              // componente vermelho
        c.g    = s.Colors[i].y;              // componente verde
        c.b    = s.Colors[i].z;              // componente azul
        c.a    = s.Colors[i].w;              // componente alpha
    }
}

// =============================================================================
// ApplyStyleToImGui
// =============================================================================

/**
 * @brief Aplica AppSettings ao ImGuiStyle global.
 *
 * ORDEM DE APLICAÇÃO:
 *  1. FontSettings  → FontSizeBase, FontScaleMain
 *  2. StyleSettings → dimensões (rounding, padding, etc.)
 *  3. ColorSettings → Colors[] (54 cores)
 *  4. Se use_mica_theme: MicaTheme::ApplyMicaTheme(mica_theme)
 *     → sobrescreve cores E dimensões com os valores do Mica.
 *
 * O Mica sempre tem a última palavra quando está ativo.
 * Isso garante que ao carregar um settings.json com use_mica_theme=true,
 * o visual seja idêntico ao que existia quando foi salvo.
 */
void App::ApplyStyleToImGui() {
    if(!g_Settings) return;

    ImGuiStyle& s           = ImGui::GetStyle(); // estilo global único
    const StyleSettings& ss = g_Settings->style;
    const ColorSettings& cs = g_Settings->color;

    // ---- 1. Fonte --------------------------------------------------------
    s.FontSizeBase           = g_Settings->font.font_size_base;
    s.FontScaleMain          = g_Settings->font.font_scale_main;
    s._NextFrameFontSizeBase = s.FontSizeBase; // garante efeito no próximo frame

    // ---- 2. Dimensões ----------------------------------------------------
    s.FrameRounding  = ss.frame_rounding;
    s.WindowRounding = ss.window_rounding;
    s.PopupRounding  = ss.popup_rounding;
    s.TabRounding    = ss.tab_rounding;
    s.GrabRounding   = ss.grab_rounding;
    s.ChildRounding  = ss.child_rounding;

    s.FrameBorderSize  = ss.frame_border_size;
    s.WindowBorderSize = ss.window_border_size;
    s.PopupBorderSize  = ss.popup_border_size;
    s.ChildBorderSize  = ss.child_border_size;
    s.TabBorderSize    = ss.tab_border_size;

    s.WindowPadding     = ImVec2(ss.window_padding_x,      ss.window_padding_y);
    s.FramePadding      = ImVec2(ss.frame_padding_x,       ss.frame_padding_y);
    s.ItemSpacing       = ImVec2(ss.item_spacing_x,        ss.item_spacing_y);
    s.ItemInnerSpacing  = ImVec2(ss.item_inner_spacing_x,  ss.item_inner_spacing_y);
    s.TouchExtraPadding = ImVec2(ss.touch_extra_padding_x, ss.touch_extra_padding_y);

    s.IndentSpacing              = ss.indent_spacing;
    s.GrabMinSize                = ss.grab_min_size;
    s.ScrollbarSize              = ss.scrollbar_size;
    s.Alpha                      = ss.alpha;
    s.DisabledAlpha              = ss.disabled_alpha;
    s.CurveTessellationTol       = ss.curve_tessellation_tol;
    s.CircleTessellationMaxError = ss.circle_tessellation_max_err;
    s.AntiAliasedFill            = ss.anti_aliased_fill;
    s.AntiAliasedLines           = ss.anti_aliased_lines;
    s.AntiAliasedLinesUseTex     = ss.anti_aliased_lines_use_tex;

    // ---- 3. Cores (ImGuiColor → ImVec4) ---------------------------------
    // O campo name é ignorado aqui — o índice do vetor determina o slot.
    // Iteramos até min(size, ImGuiCol_COUNT) para ser seguro contra JSONs
    // de versões diferentes do ImGui com número distinto de cores.
    const std::size_t n = std::min(
        cs.colors.size(),
        static_cast<std::size_t>(ImGuiCol_COUNT));
    for(std::size_t i = 0; i < n; ++i) {
        const ImGuiColor& c = cs.colors[i]; // name é descartado na leitura
        s.Colors[static_cast<int>(i)] = ImVec4(c.r, c.g, c.b, c.a);
    }

    // ---- 4. Mica (opcional) — SEMPRE por último -------------------------
    // Sobrescreve cores e dimensões com os valores do ThemeConfig salvo.
    // Quando false, o usuário controla tudo via StyleEditor.
    if(g_Settings->use_mica_theme)
        MicaTheme::ApplyMicaTheme(g_Settings->mica_theme); // usa ImGui::GetStyle()
}

// =============================================================================
// GetDesktopResolution
// =============================================================================

/**
 * @brief Obtém a resolução do desktop via Win32.
 */
MyResult App::GetDesktopResolution(int& horizontal, int& vertical) {
    RECT desktop;
    const HWND hDesktop = GetDesktopWindow();  // handle para o desktop Win32
    GetWindowRect(hDesktop, &desktop);          // preenche .right e .bottom
    horizontal = desktop.right;                 // largura em pixels físicos
    vertical   = desktop.bottom;               // altura em pixels físicos
    return MR_OK;
}

// =============================================================================
// RegisterCommands
// =============================================================================

// Em App.cpp — inclua no topo:
// #include "CommandRegistry.hpp"

/**
 * @brief Registra todos os comandos do Console delegando para CommandRegistry.
 *
 * A lógica real está em CommandRegistry.cpp, agrupada por tema:
 *  RegisterLifecycle() → EXIT, QUIT, BREAK, forceexit, Abort, pauses
 *  RegisterSystem()    → SPECS, VSYNC, NOVIEWPORTS, FONTRESET
 *  RegisterTheme()     → MICA, NOMICA, theme [dark|light|classic]
 *  RegisterDemo()      → implot, implot3d, Test Emojis
 */
MyResult App::RegisterCommands()
{
    if(!g_Console) return MR_MSGBOX_ERR_END_LOC("g_Console nulo.");
    if(!g_Vulkan)  return MR_MSGBOX_ERR_END_LOC("g_Vulkan nulo.");

    CommandRegistry reg(this, g_Console); // construtor leve — sem alocações
    return reg.RegisterAll();             // registra todos os grupos em ordem
}


// =============================================================================
// MainLoop
// =============================================================================

/**
 * @brief Loop SDL + ImGui + Vulkan — roda até g_Done == true.
 */
MyResult App::MainLoop() {
    g_io = &g_ImGui->GetIO(); // referência ao ImGuiIO para acesso rápido no loop

    const bool font_rt = (g_io->Fonts && g_io->Fonts->FontLoader);
    g_Console->AddLog(font_rt ? L"\n[RT] FontLoader ativo\n" : L"[RT] stb_truetype\n");

    float* color_ptr = g_Settings->clear_color.data(); // ponteiro para cor de fundo
    g_color_ptr      = color_ptr;
    g_window_opacity = 1.0f;

    while(!g_Done) {

        // ---- 1. Eventos SDL -----------------------------------------------
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            FontScale::ProcessEvent(event);      // Ctrl+Scroll → zoom de fonte
            ImGui_ImplSDL3_ProcessEvent(&event); // repassa ao ImGui

            if(event.type == SDL_EVENT_QUIT)
                g_Done = true;

            if(event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
               event.window.windowID == SDL_GetWindowID(g_Window))
                g_Done = true;

            if(event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11) {
                g_IsFullscreen = !g_IsFullscreen;
                SDL_SetWindowFullscreen(g_Window, g_IsFullscreen);
                SaveConfig(); // persiste o novo estado de fullscreen
            }
        }

        if(SDL_GetWindowFlags(g_Window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10); // cede CPU quando minimizada
            continue;
        }

        // ---- 2. Rebuild da swapchain se necessário -----------------------
        int fb_w, fb_h;
        SDL_GetWindowSize(g_Window, &fb_w, &fb_h);
        ImGui_ImplVulkanH_Window* wd = g_Vulkan->GetMainWindowData();
        if(fb_w > 0 && fb_h > 0 &&
           (g_Vulkan->NeedsSwapChainRebuild() ||
            wd->Width != fb_w || wd->Height != fb_h))
            g_Vulkan->RebuildSwapChain(fb_w, fb_h);

        // ---- 3. Conteúdo ImGui do frame ----------------------------------
        Windows();

        // ---- 4. Render Vulkan --------------------------------------------
        g_ImGui->Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        const bool minimized = (draw_data->DisplaySize.x <= 0.0f ||
                                 draw_data->DisplaySize.y <= 0.0f);

        wd->ClearValue.color.float32[0] = color_ptr[0] * color_ptr[3]; // R pré-mult
        wd->ClearValue.color.float32[1] = color_ptr[1] * color_ptr[3]; // G pré-mult
        wd->ClearValue.color.float32[2] = color_ptr[2] * color_ptr[3]; // B pré-mult
        wd->ClearValue.color.float32[3] = color_ptr[3];                 // A

        if(!minimized)                g_Vulkan->FrameRender(draw_data);
        if(g_ImGui->WantsViewports()) g_ImGui->RenderPlatformWindows();
        if(!minimized)                g_Vulkan->FramePresent();
    }

    return MyResult::ok;
}

// =============================================================================
// Windows — conteúdo ImGui do frame
// =============================================================================

/**
 * @brief Renderiza todas as janelas ImGui do frame atual.
 *
 * Qualquer alteração que deva ser persistida chama SaveConfig() imediatamente
 * no handler do widget — garante que nem um ALT+F4 perde o estado.
 */
/**
 * @brief Renderiza todas as janelas ImGui do frame atual.
 *
 * REGRA FUNDAMENTAL DO IMGUI:
 *   - p_open (segundo parâmetro de Begin) só desenha o X e zera o bool.
 *   - Quem decide se a janela EXISTE é o `if` externo que guarda o Begin/End.
 *   - Se Begin() for chamado, End() DEVE ser chamado no mesmo frame,
 *     independente de qualquer condição interna.
 *
 * ORDEM DO FRAME:
 *   1. NewFrame()          — inicia o frame ImGui
 *   2. MenuBar::Draw()     — barra de menu (modifica flags via g_Settings)
 *   3. Janelas condicionais (if + Begin/End)
 *   4. Retorna MR_OK       — Render() é chamado pelo MainLoop
 */
MyResult App::Windows()
{
    
	g_MyWindows->CreateWindows(); // cria as janelas (se já existirem, apenas chama Draw())
    return MR_OK;
}

// =============================================================================
// DisableViewportDocking
// =============================================================================

/**
 * @brief Desativa viewports flutuantes e reposiciona janelas dentro da área principal.
 */
void App::DisableViewportDocking() {
    g_io->ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    ImVec2 main_pos   = ImGui::GetMainViewport()->Pos;
    ImVec2 main_size  = ImGui::GetMainViewport()->Size;
    for(ImGuiWindow* win : ctx->Windows) {
        if(!win || win->Hidden) continue;
        float max_x = main_pos.x + main_size.x - win->Size.x;
        float max_y = main_pos.y + main_size.y - win->Size.y;
        win->Pos.x  = ImClamp(win->Pos.x, main_pos.x, ImMax(main_pos.x, max_x));
        win->Pos.y  = ImClamp(win->Pos.y, main_pos.y, ImMax(main_pos.y, max_y));
    }
    g_App->g_Console->AddLog(L"Viewports desabilitados — janelas reposicionadas.");
}

// =============================================================================
// ScrollingBuffer
// =============================================================================

ScrollingBuffer::ScrollingBuffer()
    : MaxSize(2000), Offset(0), Data({}) {}

ScrollingBuffer::ScrollingBuffer(int max_size)
    : ScrollingBuffer() {
    MaxSize = max_size;     // substitui o default 2000
    Data.reserve(MaxSize);  // pré-aloca para evitar realocações
}

void ScrollingBuffer::AddPoint(float x, float y) {
    if(Data.size() < MaxSize)
        Data.push_back(ImVec2(x, y));     // buffer ainda não cheio — append
    else {
        Data[Offset] = ImVec2(x, y);      // buffer cheio — sobrescreve o mais antigo
        Offset = (Offset + 1) % MaxSize;  // avança o ponteiro ciclicamente
    }
}

void ScrollingBuffer::Erase() {
    if(Data.size() > 0) {
        Data.shrink(0); // libera a memória interna do ImVector
        Offset = 0;     // reseta o índice de escrita
    }
}