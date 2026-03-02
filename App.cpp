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
 * PERSISTÊNCIA DE ESCALA DE FONTE
 * --------------------------------
 * io.FontGlobalScale é um multiplicador runtime aplicado a todas as fontes
 * sem reconstruir o atlas de texturas. O valor é guardado em
 * g_Settings->font_scale (AppSettings) e persistido em settings.json.
 *
 * Pontos onde a escala participa do ciclo de vida:
 *
 *  1. AllocGlobals() — após LoadConfig():
 *       io.FontGlobalScale = g_Settings->font_scale;
 *     → garante que o PRIMEIRO frame já renderiza no tamanho correto,
 *       sem nenhum "piscar" no tamanho padrão 1.0.
 *
 *  2. MainLoop() — slider "Font Scale":
 *       io.FontGlobalScale = g_Settings->font_scale;
 *       SaveConfig();
 *     → aplica imediatamente ao mover o slider e salva em disco.
 *
 *  3. SaveConfig() / LoadConfig() — rfl::json serializa font_scale
 *     como campo normal de AppSettings, igual a clear_color e show_console.
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

// =============================================================================
// Definição do ponteiro global — uma única vez em todo o projeto
// =============================================================================

/**
 * @brief Instância global de App — única no processo.
 *
 * Declarada como "extern App* g_App" em App.hpp.
 * Qualquer .cpp que inclua App.hpp e acesse g_App resolve para este símbolo.
 */
App *g_App = nullptr;

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
    : bViewportDocking(false), g_Done(false), g_ShowDemo(true),
      g_ShowStyleEd(false), g_IsFullscreen(false), g_grafico(false),
      g_window_opacity(false), g_color_ptr(nullptr), g_io(nullptr),
      g_Vulkan(nullptr), g_ImGui(nullptr), g_Console(nullptr), g_Style(nullptr),
      g_MenuBar(nullptr), g_Settings(nullptr), g_Window(nullptr),
      m_ConfigFile("settings.json") {
  // Expõe esta instância globalmente para que MenuBar.cpp e outros
  // possam acessar os membros públicos via g_App->membro
  g_App = this;
}

