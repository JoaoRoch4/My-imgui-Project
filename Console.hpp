#pragma once
#include "pch.hpp"

/**
 * @brief Metadados de um comando interno (nome + descrição em wide-char).
 *
 * Usa std::wstring_view para que nomes e descrições sejam literais wide
 * sem alocação de heap.
 */
struct CommandDefinition {
    std::wstring_view name;        ///< Nome largo, ex.: L"CLEAR"
    std::wstring_view description; ///< Descrição exibida pelo HELP
};

/**
 * @brief Console ImGui de debug — wide-char edition.
 *
 * Todo texto visível (itens de log, histórico, comandos, buffer de entrada)
 * é armazenado como wchar_t para que o range Unicode completo — incluindo
 * emoji — seja representável como caracteres individuais.
 *
 * ESCALA DE FONTE EXCLUSIVA DO CONSOLE
 * ---------------------------------------
 * m_font_scale controla ImGui::SetWindowFontScale() APENAS dentro da região
 * de scroll do console.  O tamanho global da UI não é afetado.
 * Botões "A+" / "A-" / "A=" na toolbar ajustam o valor em runtime.
 * Limites: [CONSOLE_FONT_SCALE_MIN, CONSOLE_FONT_SCALE_MAX].
 *
 * DESPACHO DE COMANDOS COM ARGUMENTOS
 * -------------------------------------
 * ExecCommand extrai apenas o PRIMEIRO token da linha como chave de lookup.
 * O wrapper gerado por RegisterCommand(name, func<args>) tokeniza a linha
 * inteira e passa os tokens subsequentes ao handler real.
 *
 *   Usuário digita: "theme dark"
 *   ExecCommand: chave = L"THEME"  → encontra a lambda wrapper
 *   wrapper:     args  = { L"dark" } → repassa ao handler
 */
class Console {
public:

    // =========================================================================
    // Constantes de escala de fonte do console
    // =========================================================================

    static constexpr float CONSOLE_FONT_SCALE_MIN  = 0.5f;  ///< Escala mínima (50%)
    static constexpr float CONSOLE_FONT_SCALE_MAX  = 3.0f;  ///< Escala máxima (300%)
    static constexpr float CONSOLE_FONT_SCALE_STEP = 0.1f;  ///< Incremento por clique

    // =========================================================================
    // Tabela de comandos internos (compile-time, somente leitura)
    // =========================================================================

    /** @brief Array constexpr de comandos internos com metadados wide-char. */
    static constexpr std::array<CommandDefinition, 4> BuiltInCommands = { {
        { L"CLEAR",   L"Limpa todo o texto do log."                             },
        { L"HELP",    L"Exibe a lista de comandos disponíveis e suas descrições."},
        { L"HISTORY", L"Mostra o histórico de comandos digitados recentemente." },
        { L"EXIT",    L"Fecha o programa."                                       }
    } };

    // =========================================================================
    // Tabelas de despacho em runtime
    // =========================================================================

    /** @brief Mapeia nome UPPERCASE → callable void(). */
    std::map<std::wstring, std::function<void()>> DispatchTable;

    /** @brief Mapeia nome UPPERCASE → descrição wide para o HELP. */
    std::map<std::wstring, std::wstring> HelpDescriptions;

    // =========================================================================
    // Ciclo de vida
    // =========================================================================

    Console();
    ~Console();

    // =========================================================================
    // Font / Atlas  (chamar ANTES do primeiro ImGui::NewFrame)
    // =========================================================================

    /**
     * @brief Retorna a tabela de ranges ImWchar para emoji e símbolos.
     *
     * Passe o retorno como 4º parâmetro de AddFontFromFileTTF.
     * O ponteiro tem duração estática — sempre válido até o fim do programa.
     */
    static const ImWchar* GetEmojiGlyphRanges();

    /**
     * @brief Auto-detecta e mescla a fonte emoji do sistema no atlas ImGui.
     *
     * Use APENAS se não houver FontManager.  Se houver, use GetEmojiGlyphRanges()
     * e passe o resultado ao FontManager para que todos os slots compartilhem
     * o mesmo atlas.
     *
     * @param font_size  Tamanho em pixels para a fonte base e o overlay emoji.
     */
    static void LoadEmojiFont(float font_size = 16.0f);

    // =========================================================================
    // Registro de comandos
    // =========================================================================

    /** @brief Registra um comando interno (nome deve existir em BuiltInCommands). */
    void RegisterBuiltIn(std::wstring_view name, std::function<void()> func);

    /** @brief Registra um comando sem descrição, sem argumentos. */
    void RegisterCommand(const std::wstring& name,
                         std::function<void()> func);

    /** @brief Registra um comando com descrição wide, sem argumentos. */
    void RegisterCommand(const std::wstring& name,
                         const std::wstring& desc,
                         std::function<void()> func);

    /**
     * @brief Registra um comando cujo handler recebe argumentos em runtime.
     *
     * O handler recebe um std::vector<std::wstring> com os tokens que seguem
     * o nome do comando na linha digitada pelo usuário.
     *
     *   "theme dark"  → args = { L"dark" }
     *   "theme"       → args = {}
     *
     * @param name  Nome largo do comando (qualquer capitalização).
     * @param func  Callable void(std::vector<std::wstring> args).
     */
    void RegisterCommand(const std::wstring& name,
                         std::function<void(std::vector<std::wstring>)> func);

