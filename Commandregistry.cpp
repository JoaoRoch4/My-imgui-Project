/**
 * @file CommandRegistry.cpp
 * @brief Implementação do registro de comandos do Console.
 *
 * ESTRUTURA
 * ----------
 * RegisterAll()
 *   ├─ RegisterLifecycle()  — EXIT, QUIT, BREAK, forceexit, Abort, pauses
 *   ├─ RegisterSystem()     — SPECS, VSYNC, NOVIEWPORTS, FONTRESET
 *   ├─ RegisterTheme()      — MICA, NOMICA, theme [dark|light|classic]
 *   └─ RegisterDemo()       — implot, implot3d, Test Emojis
 *
 * CAPTURA NOS LAMBDAS
 * --------------------
 * Todos os lambdas capturam ponteiros por valor ([app, con] ou [this]).
 * Os ponteiros são válidos durante toda a vida útil do Console porque
 * Memory::DestroyAll() destrói o Console antes de App e Vulkan.
 *
 * COMANDOS COM ARGUMENTOS
 * -------------------------
 * A sobrecarga RegisterCommand(name, desc, func<vector<wstring>>) é usada
 * para comandos que precisam de parâmetros em runtime, como "theme dark".
 * ExecCommand extrai o primeiro token como chave; os demais chegam em args.
 */

#include "pch.hpp"
#include "CommandRegistry.hpp"

#include "App.hpp"           // membros públicos de App (g_Done, g_Vulkan, g_Settings…)
#include "Console.hpp"       // AddLog, RegisterCommand, RegisterBuiltIn
#include "MyResult.hpp"      // MR_OK
#include "FontScale.hpp"     // FontScale::ResetToDefault()
#include "SystemInfo.hpp"    // SystemInfo::Collect()
#include "MicaTheme.h"       // MicaTheme::ApplyMicaTheme()
#include "VulkanContext_Wrapper.hpp"
#include "ImGuiContext_Wrapper.hpp"

// ============================================================================
// Construtor
// ============================================================================

/**
 * @brief Armazena as referências não-possuidoras de App e Console.
 *
 * Nenhuma alocação é feita aqui — o objeto é apenas um organizador
 * de chamadas de RegisterCommand durante RegisterAll().
 *
 * @param app      Instância de App em execução.
 * @param console  Console ImGui onde os comandos serão registrados.
 */
CommandRegistry::CommandRegistry(App* app, Console* console) noexcept
    : m_app(app)       // ponteiro não-possuidor — App é dono de si mesmo
    , m_console(console) // ponteiro não-possuidor — Memory é dono do Console
{
}

// ============================================================================
// RegisterAll
// ============================================================================

/**
 * @brief Registra todos os grupos de comandos no Console.
 *
 * Chama os quatro grupos em ordem lógica:
 *  1. Lifecycle — comandos que controlam o processo em si
 *  2. System    — comandos de hardware e configuração de engine
 *  3. Theme     — comandos de aparência visual
 *  4. Demo      — comandos de demonstração e testes
 *
 * @return MyResult::ok sempre; falhas individuais são logadas no console.
 */
[[nodiscard]] MyResult CommandRegistry::RegisterAll()
{
    RegisterLifecycle(); // EXIT, QUIT, BREAK, forceexit, Abort, pauses
    RegisterSystem();    // SPECS, VSYNC, NOVIEWPORTS, FONTRESET
    RegisterTheme();     // MICA, NOMICA, theme [dark|light|classic]
    RegisterDemo();      // implot, implot3d, Test Emojis

    return MyResult::ok;
}

// ============================================================================
// RegisterLifecycle
// ============================================================================

/**
 * @brief Registra comandos que controlam o ciclo de vida da aplicação.
 *
 * Comandos:
 *  EXIT        — encerra o loop principal via g_Done = true (built-in)
 *  QUIT        — alias de EXIT
 *  BREAK       — __debugbreak() se debugger presente; aviso caso contrário
 *  forceexit   — std::exit(0) imediato sem cleanup (uso de emergência)
 *  Abort       — std::abort() para gerar core dump em debug
 *  System Pause — pausa via Windows (system("pause"))
 *  Cpp Pause   — pausa via std::cin.get()
 */
