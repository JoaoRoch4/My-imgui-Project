#pragma once
#include "pch.hpp"

/// @brief Tecla de atalho padrão para abrir/fechar o WindowsConsole (pode ser mudada).
constexpr int WINDOWS_CONSOLE_DEFAULT_KEY = VK_F1;

/**
 * @brief Gerencia a janela de console externa do Windows (cmd) em aplicações wWinMain.
 *
 * Diferente do Console ImGui (que é um widget dentro da janela),
 * o WindowsConsole é a janela preta do sistema operacional que exibe
 * stdout/stderr e pode ser aberta/fechada em runtime sem reiniciar o app.
 */
class WindowsConsole {
public:

    /**
     * @brief Inicializa o console externo — deve ser chamado uma vez no wWinMain.
     * @param hotkey Tecla virtual (VK_*) que faz o toggle. Padrão: F1.
     */
    static void init(int hotkey = WINDOWS_CONSOLE_DEFAULT_KEY);

    /** @brief Libera o console externo — chame ao encerrar o programa. */
    static void shutdown();

    /** @brief Abre ou fecha o console externo dependendo do estado atual. */
    static void toggle();

    /** @brief Renderiza o botão ImGui de toggle. Chame dentro do seu ImGui frame. */
    static void render_imgui_button();

    /**
     * @brief Processa o hotkey — chame uma vez por frame antes do ImGui::Render().
     * Internamente usa GetAsyncKeyState para detectar a tecla.
     */
    static void poll_hotkey();

    /** @brief Retorna true se o console externo estiver visível no momento. */
    static bool is_visible();

private:
    /** @brief Torna a janela do console externa visível e redireciona streams. */
    static void open();

    /** @brief Esconde a janela do console externo sem destruí-la. */
    static void hide();

    static bool s_visible; ///< Estado atual: true = console externo aberto
    static int  s_hotkey;  ///< Tecla virtual registrada no init()
    static HWND s_hwnd;    ///< Handle da janela do console (GetConsoleWindow)
};