MyResult App::Windows() {

  // ----------------------------------------------------------------
  // 3. Hotkey do console externo
  // ----------------------------------------------------------------

  WindowsConsole::poll_hotkey();

  // ----------------------------------------------------------------
  // 4. Frame ImGui
  // ----------------------------------------------------------------

  g_ImGui->NewFrame();

  g_MenuBar->Draw(); // acessa g_App->g_Done, g_App->g_Settings, etc.

  if (g_ShowDemo)
    ImGui::ShowDemoWindow(&g_ShowDemo);

  {
    ImGui::Begin("Window Controls");

    float btn_w = 60.0f;
    float padding = ImGui::GetStyle().WindowPadding.x;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - btn_w - padding);
    if (ImGui::Button("❌", ImVec2(btn_w, 0)))
      g_Done = true;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Fechar o programa");

    ImGui::SameLine();
    ImGui::Text("Global Alpha Blending");

    if (ImGui::SliderFloat("Window Opacity", &g_window_opacity, 0.1f, 1.0f))
      SDL_SetWindowOpacity(g_Window, g_window_opacity);

    if (ImGui::Button("Reset to Opaque")) {
      g_window_opacity = 1.0f;
      SDL_SetWindowOpacity(g_Window, 1.0f);
    }

    ImGui::Separator();

    if (ImGui::ColorEdit3("Background Color", g_color_ptr))
      SaveConfig();

    // ---- [FONTE] Escala de fonte — persistida entre sessões
    // ---------------- io.FontGlobalScale multiplica o tamanho de todas as
    // fontes em runtime. Não requer reconstrução do atlas — efeito imediato
    // no próximo NewFrame().
    //
    // Ciclo de persistência completo:
    //   Slider alterado → g_Settings->font_scale atualizado
    //                   → io.FontGlobalScale aplicado (efeito visual
    //                   imediato) → SaveConfig() salva em settings.json
    //   Próximo início  → LoadConfig() lê font_scale
    //                   → AllocGlobals() aplica io.FontGlobalScale =
    //                   font_scale → primeiro frame já renderiza no tamanho
    //                   correto
    ImGui::Separator();
    if (ImGui::SliderFloat(
            "Font Scale",
            &g_Settings->font_scale, // lido e escrito diretamente no settings
            0.5f,    // mínimo: metade do tamanho original do atlas
            3.0f,    // máximo: três vezes o tamanho original
            "%.2f")) // formato numérico exibido no slider
    {
      // Aplica imediatamente — visível já no próximo NewFrame()
      ImGui::GetStyle().FontScaleMain = g_Settings->font_scale;
      SaveConfig(); // persiste em settings.json para a próxima sessão
    }

    // Botão de reset — volta para 1.0 e salva
    if (ImGui::Button("Reset Font")) {
      g_Settings->font_scale = 1.0f;          // restaura valor padrão
      ImGui::GetStyle().FontScaleMain = 1.0f; // aplica imediatamente
      SaveConfig();                           // persiste o reset
    }

    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                1000.0f / g_io->Framerate, g_io->Framerate);

    ImGui::Separator();
    WindowsConsole::render_imgui_button();

    if (ImGui::Checkbox("Show ImGui Console", &g_Settings->show_console))
      SaveConfig();

    ImGui::Checkbox("Demo Window", &g_ShowDemo);

    ImGui::Checkbox("Style Editor", &g_ShowStyleEd);

    if (ImGui::Button("Log Test msg"))
      g_Console->AddLog(L"Botao pressg_ionado no frame %d \U0001F680",
                        ImGui::GetFrameCount());

    // ---- Exemplo de uso das imagens ---------------------------------
    // g_Logo.IsLoaded() evita crash se o arquivo não foi encontrado.
    // Draw, DrawFitted, DrawCentered e DrawButton são no-ops se false.

    if (g_Logo.IsLoaded()) {
      ImGui::Separator();

      // DrawCentered: centraliza horizontalmente na janela
      g_Logo.DrawCentered(180.0f, 60.0f);

      // Tooltip com dimensões originais ao passar o mouse
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Logo %dx%d", g_Logo.GetWidth(), g_Logo.GetHeight());
    }

    if (g_IconSettings.IsLoaded()) {
      // DrawButton: ícone clicável (16×16) ao lado de um label
      if (g_IconSettings.DrawButton("##btn_settings", {16.0f, 16.0f}))
        g_ShowStyleEd = !g_ShowStyleEd; // toggle Style Editor

      ImGui::SameLine();
      ImGui::Text("Configuracoes");
    }

    ImGui::Checkbox("grafico", &g_grafico);

    ImGui::Checkbox("Viewports", &bViewportDocking);

    ImGui::End();
  }

  if (g_Settings->show_console) {
    g_Console->Draw(L"Debug Console", &g_Settings->show_console);
    if (!g_Settings->show_console)
      SaveConfig();
  }

  if (g_ShowStyleEd)
    g_Style->Show(nullptr, &g_ShowStyleEd);

  if (bViewportDocking)
    bViewportDocking = !bViewportDocking;

  // grafico de exemplo do ImPlot

  if (g_grafico) {

    ImGui::Begin("Grafico de Exemplo", &g_grafico);

    if (ImPlot::BeginPlot("Gráfico de Exemplo")) {

      // Configura os eixos (opcional)
      ImPlot::SetupAxes("Tempo", "Valor");

      // Dados estáticos para teste
      static float x_data[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
      static float y_data[10] = {1, 3, 2, 4, 5, 3, 6, 5, 7, 8};

      // Desenha a linha
      ImPlot::PlotLine("Sinal A", x_data, y_data, 10);

      // Você também pode usar funções matemáticas em tempo real
      ImPlot::PlotLineG(
          "Cosseno",
          [](int idx, void *data) {
            float x = idx * 0.1f;
            return ImPlotPoint(x, cosf(x));
          },
          nullptr, 100);

      ImPlot::EndPlot();

      ImGui::End();
    }
  }

  return MR_OK;
}