void CommandRegistry::RegisterLifecycle()
{
    // Captura app e console por valor — seguros durante toda a vida do Console
    App*     app = m_app;
    Console* con = m_console;

    // ---- EXIT / QUIT --------------------------------------------------------

    // Lambda compartilhado por EXIT (built-in) e QUIT (alias)
    auto cmd_quit = [app, con]()
    {
        con->AddLog(L"Saindo..."); // notifica o usuário no log
        app->g_Done = true;        // sinaliza o MainLoop para encerrar
    };

    con->RegisterBuiltIn(L"EXIT", cmd_quit); // valida contra BuiltInCommands[]
    con->RegisterCommand(L"QUIT",            // alias sem validação de built-in
        L"Alias de EXIT — encerra o programa.",
        cmd_quit);

    // ---- BREAK --------------------------------------------------------------

    con->RegisterCommand(
        L"BREAK",
        L"Dispara __debugbreak() se um depurador estiver presente.",
        [con]()
        {
            if(IsDebuggerPresent())
                __debugbreak();         // interrompe no depurador (MSVC/WinDbg)
            else
                con->AddLog(L"[yellow]AVISO:[/] Nenhum depurador detectado.");
        });

    // ---- forceexit ----------------------------------------------------------

    con->RegisterCommand(
        L"forceexit",
        L"Encerra imediatamente via std::exit(0) — sem cleanup.",
        [con]()
        {
            con->AddLog(L"[error]FORCE EXIT[/] — saindo sem cleanup.");
            std::exit(0); // saída imediata; destrutores NÃO são chamados
        });

    // ---- Abort --------------------------------------------------------------

    con->RegisterCommand(
        L"Abort",
        L"Aborta o processo via std::abort() — gera core dump em debug.",
        []() { std::abort(); }); // SIGABRT → core dump no Linux, diálogo no MSVC

    // ---- System Pause -------------------------------------------------------

    con->RegisterCommand(
        L"System Pause",
        L"Pausa a execução via system(\"pause\") — apenas Windows.",
        []() { std::system("pause"); }); // bloqueia até o usuário pressionar Enter

    // ---- Cpp Pause ----------------------------------------------------------

    con->RegisterCommand(
        L"Cpp Pause",
        L"Pausa a execução via std::cin.get().",
        []() { std::cin.get(); }); // aguarda Enter no stdin
}

// ============================================================================
// RegisterSystem
// ============================================================================

/**
 * @brief Registra comandos de sistema, hardware e configuração de engine.
 *
 * Comandos:
 *  SPECS       — coleta e exibe especificações de hardware via SystemInfo
 *  VSYNC       — toggle do VSync no VulkanContext
 *  NOVIEWPORTS — desabilita viewports flutuantes e reposiciona janelas
 *  FONTRESET   — restaura o tamanho original da fonte global
 */
void CommandRegistry::RegisterSystem()
{
    App*     app = m_app;
    Console* con = m_console;

    // ---- SPECS --------------------------------------------------------------

    con->RegisterCommand(
        L"SPECS",
        L"Exibe especificações de hardware (CPU, GPU, memória).",
        [app, con]()
        {
            // Collect() lê dados via Vulkan + Win32 e retorna um objeto imprimível
            SystemInfo::Collect(app->g_Vulkan, L"Vulkan")
                .PrintToConsole(con); // imprime linhas no console ImGui
        });

    // ---- VSYNC --------------------------------------------------------------

    con->RegisterCommand(
        L"VSYNC",
        L"Liga ou desliga o VSync do swapchain Vulkan.",
        [app, con]()
        {
            const bool novo = !app->g_Vulkan->GetVSync(); // inverte o estado atual
            app->g_Vulkan->SetVSync(novo);                 // reconstrói o swapchain
            con->AddLog(novo ? L"[green]VSync ON[/]" : L"[yellow]VSync OFF[/]");
        });

    // ---- NOVIEWPORTS --------------------------------------------------------

    con->RegisterCommand(
        L"NOVIEWPORTS",
        L"Desabilita viewports flutuantes e reposiciona janelas dentro da área principal.",
        [app, con]()
        {
            app->DisableViewportDocking(); // desabilita flag + reposiciona janelas

            if(app->bViewportDocking)
                app->bViewportDocking = false; // zera o espelho em App
        });

    // ---- FONTRESET ----------------------------------------------------------

    con->RegisterCommand(
        L"FONTRESET",
        L"Restaura o tamanho original da fonte global (desfaz Ctrl+Scroll).",
        [app]()
        {
            FontScale::ResetToDefault(); // volta ao tamanho capturado na inicialização
            app->SaveConfig();           // persiste o reset para o próximo boot
        });
}

