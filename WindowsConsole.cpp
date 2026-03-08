/**
 * @file WindowsConsole.cpp
 * @brief Console externo com visual idêntico ao PowerShell 7 (pwsh.exe).
 *
 * ============================================================
 *  DIFERENÇA ENTRE POWERSHELL 5 E POWERSHELL 7
 * ============================================================
 *
 *  PowerShell 5 (Windows PowerShell — azul brilhante legado):
 *    Fundo  : BACKGROUND_BLUE | BACKGROUND_INTENSITY = índice 9 = #0037DA
 *    Texto  : FOREGROUND_RED | GREEN | BLUE | INTENSITY = branco puro
 *    Fonte  : Consolas 16px
 *    Paleta : conhost legado (cores ANSI básicas antigas)
 *
 *  PowerShell 7 (pwsh.exe — tema moderno One Half Dark):
 *    Fundo  : #012456  — navy muito escuro, quase preto
 *    Texto  : #CCCCCC  — cinzento claro (não branco puro)
 *    Fonte  : Cascadia Mono 12pt (distribuída com Windows Terminal)
 *    Paleta : One Half Dark — cores balanceadas, bom contraste
 *    Cursor : barra fina piscante (não o bloco cheio do PS5)
 *    ANSI   : ENABLE_VIRTUAL_TERMINAL_PROCESSING activo por padrão
 *
 *  Para replicar fielmente, é preciso:
 *    1. Definir a paleta de 16 cores via CONSOLE_SCREEN_BUFFER_INFOEX
 *    2. Usar índice 0 como fundo (mapeado para #012456 na paleta)
 *    3. Usar índice 7 como texto (mapeado para #CCCCCC na paleta)
 *    4. Activar VT processing para sequências ANSI modernas
 *    5. Cursor de barra fina via CONSOLE_CURSOR_INFO + SetConsoleCursorInfo
 *
 * ============================================================
 *  CASCADIA MONO — VERIFICAÇÃO VIA WinRT
 * ============================================================
 *
 *  EnsureCascadiaFont() usa a API WinRT Windows::Globalization::Fonts
 *  (via IFontInformation) para verificar se "Cascadia Mono" está instalada.
 *  Se não estiver, tenta encontrar o pacote via
 *  Windows::Management::Deployment::PackageManager (WinRT) —
 *  procura "Microsoft.WindowsTerminal" ou "CascadiaCode" nos pacotes
 *  instalados para obter o caminho da fonte.
 *
 *  Requer: #include <winrt/...> e linkagem com WindowsApp.lib
 *          Windows 10 1809+ (para WinRT PackageManager em apps não-packaged)
 *
 *  Fallback automático: Cascadia Code → Consolas (sempre disponível).
 *
 * ============================================================
 *  CTRL_CLOSE_EVENT — PORQUÊ FUNCIONA
 * ============================================================
 *
 *  SetConsoleCtrlHandler regista um handler chamado pelo Windows
 *  numa thread separada quando ocorre um evento de controlo.
 *
 *  Para CTRL_CLOSE_EVENT (clique no X):
 *    - Handler retorna FALSE → DefaultHandler termina o processo
 *    - Handler retorna TRUE  → evento tratado, processo continua
 *
 *  O "timeout de 5 segundos" aplica-se apenas se o handler ainda
 *  estiver a CORRER após 5s. Aqui fazemos ShowWindow + return TRUE
 *  em menos de 1ms — o Windows nunca inicia o timeout.
 */

#include "pch.hpp"
#include "WindowsConsole.hpp"
 #include<string.h>
// WinRT para verificação da fonte Cascadia Mono
// Requer /std:c++20 ou superior e linkagem com WindowsApp.lib
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>
// ============================================================================
// Definição dos membros estáticos
// ============================================================================

bool WindowsConsole::s_visible = false;
int  WindowsConsole::s_hotkey  = WINDOWS_CONSOLE_DEFAULT_KEY;
HWND WindowsConsole::s_hwnd    = nullptr;

// ============================================================================
// CtrlHandler
// ============================================================================

/**
 * @brief Interceta o clique no X da janela do console.
 *
 * Chamado pelo Windows numa thread do conhost quando ocorre um evento
 * de controlo. CTRL_CLOSE_EVENT é enviado exclusivamente quando o
 * utilizador fecha a janela do console (X, Alt+F4, ou barra de tarefas).
 *
 * ShowWindow(SW_HIDE) funciona cross-process via mensagem do kernel —
 * é a única operação de janela segura para uma janela de outro processo.
 *
 * @param ctrl_type  CTRL_CLOSE_EVENT, CTRL_C_EVENT, CTRL_BREAK_EVENT, etc.
 * @return TRUE para CTRL_CLOSE_EVENT (processo não termina); FALSE para o resto.
 */
