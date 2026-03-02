/**
 * @file main.cpp
 * @brief Ponto de entrada do programa — cria App e chama run().
 *
 * RESPONSABILIDADE DESTE ARQUIVO
 * --------------------------------
 * Apenas instanciar App e delegar tudo para ele.
 * Toda lógica de inicialização, loop e shutdown está em App.cpp.
 *
 * FLUXO COMPLETO (implementado em App.cpp):
 *
 *   App::App()
 *     └─ g_App = this               ← expõe a instância globalmente
 *
 *   App::run()
 *     ├─ ParseArgs()                ← lê --noviewports, etc. de __wargv
 *     ├─ SDL_Init + CreateWindow
 *     ├─ AllocGlobals()             ← Memory::AllocAll + AppSettings + MenuBar
 * + Images ├─ SDL_ShowWindow ├─ RegisterCommands()         ← EXIT, VSYNC,
 * SPECS, NOVIEWPORTS, FONTSCALE ├─ MainLoop()                 ← loop
 * SDL+ImGui+Vulkan até g_Done └─ Close()                    ← vkDeviceWaitIdle
 * → DestroyAll → SDL_Quit
 *
 * LINHA DE COMANDO
 * ----------------
 * Argumentos reconhecidos por App::ParseArgs() (via __argc / __wargv):
 *
 *   --noviewports   desabilita ImGuiConfigFlags_ViewportsEnable na
 * inicialização
 *
 * Exemplo:
 *   example_sdl3_vulkan.exe --noviewports
 *
 * CONSOLE EXTERNO (Windows)
 * -------------------------
 * WindowsConsole é um console Win32 separado, com hotkey F1.
 * Inicializado antes de App::run() para capturar logs desde o início.
 * Desligado após run() retornar.
 */

 #include "pch.hpp"            // cabeçalho pré-compilado — SEMPRE PRIMEIRO

#include "main.hpp"
#include "App.hpp"            // classe App + extern App* g_App
#include "WindowsConsole.hpp" // console externo Win32 com hotkey
#include "MyResult.hpp"

// =============================================================================
// main — ponto de entrada de console (/SUBSYSTEM:CONSOLE)
// =============================================================================

/**
 * @brief Ponto de entrada padrão C++.
 *
 * Cria App na stack — RAII garante que o destrutor é chamado ao sair de main,
 * após run() retornar e ANTES de qualquer objeto estático global ser destruído.
 *
 * App::App() atribui g_App = this antes de qualquer código em run(),
 * portanto g_App é válido durante toda a execução de App::run().
 *
 * @return EXIT_SUCCESS se o programa encerrou normalmente, EXIT_FAILURE caso
 * contrário.
 */
int main(int argc, char *argv[]) {
  App app;                     // construtor: g_App = this
  MyResult result = app.run(); // encapsula SDL_Init → loop → Close
  return MR_IS_OK(result) ? EXIT_SUCCESS : EXIT_FAILURE;
}

// =============================================================================
// wWinMain — ponto de entrada GUI do Windows (/SUBSYSTEM:WINDOWS)
// =============================================================================

/**
 * @brief Ponto de entrada Windows GUI — sem janela de console visível.
 *
 * _Use_decl_annotations_ é uma macro SAL (Source Annotation Language) do MSVC
 * que instrui o analisador estático a verificar os parâmetros segundo as
 * anotações definidas em winbase.h, sem repetição no .cpp.
 *
 * Todos os parâmetros são ignorados intencionalmente:
 *   • hInstance     — SDL usa GetModuleHandle(NULL) internamente.
 *   • hPrevInstance — sempre NULL em Win32 moderno (legado Win16).
 *   • lpCmdLine     — ignorado; App::ParseArgs() lê __argc/__wargv do CRT.
 *   • nCmdShow      — ignorado; SDL controla o estado inicial da janela.
 *
 * @param hInstance      Handle da instância do processo.
 * @param hPrevInstance  Sempre NULL.
 * @param lpCmdLine      Linha de comando sem o nome do executável.
 * @param nCmdShow       Estado inicial da janela sugerido pelo shell.
 * @return EXIT_SUCCESS ou EXIT_FAILURE.
 */
_Use_decl_annotations_ INT  WINAPI  wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_     LPWSTR    lpCmdLine,
    _In_     int       nCmdShow) {

	setlocale(LC_ALL, "utf-8"); // para suporte a caracteres acentuados no console
	SetConsoleOutputCP(CP_UTF8); // para entrada UTF-8 no console
  // Console externo Win32 inicializado ANTES de App para capturar
  // todos os logs de inicialização do Vulkan, ImGui e FontManager.
  WindowsConsole::init(VK_F1); // F1 abre/fecha o console externo

    //App *app = Memory::Get()->GetApp(); // aloca App para que g_App seja válido durante run()
   std::unique_ptr<App> app = std::make_unique<App>();                     // construtor: g_App = this
  MyResult result = app->run(); // encapsula SDL_Init → loop → Close

  // Shutdown após run() — Close() já liberou todos os recursos.
  WindowsConsole::shutdown();

  return MR_IS_OK(result) ? EXIT_SUCCESS : EXIT_FAILURE;
}