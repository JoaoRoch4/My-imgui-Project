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
 *
 * DETECÇÃO DO MONITOR CORRETO NA INICIALIZAÇÃO
 * ---------------------------------------------
 * O problema com SDL_GetPrimaryDisplay() é que em setups multi-monitor,
 * o usuário pode ter configurado o app para abrir em um monitor secundário.
 * Usar o DPI do monitor primário para um app que abre no secundário resulta
 * em fontes e espaçamento incorretos.
 *
 * SOLUÇÃO: em AllocVulkan(), após a janela já existir (g_App->g_Window),
 * chamamos SDL_GetDisplayForWindow() para identificar em qual monitor a
 * janela está posicionada. O resultado é salvo em m_display_id e
 * m_window_scale para uso em AllocImGui() e AllocFontManager().
 *
 * Fluxo de detecção em AllocVulkan():
 *   1. SDL_GetDisplayForWindow(window)        → m_display_id (qual monitor)
 *   2. SDL_GetDisplayContentScale(m_display_id) → m_window_scale (DPI scale)
 *   3. SDL_GetCurrentDisplayMode(m_display_id)  → m_display_w, m_display_h
 *   4. Loga no printf: monitor, resolução, DPI, scale
 *
 * AllocImGui() e AllocFontManager() usam m_window_scale diretamente —
 * sem nova chamada SDL — garantindo consistência entre os três sub-métodos.
 *
 * COLETA DE ARGUMENTOS SEM PARÂMETROS
 * -------------------------------------
 * Todos os dados que antes eram passados por AllocAll(extensions, window, w, h, scale)
 * são agora coletados internamente pelos sub-métodos via g_App e SDL:
 *
 *   SDL_Window*           → g_App->g_Window
 *   int w, h              → SDL_GetWindowSize(g_App->g_Window, &w, &h)
 *   float scale           → SDL_GetDisplayContentScale(m_display_id)
 *   ImVector<const char*> → SDL_Vulkan_GetInstanceExtensions(&n)
 *
 * PRÉ-REQUISITO: g_App->g_Window != nullptr quando AllocAll() é chamado.
 * Fluxo garantido em App::run():
 *   SDL_CreateWindow → g_Window = result → AllocGlobals() → Memory::AllocAll()
 */
#include "pch.hpp"
#include "Memory.hpp"
#include "MyResult.hpp"
#include "App.hpp"    // g_App — ponteiro global para App em execução
#include "VulkanContext_Wrapper.hpp"
#include "ImGuiContext_Wrapper.hpp"
#include "FontManager.hpp"
#include "EmojiDebugHelper.h"
#include "Console.hpp"
#include "StyleEditor.hpp"

// ============================================================================
// Construtor / Destrutor
// ============================================================================

/**
 * @brief Construtor privado — unique_ptrs começam nullptr, flags começam false.
 */
Memory::Memory()
    : app_instance(nullptr)
    , vulkan_instance(nullptr)
    , imgui_instance(nullptr)
    , font_manager_instance(nullptr)
    , console_instance(nullptr)
    , style_editor_instance(nullptr)
    , app_allocated(false)
    , vulkan_allocated(false)
    , imgui_allocated(false)
    , font_manager_allocated(false)
    , console_allocated(false)
    , style_editor_allocated(false)
    , m_window_scale(1.0f)
    , m_display_id(0)
    , m_display_w(0)
    , m_display_h(0)
{}

/**
 * @brief Destrutor privado — unique_ptrs liberam automaticamente se
 * DestroyAll() não tiver sido chamado (segurança em caso de crash no shutdown).
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
    static std::unique_ptr<Memory> instance =
        std::make_unique<Memory>(); // construído uma vez, destruído ao fim do programa
    return instance.get();
}

// ============================================================================
// AllocAll / DestroyAll
// ============================================================================

/**
 * @brief Aloca todos os recursos na ordem correta — sem argumentos externos.
 *
 * PRÉ-REQUISITO: g_App->g_Window já atribuído (App::run() garante isso).
 *
 * A ordem é obrigatória:
 *  1. AllocVulkan()      — detecta monitor, lê w/h/scale/extensions, SetupWindow
 *  2. AllocApp()         — no-op se g_App é externo (caso normal: stack em main)
 *  3. AllocImGui()       — usa m_window_scale; ImplVulkan_Init usa o swapchain
 *  4. AllocFontManager() — usa m_window_scale; atlas ImGui (requer contexto ImGui)
 *  5. AllocConsole()     — ImGui::MemAlloc (requer contexto ImGui)
 *  6. AllocStyleEditor() — sem dependência crítica de ordem
 */
