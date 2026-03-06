#pragma once

#include "pch.hpp"
#include "MyResult.hpp"
#include "Appsettings.hpp" // AppSettings + WindowSettings + FontSettings + StyleSettings + ColorSettings+
#include "Image.hpp"

/**
 * @file App.hpp
 * @brief Classe principal da aplicação — encapsula o ciclo de vida completo.
 *
 * PERSISTÊNCIA UNIFICADA
 * -----------------------
 * Toda configuração da aplicação é salva em um único settings.json via
 * App::SaveConfig() / App::LoadConfig(). O AppSettings agora cobre:
 *
 *  AppSettings::clear_color  — cor de fundo Vulkan
 *  AppSettings::window       — flags booleanas (show_demo, show_style_editor, etc.)
 *  AppSettings::font         — font_size_base e font_scale_main
 *  AppSettings::style        — dimensões do ImGuiStyle (rounding, padding, etc.)
 *  AppSettings::color        — as 54 cores do ImGuiStyle::Colors[]
 *
 * O imgui_style.json separado foi ELIMINADO. StyleEditor::SaveToFile() e
 * LoadFromFile() agora delegam para App::SaveConfig() / LoadConfig().
 *
 * ALIASES SEM POSSE (g_Vulkan, g_ImGui, g_Console, g_Style)
 * -----------------------------------------------------------
 * A posse desses recursos é do Memory singleton.
 * App guarda cópias dos ponteiros apenas para acesso rápido no loop.
 * Close() NÃO faz delete nesses ponteiros; Memory::DestroyAll() cuida deles.
 */

// Forward declarations
class VulkanContext;
class ImGuiContext_Wrapper;
class Console;
class StyleEditor;
class MenuBar;
struct SDL_Window;
class Image;

// ============================================================================
// ScrollingBuffer — buffer circular para gráficos ImPlot
// ============================================================================

/**
 * @brief Buffer circular de ImVec2 para gráficos de scroll contínuo.
 */
struct ScrollingBuffer {
    int              MaxSize; ///< Capacidade máxima do buffer
    int              Offset;  ///< Índice do próximo slot a ser sobrescrito
    ImVector<ImVec2> Data;    ///< Dados do buffer circular

    ScrollingBuffer();                    ///< Constrói com MaxSize=2000
    ~ScrollingBuffer() = default;
    explicit ScrollingBuffer(int max_size); ///< Constrói com capacidade customizada

    void AddPoint(float x, float y); ///< Insere ponto; sobrescreve o mais antigo se cheio
    void Erase();                     ///< Limpa todos os dados
};

// ============================================================================
// App
// ============================================================================

class App {
public:


    // =========================================================================
    // Construtor / Destrutor
    // =========================================================================

    App();
    ~App() = default;

    bool started; ///< true após Startup() ser chamado com sucesso

    /** @brief Atribui g_App = this e chama InitRenderDoc(). */
    void Startup();

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
     */
    MyResult run();

    

    /** @brief Renderiza todas as janelas ImGui do frame atual. */
    MyResult Windows();

    /** @brief Desativa viewports flutuantes e reposiciona janelas. */
    void DisableViewportDocking();

    // =========================================================================
    // Estado de UI — espelha AppSettings::window para acesso rápido no loop
    // =========================================================================
    //
    // Estes membros são ALIASES de leitura/escrita para g_Settings->window.*
    // Após LoadConfig(), SyncFlagsFromSettings() copia os valores salvos para
    // cá. SaveConfig() faz o caminho inverso via SyncFlagsToSettings().
    //
    // POR QUE NÃO ACESSAR g_Settings->window.* DIRETAMENTE NO LOOP?
    // → Manter os nomes originais (g_Done, g_ShowDemo, etc.) evita alterar
    //   todo o código de Windows() e dos comandos do console que já os usam.
  
    bool  g_Done;          ///< true = MainLoop encerra na próxima iteração (não persistido)
    bool  g_ShowDemo;      ///< Espelho de AppSettings::window.show_demo
    bool  g_ShowStyleEd;   ///< Espelho de AppSettings::window.show_style_editor
    bool  g_IsFullscreen;  ///< Espelho de AppSettings::window.is_fullscreen
    bool  g_grafico;       ///< Espelho de AppSettings::window.show_graph
    bool m_Window_Controls;

