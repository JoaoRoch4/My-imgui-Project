#pragma once
#include "pch.hpp"

/**
 * @brief Metadata for a built-in console command (name + description).
 *
 * Uses std::wstring_view so names and descriptions are wide-char literals
 * (L"CLEAR", L"Limpa…") with no heap allocation.
 */
struct CommandDefinition {
    std::wstring_view name;        ///< Wide command name,  e.g. L"CLEAR"
    std::wstring_view description; ///< Wide description shown by HELP
};

/**
 * @brief ImGui debug console.
 *
 * All user-visible text (log items, history, commands, input buffer) is stored
 * as wchar_t so the full Unicode range — including emoji — is natively
 * representable as individual characters rather than multi-byte sequences.
 *
 * ImGui's InputText only accepts charUTF-8, so a small UTF-8 staging buffer
     * (InputBufUtf8) is kept alongside the wchar_t InputBuf and the two are
     * synchronised on each frame via WideToUtf8 / Utf8ToWide.
     */
class Console {
public:
    // -------------------------------------------------------------------------
    // Built-in command table  (compile-time, read-only)
    // -------------------------------------------------------------------------

    /** @brief Compile-time array of built-in commands with wide-char metadata. */
    static constexpr std::array<CommandDefinition, 4> BuiltInCommands = { {
        { L"CLEAR", L"Limpa todo o texto do log." },
        { L"HELP", L"Exibe a lista de comandos disponiveis e suas descricoes." },
        { L"HISTORY", L"Mostra o historico de comandos digitados recentemente." },
        { L"EXIT", L"Fecha o programa" }
        } };

    // -------------------------------------------------------------------------
    // Public runtime dispatch table
    // -------------------------------------------------------------------------

    /** @brief Maps UPPERCASE wide command name -> callable. */
    std::map<std::wstring, std::function<void()>> DispatchTable;

    /** @brief Maps UPPERCASE wide command name -> wide help description. */
    std::map<std::wstring, std::wstring> HelpDescriptions;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    Console();
    ~Console();

    // -------------------------------------------------------------------------
    // Font / Atlas  (call BEFORE first ImGui::NewFrame)
    // -------------------------------------------------------------------------

    /**
     * @brief Auto-detects and merges the platform emoji font into the ImGui atlas.
     *
     * Probes known system font paths in order:
     *  - Windows : Segoe UI Emoji  (%WINDIR%\Fonts\seguiemj.ttf)
     *  - macOS   : Apple Color Emoji
     *  - Linux   : Noto Color Emoji / Noto Emoji / Symbola (several paths)
     *
     * Call ONCE: AFTER ImGui::CreateContext() + backend Init, BEFORE NewFrame().
     *
     * @code
     *   ImGui::CreateContext();
     *   ImGui_ImplXXX_Init(...);
     *   Console::LoadEmojiFont();   // no path needed
     * @endcode
     *
     * @param font_size  Pixel size for both base font and the emoji overlay.
     */
     /**
      * @brief Returns the ImWchar glyph-range table for emoji/symbol codepoints.
      *
      * Pass this to ImFontAtlas::AddFontFromFileTTF() as the glyph_ranges parameter
      * whenever you load an emoji font through an external font manager.
      * The array is statically allocated (program lifetime) so the pointer is
      * always valid until the application exits.
      *
      * Ranges covered:
      *  U+00A0-U+00FF  Latin-1 Supplement
      *  U+2000-U+27BF  General Punctuation + Misc Symbols (checkmark, warning...)
      *  U+2B00-U+2BFF  Misc Symbols and Arrows
      *  U+1F300-U+1F9FF Emoji block (rocket, target, key...)
      *
      * @code
      *   // In your FontManager or main.cpp:
      *   ImFontConfig cfg;
      *   cfg.MergeMode = true;
      *   io.Fonts->AddFontFromFileTTF(emoji_path, size, &cfg,
      *       Console::GetEmojiGlyphRanges()); // <-- pass this
      * @endcode
      *
      * @return Pointer to a static sentinel-terminated ImWchar[] array.
      */
    static const ImWchar* GetEmojiGlyphRanges();

    /**
     * @brief Loads the emoji font automatically (self-contained, no FontManager needed).
     *
     * Use this ONLY if you are NOT using a FontManager.  If you have a FontManager,
     * use GetEmojiGlyphRanges() instead and pass the result to your FontManager so
     * all fonts share the same atlas.
     */
    static void LoadEmojiFont(float font_size = 16.0f);