BOOL WINAPI WindowsConsole::CtrlHandler(DWORD ctrl_type)
{
    if(ctrl_type == CTRL_CLOSE_EVENT)
    {
        // GetConsoleWindow() é thread-safe — pode ser chamado de qualquer thread
        const HWND hwnd = ::GetConsoleWindow();

        if(hwnd != nullptr)
        {
            // SW_HIDE cross-process: implementado via NtUserShowWindow do kernel
            // Remove a janela do ecrã sem destruir o console ou os seus buffers
            ::ShowWindow(hwnd, SW_HIDE);
            s_visible = false;
        }

        // TRUE: evento consumido. O Windows não chama o DefaultHandler.
        // O DefaultHandler chamaria ExitProcess — que NÃO é chamado.
        return TRUE;
    }

    // FALSE: deixa Ctrl+C, Ctrl+Break, etc. com o comportamento padrão
    return FALSE;
}

// ============================================================================
// EnsureCascadiaFont  (WinRT)
// ============================================================================

/**
 * @brief Verifica se Cascadia Mono está instalada via WinRT PackageManager.
 *
 * WinRT Windows::Management::Deployment::PackageManager permite enumerar
 * os pacotes instalados para o utilizador corrente, incluindo pacotes MSIX
 * como o Windows Terminal (que distribui Cascadia Mono e Cascadia Code).
 *
 * PORQUÊ WinRT E NÃO Win32:
 *   EnumFontFamiliesExW (GDI) verifica se a fonte está no sistema,
 *   mas não devolve o caminho do ficheiro .ttf para instalação manual.
 *   PackageManager.FindPackagesForUser() permite encontrar o pacote
 *   "Microsoft.WindowsTerminal" e obter o InstalledLocation onde as
 *   fontes estão em \CascadiaCode\ — possibilitando instalação via
 *   AddFontResourceExW se necessário.
 *
 * REQUISITOS:
 *   Windows 10 1809+ (para PackageManager em apps não-packaged)
 *   Link: WindowsApp.lib  (ou via #pragma comment(lib, "WindowsApp.lib"))
 *   Inclui: <winrt/Windows.Management.Deployment.h>
 *
 * @return true se "Cascadia Mono" ou "Cascadia Code" está disponível para uso.
 */
