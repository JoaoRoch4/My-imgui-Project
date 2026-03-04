#pragma once

#include "pch.hpp"
#include "MyResult.hpp"
#include "Image.hpp"
#include "Appsettings.hpp"

///< Image — wrapper de textura Vulkan para ImGui

/**
 * @file App.hpp
 * @brief Classe principal da aplicação — encapsula o ciclo de vida completo.
 *
 * COMO MenuBar (e outros) ACESSAM O ESTADO SEM PARÂMETROS
 * --------------------------------------------------------
 * App.cpp define um ponteiro global:
 *
 *   App* g_App = nullptr;   ← definido em App.cpp
 *
 * O construtor de App faz:
 *
 *   g_App = this;           ← atribuído em App::App()
 *
 * App.hpp declara esse ponteiro com extern:
 *
 *   extern App* g_App;      ← declarado aqui embaixo
 *
 * Qualquer arquivo que incluir App.hpp pode então escrever:
 *
 *   g_App->g_Done    = true;
 *   g_App->g_Console->AddLog(L"...");
 *   g_App->g_Logo.Draw(200.0f, 80.0f);
 *
 * Sem nenhum parâmetro de instância, sem 7 extern separados.
 *
 * POR QUE OS MEMBROS SÃO PÚBLICOS?
 * ----------------------------------
 * MenuBar, RegisterCommands e MainLoop precisam ler E escrever
 * g_Done, g_ShowDemo, g_Settings, g_Window, g_Console, etc.
 * Usar getters/setters para cada campo seria boilerplate desnecessário.
 * Os membros são "públicos por necessidade de subsistema", não por acidente.
 *
 * IMAGENS (Image)
 * ---------------
 * Imagens de uso global (logos, ícones) vivem como membros de App.
 * Carregadas em AllocImages() (após AllocGlobals) e descarregadas
 * em Close() antes de Memory::DestroyAll() — garantindo a ordem correta.
 */

// Forward declarations — tipos incluídos apenas por ponteiro
class VulkanContext;
class ImGuiContext_Wrapper;
class Console;
class StyleEditor;
class MenuBar;
// Image não é forward-declared — incluímos Image.hpp diretamente
// porque App declara membros Image por valor (g_Logo, g_IconSettings).
struct SDL_Window;

struct ScrollingBuffer {
    int MaxSize;
    int Offset;
    ImVector<ImVec2> Data;

    ScrollingBuffer();
    ~ScrollingBuffer() =default;

    ScrollingBuffer(int max_size);

    void AddPoint(float x, float y);

    void Erase();
};


class App {
public:

    // =========================================================================
    // Construtor / Destrutor
    // =========================================================================

    App();
    ~App() = default;
    RENDERDOC_API_1_1_2 *rdoc_api = NULL;

    void InitRenderDoc() {
    // Tenta obter o handle da DLL se o RenderDoc estiver injetado no processo
    if (HMODULE mod = GetModuleHandleA("renderdoc.dll")) {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = std::bit_cast<pRENDERDOC_GetAPI>(GetProcAddress(mod, "RENDERDOC_GetAPI"));
        const int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, std::bit_cast<void **> ( & rdoc_api));
        if (ret != 1) rdoc_api = NULL;
    }
}