// =============================================================================
// Persistência de configurações
// =============================================================================

/**
 * @brief Serializa *g_Settings para m_ConfigFile via reflect-cpp.
 *
 * Chamado quando o usuário altera qualquer configuração persistida:
 *   • cor de fundo (ColorEdit3)
 *   • show_console (Checkbox)
 *   • font_scale (SliderFloat)  ← adicionado para persistência de fonte
 *
 * reflect-cpp serializa todos os campos de AppSettings automaticamente,
 * incluindo font_scale, sem nenhuma linha extra aqui.
 */
void App::SaveConfig() {
  if (g_Settings)
    rfl::json::save(m_ConfigFile, *g_Settings); // reflect-cpp gera o JSON
}

/**
 * @brief Carrega m_ConfigFile em *g_Settings.
 * Se o arquivo não existir ou estiver corrompido, mantém os defaults.
 */
void App::LoadConfig() {
  if (!g_Settings)
    return;
  auto r = rfl::json::load<AppSettings>(m_ConfigFile);
  if (r)
    *g_Settings = *r; // sobrescreve apenas se o parse teve sucesso
}

// =============================================================================
// AllocGlobals
// =============================================================================

/**
 * @brief Inicializa todos os subsistemas e atribui os ponteiros de membro.
 *
 * ORDEM OBRIGATÓRIA (veja Memory.cpp para o detalhamento):
 *  1. Memory::AllocAll → Vulkan (+SetupWindow) → ImGui → Fonts → Console →
 * Style
 *  2. Atribui aliases g_Vulkan … g_Style (sem posse)
 *  3. new AppSettings → LoadConfig()
 *  4. [FONTE] io.FontGlobalScale = g_Settings->font_scale
 *       → restaura a escala da sessão anterior ANTES do primeiro NewFrame().
 *       → sem isso, o primeiro frame renderiza em escala 1.0 e "pisca" para
 *         o tamanho salvo apenas no segundo frame.
 *  5. new MenuBar
 *  6. AllocImages() — exige VkDevice + command pool prontos (criados nos passos
 *     1 e 2)
 *
 * @param extensions  Extensões Vulkan requeridas pelo SDL.
 * @param w           Largura inicial do swapchain em pixels.
 * @param h           Altura inicial em pixels.
 * @param scale       Escala DPI do display primário.
 */
MyResult App::AllocGlobals(const ImVector<const char *> &extensions, int w,
                           int h, float scale) {
  // ---- 1. Memory::AllocAll — todos os subsistemas na ordem correta --------

  if (!MR_IS_OK(Memory::Get()->AllocAll()))
    return MR_MSGBOX_ERR_END_LOC("Memory::AllocAll falhou.");

  // ---- 2. Aliases para os objetos geridos pelo Memory --------------------
  // Estes ponteiros NÃO têm posse — Memory::DestroyAll() cuida deles

  g_Vulkan = Memory::Get()->GetVulkan();
  g_ImGui = Memory::Get()->GetImGui();
  g_Console = Memory::Get()->GetConsole();
  g_Style = Memory::Get()->GetStyleEditor();

  if (!g_Vulkan || !g_ImGui || !g_Console || !g_Style)
    return MR_MSGBOX_ERR_END_LOC("Ponteiro de subsistema nulo apos AllocAll.");

  // ---- 3. AppSettings — posse de App, delete em Close() -----------------

  g_Settings = new AppSettings(); // valores default do construtor
  LoadConfig(); // sobrescreve com dados do disco, se existirem

  // ---- 4. [FONTE] Restaura a escala de fonte da sessão anterior ----------
  // io.FontGlobalScale é um float multiplicador aplicado a todas as fontes
  // em runtime — sem precisar reconstruir o atlas de texturas Vulkan.
  //
  // Por que aqui e não no início de MainLoop()?
  //   ImGui lê FontGlobalScale no início de cada NewFrame(). Se aplicarmos
  //   só na primeira iteração do loop, o primeiro NewFrame() já ocorreu com
  //   escala 1.0 — causando um "piscar" visível. Aplicar aqui, antes de
  //   qualquer NewFrame(), garante que o primeiro frame já usa o valor salvo.
  //
  // g_Settings->font_scale foi preenchido por LoadConfig() na linha acima,
  // portanto reflete o valor da sessão anterior (ou 1.0 se não há
  // settings.json).
  ImGui::GetStyle().FontScaleMain = g_Settings->font_scale;

  // ---- 5. MenuBar — posse de App, delete em Close() ----------------------
  // MenuBar::MenuBar() não usa recursos externos — pode ser construído aqui

  g_MenuBar = new MenuBar();

  // ---- 6. Imagens — exige VkDevice + pool do frame 0 já criados ----------
  // AllocImages usa g_Vulkan->GetMainWindowData()->Frames[0].CommandPool,
  // que é criado por SetupWindow() dentro de AllocVulkan() (passo 1).

  if (!MR_IS_OK(AllocImages()))
    return MR_CLS_WARN_LOC("AllocImages falhou — continuando sem imagens.");

  return MyResult::ok;
}

