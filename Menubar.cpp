/**
 * @file MenuBar.cpp
 * @brief Implementação da MenuBar — acessa estado via g_App.
 *
 * ANTES (causava LNK2001):
 * ------------------------
 *   extern bool        g_Done;         ← símbolo que o linker não encontrava
 *   extern bool        g_ShowDemo;     ← idem
 *   extern SDL_Window* g_Window;       ← idem
 *   ... (7 extern no total)
 *
 * AGORA (correto):
 * ----------------
 *   #include "App.hpp"                 ← traz "extern App* g_App"
 *   g_App->g_Done      = true;         ← acessa membro público de App
 *   g_App->g_Console->AddLog(...)      ← idem
 *
 * Por que funciona?
 *   "extern App* g_App" em App.hpp é resolvido para a definição
 *   "App* g_App = nullptr;" em App.cpp — um único símbolo que existe.
 *   Os 7 extern anteriores apontavam para símbolos que nunca foram
 *   definidos em nenhum .cpp, por isso o LNK2001.
 */

#include "pch.hpp"
#include "MenuBar.hpp"
#include "App.hpp"       // ← traz extern App* g_App e os membros públicos de App
#include "Console.hpp"   // ← para g_App->g_Console->AddLog()
#include "Memory.hpp"
#include "Imageviewerfactory.hpp"
#include "OnlineClock.hpp"

// =============================================================================
// Construtor
// =============================================================================

/**
 * @brief Inicializa o estado interno da MenuBar.
 * m_ShowAbout começa false — popup "Sobre" fecha até o usuário clicar.
 */
MenuBar::MenuBar()
    : m_ShowAbout(false), g_Settings(nullptr) {


}

// =============================================================================
// Draw
// =============================================================================

/**
 * @brief Ponto de entrada por frame — desenha a barra completa e os popups.
 *
 * BeginMainMenuBar() cria a barra no topo da viewport e retorna true se deve
 * ser populada. EndMainMenuBar() DEVE ser chamado nesse caso.
 * DrawAboutPopup() fica FORA do par Begin/End (BeginPopupModal exige nível raiz).
 */
void MenuBar::Draw() {
	g_App = Memory::Get()->GetApp();
        g_Settings = Memory::Get()->GetAppSettings();
	m_ImageViewerFactory = Memory::Get()->GetImageViewerFactory();
        clock = Memory::Get()->GetOnlineClock();


    if(ImGui::BeginMainMenuBar()) {

        DrawMenuFile();                           // File
        DrawMenuEdit();                           // Edit
        DrawMenuView();                           // View
        DrawMenuHelp();                           // Help
        DrawFpsCounter(ImGui::GetIO().Framerate); // FPS à direita
        ImGui::EndMainMenuBar();
    }
          

    DrawAboutPopup(); // FORA do BeginMainMenuBar — ver doc no topo do arquivo
}

// =============================================================================
// DrawMenuFile
// =============================================================================

/**
 * @brief Submenu "File": New, Open, separador, Exit.
 *
 * Exit escreve em g_App->g_Done, que é membro público de App.
 * O MainLoop verifica g_Done a cada iteração e encerra quando true.
 */
void MenuBar::DrawMenuFile() {
    if(ImGui::BeginMenu("File")) {

        if(ImGui::MenuItem("New"))  { /* TODO */ }
        if(ImGui::MenuItem("Open")) { /* TODO */ }

        // Abre o explorador de ficheiros para seleccionar imagem(ns).
        // OpenFileDialog() usa IFileOpenDialog com FOS_ALLOWMULTISELECT —
        // suporta Ctrl+Clique para selecção múltipla.
        if(ImGui::MenuItem("Abrir Imagem...", "Ctrl+O"))
            m_ImageViewerFactory->OpenFileDialog();

        ImGui::Separator();

        if(ImGui::MenuItem("Exit", "Alt+F4"))
            g_App->g_Done = true;

        ImGui::EndMenu();
    }
}
// =============================================================================
// DrawMenuEdit
// =============================================================================

/**
 * @brief Submenu "Edit": Undo, Redo, separador, Preferences.
 * Preferences abre o StyleEditor definindo g_App->g_ShowStyleEd = true.
 */
void MenuBar::DrawMenuEdit() {
    if(ImGui::BeginMenu("Edit")) {

        if(ImGui::MenuItem("Undo", "Ctrl+Z")) { /* TODO */ }
        if(ImGui::MenuItem("Redo", "Ctrl+Y")) { /* TODO */ }

        ImGui::Separator();

        if(ImGui::MenuItem("Preferences"))
            g_App->g_ShowStyleEd = true; // membro público de App

        ImGui::EndMenu();
    }
}

// =============================================================================
// DrawMenuView
// =============================================================================