// Chame isso quando quiser capturar (ex: ao apertar F12)
void CaptureFrame() {
    if (rdoc_api) {
        rdoc_api->StartFrameCapture(NULL, NULL);
        // O RenderDoc captura o que estiver entre Start e End
        rdoc_api->EndFrameCapture(NULL, NULL);
        
        // Abre a UI do RenderDoc automaticamente para mostrar o resultado
        if (!rdoc_api->IsTargetControlConnected()) {
            rdoc_api->LaunchReplayUI(1, "");
        }
    }
}

    // =========================================================================
    // Ponto de entrada
    // =========================================================================

    /**
     * @brief Inicializa SDL, aloca subsistemas e executa o loop principal.
     * Fluxo: SDL_Init → CreateWindow → AllocGlobals → ShowWindow
     *        → RegisterCommands → MainLoop → Close
     */
    MyResult run();

    bool bViewportDocking; ///< true = ImGui::GetIO().ConfigFlags |=
                           ///< ImGuiConfigFlags_DockingEnable

    void DisableViewportDocking(); ///< Desativa a funcionalidade de docking do
                                   ///< ImGui (chamada por MenuBar)
    MyResult Windows();            

    // =========================================================================
    // Estado de UI — público para acesso direto de MenuBar e comandos
    // =========================================================================

    bool g_Done;         ///< true = MainLoop encerra na próxima iteração
    bool g_ShowDemo;     ///< Exibe ImGui Demo Window
    bool g_ShowStyleEd;  ///< Exibe Style Editor
    bool g_IsFullscreen; ///< Janela em modo fullscreen
    bool g_grafico;      ///< Exibe gráfico de exemplo (ScrollingBuffer)

    float g_window_opacity;
    float *g_color_ptr; ///< Ponteiro para g_Settings->clear_color[0] — usado no
                        ///< ImGui ColorEdit4

                       ImGuiIO* g_io;


    // =========================================================================
    // Ponteiros de subsistema — aliases sem posse (dono: Memory singleton)
    // NÃO chame delete: Close() delega ao Memory::DestroyAll()
    // =========================================================================

    VulkanContext*        g_Vulkan;  ///< Contexto Vulkan (device, queues, swapchain)
    ImGuiContext_Wrapper* g_ImGui;   ///< Contexto ImGui (backends SDL3 + Vulkan)
    Console*              g_Console; ///< Console ImGui wide-char
    StyleEditor*          g_Style;   ///< Editor de estilo ImGui com JSON

    // =========================================================================
    // Ponteiros com posse em App — Close() faz delete nestes
    // =========================================================================

    MenuBar*     g_MenuBar;  ///< Barra de menu principal
    AppSettings* g_Settings; ///< Configurações persistidas em settings.json

    // =========================================================================
    // Janela SDL — posse do SDL, SDL_DestroyWindow em Close()
    // =========================================================================

    SDL_Window* g_Window; ///< Janela SDL3 principal

    // =========================================================================
    // Imagens da aplicação — posse de App, Unload() em Close()
    //
    // Declare aqui imagens de uso global (ícones, logos, splash, etc.).
    // Nunca declare Image como membro de stack em classes UI — elas vivem
    // menos que o contexto Vulkan e podem ser destruídas na ordem errada.
    //
    // Regras:
    //   • Load() deve ser chamado em AllocImages(), APÓS AllocGlobals()
    //     (exige VkDevice + command pool do frame 0 prontos).
    //   • Unload() é chamado em Close(), ANTES de Memory::DestroyAll().
    //   • Image possui RAII — o destrutor chama Unload() automaticamente,
    //     mas Close() o faz explicitamente para controlar a ordem.
    //
    // Exemplo de uso em MainLoop (após NewFrame()):
    // @code
    //   g_App->g_Logo.DrawCentered(200.0f, 80.0f);
    //   g_App->g_IconSettings.Draw(16.0f, 16.0f);
    // @endcode
    // =========================================================================

    Image g_Logo;         ///< Logo principal da aplicação
    Image g_IconSettings; ///< Ícone de configurações (barra de menu / toolbar)

    // =========================================================================
    // Persistência (chamadas por MainLoop e pelo usuário)
    // =========================================================================

    void SaveConfig(); ///< Serializa *g_Settings → settings.json
    void LoadConfig(); ///< Carrega settings.json → *g_Settings

private:

    std::string m_ConfigFile; ///< Caminho do JSON de configuração

    // =========================================================================
    // Funções internas do ciclo de vida (private — só run() as chama)
    // =========================================================================

    /**
     * @brief Inicializa subsistemas e atribui os ponteiros de membro.
     * Ordem: Memory::AllocAll → aliases → new AppSettings → new MenuBar → AllocImages
     */
    MyResult AllocGlobals(const ImVector<const char*>& extensions,
                          int   w,
                          int   h,
                          float scale);

    /**
     * @brief Carrega todas as imagens da aplicação (g_Logo, g_IconSettings, etc.).
     *
     * DEVE ser chamado após AllocGlobals() — exige:
     *   • VkDevice criado (AllocVulkan)
     *   • Command pool do frame 0 válido (ImGui inicializado + primeiro frame renderizado
     *     OR pool criado explicitamente — o pool de frame é criado em SetupWindow)
     *
     * As imagens são liberadas em Close() via g_Logo.Unload() antes de DestroyAll().
     *
     * @return MyResult::ok se todas as imagens carregaram com sucesso.
     */
    MyResult AllocImages();

    /**
     * @brief Ciclo completo de destruição — vkDeviceWaitIdle → DestroyAll → SDL_Quit.
     *
     * Ordem:
     *  1. vkDeviceWaitIdle
     *  2. g_Logo.Unload() + g_IconSettings.Unload()  ← imagens antes do DestroyAll
     *  3. delete g_MenuBar / g_Settings
     *  4. Memory::DestroyAll
     *  5. SDL_DestroyWindow + SDL_Quit
     *
     * Sempre chamado ao final de run(), mesmo em caso de erro.
     */
    void Close();

    /**
     * @brief Registra EXIT, QUIT, BREAK, SPECS, VSYNC no Console.
     */
    MyResult RegisterCommands();

    /**
     * @brief Loop SDL + ImGui + Vulkan — roda até g_Done == true.
     */
    MyResult MainLoop();

    MyResult GetDesktopResolution(int& horizontal, int& vertical);

};

// =============================================================================
// Ponteiro global para a instância única de App
// =============================================================================

/**
 * @brief Ponteiro global para a instância App em execução.
 *
 * DEFINIDO em App.cpp como:  App* g_App = nullptr;
 * ATRIBUÍDO no construtor:    g_App = this;
 * ANULADO em Close():         g_App = nullptr;
 *
 * Como usar em qualquer .cpp que inclua App.hpp:
 * @code
 *   g_App->g_Done    = true;
 *   g_App->g_Console->AddLog(L"...");
 *   g_App->g_Window;
 * @endcode
 *
 * Nunca acesse g_App fora do intervalo [App() .. Close()].
 */
extern App* g_App;
