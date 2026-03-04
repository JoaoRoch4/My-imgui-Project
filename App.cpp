/**
 * @file App.cpp
 * @brief Implementação de App — ciclo de vida completo da aplicação.
 *
 * DEFINIÇÃO DO PONTEIRO GLOBAL
 * -----------------------------
 * A linha:
 *   App* g_App = nullptr;
 *
 * É a DEFINIÇÃO real da variável. App.hpp apenas a DECLARA com extern.
 * O linker resolve a referência de qualquer .obj que inclua App.hpp
 * para este símbolo aqui.
 *
 * FLUXO DE VIDA DO g_App
 *   App::App()   → g_App = this     (atribui)
 *   App::Close() → g_App = nullptr  (anula, previne use-after-free)
 *
 * DIVISÃO run() / AllocGlobals()
 * --------------------------------
 * run()          → SDL + janela + Memory::AllocAll(window)
 * AllocGlobals() → aliases dos ponteiros Memory + AppSettings + MenuBar + Images
 *
 * Memory::AllocAll() aloca os recursos (Vulkan, ImGui, Console, etc.).
 * AllocGlobals() lê esses recursos via Memory::Get()->Get*() e os copia
 * para os membros de App (g_Vulkan, g_ImGui, g_Console, g_Style) — que são
 * aliases SEM posse; a posse continua no Memory singleton.
 *
 * Por que aliases e não Memory::Get()->Get*() direto no loop?
 *   → Evita uma chamada de função extra por acesso no hot-path do frame.
 *   → Mantém a sintaxe curta: g_Vulkan->X em vez de Memory::Get()->GetVulkan()->X.
 *
 * PERSISTÊNCIA DE ESCALA DE FONTE
 * --------------------------------
 * io.FontGlobalScale é aplicado em AllocGlobals() logo após LoadConfig()
 * para que o PRIMEIRO frame já renderize no tamanho salvo — sem "piscar".
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

#include <implot3d.h>
	#include <implot3d_internal.h>
    #include<implot3d_demo.cpp>

// =============================================================================
// Definição do ponteiro global — uma única vez em todo o projeto
// =============================================================================

/**
 * @brief Instância global de App — única no processo.
 *
 * Declarada como "extern App* g_App" em App.hpp.
 * Qualquer .cpp que inclua App.hpp e acesse g_App resolve para este símbolo.
 */
App* g_App = nullptr;

// =============================================================================
// Construtor
// =============================================================================

/**
 * @brief Inicializa membros com null/false e expõe a instância via g_App.
 *
 * g_App = this é atribuído ANTES de qualquer código que possa usar g_App,
 * garantindo que MenuBar e outros possam acessar a instância desde o início.
 */
App::App()
    : started(false)
    , bViewportDocking(false)
    , g_Done(false)
    , g_ShowDemo(true)
    , g_ShowStyleEd(false)
    , g_IsFullscreen(false)
    , g_grafico(false)
    , g_window_opacity(1.0f)   // 1.0 = totalmente opaco
    , g_color_ptr(nullptr)
    , g_io(nullptr)
    , g_Vulkan(nullptr)        // alias — preenchido em AllocGlobals()
    , g_ImGui(nullptr)         // alias — preenchido em AllocGlobals()
    , g_Console(nullptr)       // alias — preenchido em AllocGlobals()
    , g_Style(nullptr)         // alias — preenchido em AllocGlobals()
    , g_MenuBar(nullptr)       // posse de App — alocado em AllocGlobals()
    , g_Settings(nullptr)      // posse de App — alocado em AllocGlobals()
    , g_Window(nullptr)        // preenchido em run() após SDL_CreateWindow
    , m_ConfigFile("settings.json")
{
    // Expõe esta instância globalmente para que MenuBar.cpp e outros
    // possam acessar os membros públicos via g_App->membro
	
    
}

void App::Startup() {
g_App = Memory::Get()->GetApp();
    InitRenderDoc();
	started = true;
}