    // -------------------------------------------------------------------------
    // Command registration
    // -------------------------------------------------------------------------

    /** @brief Registers a built-in command (name must exist in BuiltInCommands). */
    void RegisterBuiltIn(std::wstring_view name, std::function<void()> func);

    /** @brief Registers a command with no description. */
    void RegisterCommand(const std::wstring& name, std::function<void()> func);

    /** @brief Registers a command with a wide description shown by HELP. */
    void RegisterCommand(const std::wstring& name,
        const std::wstring& desc,
        std::function<void()> func);

        /**
 * @brief Registra um comando que recebe argumentos em tempo de execução.
 *
 * O handler recebe um std::vector<std::wstring> com os tokens que
 * seguem o nome do comando.
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
 * @brief Sobrecarga com descrição larga + handler com argumentos.
 */
void RegisterCommand(const std::wstring& name,
                     const std::wstring& desc,
                     std::function<void(std::vector<std::wstring>)> func);

    // -------------------------------------------------------------------------
    // Log helpers  (all accept wide-char text)
    // -------------------------------------------------------------------------

    /** @brief Clears all log items and frees their memory. */
    void ClearLog();

    /**
     * @brief Formats a wide string (wprintf-style) and appends it to the log.
     * @param fmt  Wide format string, e.g. L"Value: %d  Emoji: %ls".
     */
    void AddLog(const wchar_t* fmt, ...);

    /**
     * @brief Appends a log line prefixed with a wide-char emoji.
     * @param emoji  Wide emoji, e.g. L"\U0001F680" or L"🚀".
     * @param fmt    Wide printf format string for the message body.
     */
    void AddLogWithEmoji(const wchar_t* emoji, const wchar_t* fmt, ...);

    /** @brief Returns a random wide-char emoji from an internal table. */
    std::wstring GetRandomEmoji() const;

    // -------------------------------------------------------------------------
    // Rendering
    // -------------------------------------------------------------------------

    /** @brief Draws the console window.  Call every frame. */
    void Draw(const wchar_t* title, bool* p_open);

    /**
     * @brief Executes a wide-char command string (history + dispatch).
     * @param command_line  Wide command typed by the user.
     */
    void ExecCommand(const wchar_t* command_line);

private:
    // -------------------------------------------------------------------------
    // Internal state  (all text is wchar_t)
    // -------------------------------------------------------------------------

    wchar_t                  InputBuf[512];      ///< Wide-char input (one codepoint per element)
    char                     InputBufUtf8[2048]; ///< UTF-8 staging buffer for ImGui::InputText
    ImVector<wchar_t*>       Items;              ///< Heap-allocated wide log lines
    ImVector<const wchar_t*> Commands;           ///< Wide autocomplete command pointers
    ImVector<wchar_t*>       History;            ///< Wide command history entries
    int                      HistoryPos;         ///< -1 = new line; 0..N = browsing history
    ImGuiTextFilter          Filter;             ///< Filter widget (UTF-8 internally)
    bool                     AutoScroll;         ///< Auto-scroll to bottom flag
    bool                     ScrollToBottom;     ///< Force scroll-to-bottom next frame

    // -------------------------------------------------------------------------
    // Wide-char portable string helpers
    // -------------------------------------------------------------------------

    static int      Wcsicmp(const wchar_t* s1, const wchar_t* s2);
    static int      Wcsnicmp(const wchar_t* s1, const wchar_t* s2, int n);
    static wchar_t* Wcsdup(const wchar_t* s);
    static void     Wcstrim(wchar_t* s);

    // -------------------------------------------------------------------------
    // Encoding helpers  (wchar_t <-> UTF-8 for ImGui boundary)
    // -------------------------------------------------------------------------

    /** @brief Converts a null-terminated wchar_t string to a UTF-8 std::string. */
    static std::string  WideToUtf8(const wchar_t* wstr);

    /** @brief Converts a null-terminated UTF-8 char string to a std::wstring. */
    static std::wstring Utf8ToWide(const char* str);

    // -------------------------------------------------------------------------
    // ImGui InputText callback plumbing
    // -------------------------------------------------------------------------

    static int TextEditCallbackStub(ImGuiInputTextCallbackData* data);
    int        TextEditCallback(ImGuiInputTextCallbackData* data);

    // -------------------------------------------------------------------------
    // Color-tag renderer  (Termícolor)
    // -------------------------------------------------------------------------

    ImVec4 ParseColor(const wchar_t* start, const wchar_t* end);
    void   RenderTermicolor(const wchar_t* text);
};
