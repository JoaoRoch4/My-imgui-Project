#pragma once

#include "pch.hpp"
#include "MyResult.hpp"
#include "Image.hpp"
#include "Appsettings.hpp"

/**
 * @file App.hpp
 * @brief Classe principal da aplicação — encapsula o ciclo de vida completo.
 *
 * DIVISÃO DE RESPONSABILIDADES
 * -----------------------------
 *  wWinMain      → Memory::Init / Memory::Shutdown / SDL_DestroyWindow / SDL_Quit
 *  App::run()    → SDL_Init / SDL_CreateWindow / Memory::AllocAll / AllocGlobals
 *                  / SDL_ShowWindow / RegisterCommands / MainLoop
 *  AllocGlobals  → aliases (g_Vulkan…g_Style) / AppSettings / MenuBar / AllocImages
 *  Close()       → vkDeviceWaitIdle / Unload imagens / delete posse / DestroyAll
 *
 * POR QUE AllocGlobals() NÃO TEM MAIS PARÂMETROS?
 * -------------------------------------------------
 * Os parâmetros antigos (extensions, w, h, scale) eram passados para
 * VulkanContext::Initialize() e SetupWindow() — que agora vivem dentro de
 * Memory::AllocVulkan(). AllocGlobals() apenas lê os resultados já prontos
 * via Memory::Get()->Get*().
 *
 * FLUXO CORRETO (wWinMain):
 * @code
 *   Memory::Init();
 *   App app;
 *   app.run();                        // AllocAll + AllocGlobals + loop
 *   Memory::Get()->DestroyAll();      // usa g_Window para destruir a surface
 *   Memory::Shutdown();
 *   SDL_DestroyWindow(app.g_Window);  // APÓS Vulkan
 *   SDL_Quit();
 * @endcode
 *
 * ALIASES SEM POSSE (g_Vulkan, g_ImGui, g_Console, g_Style)
 * -----------------------------------------------------------
 * A posse desses recursos é do Memory singleton.
 * App guarda cópias dos ponteiros apenas para acesso rápido no loop —
 * evitando Memory::Get()->GetVulkan() em todo frame.
 * Close() NÃO faz delete nesses ponteiros; Memory::DestroyAll() cuida deles.
 *
 * PONTEIROS COM POSSE EM App (g_MenuBar, g_Settings)
 * ----------------------------------------------------
 * Criados com new em AllocGlobals(), destruídos com delete em Close().
 *
 * IMAGENS (g_Logo, g_IconSettings)
 * ---------------------------------
 * Carregadas em AllocImages() após AllocGlobals().
 * Descarregadas em Close() ANTES de Memory::DestroyAll() — o Unload()
 * usa g_Vulkan->GetDevice() que ainda existe neste ponto.
 */

// Forward declarations
class VulkanContext;
class ImGuiContext_Wrapper;
class Console;
class StyleEditor;
class MenuBar;
struct SDL_Window;

struct ScrollingBuffer {
    int              MaxSize; ///< Capacidade máxima do buffer circular
    int              Offset;  ///< Índice do próximo slot a ser sobrescrito
    ImVector<ImVec2> Data;    ///< Dados do buffer

    ScrollingBuffer();
    ~ScrollingBuffer() = default;
    ScrollingBuffer(int max_size);

    void AddPoint(float x, float y); ///< Insere ponto; sobrescreve o mais antigo se cheio
    void Erase();                     ///< Limpa todos os dados
};


class App {
public:

    // =========================================================================
    // Construtor / Destrutor
    // =========================================================================

    App();
    ~App() = default;

	bool started; ///< true após Startup() ser chamado com sucesso
	void Startup();  ///< Inicializa subsistemas e recursos (chamado por run())

    RENDERDOC_API_1_1_2* rdoc_api = NULL; ///< API do RenderDoc — nullptr se não injetado

