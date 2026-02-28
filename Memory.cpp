/**
 * @file Memory.cpp
 * @brief Implementação do singleton Memory.
 *
 * CORREÇÃO DO ASSERT (info->ImageCount >= info->MinImageCount):
 * -------------------------------------------------------------
 * ImGui_ImplVulkan_Init() (chamado dentro de ImGuiContext_Wrapper::Initialize)
 * acessa o swapchain para ler MinImageCount e validar ImageCount.
 * Se SetupWindow() ainda não tiver sido chamado, o swapchain não existe e
 * o assert falha na linha 1323 de imgui_impl_vulkan.cpp.
 *
 * SOLUÇÃO: mover SetupWindow() para dentro de AllocVulkan(), garantindo que
 * o swapchain existe ANTES de AllocImGui() ser chamado.
 *
 * ORDEM FINAL:
 *   AllocVulkan  → Initialize() + SetupWindow()   ← swapchain criado aqui
 *   AllocImGui   → ImplVulkan_Init usa o swapchain ← agora é seguro
 *   AllocFontManager
 *   AllocConsole                                   ← GImGui já existe
 *   AllocStyleEditor
 */

#include "pch.hpp"
#include "Memory.hpp"
#include "VulkanContext_Wrapper.hpp"
#include "ImGuiContext_Wrapper.hpp"
#include "FontManager.hpp"
#include "Console.hpp"
#include "StyleEditor.hpp"

 // ============================================================================
 // Construtor / Destrutor
 // ============================================================================

 /**
  * @brief Construtor privado — unique_ptrs começam nullptr, flags começam false.
  */
Memory::Memory()
    : vulkan_instance(nullptr)
    , imgui_instance(nullptr)
    , font_manager_instance(nullptr)
    , console_instance(nullptr)
    , style_editor_instance(nullptr) {
}

/**
 * @brief Destrutor privado — unique_ptrs liberam automaticamente se DestroyAll()
 *        não tiver sido chamado (segurança em caso de crash no shutdown).
 */
Memory::~Memory() = default;

// ============================================================================
// Get
// ============================================================================

/**
 * @brief Meyers Singleton — thread-safe desde C++11.
 * @return Ponteiro não-nulo para a instância estática do Memory.
 */
Memory* Memory::Get() {
    static Memory instance; // construído uma vez, destruído ao fim do programa
    return &instance;
}

// ============================================================================
// AllocAll / DestroyAll
// ============================================================================

/**
 * @brief Aloca todos os recursos na ordem correta.
 *
 * A ordem é obrigatória:
 *  1. AllocVulkan  — Initialize() + SetupWindow() (swapchain criado aqui)
 *  2. AllocImGui   — ImplVulkan_Init usa o swapchain já existente
 *  3. AllocFontManager — atlas ImGui (requer contexto ImGui)
 *  4. AllocConsole     — ImGui::MemAlloc (requer contexto ImGui)
 *  5. AllocStyleEditor
 *
 * @param extensions  Extensões Vulkan requeridas pelo SDL.
 * @param window      Janela SDL3 já criada.
 * @param w           Largura inicial em pixels.
 * @param h           Altura inicial em pixels.
 * @param scale       Escala DPI do display primário.
 */
MyResult Memory::AllocAll(const ImVector<const char*>& extensions,
    SDL_Window* window,
    int                          w,
    int                          h,
    float                        scale) {
    // 1. Vulkan: Initialize() + SetupWindow() — swapchain DEVE existir antes do ImGui
    if(!MR_IS_OK(AllocVulkan(extensions, window, w, h)))
        return MR_CLS_ERR_LOC("AllocAll: falhou em AllocVulkan");

    // 2. ImGui: CreateContext + ImplSDL3_Init + ImplVulkan_Init
    //    ImplVulkan_Init acessa wd->ImageCount — swapchain já existe graças ao passo 1
    if(!MR_IS_OK(AllocImGui(window, scale)))
        return MR_CLS_ERR_LOC("AllocAll: falhou em AllocImGui");

    // 3. Fontes: carrega TTFs + emoji no atlas — requer contexto ImGui ativo
    if(!MR_IS_OK(AllocFontManager(scale)))
        return MR_CLS_ERR_LOC("AllocAll: falhou em AllocFontManager");

    // 4. Console: Console::Console() chama ImGui::MemAlloc() — requer GImGui != nullptr
    if(!MR_IS_OK(AllocConsole()))
        return MR_CLS_ERR_LOC("AllocAll: falhou em AllocConsole");

    // 5. StyleEditor: sem dependência crítica de ordem
    if(!MR_IS_OK(AllocStyleEditor()))
        return MR_CLS_ERR_LOC("AllocAll: falhou em AllocStyleEditor");

    return MyResult::ok;
}

/**
 * @brief Destroi todos os recursos na ordem inversa da alocação.
 */