    /**
     * @brief Registra um comando com descrição wide + handler com argumentos.
     *
     * @param name  Nome largo do comando.
     * @param desc  Descrição larga exibida pelo HELP.
     * @param func  Callable void(std::vector<std::wstring> args).
     */
    void RegisterCommand(const std::wstring& name,
                         const std::wstring& desc,
                         std::function<void(std::vector<std::wstring>)> func);

    // =========================================================================
    // Helpers de log (todos aceitam texto wide-char)
    // =========================================================================

    /** @brief Libera todos os itens de log e limpa o vetor. */
    void ClearLog();

    /**
     * @brief Formata uma string larga (estilo wprintf) e adiciona ao log.
     * @param fmt  String de formato wide, ex.: L"Valor: %d  Emoji: %ls".
     */
    void AddLog(const wchar_t* fmt, ...);

    /**
     * @brief Adiciona uma linha de log prefixada com um emoji wide-char.
     * @param emoji  Emoji wide, ex.: L"\U0001F680" ou L"🚀".
     * @param fmt    String de formato wide para o corpo da mensagem.
     */
    void AddLogWithEmoji(const wchar_t* emoji, const wchar_t* fmt, ...);

    /** @brief Retorna um emoji wide-char aleatório de uma tabela interna. */
    std::wstring GetRandomEmoji() const;

    // =========================================================================
    // Escala de fonte exclusiva do console
    // =========================================================================

    /**
     * @brief Aumenta a escala de fonte do console em CONSOLE_FONT_SCALE_STEP.
     *
     * Clampeado a CONSOLE_FONT_SCALE_MAX.
     * Não afeta o ImGuiStyle global — apenas SetWindowFontScale() interno.
     */
    void IncreaseFontScale();

    /**
     * @brief Diminui a escala de fonte do console em CONSOLE_FONT_SCALE_STEP.
     *
     * Clampeado a CONSOLE_FONT_SCALE_MIN.
     */
    void DecreaseFontScale();

    /** @brief Restaura a escala de fonte do console para 1.0 (100%). */
    void ResetFontScale();

    /** @brief Retorna a escala de fonte atual do console [0.5, 3.0]. */
    [[nodiscard]] float GetFontScale() const noexcept;

    // =========================================================================
    // Renderização
    // =========================================================================

    /** @brief Desenha a janela do console. Chame uma vez por frame. */
    void Draw(const wchar_t* title, bool* p_open);

    /**
     * @brief Executa uma string de comando wide-char (histórico + despacho).
     *
     * Extrai o PRIMEIRO token como chave de lookup no DispatchTable.
     * Os tokens seguintes ficam disponíveis para handlers registrados com
     * a sobrecarga void(std::vector<std::wstring>).
     *
     * @param command_line  Comando digitado pelo usuário (wide-char).
     */
    void ExecCommand(const wchar_t* command_line);

private:

    // =========================================================================
    // Estado interno (todo texto em wchar_t)
    // =========================================================================

    wchar_t                  InputBuf[512];      ///< Buffer de entrada wide-char
    char                     InputBufUtf8[2048]; ///< Buffer UTF-8 para ImGui::InputText
    ImVector<wchar_t*>       Items;              ///< Linhas de log alocadas no heap
    ImVector<const wchar_t*> Commands;           ///< Ponteiros wide para autocomplete
    ImVector<wchar_t*>       History;            ///< Histórico de comandos wide
    int                      HistoryPos;         ///< -1 = nova linha; ≥0 = navegando
    ImGuiTextFilter          Filter;             ///< Widget de filtro (UTF-8 interno)
    bool                     AutoScroll;         ///< Auto-scroll para o final
    bool                     ScrollToBottom;     ///< Força scroll no próximo frame

    float                    m_font_scale;       ///< Escala de fonte exclusiva do console

    // =========================================================================
    // Helpers de string wide-char portáveis
    // =========================================================================

    static int      Wcsicmp (const wchar_t* s1, const wchar_t* s2);
    static int      Wcsnicmp(const wchar_t* s1, const wchar_t* s2, int n);
    static wchar_t* Wcsdup  (const wchar_t* s);
    static void     Wcstrim (wchar_t* s);

    // =========================================================================
    // Helpers de encoding (wchar_t ↔ UTF-8 na fronteira com ImGui)
    // =========================================================================

    static std::string  WideToUtf8(const wchar_t* wstr);
    static std::wstring Utf8ToWide(const char*    str);

    // =========================================================================
    // Callback do InputText
    // =========================================================================

    static int TextEditCallbackStub(ImGuiInputTextCallbackData* data);
    int        TextEditCallback    (ImGuiInputTextCallbackData* data);

    // =========================================================================
    // Renderizador de tags de cor (Termícolor)
    // =========================================================================

    ImVec4 ParseColor      (const wchar_t* start, const wchar_t* end);
    void   RenderTermicolor(const wchar_t* text);
};