// =============================================================================
// AllocImages
// =============================================================================

/**
 * @brief Carrega todas as imagens da aplicação nos membros g_Logo,
 * g_IconSettings, etc.
 *
 * QUANDO CHAMAR: apenas após AllocGlobals() — o Image::Load() usa internamente:
 *   • g_App->g_Vulkan->GetDevice()                — criado em AllocVulkan()
 *   • g_App->g_Vulkan->GetMainWindowData()→Frames — criado em SetupWindow()
 *   • g_App->g_Vulkan->GetQueue()                 — criado em AllocVulkan()
 *
 * FALHAS NÃO SÃO FATAIS por padrão:
 *   • Se um arquivo de imagem não existir, o Load() retorna false e loga o
 * erro. • A aplicação continua — IsLoaded() retorna false e os helpers Draw()
 *     são no-ops.
 *   • Mude para MR_MSGBOX_ERR_END_LOC se uma imagem for obrigatória.
 *
 * PARA ADICIONAR NOVAS IMAGENS:
 *   1. Declare o membro "Image g_MinhaImagem;" em App.hpp (seção IMAGENS)
 *   2. Carregue aqui: g_MinhaImagem.Load("assets/minha.png")
 *   3. Descarregue em Close(): g_MinhaImagem.Unload()
 *   4. Use em MainLoop(): g_App->g_MinhaImagem.Draw(w, h)
 */
MyResult App::AllocImages() {
  // ---- Logo principal -----------------------------------------------------
  // Exibida na splash screen, About popup e janela principal.
  // Falha não é fatal — aplica apenas log de aviso.

  if (!g_Logo.Load("assets/logo.png"))
    g_Console->AddLog(L"[Aviso] Logo nao carregou (assets/logo.png)");
  else
    g_Console->AddLog(L"[OK] Logo carregada (%dx%d)", g_Logo.GetWidth(),
                      g_Logo.GetHeight());

  // ---- Ícone de configurações (16×16 ou 32×32) ----------------------------
  // Usado na barra de menu e toolbar.

  if (!g_IconSettings.Load("assets/icon_settings.png"))
    g_Console->AddLog(
        L"[Aviso] IconSettings nao carregou (assets/icon_settings.png)");
  else
    g_Console->AddLog(L"[OK] IconSettings carregado (%dx%d)",
                      g_IconSettings.GetWidth(), g_IconSettings.GetHeight());

  // Adicione mais imagens aqui seguindo o mesmo padrão:
  // if(!g_MeuIcone.Load("assets/meu_icone.png")) { ... }

  return MyResult::ok; // imagens são opcionais — nunca retorna erro aqui
}

