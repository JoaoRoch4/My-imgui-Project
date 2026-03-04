/**
 * @file WindowsConsole.cpp
 * @brief Implementação de WindowsConsole — gerencia a janela de console externa
 *        do Windows (cmd/stdout) em aplicações com subsystem Windows (wWinMain).
 *
 * DIFERENÇA ENTRE WindowsConsole E Console:
 * -----------------------------------------
 *  - WindowsConsole : janela preta do SO (AllocConsole), exibe stdout/stderr,
 *                     pode ser aberta/fechada via botão ImGui ou tecla F1.
 *  - Console        : widget ImGui desenhado dentro da janela do aplicativo,
 *                     aceita comandos wide-char, tem histórico e autocomplete.
 *
 * O console externo é criado UMA vez no init() e depois apenas mostrado/escondido
 * via ShowWindow(), mantendo stdout sempre funcional mesmo quando invisível.
 */

#include "pch.hpp"              // cabeçalhos pré-compilados (Windows.h, imgui.h…)
#include "WindowsConsole.hpp"   // declaração da classe

 // ============================================================================
 // Inicialização dos membros estáticos
 // ============================================================================

bool WindowsConsole::s_visible = false;                      // começa invisível
int  WindowsConsole::s_hotkey = WINDOWS_CONSOLE_DEFAULT_KEY; // F1 por padrão
HWND WindowsConsole::s_hwnd = nullptr;                    // preenchido no init()

// ============================================================================
// init
// ============================================================================

/**
 * @brief Registra o hotkey, aloca o console externo e o esconde imediatamente.
 *
 * Chame uma única vez no início do wWinMain, antes do loop principal.
 * O console começa escondido — o usuário abre com o botão ImGui ou a tecla.
 *
 * @param hotkey Código de tecla virtual (VK_F1, VK_F2, etc.).
 */
void WindowsConsole::init(int hotkey) {
    s_hotkey = hotkey; // salva a tecla para ser verificada em poll_hotkey()

    AllocConsole(); // cria a janela de console do Windows associada ao processo

    // Redireciona os streams padrão para a janela recém-criada.
    // CONOUT$ é o nome especial Win32 para o buffer de saída do console.
    freopen_s(std::bit_cast<FILE**> stdout, "CONOUT$", "w", stdout); // redireciona stdout
    freopen_s(std::bit_cast<FILE**> stderr, "CONOUT$", "w", stderr); // redireciona stderr
    freopen_s(std::bit_cast<FILE**> stdin, "CONIN$", "r", stdin);  // redireciona stdin

    setlocale(LC_ALL, "utf-8"); // para suporte a caracteres acentuados no console
	SetConsoleOutputCP(CP_UTF8); // para entrada UTF-8 no console
	SetConsoleCP(CP_UTF8); // para saída UTF-8 no console
    SetConsoleTitle(L"My imgui aplication Windows Console"); // define o título da janela do console
     HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_FONT_INFOEX cfi{};
    cfi.cbSize = sizeof(cfi);

    // Obtém a configuração atual
    GetCurrentConsoleFontEx(hOut, FALSE, &cfi);

    // Altera propriedades específicas
    cfi.dwFontSize.X = NULL;                  // Largura (0 permite ajuste automático baseado na altura)
    cfi.dwFontSize.Y = 24;                 // Altura da fonte
    cfi.FontFamily = FF_DONTCARE;
    cfi.FontWeight = FW_MEDIUM;              // Define como negrito (700)

    wcscpy_s(cfi.FaceName, L"Lucida Console"); // Nome da fonte (Unicode)

    // Aplica a nova fonte
    SetCurrentConsoleFontEx(hOut, FALSE, &cfi);
    s_hwnd = GetConsoleWindow(); // obtém o HWND da janela para ShowWindow

    ShowWindow(s_hwnd, SW_HIDE); // começa escondido — usuário decide quando abrir
    s_visible = false;           // estado interno sincronizado com SW_HIDE
}

// ============================================================================
// shutdown
// ============================================================================

/**
 * @brief Fecha os streams redirecionados e libera o console externo.
 *
 * Deve ser chamado ao encerrar o programa, após sair do loop principal.
 * Após esta chamada, stdout/stderr voltam ao comportamento padrão.
 */
