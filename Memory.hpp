#pragma once
#include "pch.hpp"
#include "MyResult.hpp"

// Forward declarations — evita incluir headers pesados no .hpp
class FontManager;
class VulkanContext;
class ImGuiContext_Wrapper;
class Console;
class StyleEditor;

/**
 * @brief Singleton que centraliza o ciclo de vida de todos os recursos da aplicação.
 *
 * ORDEM DE ALLOC — encapsulada em AllocAll():
 * --------------------------------------------
 *  1. AllocVulkan(extensions, window, w, h)
 *       → Initialize() + SetupWindow()
 *       SetupWindow DEVE vir antes do ImGui porque ImGui_ImplVulkan_Init
 *       exige que o swapchain já exista.
 *       (assert: info->ImageCount >= info->MinImageCount)
 *
 *  2. AllocImGui(window, scale)
 *       → ImGui::CreateContext() + ImplSDL3_Init + ImplVulkan_Init
 *
 *  3. AllocFontManager(scale)
 *       → LoadAllFontsWithEmoji() — requer contexto ImGui
 *
 *  4. AllocConsole()
 *       → Console::Console() chama ImGui::MemAlloc() — requer contexto ImGui
 *       (assert anterior: GImGui != 0)
 *
 *  5. AllocStyleEditor()
 *
 * ORDEM DE DESTROY — DestroyAll() faz a ordem inversa automaticamente.
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
     * @brief Aloca todos os recursos na ordem correta.
     *
     * Internamente: AllocVulkan → AllocImGui → AllocFontManager
     *               → AllocConsole → AllocStyleEditor
     *
     * @param extensions  Extensões Vulkan requeridas pelo SDL.
     * @param window      Janela SDL3 já criada.
     * @param w           Largura inicial da janela (pixels).
     * @param h           Altura inicial da janela (pixels).
     * @param scale       Escala DPI do display primário.
     */
    MyResult AllocAll(const ImVector<const char*>& extensions,
        SDL_Window* window,
        int                          w,
        int                          h,
        float                        scale);

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
     * SetupWindow é feito aqui (não em run()) porque ImGui_ImplVulkan_Init,
     * chamado em AllocImGui(), exige que o swapchain já exista.
     * Chamar SetupWindow depois do AllocImGui causa:
     *   Assertion failed: info->ImageCount >= info->MinImageCount
     *
     * @param extensions  Extensões de instância Vulkan requeridas pelo SDL.
     * @param window      Janela SDL3 já criada.
     * @param w           Largura inicial em pixels para o swapchain.
     * @param h           Altura inicial em pixels para o swapchain.
     */
    MyResult AllocVulkan(const ImVector<const char*>& extensions,
        SDL_Window* window,
        int                          w,
        int                          h);

    /** @brief Destrói o VulkanContext (swapchain + device + instance). */
    MyResult DestroyVulkan();

    /**
     * @brief Inicializa o ImGuiContext_Wrapper (ImGui::CreateContext + backends).
     *
     * DEVE ser chamado após AllocVulkan (swapchain já existe).
     * DEVE ser chamado antes de AllocFontManager e AllocConsole.
     *
     * @param window  Janela SDL3.
     * @param scale   Escala DPI para fontes e espaçamento.
     */
    MyResult AllocImGui(SDL_Window* window, float scale);

    /** @brief Destrói o ImGuiContext_Wrapper (ImGui::DestroyContext). */
    MyResult DestroyImGui();

    /**
     * @brief Carrega todas as fontes TTF + emoji no atlas ImGui.
     *
     * Requer contexto ImGui ativo. Deve ser chamado ANTES do primeiro NewFrame().
     *
     * @param scale  Escala DPI — tamanho base = 13.0f * scale.
     */
    MyResult AllocFontManager(float scale);

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

    VulkanContext* GetVulkan();       ///< nullptr se não alocado
    ImGuiContext_Wrapper* GetImGui();        ///< nullptr se não alocado
    FontManager* GetFontManager();  ///< nullptr se não alocado
    Console* GetConsole();      ///< nullptr se não alocado
    StyleEditor* GetStyleEditor();  ///< nullptr se não alocado

private:
    Memory();
    ~Memory();

    std::unique_ptr<VulkanContext>        vulkan_instance;
    std::unique_ptr<ImGuiContext_Wrapper> imgui_instance;
    std::unique_ptr<FontManager>          font_manager_instance;
    std::unique_ptr<Console>              console_instance;
    std::unique_ptr<StyleEditor>          style_editor_instance;

    bool vulkan_allocated = false; ///< true após AllocVulkan()
    bool imgui_allocated = false; ///< true após AllocImGui()
    bool font_manager_allocated = false; ///< true após AllocFontManager()
    bool console_allocated = false; ///< true após AllocConsole()
    bool style_editor_allocated = false; ///< true após AllocStyleEditor()
};