bool WindowsConsole::EnsureCascadiaFont()
{
    // ---- Passo 1: verifica via GDI se a fonte já está instalada --------------
    // EnumFontFamiliesExW: enumera as famílias de fontes instaladas no sistema
    // Se encontrar "Cascadia Mono", não precisamos de fazer mais nada.

    bool cascadia_mono_found  = false;
    bool cascadia_code_found  = false;

    // Callback para EnumFontFamiliesExW — chamado para cada fonte encontrada
    // Usa LOGFONTW com lfFaceName para comparar o nome da família
    auto enum_callback = [](const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM param) -> int
    {
        // param é um ponteiro para bool — sinaliza se a fonte foi encontrada
        bool* found = reinterpret_cast<bool*>(param);

        // Compara o nome da família (case-insensitive no Win32)
        if(::_wcsicmp(lf->lfFaceName, L"Cascadia Mono") == 0 ||
           ::_wcsicmp(lf->lfFaceName, L"Cascadia Code") == 0)
        {
            *found = true;
            return 0; // para a enumeração — fonte encontrada
        }
        return 1; // continua a enumerar
    };

    // Obtém um HDC da janela do console para a enumeração GDI
    const HDC hdc = ::GetDC(::GetConsoleWindow());

    if(hdc != nullptr)
    {
        LOGFONTW lf{};
        lf.lfCharSet = DEFAULT_CHARSET; // enumera todas as charsets

        // Tenta encontrar Cascadia Mono
        ::wcscpy_s(lf.lfFaceName, L"Cascadia Mono");
        ::EnumFontFamiliesExW(hdc, &lf,
            static_cast<FONTENUMPROCW>(enum_callback),
            reinterpret_cast<LPARAM>(&cascadia_mono_found), 0);

        if(!cascadia_mono_found)
        {
            // Fallback: tenta Cascadia Code
            ::wcscpy_s(lf.lfFaceName, L"Cascadia Code");
            ::EnumFontFamiliesExW(hdc, &lf,
                static_cast<FONTENUMPROCW>(enum_callback),
                reinterpret_cast<LPARAM>(&cascadia_code_found), 0);
        }

        ::ReleaseDC(::GetConsoleWindow(), hdc);
    }

    // Fonte já instalada — não precisa do WinRT
    if(cascadia_mono_found || cascadia_code_found)
        return true;

    // ---- Passo 2: procura via WinRT PackageManager --------------------------
    // Tenta encontrar o pacote do Windows Terminal que contém as fontes Cascadia
    // Requer Windows 10 1809+ e WindowsApp.lib

    try
    {
        // Inicializa o WinRT apartment — MTA (multi-threaded apartment)
        // init_apartment é idempotente se já foi chamado nesta thread
        winrt::init_apartment(winrt::apartment_type::multi_threaded);

        // PackageManager permite enumerar pacotes instalados para o utilizador
        winrt::Windows::Management::Deployment::PackageManager pkg_mgr;

        // FindPackagesForUser("") = utilizador corrente
        // Itera em busca do pacote do Windows Terminal (distribuidor das fontes)
        for(auto const& pkg : pkg_mgr.FindPackagesForUser(L""))
        {
            const std::wstring pkg_name{ pkg.Id().Name() };

            // Windows Terminal distribui Cascadia Mono e Cascadia Code
            // O nome do pacote pode variar ligeiramente entre versões
            const bool is_terminal =
                pkg_name.find(L"WindowsTerminal")  != std::wstring::npos ||
                pkg_name.find(L"CascadiaCode")      != std::wstring::npos ||
                pkg_name.find(L"Microsoft.Terminal") != std::wstring::npos;

            if(!is_terminal) continue;

            // InstalledLocation: pasta raiz do pacote
            // As fontes estão tipicamente em <pkg_root>\CascadiaCode\

            const std::wstring install_path{
                pkg.InstalledLocation().Path()};
                

            // Caminhos conhecidos das fontes dentro do pacote
            const std::wstring candidates[] = {
                install_path + L"\\CascadiaCode\\CascadiaMonoPL.ttf",
                install_path + L"\\CascadiaCode\\CascadiaMono.ttf",
                install_path + L"\\CascadiaCode\\CascadiaCodePL.ttf",
                install_path + L"\\CascadiaCode\\CascadiaCode.ttf",
            };

            for(const auto& font_path : candidates)
            {
                // Verifica se o ficheiro existe antes de tentar carregar
                if(::GetFileAttributesW(font_path.c_str()) == INVALID_FILE_ATTRIBUTES)
                    continue;

                // AddFontResourceExW: carrega a fonte para a sessão corrente
                // FR_PRIVATE: visível apenas para este processo
                if(::AddFontResourceExW(font_path.c_str(), FR_PRIVATE, nullptr) > 0)
                {
                    // Notifica o sistema que uma nova fonte foi adicionada
                    ::SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE,
                                         0, 0, SMTO_ABORTIFHUNG, 100, nullptr);
                    return true; // fonte carregada com sucesso
                }
            }
        }
    }
    catch(const winrt::hresult_error&)
    {
        // WinRT não disponível ou pacote não encontrado — usa fallback silenciosamente
    }

    return false; // fonte não encontrada — ApplyPowerShell7Style usa Consolas
}

// ============================================================================
// ApplyPowerShell7Style
// ============================================================================

/**
 * @brief Configura o console para ser visualmente idêntico ao PowerShell 7.
 *
 * PALETA "ONE HALF DARK" (tema padrão do PowerShell 7 / Windows Terminal):
 *   Os 16 índices da tabela de cores são substituídos pelos valores exactos
 *   do tema One Half Dark usado pelo Windows Terminal.
 *   As cores seguem o formato COLORREF Win32: 0x00BBGGRR (BGR, não RGB).
 *
 *   Índice 0  = fundo padrão = #282C34 (cinzento muito escuro)
 *   Índice 4  = azul escuro  = #012456 (navy do PowerShell — usado como fundo)
 *   Índice 7  = texto padrão = #DCDFE4 (cinzento claro)
 *   Índice 15 = branco       = #FFFFFF
 *
 * FUNDO E TEXTO:
 *   wAttributes com background = índice 0 (navy #012456) e texto = índice 7
 *   O PowerShell 7 usa especificamente este esquema de cores.
 *
 * CURSOR DE BARRA FINA:
 *   O PowerShell 7 usa cursor de barra fina (dwSize=25) em vez do bloco
 *   cheio do PowerShell 5 (dwSize=100).
 *
 * MODO VIRTUAL TERMINAL:
 *   ENABLE_VIRTUAL_TERMINAL_PROCESSING: suporte a sequências ANSI/VT100.
 *   DISABLE_NEWLINE_AUTO_RETURN: controlo explícito de \r\n.
 *   Activo por padrão no PowerShell 7.
 */