void WindowsConsole::shutdown() {
    if(s_hwnd) // só libera se o console foi alocado pelo init()
    {
        fclose(stdout); // fecha o stream redirecionado para CONOUT$
        fclose(stderr); // fecha o stream redirecionado para CONOUT$
        fclose(stdin);  // fecha o stream redirecionado para CONIN$
        FreeConsole();  // desassocia e destrói a janela de console do processo
        s_hwnd = nullptr; // limpa o handle para evitar uso após free
        s_visible = false;   // reseta o estado
    }
}

// ============================================================================
// open (privado)
// ============================================================================

/**
 * @brief Torna a janela do console externo visível.
 *
 * Usa SW_SHOW para exibir a janela já existente (sem recriar).
 * SetForegroundWindow traz o console para frente, mas o foco retorna
 * para a janela principal naturalmente (comportamento padrão do Win32).
 */
void WindowsConsole::open() {
    if(!s_hwnd) return;          // segurança: init() não foi chamado

    ShowWindow(s_hwnd, SW_SHOW);  // torna a janela visível na tela
    SetForegroundWindow(s_hwnd);  // traz para frente da pilha de janelas
    s_visible = true;             // atualiza o estado interno
}

// ============================================================================
// hide (privado)
// ============================================================================

/**
 * @brief Esconde a janela do console externo sem destruí-la.
 *
 * SW_HIDE remove a janela da tela mas mantém o handle e os streams válidos.
 * stdout/stderr continuam funcionando mesmo com o console invisível.
 */
void WindowsConsole::hide() {
    if(!s_hwnd) return;          // segurança: init() não foi chamado

    ShowWindow(s_hwnd, SW_HIDE);  // remove da tela, não destrói
    s_visible = false;            // atualiza o estado interno
}

// ============================================================================
// toggle
// ============================================================================

/**
 * @brief Alterna entre aberto e escondido com base no estado atual.
 *
 * Chamado tanto pelo botão ImGui (render_imgui_button) quanto pelo
 * poll_hotkey(). Decide a ação baseado em s_visible.
 */
void WindowsConsole::toggle() {
    if(s_visible) // se está visível, esconde
        hide();
    else           // se está escondido, abre
        open();
}

// ============================================================================
// render_imgui_button
// ============================================================================

/**
 * @brief Renderiza um botão ImGui que faz toggle do console externo.
 *
 * O label muda conforme o estado para dar feedback visual imediato.
 * Exemplo de uso dentro de uma janela ImGui:
 * @code
 *   ImGui::Begin("Debug");
 *   WindowsConsole::render_imgui_button();
 *   ImGui::End();
 * @endcode
 */
void WindowsConsole::render_imgui_button() {
    // Label reflete o estado atual — usuário sabe o que vai acontecer ao clicar
    const char* label = s_visible ? "Fechar Console Externo" : "Abrir Console Externo";

    if(ImGui::Button(label)) // retorna true apenas no frame em que foi clicado
        toggle();             // alterna o estado do console externo

    // Tooltip mostra o atalho de teclado configurado
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("Atalho: F%d", s_hotkey - VK_F1 + 1); // VK_F1=0x70, VK_F2=0x71…
}

// ============================================================================
// poll_hotkey
// ============================================================================

/**
 * @brief Detecta o pressionamento do hotkey e aciona o toggle.
 *
 * Chame uma vez por frame, antes de ImGui::Render():
 * @code
 *   WindowsConsole::poll_hotkey();
 *   ImGui::Render();
 * @endcode
 *
 * Usa GetAsyncKeyState com detecção de borda de subida (solto→pressionado)
 * para garantir que o toggle dispara apenas uma vez por pressionamento,
 * mesmo que a tecla fique pressionada por vários frames.
 */
void WindowsConsole::poll_hotkey() {
    // s_was_pressed persiste entre chamadas graças ao static
    static bool s_was_pressed = false;

    // Bit 0x8000: tecla pressionada agora (estado físico do teclado)
    const bool is_pressed = (GetAsyncKeyState(s_hotkey) & 0x8000) != 0;

    // Dispara apenas na transição solto→pressionado (evita toggle contínuo)
    if(is_pressed && !s_was_pressed)
        toggle();

    s_was_pressed = is_pressed; // guarda para comparar no próximo frame
}

// ============================================================================
// is_visible
// ============================================================================

/**
 * @brief Retorna se a janela do console externo está visível.
 * @return true se o console externo está aberto na tela.
 */
bool WindowsConsole::is_visible() {
    return s_visible; // leitura simples do membro estático
}