MyResult Memory::DestroyAll() {
    DestroyStyleEditor();  // 5 → primeiro a ser destruído
    DestroyConsole();      // 4
    DestroyFontManager();  // 3
    DestroyImGui();        // 2 — DestroyContext() do ImGui
    DestroyVulkan();       // 1 → último a ser destruído
    return MyResult::ok;
}

// ============================================================================
// Vulkan
// ============================================================================

/**
 * @brief Inicializa o VulkanContext e configura a surface/swapchain da janela.
 *
 * SetupWindow() É CHAMADO AQUI — não em run() — porque ImGui_ImplVulkan_Init
 * (chamado em AllocImGui) precisa que o swapchain já exista:
 *   imgui_impl_vulkan.cpp:1323  assert(info->ImageCount >= info->MinImageCount)
 *
 * @param extensions  Extensões de instância Vulkan requeridas pelo SDL.
 * @param window      Janela SDL3 já criada.
 * @param w           Largura do swapchain em pixels.
 * @param h           Altura do swapchain em pixels.
 */
MyResult Memory::AllocVulkan(const ImVector<const char*>& extensions,
    SDL_Window* window,
    int                          w,
    int                          h) {
    if(vulkan_allocated)
        return MR_CLS_WARN_LOC("VulkanContext já alocado, ignorando.");

    vulkan_instance = std::make_unique<VulkanContext>();

    if(!vulkan_instance)
        return MR_MSGBOX_ERR_LOC("Falha ao alocar VulkanContext.");

    if(!vulkan_instance->Initialize(extensions)) // cria device, instance, queues
        return MR_MSGBOX_ERR_LOC("VulkanContext::Initialize() falhou.");

    vulkan_instance->SetVSync(false); // sem VSync para framerate máximo

    // SetupWindow cria a surface Vulkan e o swapchain.
    // DEVE vir aqui, antes de AllocImGui, para que ImplVulkan_Init
    // encontre ImageCount já preenchido.
    if(!vulkan_instance->SetupWindow(window, w, h))
        return MR_MSGBOX_ERR_LOC("VulkanContext::SetupWindow() falhou.");

    vulkan_allocated = true;
    return MyResult::ok;
}

/**
 * @brief Espera a GPU terminar e destroi swapchain, surface, device e instance.
 */
MyResult Memory::DestroyVulkan() {
    if(!vulkan_allocated) return MyResult::ok;

    vulkan_instance->CleanupWindow(); // destroi swapchain e surface
    vulkan_instance->Cleanup();       // destroi device e instance
    vulkan_instance.reset();
    vulkan_allocated = false;
    return MyResult::ok;
}

// ============================================================================
// ImGui
// ============================================================================

/**
 * @brief Cria o ImGuiContext_Wrapper (ImGui::CreateContext + backends SDL3/Vulkan).
 *
 * ImGui_ImplVulkan_Init acessa o swapchain para ler MinImageCount.
 * Por isso AllocVulkan (que chama SetupWindow) DEVE ter sido chamado antes.
 *
 * @param window  Janela SDL3.
 * @param scale   Escala DPI para fontes e espaçamento.
 */
MyResult Memory::AllocImGui(SDL_Window* window, float scale) {
    if(imgui_allocated)
        return MR_CLS_WARN_LOC("ImGuiContext_Wrapper já alocado, ignorando.");

    if(!vulkan_allocated) // swapchain precisa existir para ImplVulkan_Init
        return MR_MSGBOX_ERR_LOC("AllocImGui chamado antes de AllocVulkan.");

    imgui_instance = std::make_unique<ImGuiContext_Wrapper>();

    if(!imgui_instance)
        return MR_MSGBOX_ERR_LOC("Falha ao alocar ImGuiContext_Wrapper.");

    // Initialize: CreateContext + ImplSDL3_Init + ImplVulkan_Init
    // ImplVulkan_Init usa vulkan_instance internamente via o ponteiro passado
    if(!imgui_instance->Initialize(window, vulkan_instance.get(), scale))
        return MR_MSGBOX_ERR_LOC("ImGuiContext_Wrapper::Initialize() falhou.");

    imgui_allocated = true; // GImGui != nullptr a partir daqui
    return MyResult::ok;
}

/**
 * @brief Destrói o ImGuiContext_Wrapper (shutdown dos backends + DestroyContext).
 */
MyResult Memory::DestroyImGui() {
    if(!imgui_allocated) return MyResult::ok;

    imgui_instance->Shutdown();
    imgui_instance.reset();
    imgui_allocated = false;
    return MyResult::ok;
}

// ============================================================================
// FontManager
// ============================================================================

/**
 * @brief Carrega todas as fontes TTF + emoji no atlas do ImGui.
 *
 * Deve ser chamado após AllocImGui() e antes do primeiro NewFrame().
 * O atlas é compilado e enviado à GPU no primeiro NewFrame().
 *
 * @param scale  Escala DPI — tamanho base das fontes = 13.0f * scale.
 */