void WindowsConsole::ApplyPowerShell7Style()
{
    const HANDLE h_out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if(h_out == INVALID_HANDLE_VALUE) return;

    // ---- Título ---------------------------------------------------------------
    ::SetConsoleTitleW(L"Imgui WIndows Console");

    // ---- Dimensão: buffer ANTES da janela (evita erro "janela > buffer") ------
    constexpr SHORT COLS     = 120;  // colunas
    constexpr SHORT ROWS     = 30;   // linhas visíveis
    constexpr SHORT BUF_ROWS = 9001; // buffer clássico do PowerShell (>9000)

    // Define o buffer de scroll primeiro
    const COORD buf_size{ COLS, BUF_ROWS };
    ::SetConsoleScreenBufferSize(h_out, buf_size);

    // Define a área visível (SMALL_RECT — coordenadas inclusivas)
    const SMALL_RECT win_rect{
        0, 0,
        static_cast<SHORT>(COLS - 1),
        static_cast<SHORT>(ROWS - 1) };
    ::SetConsoleWindowInfo(h_out, TRUE, &win_rect);

    // ---- Paleta One Half Dark -------------------------------------------------
    // Todos os valores em COLORREF Win32: 0x00BBGGRR
    CONSOLE_SCREEN_BUFFER_INFOEX sbi{};
    sbi.cbSize = sizeof(sbi);

    if(::GetConsoleScreenBufferInfoEx(h_out, &sbi))
    {
        // One Half Dark — valores exactos do Windows Terminal
        // Convertidos de #RRGGBB → 0x00BBGGRR
        sbi.ColorTable[0]  = 0x00342B28; // #282C34 — preto/fundo escuro
        sbi.ColorTable[1]  = 0x00514BC2; // #C24B51 — vermelho
        sbi.ColorTable[2]  = 0x0053A598; // #98A553 — verde
        sbi.ColorTable[3]  = 0x0039A0D5; // #D5A039 — amarelo/dourado
        sbi.ColorTable[4]  = 0x00AC6561; // #6165AC — azul
        sbi.ColorTable[5]  = 0x00B46DB4; // #B46DB4 — magenta
        sbi.ColorTable[6]  = 0x0098A556; // #56A598 — ciano
        sbi.ColorTable[7]  = 0x00E4DFDC; // #DCDFE4 — branco/texto padrão
        sbi.ColorTable[8]  = 0x005C5450; // #50545C — cinzento escuro (bright black)
        sbi.ColorTable[9]  = 0x00514BC2; // #C24B51 — vermelho claro
        sbi.ColorTable[10] = 0x0053A598; // #98A553 — verde claro
        sbi.ColorTable[11] = 0x0039A0D5; // #D5A039 — amarelo claro
        sbi.ColorTable[12] = 0x00AC6561; // #6165AC — azul claro
        sbi.ColorTable[13] = 0x00B46DB4; // #B46DB4 — magenta claro
        sbi.ColorTable[14] = 0x0098A556; // #56A598 — ciano claro
        sbi.ColorTable[15] = 0x00FFFFFF; // #FFFFFF — branco puro

        // Índice 0 = fundo (navy escuro #282C34 mapeado para preto da paleta)
        // Texto = índice 7 = #DCDFE4 (cinzento claro — texto padrão PS7)
        // Formato: bits 0-3 = texto (nibble baixo), bits 4-7 = fundo (nibble alto)
        // Índice 0 como fundo = nenhum bit BACKGROUND activo = 0x00 no nibble alto
        // Índice 7 como texto = FOREGROUND_RED|GREEN|BLUE = 0x07
        sbi.wAttributes =
            FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // texto índice 7

        // Mantém o rect para evitar que a API encolha a janela
        sbi.srWindow = win_rect;

        ::SetConsoleScreenBufferInfoEx(h_out, &sbi);
    }

    // ---- Fonte: Cascadia Mono → Cascadia Code → Consolas (fallback) ----------
    CONSOLE_FONT_INFOEX cfi{};
    cfi.cbSize       = sizeof(cfi);
    cfi.dwFontSize.X = 0;   // largura: automática
    cfi.dwFontSize.Y = 36;  // 16px ≈ 12pt em 96 DPI (tamanho padrão PS7)
    cfi.FontFamily   = FF_DONTCARE;
    cfi.FontWeight   = FW_NORMAL;

    // Cascadia Mono — fonte oficial do PowerShell 7 (sem ligaduras no modo texto)
    ::wcscpy_s(cfi.FaceName, L"Cascadia Mono");
    if(!::SetCurrentConsoleFontEx(h_out, FALSE, &cfi))
    {
        // Cascadia Code — inclui ligaduras (usada pelo Windows Terminal)
        ::wcscpy_s(cfi.FaceName, L"Cascadia Code");
        if(!::SetCurrentConsoleFontEx(h_out, FALSE, &cfi))
        {
            // Consolas — disponível em todas as versões do Windows desde Vista
            ::wcscpy_s(cfi.FaceName, L"Consolas");
            ::SetCurrentConsoleFontEx(h_out, FALSE, &cfi);
        }
    }

    // ---- Cursor de barra fina (PowerShell 7) vs bloco cheio (PS5) ------------
    CONSOLE_CURSOR_INFO cursor_info{};
    cursor_info.dwSize   = 25;   // 25% = barra fina; 100% = bloco cheio (PS5)
    cursor_info.bVisible = TRUE; // cursor visível
    ::SetConsoleCursorInfo(h_out, &cursor_info);

    // ---- Modo Virtual Terminal (ANSI escape codes) ---------------------------
    DWORD console_mode = 0;
    if(::GetConsoleMode(h_out, &console_mode))
    {
        ::SetConsoleMode(h_out,
            console_mode                       |
            ENABLE_VIRTUAL_TERMINAL_PROCESSING | // \033[32m etc.
            DISABLE_NEWLINE_AUTO_RETURN);         // \n não adiciona \r automaticamente
    }

    // ---- Limpa o ecrã com as novas cores ------------------------------------
    // Sem cls, a área anterior fica com as cores antigas do conhost
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if(::GetConsoleScreenBufferInfo(h_out, &info))
    {
        const DWORD total =
            static_cast<DWORD>(info.dwSize.X) *
            static_cast<DWORD>(info.dwSize.Y);
        const COORD origin{ 0, 0 };
        DWORD written = 0;

        // Preenche com espaços brancos usando os novos atributos de cor
        ::FillConsoleOutputCharacterW(h_out, L' ',           total, origin, &written);
        ::FillConsoleOutputAttribute (h_out, sbi.wAttributes, total, origin, &written);

        // Reposiciona o cursor no topo
        ::SetConsoleCursorPosition(h_out, origin);
    }
}