// =============================================================================
// run() — ponto de entrada: SDL + janela + AllocAll + AllocGlobals + loop
// =============================================================================

/**
 * @brief Inicializa SDL, cria a janela, aloca recursos e roda o loop principal.
 *
 * DIVISÃO DE RESPONSABILIDADES
 * -----------------------------
 *  run()          → SDL_Init, SDL_CreateWindow, Memory::AllocAll, AllocGlobals,
 *                   SDL_ShowWindow, RegisterCommands, MainLoop
 *  AllocGlobals() → aliases (g_Vulkan…g_Style), AppSettings, MenuBar, AllocImages
 *  wWinMain       → Memory::DestroyAll, Memory::Shutdown, SDL_DestroyWindow, SDL_Quit
 *
 * POR QUE SDL_DestroyWindow FICA EM wWinMain E NÃO AQUI?
 * --------------------------------------------------------
 * Memory::DestroyAll() chama VulkanContext::CleanupWindow() que usa g_Window
 * para destruir a VkSurfaceKHR antes da VkInstance.
 * Se SDL_DestroyWindow fosse chamado aqui, DestroyAll() em wWinMain receberia
 * um handle inválido → crash/UB. Por isso a janela sobrevive até depois de
 * DestroyAll() retornar.
 */
MyResult App::run() {
	if(!started)
        return MR_MSGBOX_ERR_END_LOC("App::Startup() must be called before run().");
    // ---- 1. Inicializa o SDL -----------------------------------------------

    // SDL_INIT_VIDEO:   janela + teclado + mouse
    // SDL_INIT_GAMEPAD: controles (requerido pelo backend ImGui SDL3)
    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        return MR_MSGBOX_ERR_END_LOC(
            "Failed to initialize SDL: " + StrToWStr(SDL_GetError()));

    // ---- 2. Escala DPI do monitor primário --------------------------------
    // Usada APENAS para o tamanho inicial da janela.
    // AllocGlobals() detecta o monitor REAL da janela após AllocAll().
    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

    // ---- 3. Resolução do desktop ------------------------------------------
    // Garante um tamanho inicial explícito — alguns drivers criam a janela
    // com tamanho zero se só SDL_WINDOW_MAXIMIZED for passado.
    int desktop_w = 0;
    int desktop_h = 0;
    if(!MR_IS_OK(GetDesktopResolution(desktop_w, desktop_h)) ||
        desktop_w <= 0 || desktop_h <= 0) {
        SDL_Quit();
        return MR_MSGBOX_ERR_END_LOC("Failed to get desktop resolution.");
    }

    // ---- 4. Cria a janela SDL ----------------------------------------------

    // SDL_WINDOW_VULKAN            → habilita VkSurfaceKHR via SDL
    // SDL_WINDOW_RESIZABLE         → usuário pode redimensionar
    // SDL_WINDOW_HIDDEN            → oculta até SDL_ShowWindow()
    // SDL_WINDOW_HIGH_PIXEL_DENSITY → pixels físicos em monitores HiDPI
    // SDL_WINDOW_MAXIMIZED         → maximizada ao ser exibida
    constexpr SDL_WindowFlags flags =
        SDL_WINDOW_VULKAN             |
        SDL_WINDOW_RESIZABLE          |
        SDL_WINDOW_HIDDEN             |
        SDL_WINDOW_HIGH_PIXEL_DENSITY |
        SDL_WINDOW_MAXIMIZED;

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

    // ---- 5. Aloca Vulkan, ImGui, fontes, console, style via Memory --------
    // AllocAll() detecta o monitor real da janela via SDL_GetDisplayForWindow,
    // cria o swapchain, inicializa ImGui e carrega fontes.
    // Os recursos ficam no Memory — AllocGlobals() extrai os ponteiros.
    if(!MR_IS_OK(Memory::Get()->AllocAll(g_Window)))
        return MR_MSGBOX_ERR_END_LOC("Memory::AllocAll falhou.");

    // ---- 6. Atribui aliases + AppSettings + MenuBar + Images --------------
    // DEVE ser chamado APÓS AllocAll() — os ponteiros do Memory só são
    // válidos depois que AllocAll() retorna com sucesso.
    if(!MR_IS_OK(AllocGlobals()))
        return MR_MSGBOX_ERR_END_LOC("AllocGlobals falhou.");

    // ---- 7. Exibe a janela ------------------------------------------------
    // Após swapchain e ImGui prontos — evita frame em branco na abertura.
    SDL_SetWindowPosition(g_Window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(g_Window); // aparece já maximizada

    // ---- 8. Registra comandos do console ----------------------------------
    // Console alocado por AllocAll() e agora acessível via g_Console (alias).
    if(!MR_IS_OK(RegisterCommands()))
        return MR_MSGBOX_ERR_END_LOC("RegisterCommands falhou.");

    // ---- 9. Loop principal ------------------------------------------------
    const MyResult result = MainLoop();

    // ---- 10. Retorna para wWinMain ----------------------------------------
    // wWinMain chama na sequência:
    //   Memory::Get()->DestroyAll() → usa g_Window para destruir a surface
    //   Memory::Shutdown()          → delete do singleton
    //   SDL_DestroyWindow(g_Window) → janela destruída APÓS o Vulkan
    //   SDL_Quit()
    return result;
}

// =============================================================================
// AllocGlobals — aliases + AppSettings + MenuBar + Images
// =============================================================================

/**
 * @brief Extrai os ponteiros do Memory e inicializa os subsistemas de App.
 *
 * DEVE ser chamado APÓS Memory::AllocAll() — os Get*() retornam nullptr
 * antes disso, o que causaria o assert/crash que você estava vendo.
 *
 * ORDEM OBRIGATÓRIA DENTRO DESTA FUNÇÃO:
 *  1. Aliases (g_Vulkan … g_Style) — lidos do Memory
 *  2. Validação: assert que nenhum alias é nullptr
 *  3. new AppSettings + LoadConfig()
 *  4. io.FontGlobalScale = font_scale salvo  ← ANTES do primeiro NewFrame()
 *  5. new MenuBar
 *  6. AllocImages()
 *
 * POR QUE SEM PARÂMETROS?
 * ------------------------
 * Os parâmetros antigos (extensions, w, h, scale) eram passados para
 * VulkanContext::Initialize() e SetupWindow() — que agora vivem dentro de
 * Memory::AllocVulkan(). AllocGlobals() apenas lê o resultado já pronto.
 */
MyResult App::AllocGlobals() {

    // ---- 1. Aliases — ponteiros SEM posse, dono é o Memory ----------------
    // Memory::Get()->Get*() retorna nullptr se o recurso não foi alocado.
    // Isso aconteceria se AllocAll() não tivesse sido chamado — o assert
    // abaixo detecta isso em builds Debug.

    g_Vulkan  = Memory::Get()->GetVulkan();        // VkDevice, queues, swapchain
    g_ImGui   = Memory::Get()->GetImGui();          // ImGuiContext + backends
    g_Console = Memory::Get()->GetConsole();        // console ImGui wide-char
    g_Style   = Memory::Get()->GetStyleEditor();    // editor de estilo + JSON
    g_Settings = Memory::Get()->GetAppSettings(); // valores default do construtor
    LoadConfig();                   // sobrescreve com dados do disco, se existirem
    g_MenuBar =  Memory::Get()->GetMenuBar();
    // ---- 2. Validação dos aliases -----------------------------------------
    // Se qualquer ponteiro for nulo, AllocAll() falhou silenciosamente ou
    // AllocGlobals() foi chamado antes de AllocAll() — ambos são bugs.

    if(!g_Vulkan)
        return MR_MSGBOX_ERR_END_LOC(
            "g_Vulkan nulo após AllocAll. "
            "Certifique-se de que Memory::AllocAll() foi chamado antes de AllocGlobals().");

    if(!g_ImGui)
        return MR_MSGBOX_ERR_END_LOC(
            "g_ImGui nulo após AllocAll. "
            "AllocVulkan() deve preceder AllocImGui() — verifique a ordem em Memory::AllocAll().");

    if(!g_Console)
        return MR_MSGBOX_ERR_END_LOC(
            "g_Console nulo após AllocAll. "
            "AllocConsole() requer GImGui != nullptr — verifique a ordem em Memory::AllocAll().");

    if(!g_Style)
        return MR_MSGBOX_ERR_END_LOC(
            "g_Style nulo após AllocAll. "
            "AllocStyleEditor() falhou — verifique Memory::AllocAll().");

    if(!g_Settings)
        return MR_MSGBOX_ERR_END_LOC(
            "g_Settings nulo após AllocAll. "
            "AllocAppSettings() falhou — verifique Memory::AllocAll().");
     if(!g_MenuBar)
        return MR_MSGBOX_ERR_END_LOC(
            "g_MenuBar nulo após AllocAll. "
			"AllocMenuBar() falhou — verifique Memory::AllocAll().");

    // ---- 3. AppSettings — posse de App, delete em Close() -----------------

    

    // ---- 4. Restaura escala de fonte da sessão anterior -------------------
    // ImGui lê FontScaleMain no início de cada NewFrame(). Aplicar aqui,
    // ANTES do primeiro NewFrame(), garante que o primeiro frame já usa o
    // tamanho salvo — sem "piscar" para 1.0 e depois para o valor real.

    ImGui::GetStyle().FontScaleMain = g_Settings->font_scale;

    // ---- 5. MenuBar — posse de App, delete em Close() ---------------------

     // MenuBar::MenuBar() não usa recursos externos

    // ---- 6. Imagens -------------------------------------------------------
    // AllocImages() usa g_Vulkan->GetDevice() e o command pool criado por
    // SetupWindow() dentro de Memory::AllocVulkan() — ambos já existem aqui.

    if(!MR_IS_OK(AllocImages()))
        return MR_CLS_WARN_LOC("AllocImages falhou — continuando sem imagens.");

    return MyResult::ok;
}

// =============================================================================
// AllocImages
// =============================================================================

/**
 * @brief Carrega todas as imagens da aplicação (g_Logo, g_IconSettings, etc.).
 *
 * Falhas não são fatais — IsLoaded() retorna false e os helpers Draw() são
 * no-ops. Mude para MR_MSGBOX_ERR_END_LOC se uma imagem for obrigatória.
 */
MyResult App::AllocImages() {
    if(!g_Logo.Load("assets/logo.png"))
        g_Console->AddLog(L"[Aviso] Logo nao carregou (assets/logo.png)");
    else
        g_Console->AddLog(L"[OK] Logo carregada (%dx%d)",
            g_Logo.GetWidth(), g_Logo.GetHeight());

    if(!g_IconSettings.Load("assets/icon_settings.png"))
        g_Console->AddLog(L"[Aviso] IconSettings nao carregou (assets/icon_settings.png)");
    else
        g_Console->AddLog(L"[OK] IconSettings carregado (%dx%d)",
            g_IconSettings.GetWidth(), g_IconSettings.GetHeight());

    return MyResult::ok; // imagens são opcionais — nunca retorna erro aqui
}

// =============================================================================
// Close
// =============================================================================

/**
 * @brief Executa o ciclo completo de destruição na ordem obrigatória.
 *
 * ORDEM (desviar causa crash ou VK_ERROR_DEVICE_LOST):
 *  1. vkDeviceWaitIdle      → GPU termina todos os frames em voo
 *  2. Imagens Unload()      → usa VkDevice — antes de DestroyAll()
 *  3. delete g_MenuBar      → sem dependência de GPU
 *  4. delete g_Settings     → idem
 *  5. Memory::DestroyAll()  → StyleEditor → Console → FontManager → ImGui → Vulkan
 *  6. Anula aliases         → previne acesso a memória liberada
 *  7. SDL_DestroyWindow     → após Vulkan (surface já destruída pelo Memory)
 *  8. SDL_Quit
 *  9. g_App = nullptr       → previne use-after-free
 */
void App::Close() {
    // ---- 1. GPU idle -------------------------------------------------------
    if(g_Vulkan && g_Vulkan->GetDevice() != VK_NULL_HANDLE) {
        const VkResult err = vkDeviceWaitIdle(g_Vulkan->GetDevice());
        VulkanContext::CheckVkResult(err);
    }

    // ---- 2. Imagens — ANTES de DestroyAll ----------------------------------
    g_Logo.Unload();
    g_IconSettings.Unload();

    // ---- 3 e 4. Objetos com posse em App -----------------------------------
     g_MenuBar  = nullptr;
     g_Settings = nullptr;

    // ---- 5. Memory::DestroyAll — ordem inversa da alocação -----------------
    Memory::Get()->DestroyAll();

    // ---- 6. Anula aliases — apontam para memória já liberada ---------------
    g_Style   = nullptr;
    g_Console = nullptr;
    g_ImGui   = nullptr;
    g_Vulkan  = nullptr;

    // ---- 7 e 8. SDL --------------------------------------------------------
    if(g_Window) {
        SDL_DestroyWindow(g_Window); // surface já destruída pelo Memory
        g_Window = nullptr;
    }
    SDL_Quit();

    // ---- 9. Anula ponteiro global ------------------------------------------
    g_App = nullptr;

    ImPlot::DestroyContext();
}

// =============================================================================
// Persistência
// =============================================================================

/**
 * @brief Serializa *g_Settings para m_ConfigFile via reflect-cpp.
 */
void App::SaveConfig() {
    if(g_Settings)
        rfl::json::save(m_ConfigFile, *g_Settings);
}

/**
 * @brief Carrega m_ConfigFile em *g_Settings.
 * Se o arquivo não existir ou estiver corrompido, mantém os defaults.
 */
void App::LoadConfig() {
    if(!g_Settings)
        return;
    auto r = rfl::json::load<AppSettings>(m_ConfigFile);
    if(r)
        *g_Settings = *r;
}

// =============================================================================
// GetDesktopResolution
// =============================================================================

/**
 * @brief Obtém a resolução do desktop via Win32.
 *
 * GetDesktopWindow() + GetWindowRect() retorna as dimensões do monitor
 * primário em pixels físicos — independente de escala DPI do Windows.
 */
MyResult App::GetDesktopResolution(int& horizontal, int& vertical) {
    RECT desktop;
    const HWND hDesktop = GetDesktopWindow(); // handle para o desktop do Windows
    GetWindowRect(hDesktop, &desktop);         // preenche desktop.right e .bottom
    horizontal = desktop.right;                // largura em pixels
    vertical   = desktop.bottom;               // altura em pixels
    return MR_OK;
}

// =============================================================================
// RegisterCommands
// =============================================================================
/**
 *
 * Chamado após AllocGlobals() — g_Console e g_Vulkan já estão válidos.
 */
MyResult App::RegisterCommands() {
    if(!g_Console)
        return MR_MSGBOX_ERR_END_LOC("g_Console nulo em RegisterCommands.");
    if(!g_Vulkan)
        return MR_MSGBOX_ERR_END_LOC("g_Vulkan nulo em RegisterCommands.");

    auto cmd_quit = [this]() {
        g_Console->AddLog(L"Saindo...");
        g_Done = true;
    };
    g_Console->RegisterBuiltIn(L"EXIT", cmd_quit);
    g_Console->RegisterCommand(L"QUIT", cmd_quit);

    g_Console->RegisterCommand(L"BREAK", L"USAR SOMENTE EM DEBUG",
        [this]() { 
        if(IsDebuggerPresent()) {
            __debugbreak();
        } else {
			g_Console->AddLog(L"[AVISO] BREAK chamado, mas nenhum depurador detectado. Ignorando.");
        }
    }  );

    g_Console->RegisterCommand(L"SPECS", L"Exibe as especificacoes de hardware do PC.",
        [this]() {
            SystemInfo::Collect(g_Vulkan, L"Vulkan").PrintToConsole(g_Console);
        });

    g_Console->RegisterCommand(L"VSYNC", L"Liga ou desliga o VSync.",
        [this]() {
            const bool novo = !g_Vulkan->GetVSync();
            g_Vulkan->SetVSync(novo);
            g_Console->AddLog(novo ? L"VSync ON" : L"VSync OFF");
        });

    g_Console->RegisterCommand(L"NOVIEWPORTS",
        L"Desabilita os viewports flutuantes do ImGui.",
        [this]() {
            this->DisableViewportDocking();
            if(bViewportDocking) bViewportDocking = !bViewportDocking;
        });

    g_Console->RegisterCommand(L"forceexit",
        L"FORÇA o encerramento imediato do programa (sem cleanup).",
        []() {
            g_App->g_Console->AddLog(L"FORCE EXIT: Encerrando imediatamente...");
            std::exit(0);
        });

    g_Console->RegisterCommand(L"implot", L"Mostra funcionalidades do ImPlot",
        []() { ImPlot::ShowDemoWindow(); });

         g_Console->RegisterCommand(L"implot3d", L"Mostra funcionalidades do ImPlot3d",
        []() { });

    g_Console->RegisterCommand(L"Abort",
        L"Aborta o programa com falha (útil para testar handlers de crash)",
        []() { std::abort(); });

    g_Console->RegisterCommand(L"System Pause",
        L"Pausa o sistema atraves do Windows (útil para depuração)",
        []() { std::system("pause"); });

    g_Console->RegisterCommand(L"Cpp Pause",
        L"Pausa o sistema atraves do C++ (útil para depuração)",
        []() { std::cin.get(); });

    return MyResult::ok;
}

// =============================================================================
// MainLoop
// =============================================================================

/**
 * @brief Loop SDL + ImGui + Vulkan — roda até g_Done == true.
 */
MyResult App::MainLoop() {
    g_io = &g_ImGui->GetIO();

    const bool font_rt = (g_io->Fonts && g_io->Fonts->FontLoader);
    g_Console->AddLog(font_rt
        ? L"\n[RT] FontLoader ativo\n"
        : L"[RT] FontLoader: stb_truetype (padrao)\n");

    float* color_ptr = g_Settings->clear_color.data();
    g_color_ptr      = color_ptr;
    g_window_opacity = 1.0f;

    static ScrollingBuffer sdata = ScrollingBuffer::ScrollingBuffer();
    float t = 0;

    while(!g_Done) {

        // ---- 1. Eventos SDL -----------------------------------------------
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if(event.type == SDL_EVENT_QUIT)
                g_Done = true;

            if(event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
               event.window.windowID == SDL_GetWindowID(g_Window))
                g_Done = true;

            if(event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11) {
                g_IsFullscreen = !g_IsFullscreen;
                SDL_SetWindowFullscreen(g_Window, g_IsFullscreen);
            }
        }

        if(SDL_GetWindowFlags(g_Window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        // ---- 2. Rebuild da swapchain --------------------------------------
        int fb_w, fb_h;
        SDL_GetWindowSize(g_Window, &fb_w, &fb_h);
        ImGui_ImplVulkanH_Window* wd = g_Vulkan->GetMainWindowData();

        if(fb_w > 0 && fb_h > 0 &&
           (g_Vulkan->NeedsSwapChainRebuild() ||
            wd->Width != fb_w || wd->Height != fb_h))
            g_Vulkan->RebuildSwapChain(fb_w, fb_h);

        // ---- 3. Frame ImGui -----------------------------------------------
        Windows();

        // ---- 4. Render Vulkan ---------------------------------------------
        g_ImGui->Render();

        ImDrawData* draw_data = ImGui::GetDrawData();
        const bool minimized = (draw_data->DisplaySize.x <= 0.0f ||
                                 draw_data->DisplaySize.y <= 0.0f);

        wd->ClearValue.color.float32[0] = color_ptr[0] * color_ptr[3];
        wd->ClearValue.color.float32[1] = color_ptr[1] * color_ptr[3];
        wd->ClearValue.color.float32[2] = color_ptr[2] * color_ptr[3];
        wd->ClearValue.color.float32[3] = color_ptr[3];

        if(!minimized)   g_Vulkan->FrameRender(draw_data);
        if(g_ImGui->WantsViewports()) g_ImGui->RenderPlatformWindows();
        if(!minimized)   g_Vulkan->FramePresent();
    }

    return MyResult::ok;
}

// =============================================================================
// Windows — conteúdo ImGui do frame
// =============================================================================

/**
 * @brief Renderiza todas as janelas ImGui do frame atual.
 *
 * Chamado por MainLoop() a cada frame, após o rebuild do swapchain e
 * antes do Render(). g_ImGui->NewFrame() é chamado aqui.
 */
MyResult App::Windows() {
    WindowsConsole::poll_hotkey();

    g_ImGui->NewFrame();

    g_MenuBar->Draw();

    if(g_ShowDemo)
        ImGui::ShowDemoWindow(&g_ShowDemo);

    {
        ImGui::Begin("Window Controls");

        const float btn_w   = 60.0f;
        const float padding = ImGui::GetStyle().WindowPadding.x;
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - btn_w - padding);
        if(ImGui::Button("❌", ImVec2(btn_w, 0)))
            g_Done = true;
        if(ImGui::IsItemHovered())
            ImGui::SetTooltip("Fechar o programa");

        ImGui::SameLine();
        ImGui::Text("Global Alpha Blending");

        if(ImGui::SliderFloat("Window Opacity", &g_window_opacity, 0.1f, 1.0f))
            SDL_SetWindowOpacity(g_Window, g_window_opacity);

        if(ImGui::Button("Reset to Opaque")) {
            g_window_opacity = 1.0f;
            SDL_SetWindowOpacity(g_Window, 1.0f);
        }

        ImGui::Separator();

        if(ImGui::ColorEdit3("Background Color", g_color_ptr))
            SaveConfig();

        // ---- Escala de fonte — persistida entre sessões -------------------
        // io.FontGlobalScale multiplica o tamanho de todas as fontes em
        // runtime sem reconstruir o atlas. Efeito imediato no próximo NewFrame().
        ImGui::Separator();
        if(ImGui::SliderFloat("Font Scale", &g_Settings->font_scale,
            0.5f, 3.0f, "%.2f")) {
            ImGui::GetStyle().FontScaleMain = g_Settings->font_scale;
            SaveConfig();
        }
        if(ImGui::Button("Reset Font")) {
            g_Settings->font_scale       = 1.0f;
            ImGui::GetStyle().FontScaleMain = 1.0f;
            SaveConfig();
        }

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
            1000.0f / g_io->Framerate, g_io->Framerate);

        ImGui::Separator();
        WindowsConsole::render_imgui_button();

        if(ImGui::Checkbox("Show ImGui Console", &g_Settings->show_console))
            SaveConfig();

        ImGui::Checkbox("Demo Window",    &g_ShowDemo);
        ImGui::Checkbox("Style Editor",   &g_ShowStyleEd);

        if(ImGui::Button("Log Test msg"))
            g_Console->AddLog(L"Botao pressionado no frame %d \U0001F680",
                ImGui::GetFrameCount());

        if(g_Logo.IsLoaded()) {
            ImGui::Separator();
            g_Logo.DrawCentered(180.0f, 60.0f);
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("Logo %dx%d", g_Logo.GetWidth(), g_Logo.GetHeight());
        }

        if(g_IconSettings.IsLoaded()) {
            if(g_IconSettings.DrawButton("##btn_settings", {16.0f, 16.0f}))
                g_ShowStyleEd = !g_ShowStyleEd;
            ImGui::SameLine();
            ImGui::Text("Configuracoes");
        }

        ImGui::Checkbox("grafico",    &g_grafico);
        ImGui::Checkbox("Viewports",  &bViewportDocking);
		ImGui::Checkbox("ImPlot3D Demo RealtimePlots", &bImPlot3d_DemoRealtimePlots);
		ImGui::Checkbox("ImPlot3D Demo QuadPlots", &bImPlot3d_DemoQuadPlots);
		ImGui::Checkbox("ImPlot3D Demo TickLabels", &bImPlot3d_DemoTickLabels);
        ImGui::End();
    }

    if(g_Settings->show_console) {
        g_Console->Draw(L"Debug Console", &g_Settings->show_console);
        if(!g_Settings->show_console) SaveConfig();
    }

    if(g_ShowStyleEd)
        g_Style->Show(nullptr, &g_ShowStyleEd);

    if(bViewportDocking)
        bViewportDocking = !bViewportDocking;

    if(g_grafico) {
        ImGui::Begin("Grafico de Exemplo", &g_grafico);
        if(ImPlot::BeginPlot("Gráfico de Exemplo")) {
            ImPlot::SetupAxes("Tempo", "Valor");
            static float x_data[10] = {0,1,2,3,4,5,6,7,8,9};
            static float y_data[10] = {1,3,2,4,5,3,6,5,7,8};
            ImPlot::PlotLine("Sinal A", x_data, y_data, 10);
            ImPlot::PlotLineG("Cosseno",
                [](int idx, void*) {
                    const float x = idx * 0.1f;
                    return ImPlotPoint(x, cosf(x));
                }, nullptr, 100);
            ImPlot::EndPlot();
            ImGui::End();
        }
    }

    if(bImPlot3d_DemoRealtimePlots){
		ImGui::Begin("ImPlot3D Demo RealtimePlots", &bImPlot3d_DemoRealtimePlots);
    ImPlot3D::CreateContext();
    ImPlot3D::DemoRealtimePlots();
    ImPlot3D::DestroyContext();
    ImGui::End();
    }
    
    if(bImPlot3d_DemoQuadPlots){
		ImGui::Begin("ImPlot3D Demo QuadPlots", &bImPlot3d_DemoQuadPlots);
    ImPlot3D::CreateContext();
    ImPlot3D::DemoQuadPlots();
    ImPlot3D::DestroyContext();
    ImGui::End();
    }

    if(bImPlot3d_DemoTickLabels){
		ImGui::Begin("ImPlot3D Demo TickLabels", &bImPlot3d_DemoTickLabels);
    ImPlot3D::CreateContext();
    ImPlot3D::DemoTickLabels();
    ImPlot3D::DestroyContext();
    ImGui::End();
    }

    return MR_OK;
}

// =============================================================================
// DisableViewportDocking
// =============================================================================

void App::DisableViewportDocking() {
    g_io->ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

    ImGuiContext* ctx       = ImGui::GetCurrentContext();
    ImVec2 main_pos         = ImGui::GetMainViewport()->Pos;
    ImVec2 main_size        = ImGui::GetMainViewport()->Size;

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
    MaxSize = max_size;
    Data.reserve(MaxSize);
}

void ScrollingBuffer::AddPoint(float x, float y) {
    if(Data.size() < MaxSize)
        Data.push_back(ImVec2(x, y));
    else {
        Data[Offset] = ImVec2(x, y);
        Offset = (Offset + 1) % MaxSize;
    }
}

void ScrollingBuffer::Erase() {
    if(Data.size() > 0) {
        Data.shrink(0);
        Offset = 0;
    }
}