// =============================================================================
// Close
// =============================================================================

/**
 * @brief Executa o ciclo completo de destruição na ordem obrigatória.
 *
 * ORDEM (desviar causa crash ou VK_ERROR_DEVICE_LOST):
 *
 *  1. vkDeviceWaitIdle      → GPU termina TODOS os frames em voo.
 *                             Destruir recursos Vulkan antes = UB garantido.
 *
 *  2. Imagens (Unload)      → ANTES de DestroyAll: Image::Unload chama
 *                             vkDestroyImage, vkDestroySampler, etc.
 *                             O device ainda existe neste ponto.
 *
 *  3. delete g_MenuBar      → sem dependência de GPU.
 *  4. delete g_Settings     → idem.
 *
 *  5. Memory::DestroyAll    → StyleEditor → Console → FontManager → ImGui →
 *                             Vulkan (ordem inversa da alocação, garantida
 *                             pelo Memory)
 *
 *  6. SDL_DestroyWindow     → APÓS Vulkan: CleanupWindow já destruiu a surface.
 *  7. SDL_Quit              → encerra subsistemas SDL.
 *  8. g_App = nullptr       → previne use-after-free em chamadas duplas.
 */
void App::Close() {
  // ---- 1. GPU idle — NUNCA destrua recursos Vulkan com frames em voo -----

  if (g_Vulkan && g_Vulkan->GetDevice() != VK_NULL_HANDLE) {
    VkResult err = vkDeviceWaitIdle(g_Vulkan->GetDevice());
    VulkanContext::CheckVkResult(err); // aborta em VK_ERROR_DEVICE_LOST
  }

  // ---- 2. Imagens — Unload ANTES de Memory::DestroyAll -------------------
  // Image::Unload() usa g_Vulkan->GetDevice() internamente.
  // Se chamado depois de DestroyAll(), o device já não existe → crash.
  // O destrutor de App chamaria Unload() de novo, mas Image::Unload()
  // verifica m_Loaded == false e retorna imediatamente (seguro).

  g_Logo.Unload();
  g_IconSettings.Unload();
  // Adicione Unload() para cada nova imagem declarada em App.hpp:
  // g_MeuIcone.Unload();

  // ---- 3 e 4. Objetos com posse em App -----------------------------------

  delete g_MenuBar;
  g_MenuBar = nullptr;
  delete g_Settings;
  g_Settings = nullptr;

  // ---- 5. Memory::DestroyAll — destrói na ordem inversa ------------------

  Memory::Get()->DestroyAll();

  // Os aliases apontam para memória já liberada pelo Memory — anulamos
  g_Style = nullptr;
  g_Console = nullptr;
  g_ImGui = nullptr;
  g_Vulkan = nullptr;

  // ---- 6 e 7. SDL --------------------------------------------------------

  if (g_Window) {
    SDL_DestroyWindow(g_Window); // surface Vulkan já destruída pelo Memory
    g_Window = nullptr;
  }
  SDL_Quit();

  // ---- 8. Anula o ponteiro global ----------------------------------------

  g_App = nullptr; // sinaliza que a instância não existe mais

  ImPlot::DestroyContext();
}

// =============================================================================
// RegisterCommands
// =============================================================================

/**
 * @brief Registra os comandos do Console ImGui.
 *
 * As lambdas capturam [this] e acessam os membros de App diretamente.
 * Nenhum parâmetro de instância é necessário.
 */