// ============================================================================
// RegisterTheme
// ============================================================================

/**
 * @brief Registra comandos de tema visual do ImGui.
 *
 * Comandos:
 *  MICA             — ativa o tema Windows 11 Mica e persiste
 *  NOMICA           — desativa o tema Mica (usa style+color puros) e persiste
 *  theme [arg]      — aplica preset: dark | light | classic (case-insensitive)
 *
 * NOTA SOBRE "theme"
 * -------------------
 * Usa a sobrecarga RegisterCommand(name, desc, func<vector<wstring>>).
 * ExecCommand extrai o primeiro token ("theme") como chave de dispatch;
 * os tokens seguintes ("dark", "light", etc.) chegam em args[0].
 *
 *   Usuário digita: "theme dark"
 *   Dispatch chave: "THEME"   → encontra a lambda
 *   args:           { L"dark" } → aplica StyleColorsDark()
 */
void CommandRegistry::RegisterTheme()
{
    App*     app = m_app;
    Console* con = m_console;

    // ---- MICA ---------------------------------------------------------------

    con->RegisterCommand(
        L"MICA",
        L"Ativa o tema Windows 11 Mica e persiste em settings.json.",
        [app, con]()
        {
            app->g_Settings->use_mica_theme = true;                       // liga o flag
            MicaTheme::ApplyMicaTheme(app->g_Settings->mica_theme);       // aplica ao ImGuiStyle
            app->SaveConfig();                                             // persiste
            con->AddLog(L"[cyan]Tema Mica ativado.[/]");
        });

    // ---- NOMICA -------------------------------------------------------------

    con->RegisterCommand(
        L"NOMICA",
        L"Desativa o tema Mica (usa style+color salvos) e persiste.",
        [app, con]()
        {
            app->g_Settings->use_mica_theme = false; // desliga o flag
            app->ApplyStyleToImGui();                 // reaplicação sem Mica
            app->SaveConfig();                        // persiste
            con->AddLog(L"[yellow]Tema Mica desativado.[/]");
        });

    // ---- theme [dark|light|classic] -----------------------------------------

    con->RegisterCommand(
        L"theme",
        L"Aplica um preset de tema ImGui. Uso: theme [dark|light|classic]",
        [con](std::vector<std::wstring> args) // sobrecarga com argumentos
        {
            // Sem argumentos → exibe ajuda resumida
            if(args.empty())
            {
                con->AddLog(L"[yellow]Uso:[/] theme [dark|light|classic]");
                return; // nada a aplicar sem subcomando
            }

            // Copia o primeiro argumento e converte para UPPERCASE
            // para comparação case-insensitive (L"Dark" == L"DARK").
            std::wstring sub = args[0]; // cópia do primeiro argumento
            std::transform(
                sub.begin(), sub.end(),
                sub.begin(),
                [](const wchar_t c)
                { return static_cast<wchar_t>(towupper(c)); }); // uppercase largo

            if(sub == L"DARK")
            {
                ImGui::StyleColorsDark();                    // preset escuro do ImGui
                con->AddLog(L"[cyan]Tema:[/] dark aplicado.");
            }
            else if(sub == L"LIGHT")
            {
                ImGui::StyleColorsLight();                   // preset claro do ImGui
                con->AddLog(L"[cyan]Tema:[/] light aplicado.");
            }
            else if(sub == L"CLASSIC" || sub == L"CLEAR")
            {
                ImGui::StyleColorsClassic();                 // preset clássico do ImGui
                con->AddLog(L"[cyan]Tema:[/] classic aplicado.");
            }
            else
            {
                // Subcomando não reconhecido — informa sem encerrar
                con->AddLog(L"[error]Subcomando desconhecido:[/] '%ls'", args[0].c_str());
                con->AddLog(L"[yellow]Uso:[/] theme [dark|light|classic]");
            }
        });
}

