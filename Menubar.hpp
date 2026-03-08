#pragma once

#include "pch.hpp"

/**
 * @file MenuBar.hpp
 * @brief Declaração da classe MenuBar — barra de menu principal do ImGui.
 *
 * MenuBar acessa todo o estado via g_App (definido em App.cpp, declarado
 * em App.hpp). Basta incluir App.hpp em MenuBar.cpp para ter acesso a:
 *
 *   g_App->g_Done
 *   g_App->g_Settings
 *   g_App->g_Window
 *   g_App->g_Console
 *   g_App->g_ShowStyleEd
 *   g_App->g_ShowDemo
 *   g_App->g_IsFullscreen
 *
 * Draw() não recebe nenhum parâmetro — tudo vem do g_App.
 */
class MenuBar {
public:

    MenuBar();
    ~MenuBar() = default;

    /**
     * @brief Desenha a barra de menu e seus popups.
     * Chame uma vez por frame após NewFrame() e antes de Render().
     */
    void Draw();

private:
    class App* g_App;
    class AppSettings* g_Settings; ///< Configurações persistidas — dono: Memory singleton
    class ImageViewerFactory* m_ImageViewerFactory;
    class OnlineClock* clock;

    bool m_ShowAbout; ///< Flag interna: true = abrir popup "Sobre" neste frame

    void DrawMenuFile();            ///< File  > New | Open | Exit
    void DrawMenuEdit();            ///< Edit  > Undo | Redo | Preferences
    void DrawMenuView();            ///< View  > Console | StyleEditor | Demo | Fullscreen
    void DrawMenuHelp();            ///< Help  > Comandos | Sobre...
    void DrawFpsCounter(float fps); ///< FPS alinhado à direita da barra
    void DrawAboutPopup();          ///< Popup modal — FORA do BeginMainMenuBar
	void DrawClock();
};