MyResult App::RegisterCommands() {
  if (!g_Console)
    return MR_MSGBOX_ERR_END_LOC("g_Console nulo em RegisterCommands.");
  if (!g_Vulkan)
    return MR_MSGBOX_ERR_END_LOC("g_Vulkan nulo em RegisterCommands.");

  // EXIT / QUIT — encerram o MainLoop via g_Done
  auto cmd_quit = [this]() {
    g_Console->AddLog(L"Saindo...");
    g_Done = true;
  };
  g_Console->RegisterBuiltIn(L"EXIT", cmd_quit);
  g_Console->RegisterCommand(L"QUIT", cmd_quit);

  // BREAK — útil com debugger anexado
  g_Console->RegisterCommand(L"BREAK", L"USAR SOMENTE EM DEBUG",
                             []() { __debugbreak(); });

  // SPECS — hardware + APIs gráficas
  g_Console->RegisterCommand(
      L"SPECS", L"Exibe as especificacoes de hardware do PC.", [this]() {
        SystemInfo::Collect(g_Vulkan, L"Vulkan").PrintToConsole(g_Console);
      });

  // VSYNC — alterna VSync
  g_Console->RegisterCommand(L"VSYNC", L"Liga ou desliga o VSync.", [this]() {
    bool novo = !g_Vulkan->GetVSync();
    g_Vulkan->SetVSync(novo);
    g_Console->AddLog(novo ? L"VSync ON" : L"VSync OFF");
  });

  // NOVIEWPORTS — desabilita viewports flutuantes em tempo de execução
  g_Console->RegisterCommand(
      L"NOVIEWPORTS",
      L"Desabilita os viewports flutuantes do ImGui (janelas fora da main "
      L"window).",
      [this]() {
        this->DisableViewportDocking(); // chama o método de App que altera o
                                        // estilo
        if (bViewportDocking)
          bViewportDocking = !bViewportDocking; // global
      });

  g_Console->RegisterCommand(
      L"forceexit",
      L"FORÇA o encerramento imediato do programa (sem cleanup, sem salvar "
      L"configurações).",
      []() {
        g_App->g_Console->AddLog(L"FORCE EXIT: Encerrando imediatamente...");
        std::exit(0); // encerra o processo sem chamar destrutores ou cleanup
      });
  g_Console->RegisterCommand(L"implot", L"Mostra funcionalidades do ImPlot",
                             []() { ImPlot::ShowDemoWindow(); });

  g_Console->RegisterCommand(L"TestEngine",
                             L"Mostra funcionalidades do Test Engine",
                             []() { /*testEnginemain(0, nullptr);*/ });

  g_Console->RegisterCommand(L"Abort",      
      L"Aborta o programa com falha (útil para testar handlers de crash)",
      []() { std::abort(); });

      g_Console->RegisterCommand(L"System Pause",      
      L"Pausa o sistema atraves do windows (útil para depuração)",
      []() { std::system("pause"); });

       g_Console->RegisterCommand(L"Cpp Pause",      
      L"Pausa o sistema atraves do proprío c++ (útil para depuração)",
      []() { std::cin.get(); });
  return MyResult::ok;
}

// =============================================================================
// MainLoop
// =============================================================================

/**
 * @brief Loop SDL + ImGui + Vulkan — roda até g_Done == true.
 * Acessa todos os recursos via membros de App (this) — sem parâmetros externos.
 */