// ============================================================================
// init
// ============================================================================

/**
 * @brief Inicializa o console com visual PowerShell 7.
 *
 * Sequência:
 *   1. AllocConsole()              — cria a janela de console
 *   2. freopen_s × 3               — redireciona stdout/stderr/stdin
 *   3. setlocale + CP_UTF8         — suporte a UTF-8
 *   4. EnsureCascadiaFont()        — verifica/carrega Cascadia Mono via WinRT
 *   5. ApplyPowerShell7Style()     — visual completo PS7
 *   6. SetConsoleCtrlHandler()     — X → esconde em vez de fechar
 *   7. GetConsoleWindow()          — obtém HWND para ShowWindow
 *   8. ShowWindow(SW_HIDE)         — começa escondido
 *
 * @param hotkey  Tecla VK_* para toggle. Padrão: VK_F1.
 */
MyResult WindowsConsole::init(int hotkey)
{
    s_hotkey = hotkey;

    // Cria a janela de console do Windows associada ao processo corrente
    ::AllocConsole();

    // Redireciona stdout, stderr e stdin para a nova janela de console
    freopen_s(std::bit_cast<FILE**>(stdout), "CONOUT$", "w", stdout);
    freopen_s(std::bit_cast<FILE**>(stderr), "CONOUT$", "w", stderr);
    freopen_s(std::bit_cast<FILE**>(stdin),  "CONIN$",  "r", stdin);

    // UTF-8: caracteres acentuados, emoji e texto internacional
    setlocale(LC_ALL, "pt_PT.UTF-8");
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);

    // Tenta garantir que Cascadia Mono está disponível antes de configurar a fonte
    // EnsureCascadiaFont() usa WinRT PackageManager — falha silenciosamente se
    // não estiver disponível e ApplyPowerShell7Style() usa Consolas como fallback
    EnsureCascadiaFont();

    // Aplica o visual completo do PowerShell 7
    ApplyPowerShell7Style();

    // Instala o handler que interceta CTRL_CLOSE_EVENT (botão X)
    // TRUE = adiciona à cadeia de handlers (não substitui os existentes)
    //::SetConsoleCtrlHandler(CtrlHandler, TRUE);

    // Obtém o HWND da janela criada por AllocConsole() — necessário para ShowWindow
    s_hwnd = ::GetConsoleWindow();
    long style = GetWindowLong(s_hwnd, GWL_STYLE);

        // Remove a barra de título (WS_CAPTION) e a borda grossa (WS_THICKFRAME)
        // Isso deixa apenas o "corpo" do console aparecendo
        style &= WS_CAPTION;
        style &= ~WS_THICKFRAME; 

        // Aplica o novo estilo
        SetWindowLong(s_hwnd, GWL_STYLE, style);

        // Força a atualização da janela para aplicar as mudanças
        SetWindowPos(s_hwnd, NULL, 0, 0, 0, 0, 
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    // Começa escondido — utilizador abre com F1 ou botão ImGui
    ::ShowWindow(s_hwnd, SW_HIDE);
    s_visible = false;

    return MyResult::ok;
}