MyResult Memory::AllocAll() {
    // Valida o pré-requisito antes de qualquer alocação
    if(!g_App || !g_App->g_Window)
        return MR_MSGBOX_ERR_LOC(
            "Memory::AllocAll() chamado antes de g_App->g_Window ser atribuido. "
            "App::run() deve criar SDL_Window e atribuir g_App->g_Window antes "
            "de chamar AllocGlobals().");

    // 1. Vulkan: detecta monitor, Initialize() + SetupWindow()
    //    swapchain DEVE existir antes do ImGui
    if(!MR_IS_OK(AllocVulkan()))
        return MR_CLS_ERR_LOC("AllocAll: falhou em AllocVulkan");

    // 2. App — no-op se g_App já existe externamente (caso normal)
    if(!MR_IS_OK(AllocApp()))
        return MR_CLS_ERR_LOC("AllocAll: falhou em AllocApp");

    // 3. ImGui: CreateContext + ImplSDL3_Init + ImplVulkan_Init
    //    ImplVulkan_Init acessa wd->ImageCount — swapchain já existe graças ao passo 1
    if(!MR_IS_OK(AllocImGui()))
        return MR_CLS_ERR_LOC("AllocAll: falhou em AllocImGui");

    // 4. Fontes: carrega TTFs + emoji no atlas — requer contexto ImGui ativo
    if(!MR_IS_OK(AllocFontManager()))
        return MR_CLS_ERR_LOC("AllocAll: falhou em AllocFontManager");

    // 5. Console: Console::Console() chama ImGui::MemAlloc() — requer GImGui != nullptr
    if(!MR_IS_OK(AllocConsole()))
        return MR_CLS_ERR_LOC("AllocAll: falhou em AllocConsole");

    // 6. StyleEditor: sem dependência crítica de ordem
    if(!MR_IS_OK(AllocStyleEditor()))
        return MR_CLS_ERR_LOC("AllocAll: falhou em AllocStyleEditor");

    return MyResult::ok;
}

/**
 * @brief Destroi todos os recursos na ordem inversa da alocação.
 */
MyResult Memory::DestroyAll() {
    DestroyStyleEditor(); // 6 → primeiro a ser destruído
    DestroyConsole();     // 5
    DestroyFontManager(); // 4
    DestroyImGui();       // 3 — DestroyContext() do ImGui
    DestroyApp();         // 2
    DestroyVulkan();      // 1 → último a ser destruído
    return MyResult::ok;
}

// ============================================================================
// App
// ============================================================================

/**
 * @brief Constrói App dentro do Memory — no-op se g_App já existe externamente.
 *
 * No caso normal (App na stack em main()), g_App já está atribuído quando
 * AllocAll() é chamado. Nesse caso apenas marcamos app_allocated = true
 * sem criar nenhuma instância — Memory não tem posse do App externo.
 *
 * Se g_App for nullptr aqui, criamos uma instância dentro do Memory.
 */
MyResult Memory::AllocApp() {
    if(app_allocated)
        return MR_CLS_WARN_LOC("App já alocado, ignorando.");

    // Caso normal: App foi criado na stack em main() e g_App já está atribuído.
    // Memory não cria outra instância — apenas sinaliza que o passo foi concluído.
    if(g_App) {
        app_allocated = true; // App externo: sem posse, sem delete
        return MyResult::ok;
    }

    // Caso alternativo: Memory cria e possui a instância de App
    app_instance = std::make_unique<App>();
    if(!app_instance)
        return MR_MSGBOX_ERR_LOC("Falha ao alocar App dentro do Memory.");

    app_allocated = true;
    return MyResult::ok;
}

/**
 * @brief Destrói a instância App gerenciada pelo Memory.
 *
 * Se App foi criado externamente (stack em main), app_instance é nullptr
 * e o reset() é no-op — a destruição fica a cargo do dono original.
 */
MyResult Memory::DestroyApp() {
    if(!app_allocated)
        return MyResult::ok;

    // app_instance é nullptr se App é externo — reset() é no-op seguro
    app_instance.reset();
    app_allocated = false;
    return MyResult::ok;
}