MyResult App::MainLoop() {
  g_io = &g_ImGui->GetIO();
  bool g_grafico = false;

  bool font_rt = (g_io->Fonts && g_io->Fonts->FontLoader);
  g_Console->AddLog(font_rt ? L"\n[RT] FontLoader ativo\n"
                            : L"[RT] FontLoader: stb_truetype (padrao)\n");

  float *color_ptr = g_Settings->clear_color.data();
  g_color_ptr = color_ptr;
  float window_opacity = 1.0f;

  g_window_opacity =
      window_opacity; // inicializa a variável global para o slider

  // No topo do seu MainLoop (fora do loop while)
  static ScrollingBuffer sdata =
      ScrollingBuffer::ScrollingBuffer(); // Você precisaria definir essa struct
                                          // auxiliar do ImPlot
  float t = 0;
  ImPlotFlags spec;
  spec = ImPlotFlags_None;

  // if (ImPlot::BeginPlot("##FPS", ImVec2(-1, 150), spec)) {
  //     // 1. Configuração dos Eixos
  //     ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels,
  //     ImPlotAxisFlags_None); ImPlot::SetupAxisLimits(ImAxis_X1, t - 10, t,
  //     ImGuiCond_Always); ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 200);
  //
  //     // 2. Verificação de segurança: só plota se houver dados
  //     if (sdata.Data.size() > 0) {
  //         // 3. Criar a struct ImPlotSpec exigida pelas suas sobrecargas
  //         ImPlotSpec spec;
  //         spec.Offset = (int)sdata.Offset;
  //         spec.Stride = (int)sizeof(ImVec2);
  //         // Nota: Se houver erro de compilação em 'spec.Flags', adicione:
  //         // spec.Flags = ImPlotLineFlags_None;
  //
  //         // 4. Chamada correta usando a assinatura Template que você possui
  //         ImPlot::PlotLine("FPS Atual",
  //                          &sdata.Data[0].x,
  //                          &sdata.Data[0].y,
  //                          (int)sdata.Data.size(),
  //                          spec);
  //     }
  //
  //     ImPlot::EndPlot();
  // }
  while (!g_Done) {

    // ----------------------------------------------------------------
    // 1. Eventos SDL
    // ----------------------------------------------------------------

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);

      if (event.type == SDL_EVENT_QUIT)
        g_Done = true;

      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(g_Window))
        g_Done = true;

      if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11) {
        g_IsFullscreen = !g_IsFullscreen;
        SDL_SetWindowFullscreen(g_Window, g_IsFullscreen);
      }
    }

    if (SDL_GetWindowFlags(g_Window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(10);
      continue;
    }

    // ----------------------------------------------------------------
    // 2. Rebuild da swapchain
    // ----------------------------------------------------------------

    int fb_w, fb_h;
    SDL_GetWindowSize(g_Window, &fb_w, &fb_h);
    ImGui_ImplVulkanH_Window *wd = g_Vulkan->GetMainWindowData();

    if (fb_w > 0 && fb_h > 0 &&
        (g_Vulkan->NeedsSwapChainRebuild() || wd->Width != fb_w ||
         wd->Height != fb_h))
      g_Vulkan->RebuildSwapChain(fb_w, fb_h);

    Windows();

    // ----------------------------------------------------------------
    // 5. Render Vulkan
    // ----------------------------------------------------------------

    g_ImGui->Render();

    ImDrawData *draw_data = ImGui::GetDrawData();
    const bool minimized =
        (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

    wd->ClearValue.color.float32[0] = color_ptr[0] * color_ptr[3];
    wd->ClearValue.color.float32[1] = color_ptr[1] * color_ptr[3];
    wd->ClearValue.color.float32[2] = color_ptr[2] * color_ptr[3];
    wd->ClearValue.color.float32[3] = color_ptr[3];

    if (!minimized)
      g_Vulkan->FrameRender(draw_data);
    if (g_ImGui->WantsViewports())
      g_ImGui->RenderPlatformWindows();
    if (!minimized)
      g_Vulkan->FramePresent();
  }

  return MyResult::ok;
}

MyResult App::GetDesktopResolution(int& horizontal, int& vertical) {
      RECT desktop;
   // Get a handle to the desktop window
   const HWND hDesktop = GetDesktopWindow();
   // Get the size of screen to the variable desktop
   GetWindowRect(hDesktop, &desktop);
   // The top left corner will have coordinates (0,0)
   // and the bottom right corner will have coordinates
   // (horizontal, vertical)
   horizontal = desktop.right;
   vertical = desktop.bottom;

   return MR_OK;
}

// =============================================================================
// run
// =============================================================================

/**
 * @brief Orquestra SDL, AllocGlobals, MainLoop e Close.
 * Close() é SEMPRE chamado — mesmo que MainLoop retorne erro.
 */