MyResult Memory::AllocFontManager(float scale) {
    if(font_manager_allocated)
        return MR_CLS_WARN_LOC("FontManager já alocado, ignorando.");

    if(!imgui_allocated)
        return MR_MSGBOX_ERR_LOC("AllocFontManager chamado antes de AllocImGui.");

    font_manager_instance = std::make_unique<FontManager>();

    if(!font_manager_instance)
        return MR_MSGBOX_ERR_LOC("Falha ao alocar FontManager.");

    bool emoji_ok = font_manager_instance->LoadAllFontsWithEmoji(13.0f * scale);

    printf("\n=== Font Status ===\n");
    printf("Fonts loaded: %d\n", font_manager_instance->GetFontCount());
    EmojiDebugHelper::PrintEmojiStatus(emoji_ok);
    printf("===================\n\n");

    font_manager_allocated = true;
    return MyResult::ok;
}

/**
 * @brief Libera o FontManager.
 */
MyResult Memory::DestroyFontManager() {
    if(!font_manager_allocated) return MyResult::ok;

    font_manager_instance.reset();
    font_manager_allocated = false;
    return MyResult::ok;
}

// ============================================================================
// Console
// ============================================================================

/**
 * @brief Constrói o Console ImGui (wide-char, histórico, autocomplete).
 *
 * Console::Console() chama ImGui::MemAlloc() via Wcsdup() para as mensagens
 * iniciais do log. Portanto DEVE ser chamado após AllocImGui() para que
 * GImGui != nullptr — caso contrário ocorre o assert anterior.
 */
MyResult Memory::AllocConsole() {
    if(console_allocated)
        return MR_CLS_WARN_LOC("Console já alocado, ignorando.");

    if(!imgui_allocated)
        return MR_MSGBOX_ERR_LOC(
            "AllocConsole chamado antes de AllocImGui. "
            "Console::Console() usa ImGui::MemAlloc() e exige GImGui != nullptr.");

    console_instance = std::make_unique<Console>(); // contexto ImGui já existe aqui

    if(!console_instance)
        return MR_MSGBOX_ERR_LOC("Falha ao alocar Console.");

    console_allocated = true;
    return MyResult::ok;
}

/**
 * @brief Destrói o Console ImGui e libera todos os wide strings alocados.
 */
MyResult Memory::DestroyConsole() {
    if(!console_allocated) return MyResult::ok;

    console_instance.reset(); // ~Console() chama ClearLog() e libera History
    console_allocated = false;
    return MyResult::ok;
}

// ============================================================================
// StyleEditor
// ============================================================================

/**
 * @brief Constrói o StyleEditor e carrega o arquivo de estilo salvo em disco.
 */
MyResult Memory::AllocStyleEditor() {
    if(style_editor_allocated)
        return MR_CLS_WARN_LOC("StyleEditor já alocado, ignorando.");

    style_editor_instance = std::make_unique<StyleEditor>();

    if(!style_editor_instance)
        return MR_MSGBOX_ERR_LOC("Falha ao alocar StyleEditor.");

    style_editor_instance->LoadFromFile("imgui_style.json");

    style_editor_allocated = true;
    return MyResult::ok;
}

/**
 * @brief Destrói o StyleEditor.
 */
MyResult Memory::DestroyStyleEditor() {
    if(!style_editor_allocated) return MyResult::ok;

    style_editor_instance.reset();
    style_editor_allocated = false;
    return MyResult::ok;
}

// ============================================================================
// Getters
// ============================================================================

/**
 * @brief Retorna o VulkanContext ou nullptr se não alocado.
 */
VulkanContext* Memory::GetVulkan() {
    if(!vulkan_allocated) {
        MR_CLS_WARN_LOC("GetVulkan() chamado antes de AllocVulkan().");
        return nullptr;
    }
    return vulkan_instance.get();
}

/**
 * @brief Retorna o ImGuiContext_Wrapper ou nullptr se não alocado.
 */
ImGuiContext_Wrapper* Memory::GetImGui() {
    if(!imgui_allocated) {
        MR_CLS_WARN_LOC("GetImGui() chamado antes de AllocImGui().");
        return nullptr;
    }
    return imgui_instance.get();
}

/**
 * @brief Retorna o FontManager ou nullptr se não alocado.
 */
FontManager* Memory::GetFontManager() {
    if(!font_manager_allocated) {
        MR_CLS_WARN_LOC("GetFontManager() chamado antes de AllocFontManager().");
        return nullptr;
    }
    return font_manager_instance.get();
}

/**
 * @brief Retorna o Console ImGui ou nullptr se não alocado.
 */
Console* Memory::GetConsole() {
    if(!console_allocated) {
        MR_CLS_WARN_LOC("GetConsole() chamado antes de AllocConsole().");
        return nullptr;
    }
    return console_instance.get();
}

/**
 * @brief Retorna o StyleEditor ou nullptr se não alocado.
 */
StyleEditor* Memory::GetStyleEditor() {
    if(!style_editor_allocated) {
        MR_CLS_WARN_LOC("GetStyleEditor() chamado antes de AllocStyleEditor().");
        return nullptr;
    }
    return style_editor_instance.get();
}