    /** @brief Inicializa a API do RenderDoc se a DLL estiver injetada no processo. */
    void InitRenderDoc() {
        if(HMODULE mod = GetModuleHandleA("renderdoc.dll")) {
            pRENDERDOC_GetAPI RENDERDOC_GetAPI =
                std::bit_cast<pRENDERDOC_GetAPI>(GetProcAddress(mod, "RENDERDOC_GetAPI"));
            const int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2,
                std::bit_cast<void**>(&rdoc_api));
            if(ret != 1) rdoc_api = NULL;
        }
    }

    /** @brief Inicia e encerra uma captura de frame do RenderDoc. */
    void CaptureFrame() {
        if(rdoc_api) {
            rdoc_api->StartFrameCapture(NULL, NULL);
            rdoc_api->EndFrameCapture(NULL, NULL);
            if(!rdoc_api->IsTargetControlConnected())
                rdoc_api->LaunchReplayUI(1, "");
        }
    }

    // =========================================================================
    // Ponto de entrada
    // =========================================================================

    /**
     * @brief Inicializa SDL, aloca subsistemas e executa o loop principal.
     *
     * Fluxo: SDL_Init → CreateWindow → Memory::AllocAll → AllocGlobals
     *        → SDL_ShowWindow → RegisterCommands → MainLoop → return
     *
     * SDL_DestroyWindow e SDL_Quit NÃO são chamados aqui — ficam em wWinMain
     * após Memory::DestroyAll(), para que Vulkan destrua a surface com a
     * janela ainda válida.
     */
    MyResult run();

    /** @brief Renderiza todas as janelas ImGui do frame atual. */
    MyResult Windows();

    /** @brief Desativa viewports flutuantes e reposiciona janelas (chamada por MenuBar). */
    void DisableViewportDocking();

    // =========================================================================
    // Estado de UI — público para acesso direto de MenuBar e comandos
    // =========================================================================

    bool  g_Done;          ///< true = MainLoop encerra na próxima iteração
    bool  g_ShowDemo;      ///< Exibe ImGui Demo Window
    bool  g_ShowStyleEd;   ///< Exibe Style Editor
    bool  g_IsFullscreen;  ///< Janela em modo fullscreen
    bool  g_grafico;       ///< Exibe gráfico de exemplo (ScrollingBuffer)
    bool  bViewportDocking; ///< true = viewports flutuantes habilitados
    bool bImPlot3d_DemoRealtimePlots;
    bool bImPlot3d_DemoQuadPlots;
    bool bImPlot3d_DemoTickLabels;

    float  g_window_opacity; ///< Opacidade da janela SDL [0.1, 1.0]
    float* g_color_ptr;      ///< Ponteiro para g_Settings->clear_color[0]

    ImGuiIO* g_io; ///< Referência ao ImGuiIO — atribuída em MainLoop()

    // =========================================================================
    // Aliases sem posse — dono: Memory singleton
    // NÃO chame delete: Close() delega ao Memory::DestroyAll()
    // Atribuídos em AllocGlobals() APÓS Memory::AllocAll() retornar.
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
    // Janela SDL
    // =========================================================================

    SDL_Window* g_Window; ///< Criada em run(), destruída em wWinMain após DestroyAll()

    // =========================================================================
    // Imagens — posse de App, Unload() em Close() ANTES de DestroyAll()
    // =========================================================================

    Image g_Logo;         ///< Logo principal da aplicação
    Image g_IconSettings; ///< Ícone de configurações

    // =========================================================================
    // Persistência
    // =========================================================================

    void SaveConfig(); ///< Serializa *g_Settings → settings.json
    void LoadConfig(); ///< Carrega settings.json → *g_Settings

private:

    std::string m_ConfigFile; ///< Caminho do JSON de configuração

    // =========================================================================
    // Funções internas do ciclo de vida
    // =========================================================================

    /**
     * @brief Extrai aliases do Memory e inicializa AppSettings, MenuBar e Images.
     *
     * DEVE ser chamado APÓS Memory::AllocAll() — os ponteiros só estão
     * válidos depois que AllocAll() retorna com sucesso.
     * Sem parâmetros: Vulkan/ImGui já foram inicializados pelo Memory.
     */
    MyResult AllocGlobals();

    /**
     * @brief Carrega g_Logo, g_IconSettings, etc.
     * Falhas não são fatais — IsLoaded() retorna false e Draw() é no-op.
     */
    MyResult AllocImages();

    /**
     * @brief Ciclo de destruição: GPU idle → Unload → delete → DestroyAll → SDL.
     */
    void Close();

    /** @brief Registra EXIT, QUIT, BREAK, SPECS, VSYNC, etc. no Console. */
    MyResult RegisterCommands();

    /** @brief Loop SDL + ImGui + Vulkan — roda até g_Done == true. */
    MyResult MainLoop();

    /** @brief Obtém resolução do desktop via Win32 GetDesktopWindow(). */
    MyResult GetDesktopResolution(int& horizontal, int& vertical);
};

// =============================================================================
// Ponteiro global para a instância única de App
// =============================================================================

/**
 * @brief Ponteiro global para a instância App em execução.
 *
 * DEFINIDO em App.cpp:  App* g_App = nullptr;
 * ATRIBUÍDO em App():   g_App = this;
 * ANULADO em Close():   g_App = nullptr;
 *
 * Válido apenas no intervalo [App::App() .. App::Close()].
 */
extern App* g_App;