// ============================================================================
// RegisterDemo
// ============================================================================

/**
 * @brief Registra comandos de demonstração e testes visuais.
 *
 * Comandos:
 *  implot       — abre a janela de demo do ImPlot
 *  implot3d     — placeholder para demo do ImPlot3D
 *  Test Emojis  — imprime uma sequência de emoji no console para verificar
 *                 se a fonte emoji está carregada e os ranges corretos
 */
void CommandRegistry::RegisterDemo()
{
    App*     app = m_app;
    Console* con = m_console;

    // ---- implot -------------------------------------------------------------

    con->RegisterCommand(
        L"implot",
        L"Abre a janela de demo do ImPlot.",
        []() { ImPlot::ShowDemoWindow(); }); // sem parâmetros — abre a janela demo

    // ---- implot3d -----------------------------------------------------------

    con->RegisterCommand(
        L"implot3d",
        L"Placeholder para a demo do ImPlot3D (ative via checkbox na UI).",
        []() {}); // funcionalidade real está nos checkboxes da janela principal

    // ---- Test Emojis --------------------------------------------------------

    // Sequência larga de emoji cobrindo os principais ranges do atlas.
    // Serve para verificar visualmente que:
    //   a) a fonte emoji foi carregada (seguiemj.ttf / NotoColorEmoji.ttf)
    //   b) os ranges corretos foram passados em GetEmojiGlyphRanges()
    //   c) a conversão wchar_t → UTF-8 na fronteira ImGui está correta
    //
    // Cada linha usa L"...\n" — literais adjacentes unidos pelo compilador.
    static constexpr const wchar_t* k_emoji_test =
        L"😀 😁 😂 🤣 😃 😄 😅 😆 😉 😊\n"
        L"😋 😎 🥳 🥺 🙏 😍 🤩 😘 🤗 🤔\n"
        L"😐 😑 😶 😏 😒 🙄 😬 🤥 😌 😔\n"
        L"\n"
        L"👋 🤚 🖐 ✋ 🖖 👌 ✌ 🤞 👍 👎\n"
        L"✊ 👊 🤛 🤜 👏 🙌 👐 🤲 🙏\n"
        L"\n"
        L"💻 🖥 🖨 ⌨ 🖱 💾 💿 📀 📱 ☎\n"
        L"🔋 🔌 💡 🔦 🔎 🔬 🔭 📡 🧯\n"
        L"\n"
        L"🎮 🕹 🎲 ♟ 🎯 🎳 🎰 🎨 🎵 🎶\n"
        L"🎷 🎸 🥁 🎤 🎧 🎼 🎬 🎭 🎪\n"
        L"\n"
        L"🚀 🛸 🌍 🌙 ⭐ 🌟 💫 ✨ ☄ 🌈\n"
        L"⚡ 🔥 💧 🌊 🌀 🌪 ❄ ☃ ⛄\n"
        L"\n"
        L"❤ 🧡 💛 💚 💙 💜 🖤 🤍 🤎 💔\n"
        L"💕 💞 💓 💗 💖 💘 💝 💟\n"
        L"\n"
        L"🐶 🐱 🐭 🐹 🐰 🦊 🐻 🐼 🐨 🐯\n"
        L"🦁 🐮 🐷 🐸 🐵 🙈 🙉 🙊 🐔 🐧\n"
        L"\n"
        L"✓ ✗ ⚠ ℹ ⚡ 🔧 🚀 📝 📊 🎯\n"
        L"💡 🔍 ⏱ 📦 🌟 💾 📥 📤 🔐 🔑\n";

    con->RegisterCommand(
        L"Test Emojis",
        L"Imprime emoji no console para verificar a fonte e os ranges.",
        [app, con]()
        {
            con->AddLog(L"[cyan]=== Teste de Emoji ===[/]");
            con->AddLog(k_emoji_test); // imprime toda a sequência de uma vez
            con->AddLog(L"[cyan]=== Fim do teste ===[/]");
            (void) app; // silencia warning de captura não usada
        });
}