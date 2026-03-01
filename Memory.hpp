#pragma once
#include "pch.hpp"

// Forward declarations — evita incluir headers pesados no .hpp
class MyResult;
class FontManager;
class App;
class VulkanContext;
class ImGuiContext_Wrapper;
class Console;
class StyleEditor;

/**
 * @brief Singleton que centraliza o ciclo de vida de todos os recursos da aplicação.
 *
 * ORDEM DE ALLOC — encapsulada em AllocAll():
 * --------------------------------------------
 *  1. AllocVulkan()
 *       → lê g_App->g_Window internamente
 *       → detecta o monitor em que a janela está via SDL_GetDisplayForWindow()
 *       → lê resolução e DPI do monitor correto (não sempre o primário)
 *       → Initialize() + SetupWindow()
 *       SetupWindow DEVE vir antes do ImGui porque ImGui_ImplVulkan_Init
 *       exige que o swapchain já exista.
 *       (assert: info->ImageCount >= info->MinImageCount)
 *
 *  2. AllocImGui()
 *       → usa m_window_scale detectado em AllocVulkan()
 *       → ImGui::CreateContext() + ImplSDL3_Init + ImplVulkan_Init
 *
 *  3. AllocFontManager()
 *       → usa m_window_scale detectado em AllocVulkan()
 *       → LoadAllFontsWithEmoji() — requer contexto ImGui
 *
 *  4. AllocConsole()
 *       → Console::Console() chama ImGui::MemAlloc() — requer contexto ImGui
 *       (assert anterior: GImGui != 0)
 *
 *  5. AllocStyleEditor()
 *
 * ORDEM DE DESTROY — DestroyAll() faz a ordem inversa automaticamente.
 *
 * POR QUE SEM ARGUMENTOS?
 * ------------------------
 * Com g_App->g_Window atribuído antes de AllocAll(), o Memory coleta tudo:
 *
 *   SDL_Window*           → g_App->g_Window
 *   SDL_DisplayID         → SDL_GetDisplayForWindow(g_App->g_Window)
 *   int w, h              → SDL_GetWindowSize(g_App->g_Window, &w, &h)
 *   float scale           → SDL_GetDisplayContentScale(display_id)
 *   ImVector<const char*> → SDL_Vulkan_GetInstanceExtensions(&n)
 *
 * DETECÇÃO DO MONITOR CORRETO
 * ----------------------------
 * SDL_GetPrimaryDisplay() retorna sempre o monitor principal do Windows,
 * independente de onde a janela será aberta. Em setups multi-monitor onde
 * o app abre no monitor secundário (ex.: com posição inicial fora do primário),
 * isso resulta em scale e DPI errados.
 *
 * SDL_GetDisplayForWindow() retorna o display em que a janela está atualmente.
 * O resultado é guardado em m_window_scale e reutilizado por AllocImGui()
 * e AllocFontManager() sem nova chamada SDL.
 *
 * PRÉ-REQUISITO: g_App->g_Window != nullptr quando AllocAll() é chamado.
 * Fluxo garantido em App::run():
 *   SDL_CreateWindow → g_Window = result → AllocGlobals() → Memory::AllocAll()
 */
class Memory {
public:

    // -------------------------------------------------------------------------
    // Singleton
    // -------------------------------------------------------------------------

    /**
     * @brief Retorna o ponteiro para a única instância do Memory.
     *
     * Meyers Singleton: construído na primeira chamada, destruído ao fim do programa.
     * Thread-safe desde C++11.
     */
    static Memory* Get();

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * @brief Aloca todos os recursos na ordem correta — sem argumentos externos.
     *
     * Coleta internamente via g_App e SDL:
     *   SDL_Window*           = g_App->g_Window
     *   SDL_DisplayID         = SDL_GetDisplayForWindow(g_App->g_Window)
     *   int w, h              = SDL_GetWindowSize(g_App->g_Window)
     *   float scale           = SDL_GetDisplayContentScale(display_id)
     *   ImVector<const char*> = SDL_Vulkan_GetInstanceExtensions()
     *
     * PRÉ-REQUISITO: g_App->g_Window já atribuído antes desta chamada.
     *
     * Internamente: AllocVulkan → AllocApp → AllocImGui → AllocFontManager
     *               → AllocConsole → AllocStyleEditor
     */
    MyResult AllocAll();

    /**
     * @brief Destroi todos os recursos na ordem inversa de alocação.
     *
     * Seguro chamar mesmo que alguns recursos não tenham sido alocados.
     */
    MyResult DestroyAll();

    // -------------------------------------------------------------------------
    // Alloc / Destroy individuais
    // -------------------------------------------------------------------------