    bool  bViewportDocking;            ///< Espelho de AppSettings::window.viewport_docking
    bool  bImPlot3d_DemoRealtimePlots; ///< Espelho de AppSettings::window.implot3d_realtime_plots
    bool  bImPlot3d_DemoQuadPlots;     ///< Espelho de AppSettings::window.implot3d_quad_plots
    bool  bImPlot3d_DemoTickLabels;    ///< Espelho de AppSettings::window.implot3d_tick_labels

    float  g_window_opacity; ///< Opacidade da janela SDL [0.1, 1.0] (não persistida)
    float* g_color_ptr;      ///< Ponteiro para g_Settings->clear_color[0]

    ImGuiIO* g_io; ///< Referência ao ImGuiIO — atribuída em MainLoop()

    // =========================================================================
    // Aliases sem posse — dono: Memory singleton
    // =========================================================================

    VulkanContext*        g_Vulkan;  ///< Contexto Vulkan (device, queues, swapchain)
    ImGuiContext_Wrapper* g_ImGui;   ///< Contexto ImGui (backends SDL3 + Vulkan)
    Console*              g_Console; ///< Console ImGui wide-char
    StyleEditor*          g_Style;   ///< Editor de estilo ImGui

    // =========================================================================
    // Ponteiros com posse parcial em App
    // =========================================================================

    MenuBar*     g_MenuBar;  ///< Barra de menu principal
    AppSettings* g_Settings; ///< Configurações persistidas — dono: Memory singleton

    // =========================================================================
    // Janela SDL
    // =========================================================================

    SDL_Window* g_Window; ///< Criada em run(), destruída em wWinMain após DestroyAll()

    // =========================================================================
    // Imagens
    // =========================================================================

    Image g_Logo;         ///< Logo principal da aplicação
    Image g_IconSettings; ///< Ícone de configurações

    class MyWindows* g_MyWindows; ///< Gerenciador de janelas ImGui (Console, StyleEditor, etc.)

    // =========================================================================
    // Persistência
    // =========================================================================

    /**
     * @brief Copia os flags de App para AppSettings e serializa para settings.json.
     *
     * DEVE ser chamado sempre que qualquer flag ou configuração mudar:
     *  - Checkbox de show_demo, show_style_editor, etc.
     *  - Slider de font_scale_main ou font_size_base.
     *  - Qualquer alteração no StyleEditor.
     */
    void SaveConfig();

    /**
     * @brief Carrega settings.json em *g_Settings e aplica ao estado de App.
     *
     * Chamado em AllocGlobals() após g_Settings ser atribuído.
     * Se o arquivo não existir ou estiver corrompido, mantém os defaults.
     */
    void LoadConfig();
    /**
     * @brief Copia AppSettings::window.* → membros públicos g_Show*, g_grafico, etc.
     *
     * Chamado em LoadConfig() para que o primeiro frame já use os valores salvos.
     */
    void SyncFlagsFromSettings();

    /**
     * @brief Copia membros públicos g_Show*, g_grafico, etc. → AppSettings::window.*
     *
     * Chamado no início de SaveConfig() para capturar o estado atual antes de serializar.
     */
    void SyncFlagsToSettings();

    /**
     * @brief Lê ImGuiStyle atual e salva em g_Settings->style e g_Settings->color.
     *
     * Chamado em SaveConfig() para capturar qualquer alteração feita pelo StyleEditor
     * ou por código que escreva diretamente em ImGui::GetStyle().
     */
    void SyncStyleFromImGui();

    /**
     * @brief Aplica g_Settings->style e g_Settings->color ao ImGuiStyle atual.
     *
     * Chamado em LoadConfig() para restaurar a aparência salva.
     */
    void ApplyStyleToImGui();

    // =========================================================================
    // Funções internas do ciclo de vida
    // =========================================================================

    MyResult AllocGlobals();
    MyResult AllocImages();
    void     Close();
    MyResult RegisterCommands();
    MyResult MainLoop();
    MyResult GetDesktopResolution(int& horizontal, int& vertical);

protected:

    std::string m_ConfigFile; ///< Caminho do JSON de configuração ("settings.json")

    // =========================================================================
    // Helpers de sincronização de flags
    // =========================================================================

    
};

// ============================================================================
// Ponteiro global para a instância única de App
// ============================================================================