// ============================================================================
// shutdown
// ============================================================================

/**
 * @brief Remove o handler, fecha os streams e libera o console.
 *
 * Ordem:
 *   1. SetConsoleCtrlHandler(FALSE) — remove da cadeia
 *   2. fclose × 3                  — fecha streams
 *   3. FreeConsole()               — destrói a janela de console
 */
MyResult WindowsConsole::shutdown()
{
    if(s_hwnd == nullptr)
        return MyResult::ok;

    // Remove o handler da cadeia — FALSE = remover (TRUE = adicionar)
    ::SetConsoleCtrlHandler(CtrlHandler, FALSE);

    // Fecha os streams redirecionados para CONOUT$ e CONIN$
    fclose(stdout);
    fclose(stderr);
    fclose(stdin);

    // Desassocia e destrói a janela de console do processo
    ::FreeConsole();

    s_hwnd    = nullptr;
    s_visible = false;

    return MyResult::ok;
}

// ============================================================================
// open / hide / toggle
// ============================================================================

/**
 * @brief Mostra a janela do console e traz para foreground.
 * ShowWindow cross-process funciona via mensagem do kernel.
 */
void WindowsConsole::open()
{
    if(s_hwnd == nullptr) return;

    ::ShowWindow(s_hwnd, SW_SHOW);
    ::SetForegroundWindow(s_hwnd);
    s_visible = true;
}

/**
 * @brief Esconde a janela sem destruir o console ou os seus buffers.
 * stdout/stderr continuam a funcionar mesmo com a janela invisível.
 */
void WindowsConsole::hide()
{
    if(s_hwnd == nullptr) return;

    ::ShowWindow(s_hwnd, SW_HIDE);
    s_visible = false;
}

/** @brief Alterna visibilidade com base no estado actual. */
void WindowsConsole::toggle()
{
    if(s_visible)
        hide();
    else
        open();
}

// ============================================================================
// render_imgui_button / poll_hotkey / is_visible
// ============================================================================

/**
 * @brief Botão ImGui de toggle. Label reflecte o estado actual.
 */
void WindowsConsole::render_imgui_button()
{
    const char* label = s_visible ? "Fechar Console Externo" : "Abrir Console Externo";

    if(ImGui::Button(label))
        toggle();

    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("Atalho: F%d", s_hotkey - VK_F1 + 1);
}

/**
 * @brief Detecção de hotkey com borda de subida (evita toggle contínuo).
 * Chame uma vez por frame antes de ImGui::Render().
 */
void WindowsConsole::poll_hotkey()
{
    static bool s_was_pressed = false;

    const bool is_pressed = (::GetAsyncKeyState(s_hotkey) & 0x8000) != 0;

    if(is_pressed && !s_was_pressed)
        toggle();

    s_was_pressed = is_pressed;
}

/**
 * @brief Retorna true se a janela do console está visível.
 */
bool WindowsConsole::is_visible()
{
    return s_visible;
}