// ============================================================================
// Vulkan
// ============================================================================

/**
 * @brief Inicializa o VulkanContext, detecta o monitor da janela e configura
 * a surface/swapchain.
 *
 * DETECÇÃO DO MONITOR:
 * Esta é a primeira função que pode identificar corretamente em qual monitor
 * a janela está. Fazemos a detecção aqui — antes de qualquer uso de scale —
 * e salvamos os resultados em membros privados reutilizados pelos demais
 * sub-métodos.
 *
 * Passos de detecção (após a janela existir em g_App->g_Window):
 *
 *   SDL_GetDisplayForWindow(window)
 *     → m_display_id: ID do monitor em que a janela está posicionada.
 *       Em setups multi-monitor, pode ser diferente do SDL_GetPrimaryDisplay().
 *       Retorna 0 em caso de falha — fallback para SDL_GetPrimaryDisplay().
 *
 *   SDL_GetDisplayContentScale(m_display_id)
 *     → m_window_scale: fator de escala DPI do monitor correto.
 *       1.0 = 96 DPI (100%), 1.25 = 120 DPI (125%), 1.5 = 144 DPI (150%).
 *       Usado em AllocImGui() e AllocFontManager() sem nova consulta.
 *
 *   SDL_GetCurrentDisplayMode(m_display_id)
 *     → m_display_w, m_display_h: resolução do monitor em pixels.
 *       Usada apenas para log — não afeta o tamanho do swapchain
 *       (o swapchain usa o tamanho da janela, não do monitor).
 *
 * SetupWindow() É CHAMADO AQUI — não em run() — porque ImGui_ImplVulkan_Init
 * (chamado em AllocImGui) precisa que o swapchain já exista:
 *   imgui_impl_vulkan.cpp:1323  assert(info->ImageCount >= info->MinImageCount)
 */
MyResult Memory::AllocVulkan() {
    if(vulkan_allocated)
        return MR_CLS_WARN_LOC("VulkanContext já alocado, ignorando.");

    SDL_Window* window = g_App->g_Window; // atribuído antes de AllocAll()

    // ---- Detecção do monitor onde a janela está ----------------------------

    // SDL_GetDisplayForWindow retorna o ID do display em que a janela está.
    // É mais preciso que SDL_GetPrimaryDisplay() em setups multi-monitor.
    m_display_id = SDL_GetDisplayForWindow(window);

    if(m_display_id == 0) {
        // Fallback: se a janela ainda não está em nenhum display (pode ocorrer
        // antes de SDL_ShowWindow ser chamado), usa o display primário.
        m_display_id = SDL_GetPrimaryDisplay();
        printf("[Memory] SDL_GetDisplayForWindow falhou — usando display primario.\n");
    }

    // DPI scale do monitor correto — NÃO sempre o primário
    m_window_scale = SDL_GetDisplayContentScale(m_display_id);
    if(m_window_scale <= 0.0f) m_window_scale = 1.0f; // fallback seguro

    // Resolução atual do monitor (para log)
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(m_display_id);
    if(mode) {
        m_display_w = mode->w;
        m_display_h = mode->h;
    }

    // Log de inicialização do monitor detectado
    printf("\n=== Monitor de Inicializacao ===\n");
    printf("  Display ID:    %u\n",  (unsigned)m_display_id);
    printf("  Resolucao:     %d x %d px\n", m_display_w, m_display_h);
    printf("  Scale (DPI):   %.2fx  (%.0f DPI)\n",
        m_window_scale, m_window_scale * 96.0f);  // 96 DPI = 100%
    printf("  Primario:      %s\n",
        (m_display_id == SDL_GetPrimaryDisplay()) ? "sim" : "nao");
    printf("================================\n\n");

    // ---- Tamanho atual da janela (para o swapchain) -----------------------

    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h); // pixels lógicos

    // ---- Extensões de instância Vulkan requeridas pelo SDL ----------------

    ImVector<const char*> extensions;
    uint32_t n = 0;
    const char* const* sdl_ext = SDL_Vulkan_GetInstanceExtensions(&n);
    for(uint32_t i = 0; i < n; ++i)
        extensions.push_back(sdl_ext[i]);

    // ---- Criação e inicialização do VulkanContext --------------------------

    vulkan_instance = std::make_unique<VulkanContext>();
    if(!vulkan_instance)
        return MR_MSGBOX_ERR_LOC("Falha ao alocar VulkanContext.");

    if(!vulkan_instance->Initialize(extensions)) // cria instance, device, queues
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
    if(!vulkan_allocated)
        return MyResult::ok;

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
 * Usa m_window_scale detectado em AllocVulkan() — sem nova chamada SDL.
 * Isso garante que o scale usado pelo ImGui é o do monitor onde a janela
 * foi aberta, não sempre o do monitor primário.
 *
 * ImGui_ImplVulkan_Init acessa o swapchain para ler MinImageCount.
 * Por isso AllocVulkan (que chama SetupWindow) DEVE ter sido chamado antes.
 */