MyResult App::run() {

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))  
    MR_MSGBOX_ERR_END_LOC("Failed to initialize SDL: " +
                                 StrToWStr(SDL_GetError()));
                                 

  float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

  SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                          SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                          SDL_WINDOW_MAXIMIZED;
    
  int desktop_w, desktop_h;

  if (!MR_IS_OK(GetDesktopResolution(desktop_w, desktop_h)) || desktop_w <= 0 || desktop_h <= 0) {
    SDL_Quit();
    return MR_MSGBOX_ERR_END_LOC("Failed to get desktop resolution.");
  }

  g_Window =
      SDL_CreateWindow("Dear ImGui SDL3+Vulkan", static_cast<int>(desktop_w * scale),
                       static_cast<int>(desktop_h * scale), flags);

  if (!g_Window)
    return MR_MSGBOX_ERR_END_LOC("Failed to create SDL window: " +
                                 StrToWStr(SDL_GetError()));

  ImVector<const char *> extensions;
  {
    uint32_t n = 0;
    const char *const *ext = SDL_Vulkan_GetInstanceExtensions(&n);
    for (uint32_t i = 0; i < n; ++i)
      extensions.push_back(ext[i]);
  }

  int w, h;
  SDL_GetWindowSize(g_Window, &w, &h);

  if (!MR_IS_OK(AllocGlobals(extensions, w, h, scale))) {
    Close();
    return MR_MSGBOX_ERR_END_LOC("AllocGlobals falhou.");
  }

  SDL_SetWindowPosition(g_Window, SDL_WINDOWPOS_CENTERED,
                        SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(g_Window);

  if (!MR_IS_OK(RegisterCommands())) {
    Close();
    return MR_MSGBOX_ERR_END_LOC("RegisterCommands falhou.");
  }

  MyResult result = MainLoop();

  Close(); // sempre executado

  return result;
}

void App::DisableViewportDocking() {

  // Remove o flag ViewportsEnable do bitmask de ConfigFlags
  // &= ~FLAG é o idioma C++ para "apaga apenas esse bit, mantém o resto"
  g_io->ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

  // Desabilitar o flag não move janelas já fora da tela de volta.
  // Precisamos iterar todas as janelas internas do ImGui e forçar
  // suas posições para dentro dos limites do viewport principal.
  //
  // ImGui::GetCurrentContext()->Windows só está disponível via
  // imgui_internal.h — incluso aqui apenas para esta operação.
  ImGuiContext *ctx = ImGui::GetCurrentContext();
  ImVec2 main_pos =
      ImGui::GetMainViewport()->Pos; // origem (0,0 em janela normal)
  ImVec2 main_size =
      ImGui::GetMainViewport()->Size; // largura e altura da janela SDL

  for (ImGuiWindow *win : ctx->Windows) {
    if (!win || win->Hidden)
      continue; // ignora janelas invisíveis

    // Calcula a posição máxima permitida para que a janela
    // não ultrapasse o canto inferior direito do viewport
    float max_x = main_pos.x + main_size.x - win->Size.x;
    float max_y = main_pos.y + main_size.y - win->Size.y;

    // ImClamp(valor, min, max) — mantém o valor dentro do intervalo
    win->Pos.x = ImClamp(win->Pos.x, main_pos.x, ImMax(main_pos.x, max_x));
    win->Pos.y = ImClamp(win->Pos.y, main_pos.y, ImMax(main_pos.y, max_y));
  }

  g_App->g_Console->AddLog(
      L"Viewports desabilitados — janelas reposicionadas.");
}

ScrollingBuffer::ScrollingBuffer() : MaxSize(2000), Offset(), Data({}) {}

ScrollingBuffer::ScrollingBuffer(int max_size) : ScrollingBuffer() {
  MaxSize = max_size;
  Offset = 0;
  Data.reserve(MaxSize);
}

void ScrollingBuffer::AddPoint(float x, float y) {
  if (Data.size() < MaxSize)
    Data.push_back(ImVec2(x, y));
  else {
    Data[Offset] = ImVec2(x, y);
    Offset = (Offset + 1) % MaxSize;
  }
}

void ScrollingBuffer::Erase() {
  if (Data.size() > 0) {
    Data.shrink(0);
    Offset = 0;
  }
}