    /**
     * @brief Inicializa o VulkanContext E configura a surface/swapchain.
     *
     * Detecta o monitor em que g_App->g_Window está via SDL_GetDisplayForWindow()
     * e lê sua escala DPI via SDL_GetDisplayContentScale(display_id).
     * O resultado é salvo em m_window_scale para uso em AllocImGui() e
     * AllocFontManager() — sem nova chamada SDL nesses sub-métodos.
     *
     * SetupWindow é feito aqui (não em run()) porque ImGui_ImplVulkan_Init,
     * chamado em AllocImGui(), exige que o swapchain já exista.
     * Chamar SetupWindow depois do AllocImGui causa:
     *   Assertion failed: info->ImageCount >= info->MinImageCount
     */
    MyResult AllocVulkan();

    /** @brief Destrói o VulkanContext (swapchain + device + instance). */
    MyResult DestroyVulkan();

    /** @brief Constrói App dentro do Memory (no-op se g_App externo). */
    MyResult AllocApp();

    /** @brief Destrói a instância App gerenciada pelo Memory. */
    MyResult DestroyApp();

    /**
     * @brief Inicializa o ImGuiContext_Wrapper (ImGui::CreateContext + backends).
     *
     * Usa m_window_scale detectado em AllocVulkan() — sem nova chamada SDL.
     *
     * DEVE ser chamado após AllocVulkan (swapchain já existe).
     * DEVE ser chamado antes de AllocFontManager e AllocConsole.
     */
    MyResult AllocImGui();

    /** @brief Destrói o ImGuiContext_Wrapper (ImGui::DestroyContext). */
    MyResult DestroyImGui();

    /**
     * @brief Carrega todas as fontes TTF + emoji no atlas ImGui.
     *
     * Usa m_window_scale detectado em AllocVulkan() — tamanho base = 13.0f * scale.
     *
     * Requer contexto ImGui ativo. Deve ser chamado ANTES do primeiro NewFrame().
     */
    MyResult AllocFontManager();

    /** @brief Libera o FontManager. */
    MyResult DestroyFontManager();

    /**
     * @brief Constrói o Console ImGui (wide-char, comandos, histórico).
     *
     * Console::Console() chama ImGui::MemAlloc() internamente.
     * DEVE ser chamado após AllocImGui().
     */
    MyResult AllocConsole();

    /** @brief Destrói o Console ImGui e libera todos os wide strings. */
    MyResult DestroyConsole();

    /** @brief Constrói o StyleEditor e carrega imgui_style.json. */
    MyResult AllocStyleEditor();

    /** @brief Destrói o StyleEditor. */
    MyResult DestroyStyleEditor();

    // -------------------------------------------------------------------------
    // Getters
    // -------------------------------------------------------------------------

    App*                  GetApp();         ///< nullptr se não alocado pelo Memory
    VulkanContext*        GetVulkan();      ///< nullptr se não alocado
    ImGuiContext_Wrapper* GetImGui();       ///< nullptr se não alocado
    FontManager*          GetFontManager(); ///< nullptr se não alocado
    Console*              GetConsole();     ///< nullptr se não alocado
    StyleEditor*          GetStyleEditor(); ///< nullptr se não alocado
     Memory();
    ~Memory();
private:
   

    std::unique_ptr<App>                  app_instance;
    std::unique_ptr<VulkanContext>        vulkan_instance;
    std::unique_ptr<ImGuiContext_Wrapper> imgui_instance;
    std::unique_ptr<FontManager>          font_manager_instance;
    std::unique_ptr<Console>              console_instance;
    std::unique_ptr<StyleEditor>          style_editor_instance;

    bool app_allocated          = false; ///< true após AllocApp()
    bool vulkan_allocated       = false; ///< true após AllocVulkan()
    bool imgui_allocated        = false; ///< true após AllocImGui()
    bool font_manager_allocated = false; ///< true após AllocFontManager()
    bool console_allocated      = false; ///< true após AllocConsole()
    bool style_editor_allocated = false; ///< true após AllocStyleEditor()

    // -------------------------------------------------------------------------
    // Dados do monitor detectados em AllocVulkan() — reutilizados pelos outros
    // sub-métodos sem nova consulta ao SDL.
    // -------------------------------------------------------------------------

    float         m_window_scale  = 1.0f; ///< SDL_GetDisplayContentScale() do monitor da janela
    SDL_DisplayID m_display_id    = 0;    ///< SDL_GetDisplayForWindow() — 0 se não detectado
    int           m_display_w     = 0;    ///< Resolução horizontal do monitor (pixels)
    int           m_display_h     = 0;    ///< Resolução vertical do monitor (pixels)
};