MyResult Memory::AllocImGui() {
    if(imgui_allocated)
        return MR_CLS_WARN_LOC("ImGuiContext_Wrapper já alocado, ignorando.");

    if(!vulkan_allocated) // swapchain precisa existir para ImplVulkan_Init
        return MR_MSGBOX_ERR_LOC("AllocImGui chamado antes de AllocVulkan.");

    imgui_instance = std::make_unique<ImGuiContext_Wrapper>();
    if(!imgui_instance)
        return MR_MSGBOX_ERR_LOC("Falha ao alocar ImGuiContext_Wrapper.");

    // Initialize: CreateContext + ImplSDL3_Init + ImplVulkan_Init
    // Usa m_window_scale do monitor detectado em AllocVulkan() — sem nova consulta SDL.
    if(!imgui_instance->Initialize(g_App->g_Window, vulkan_instance.get(), m_window_scale))
        return MR_MSGBOX_ERR_LOC("ImGuiContext_Wrapper::Initialize() falhou.");

    imgui_allocated = true; // GImGui != nullptr a partir daqui
    return MyResult::ok;
}

/**
 * @brief Destrói o ImGuiContext_Wrapper (shutdown dos backends + DestroyContext).
 */
MyResult Memory::DestroyImGui() {
    if(!imgui_allocated)
        return MyResult::ok;

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
 * Usa m_window_scale detectado em AllocVulkan() — sem nova chamada SDL.
 * Tamanho base das fontes = 13.0f * m_window_scale.
 *
 * Deve ser chamado após AllocImGui() e antes do primeiro NewFrame().
 * O atlas é compilado e enviado à GPU no primeiro NewFrame().
 */
MyResult Memory::AllocFontManager() {
    if(font_manager_allocated)
        return MR_CLS_WARN_LOC("FontManager já alocado, ignorando.");

    if(!imgui_allocated)
        return MR_MSGBOX_ERR_LOC("AllocFontManager chamado antes de AllocImGui.");

    font_manager_instance = std::make_unique<FontManager>();
    if(!font_manager_instance)
        return MR_MSGBOX_ERR_LOC("Falha ao alocar FontManager.");

    // Usa m_window_scale do monitor detectado — não SDL_GetPrimaryDisplay()
    bool emoji_ok = font_manager_instance->LoadAllFontsWithEmoji(13.0f * m_window_scale);

    printf("\n=== Font Status ===\n");
    printf("Fonts loaded: %d\n", font_manager_instance->GetFontCount());
    printf("Base size:    %.1f px  (13.0 x %.2f scale)\n",
        13.0f * m_window_scale, m_window_scale);
    EmojiDebugHelper::PrintEmojiStatus(emoji_ok);
    printf("===================\n\n");

    font_manager_allocated = true;
    return MyResult::ok;
}

/**
 * @brief Libera o FontManager.
 */
MyResult Memory::DestroyFontManager() {
    if(!font_manager_allocated)
        return MyResult::ok;

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
    if(!console_allocated)
        return MyResult::ok;

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
    if(!style_editor_allocated)
        return MyResult::ok;

    style_editor_instance.reset();
    style_editor_allocated = false;
    return MyResult::ok;
}

// ============================================================================
// Getters
// ============================================================================

/**
 * @brief Retorna o App ou nullptr se não alocado pelo Memory.
 * Quando App é externo (stack em main), app_instance é nullptr — use g_App.
 */
App* Memory::GetApp() {
    return app_instance.get(); // nullptr se App é externo (caso normal)
}

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