/**
 * @brief Submenu "View" com toggles de janelas e fullscreen.
 *
 * MenuItem com bool* (terceiro parâmetro):
 *   - Desenha checkmark automático quando *ptr == true
 *   - Inverte *ptr ao clicar (sem if/else manual)
 *
 * Passamos o endereço de membros públicos de App via g_App.
 */
void MenuBar::DrawMenuView() {
    if(ImGui::BeginMenu("View")) {

        // &g_App->g_Settings->show_console = ponteiro para o campo do AppSettings
ImGui::MenuItem("Window Controls", nullptr, &g_Settings->window.show_window_controls);
ImGui::MenuItem("Console", nullptr, &g_App->g_Settings->window.show_console);
        ImGui::MenuItem("Style Editor", nullptr, &g_App->g_ShowStyleEd);
        ImGui::MenuItem("Demo Window",  nullptr, &g_App->g_ShowDemo);

        ImGui::Separator();

        const char* fs_label = g_App->g_IsFullscreen
            ? "Sair de Fullscreen"
            : "Fullscreen";

        if(ImGui::MenuItem(fs_label, "F11")) {
            g_App->g_IsFullscreen = !g_App->g_IsFullscreen;
            SDL_SetWindowFullscreen(g_App->g_Window, g_App->g_IsFullscreen);
        }

        ImGui::EndMenu();
    }
}

// =============================================================================
// DrawMenuHelp
// =============================================================================

/**
 * @brief Submenu "Help" com link ao console e item "Sobre...".
 *
 * OpenPopup não pode ser chamado aqui porque estamos dentro de
 * BeginMainMenuBar — usamos m_ShowAbout como ponte para DrawAboutPopup().
 */
void MenuBar::DrawMenuHelp() {
    if(ImGui::BeginMenu("Help")) {

        if(ImGui::MenuItem("Comandos do Console")) {
            if(g_App->g_Console)
                g_App->g_Console->AddLog(
                    L"Digite HELP no console para ver os comandos.");
        }

        ImGui::Separator();

        if(ImGui::MenuItem("Sobre..."))
            m_ShowAbout = true; // DrawAboutPopup() vê essa flag no mesmo frame
             ImGui::Separator();
        ImGui::EndMenu();
    }
}

// =============================================================================
// DrawFpsCounter
// =============================================================================

/**
 * @brief Exibe o FPS alinhado à direita da barra de menu.
 *
 * CalcTextSize mede o texto antes de desenhá-lo para calcular o offset.
 * SetCursorPosX move o cursor horizontalmente.
 * TextDisabled usa a cor ImGuiCol_TextDisabled (cinza) — não interativo.
 *
 * @param fps  io.Framerate do ImGuiIO.
 */
void MenuBar::DrawFpsCounter(float fps) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f FPS", fps);

    // Exemplo: Diminuir um pouco o tamanho da fonte apenas para o FPS
    // ImGui::PushFont(ImGui::GetFont(), 13.0f); 

   float largura = ImGui::CalcTextSize(buf).x;
        
        // Empurra para a direita
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - largura - 100.f);
    ImGui::TextDisabled("%s", buf);

    //ImGui::PopFont();
      DrawClock();
}
// =============================================================================
// DrawAboutPopup
// =============================================================================

/**
 * @brief Processa e renderiza o popup modal "Sobre".
 *
 * FLUXO (dois estágios):
 *  1. m_ShowAbout == true → OpenPopup() registra o ID → m_ShowAbout = false
 *  2. BeginPopupModal() encontra o ID e abre o popup (bloqueia a UI)
 *  3. "Fechar" → CloseCurrentPopup() remove o ID do stack
 *  4. EndPopup() obrigatório se BeginPopupModal retornou true
 *
 * Este método fica FORA do par BeginMainMenuBar/EndMainMenuBar porque
 * BeginPopupModal precisa estar no nível raiz do frame ImGui.
 */
void MenuBar::DrawAboutPopup() {
    if(m_ShowAbout) {
        ImGui::OpenPopup("##about_modal"); // registra — não abre ainda
        m_ShowAbout = false;               // evita reabrir no próximo frame
    }

    if(ImGui::BeginPopupModal("##about_modal", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Meu Programa");
        ImGui::Text("Build: " __DATE__ " " __TIME__);
        ImGui::Separator();

        if(ImGui::Button("Fechar", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

void MenuBar::DrawClock() {
    
    char buf[32];
    // Sincroniza em uma thread separada para não travar a UI no início
    if (!clock->m_sincronizado) {
        std::thread([this]() { clock->sincronizar(); }).detach();
        clock->m_sincronizado = true;
    }

   
const std::string h = clock->obterDataHoraFormatada();
       
       ImGui::SameLine();
 ImGui::TextDisabled("   H:%s", h.c_str());
    
}
