#pragma once
#include "pch.hpp"
#include "MyResult.hpp"

/// @brief Tecla de atalho padrão para abrir/fechar o WindowsConsole.
constexpr int WINDOWS_CONSOLE_DEFAULT_KEY = VK_F1;

/**
 * @file WindowsConsole.hpp
 * @brief Console externo com visual idêntico ao PowerShell 7 (pwsh.exe).
 *
 * VISUAL POWERSHELL 7
 * --------------------
 * O PowerShell 7 usa, por omissão, as seguintes propriedades visuais:
 *
 *   Fundo  : #012456  — navy escuro (diferente do azul brilhante do PS5)
 *   Texto  : #CCCCCC  — cinzento claro (não branco puro)
 *   Fonte  : Cascadia Mono 12pt (incluída com Windows Terminal / PS7)
 *             Fallback: Cascadia Code → Consolas
 *   Título : "PowerShell 7"
 *   Paleta : One Half Dark (tema padrão do Windows Terminal)
 *   Tamanho: 120 × 30, buffer 9001 linhas (número clássico do PowerShell)
 *   ANSI   : ENABLE_VIRTUAL_TERMINAL_PROCESSING activo
 *
 * BOTÃO X — SetConsoleCtrlHandler
 * ---------------------------------
 * AllocConsole cria janela no conhost.exe (outro processo).
 * SetWindowLongPtrW entre processos não funciona.
 * SetConsoleCtrlHandler interceta CTRL_CLOSE_EVENT:
 *   handler retorna TRUE rapidamente → Windows não termina o processo.
 */
class WindowsConsole
{
public:

    /**
     * @brief Aloca o console com visual PowerShell 7 e instala o handler de fecho.
     * @param hotkey  Tecla VK_* para toggle. Padrão: VK_F1.
     */
    static MyResult init(int hotkey = WINDOWS_CONSOLE_DEFAULT_KEY);

    /** @brief Libera o console e remove o handler de fecho. */
    static MyResult shutdown();

    /** @brief Alterna visibilidade. */
    static void toggle();

    /** @brief Botão ImGui de toggle — chame dentro do frame ImGui activo. */
    static void render_imgui_button();

    /** @brief Detecção de hotkey — chame uma vez por frame. */
    static void poll_hotkey();

    /** @brief true se o console está visível. */
    [[nodiscard]] static bool is_visible();

private:

    static void open();
    static void hide();

    /**
     * @brief Interceta CTRL_CLOSE_EVENT (clique no X).
     * Retorna TRUE → processo não é terminado.
     */
    static BOOL WINAPI CtrlHandler(DWORD ctrl_type);

    /**
     * @brief Aplica o visual completo do PowerShell 7.
     * Paleta One Half Dark, fundo #012456, Cascadia Mono, 120×30.
     */
    static void ApplyPowerShell7Style();

    /**
     * @brief Tenta instalar a fonte Cascadia Mono/Code via WinRT StorageFile.
     * Usado apenas se a fonte não estiver no sistema.
     * Requer Windows 10 1809+ e linkagem com WindowsApp.lib.
     * @return true se a fonte ficou disponível após a tentativa.
     */
    static bool EnsureCascadiaFont();

    static bool s_visible; ///< true se o console está visível
    static int  s_hotkey;  ///< código VK_ do hotkey
    static HWND s_hwnd;    ///< HWND da janela do console (GetConsoleWindow)
};