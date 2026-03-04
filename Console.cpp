/**
 * @file Console.cpp
 * @brief Implementation of the ImGui debug console — wide-char edition.
 *
 * TEXT MODEL
 * ----------
 * All user-visible text (log items, history, command names, the input buffer)
 * is stored as wchar_t*.  On Windows wchar_t is 16-bit UTF-16; on Linux/macOS
 * it is 32-bit UCS-4.  Either way a single wchar_t element maps to a full
 * Unicode codepoint (BMP on Windows, full range on Linux/macOS), so emoji and
 * any other symbol are just plain characters — no multi-byte parsing needed.
 *
 * ImGui boundary
 * --------------
 * ImGui::InputText and ImGui::TextUnformatted both require UTF-8 char* strings.
 * Two small helper functions (WideToUtf8 / Utf8ToWide) convert at the boundary:
 *
 *   InputText  →  reads InputBufUtf8 (char[2048])  →  convert to InputBuf (wchar_t[512])
 *   TextUnformatted  ←  convert each Items[i] (wchar_t*) to UTF-8 on the fly
 *
 * EMOJI
 * -----
 * Because we use wchar_t, emoji literal syntax is simply:
 *   AddLog(L"🚀 launched");   // works on any platform with a wide literal
 *   AddLog(L"\U0001F680 launched");  // same, portable escape form
 *
 * LoadEmojiFont() auto-detects Segoe UI Emoji (Windows), Apple Color Emoji
 * (macOS), or Noto Emoji (Linux) and merges it into the ImGui font atlas so
 * the glyphs actually render.
 */


#include "pch.hpp"
#include "Console.hpp"

 // ============================================================================
 //  Console::GetEmojiGlyphRanges  (public static)
 // ============================================================================

 /**
  * @brief Returns the static ImWchar glyph-range table for emoji and symbols.
  *
  * This is the single source of truth for which codepoints the console needs
  * rendered.  It is public so external font managers (FontManager in main.cpp)
  * can pass it directly to AddFontFromFileTTF — ensuring the glyph ranges are
  * NEVER accidentally omitted.
  *
  * WHY OMITTING THIS CAUSES ?????
  * --------------------------------
  * AddFontFromFileTTF has a 4th parameter: glyph_ranges (const ImWchar*).
  * Passing nullptr (the default) makes ImGui rasterize ONLY Basic Latin — even
  * if the font file has every emoji ever created.  The file is irrelevant; only
  * what you told ImGui to bake matters.
  *
  * USAGE IN FontManager / main.cpp
  * ---------------------------------
  * @code
  *   ImFontConfig cfg;
  *   cfg.MergeMode = true;
  *   // WRONG  (no ranges -> Basic Latin only -> ?????)
  *   io.Fonts->AddFontFromFileTTF(emoji_path, size, &cfg);
  *   // CORRECT
  *   io.Fonts->AddFontFromFileTTF(emoji_path, size, &cfg,
  *       Console::GetEmojiGlyphRanges());
  * @endcode
  *
  * The array has static storage duration so the pointer is always valid.
  *
  * Ranges covered:
  *  U+00A0-U+00FF  Latin-1 Supplement
  *  U+2000-U+27BF  General Punctuation + Misc Symbols (checkmark, warning...)
  *  U+2B00-U+2BFF  Misc Symbols and Arrows
  *  U+1F300-U+1F9FF Emoji block (rocket, target, key...)
  *
  * @return Pointer to a static sentinel-terminated ImWchar[] array.
  */
const ImWchar* Console::GetEmojiGlyphRanges() {
    // ImWchar = unsigned int.  Two-zero sentinel required by ImGui.
    static const ImWchar s_ranges[] =
    {
        static_cast<ImWchar>(0x00A0), static_cast<ImWchar>(0x00FF),
        static_cast<ImWchar>(0x2000), static_cast<ImWchar>(0x27BF),
        static_cast<ImWchar>(0x2B00), static_cast<ImWchar>(0x2BFF),
        static_cast<ImWchar>(0x1F300), static_cast<ImWchar>(0x1F9FF),
        static_cast<ImWchar>(0), static_cast<ImWchar>(0) // sentinel
    };
    return s_ranges;
}

/**
 * @brief Tests whether a file path exists and is readable.
 * @param path  Null-terminated UTF-8 path string.
 * @return      true if fopen succeeds.
 */
static bool FileExists(const char* path) {
    FILE* f = fopen(path, "rb"); // Try binary-read; fails silently if absent
    if(!f) return false;
    fclose(f);
    return true;
}

/**
 * @brief Builds a platform-ordered list of candidate emoji font paths.
 *
 * Windows  → Segoe UI Emoji / Segoe UI Symbol
 * macOS    → Apple Color Emoji
 * Linux    → Noto Color Emoji, Noto Emoji, Symbola (several distro paths)
 *
 * @return  Ordered std::vector<std::string> of absolute paths to try.
 */
static std::vector<std::string> GetCandidateEmojiPaths() {
    std::vector<std::string> candidates;

#if defined(_WIN32)
    char windir[MAX_PATH] = {}; // WinAPI buffer must be char*

    if(GetWindowsDirectoryA(windir, MAX_PATH) > 0) {
        candidates.push_back(std::string(windir) + "\\Fonts\\seguiemj.ttf"); // Segoe UI Emoji (color)
        candidates.push_back(std::string(windir) + "\\Fonts\\seguisym.ttf"); // Segoe UI Symbol (mono)
    }
    candidates.push_back("C:\\Windows\\Fonts\\seguiemj.ttf"); // Hard-coded fallback
    candidates.push_back("C:\\Windows\\Fonts\\seguisym.ttf");

#elif defined(__APPLE__)
    candidates.push_back("/System/Library/Fonts/Apple Color Emoji.ttc");
    candidates.push_back("/Library/Fonts/Apple Color Emoji.ttc");

#else
    candidates.push_back("/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf");
    candidates.push_back("/usr/share/fonts/noto/NotoColorEmoji.ttf");
    candidates.push_back("/usr/share/fonts/google-noto-emoji/NotoColorEmoji.ttf");
    candidates.push_back("/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf");
    candidates.push_back("/usr/share/fonts/noto/NotoEmoji-Regular.ttf");
    candidates.push_back("/usr/share/fonts/truetype/ancient-scripts/Symbola_hint.ttf");
    candidates.push_back("/usr/share/fonts/TTF/Symbola.ttf");
#endif

    return candidates;
}

// ============================================================================
//  Console::LoadEmojiFont
// ============================================================================

/**
 * @brief Auto-detects the platform emoji font and merges it into the ImGui atlas.
 *
 * Must be called ONCE: AFTER ImGui::CreateContext() + backend Init,
 * BEFORE the very first ImGui::NewFrame().
 *
 * Workflow:
 *  1. AddFontDefault() — loads ProggyClean as the base ASCII font.
 *  2. Probes GetCandidateEmojiPaths() for the first existing font file.
 *  3. AddFontFromFileTTF() with MergeMode=true merges only the emoji/symbol
 *     glyph ranges (ImWchar[]) on top of the existing font slot.
 *
 * @param font_size  Pixel size for both the base font and the emoji overlay.
 */
void Console::LoadEmojiFont(float font_size) {
    ImGuiIO& io = ImGui::GetIO(); // ImGui central state; owns the font atlas

    io.Fonts->AddFontDefault(); // Step 1: base font (ProggyClean, ASCII only)

    // Step 2: merge config — folds glyphs INTO the previously added font
    ImFontConfig merge_cfg;
    merge_cfg.MergeMode = true;   // Merge into existing font slot
    merge_cfg.OversampleH = 1;      // 1x is enough for large emoji glyphs
    merge_cfg.OversampleV = 1;      // saves atlas memory
    merge_cfg.GlyphOffset.y = 1.0f;  // nudge emoji 1px down to sit on baseline

    const ImWchar* emoji_ranges = GetEmojiGlyphRanges(); // ImWchar codepoint range table

    // Step 3: probe and load
    bool loaded = false;
    for(const std::string& path : GetCandidateEmojiPaths()) {
        if(!FileExists(path.c_str())) continue; // Skip missing files silently

        ImFont* result = io.Fonts->AddFontFromFileTTF(
            path.c_str(),  // UTF-8 path string required by ImGui
            font_size,
            &merge_cfg,
            emoji_ranges   // const ImWchar* — codepoints to rasterize
        );

        if(result != nullptr) {
            printf("[Console] Emoji font loaded: %s\n", path.c_str());
            loaded = true;
            break;
        }
    }

    if(!loaded) {
        printf("[Console] WARNING: No emoji font found. Emoji will show as '?'.\n");
        printf("[Console] Install Segoe UI Emoji (Win) / Noto Emoji (Linux).\n");
    }
}

// ============================================================================
//  Encoding helpers  (wchar_t <-> UTF-8)
//
//  These sit at the ImGui boundary: everything INSIDE the console is wchar_t;
//  everything passed TO ImGui (InputText, TextUnformatted) is UTF-8 char*.
// ============================================================================

/**
 * @brief Converts a null-terminated wchar_t string to a UTF-8 std::string.
 *
 * Windows  : uses WideCharToMultiByte (wchar_t = UTF-16 on Windows).
 * Linux/macOS : manually encodes each 32-bit wchar_t codepoint into UTF-8 bytes.
 *
 * @param wstr  Wide source string; nullptr returns "".
 * @return      UTF-8 encoded std::string.
 */
std::string Console::WideToUtf8(const wchar_t* wstr) {
    if(!wstr) return ""; // Null guard

#ifdef _WIN32
    // Windows: OS handles UTF-16 → UTF-8 conversion
    int needed = WideCharToMultiByte(
        CP_UTF8, 0,         // target code page: UTF-8
        wstr, -1,           // source: null-terminated wide string
        nullptr, 0,         // first call: query required byte count
        nullptr, nullptr);

    if(needed <= 1) return ""; // Empty string or conversion error

    std::string result(needed - 1, '\0'); // Allocate without the NUL byte
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], needed, nullptr, nullptr);
    return result;

#else
    // Linux/macOS: wchar_t is 32-bit UCS-4; encode each codepoint manually
    std::string result;
    while(*wstr) {
        uint32_t cp = static_cast<uint32_t>(*wstr++); // Unicode codepoint value

        if(cp < 0x80u) {
            // 1-byte: 0xxxxxxx
            result += static_cast<char>(cp);
        } else if(cp < 0x800u) {
            // 2-byte: 110xxxxx 10xxxxxx
            result += static_cast<char>(0xC0u | (cp >> 6));
            result += static_cast<char>(0x80u | (cp & 0x3Fu));
        } else if(cp < 0x10000u) {
            // 3-byte: 1110xxxx 10xxxxxx 10xxxxxx
            result += static_cast<char>(0xE0u | (cp >> 12));
            result += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
            result += static_cast<char>(0x80u | (cp & 0x3Fu));
        } else {
            // 4-byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            result += static_cast<char>(0xF0u | (cp >> 18));
            result += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
            result += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
            result += static_cast<char>(0x80u | (cp & 0x3Fu));
        }
    }
    return result;
#endif
}

/**
 * @brief Converts a null-terminated UTF-8 char string to a std::wstring.
 *
 * Windows  : uses MultiByteToWideChar (result is UTF-16 wchar_t).
 * Linux/macOS : uses mbstowcs with the locale; ensure setlocale(LC_ALL,"")
 *               is called once at program startup for correct multi-byte
 *               parsing.
 *
 * @param str  UTF-8 source string; nullptr returns L"".
 * @return     std::wstring.
 */
std::wstring Console::Utf8ToWide(const char* str) {
    if(!str) return L""; // Null guard

#ifdef _WIN32
    // Windows: OS handles UTF-8 → UTF-16 conversion
    const int needed = MultiByteToWideChar(
        CP_UTF8, 0,   // source code page: UTF-8
        str, -1,      // source: null-terminated byte string
        nullptr, 0);  // first call: query required wchar_t count

    if(needed <= 0) return L"";

    std::wstring result(needed - 1, L'\0'); // Allocate without the NUL
    MultiByteToWideChar(CP_UTF8, 0, str, -1, &result[0], needed);
    return result;

#else
    // Linux/macOS: mbstowcs decodes the locale's multi-byte encoding.
    // The locale must support UTF-8 (call setlocale(LC_ALL,"") at startup).
    size_t needed = mbstowcs(nullptr, str, 0); // Query the required length

    if(needed == static_cast<size_t>(-1)) return L""; // Conversion error

    std::wstring result(needed, L'\0');
    mbstowcs(&result[0], str, needed + 1); // Write wide chars including NUL
    return result;
#endif
}

// ============================================================================
//  Wide-char string helpers
//
//  Analogues of the old Stricmp / Strnicmp / Strdup / Strtrim, operating
//  on wchar_t instead of char.  towupper() is used for case folding so that
//  accented letters and other non-ASCII codepoints are handled correctly on
//  platforms that support them.
// ============================================================================

/**
 * @brief Case-insensitive wide-char strcmp.
 *
 * Uses towupper() for locale-aware case folding (works for accented letters,
 * not just ASCII A–Z).
 *
 * @param s1  First wide string.
 * @param s2  Second wide string.
 * @return    0 if equal (case-insensitive); non-zero at first mismatch.
 */
int Console::Wcsicmp(const wchar_t* s1, const wchar_t* s2) {
    int d; // Signed difference between two uppercased codepoints

    // Advance while characters match (uppercased) and s1 has not ended
    while((d = static_cast<int>(std::towupper(*s2))
        - static_cast<int>(std::towupper(*s1))) == 0
        && *s1) {
        s1++; // Next wide character in s1
        s2++; // Next wide character in s2
    }

    return d; // 0 → equal; ±N → first mismatch
}

/**
 * @brief Case-insensitive wide-char strncmp (at most n characters).
 *
 * @param s1  First wide string.
 * @param s2  Second wide string.
 * @param n   Maximum number of wchar_t elements to compare.
 * @return    0 if the first n characters match; non-zero otherwise.
 */
int Console::Wcsnicmp(const wchar_t* s1, const wchar_t* s2, int n) {
    int d = 0; // Difference accumulator

    while(n > 0
        && (d = static_cast<int>(towupper(static_cast<wint_t>(*s2)))
            - static_cast<int>(towupper(static_cast<wint_t>(*s1)))) == 0
        && *s1) {
        s1++; // Advance s1
        s2++; // Advance s2
        n--;  // One fewer element to compare
    }

    return d;
}

/**
 * @brief Duplicates a wide string using ImGui's allocator.
 *
 * Allocates (wcslen(s)+1) * sizeof(wchar_t) bytes via ImGui::MemAlloc so
 * that ImGui::MemFree can safely release it later.
 *
 * @param s  Source wide string (must not be null).
 * @return   Heap-allocated wide copy; caller must call ImGui::MemFree().
 */
wchar_t* Console::Wcsdup(const wchar_t* s) {
    IM_ASSERT(s); // Crash in debug builds if s is null

    const size_t   len = wcslen(s) + 1;                        // Wide character count including NUL
    const size_t   bytes = len * sizeof(wchar_t);                // Byte count for memcpy
    void* buf = ImGui::MemAlloc(bytes);                // Allocate via ImGui's heap
    IM_ASSERT(buf);                                        // Crash on allocation failure

    return std::bit_cast<wchar_t*>(wmemcpy(std::bit_cast<wchar_t*>(buf), s, len));
    //     ^^ cast void* back to wchar_t* after wmemcpy (copies len wide chars)
}

/**
 * @brief Trims trailing L' ' (space) wide characters from a wide string in-place.
 *
 * @param s  Wide string to trim (modified in-place).
 */
void Console::Wcstrim(wchar_t* s) {
    wchar_t* end = s + wcslen(s); // Pointer one past the last wchar_t

    // Walk backwards while we're inside the string and the character is a space
    while(end > s && end[-1] == L' ')
        end--;

    *end = L'\0'; // Write wide NUL terminator
}

// ============================================================================
//  Command registration
// ============================================================================

/**
 * @brief Validates that @p name exists in BuiltInCommands, then registers it.
 *
 * Guards against typos: if the name is not in the compile-time array an error
 * is logged and the registration is skipped.
 *
 * @param name  Wide command name.
 * @param func  Lambda to call when the command is executed.
 */
void Console::RegisterBuiltIn(std::wstring_view name, std::function<void()> func) {
    bool found = false; // Whether name was found in BuiltInCommands

    for(const auto& cmd : BuiltInCommands) {
        if(cmd.name == name) // wstring_view comparison — no heap allocation
        {
            found = true;
            break;
        }
    }

    if(!found) {
        // AddLog uses wide format; %ls prints a wide string argument
        AddLog(L"[error]Tentativa de registrar '%ls' que nao esta no BuiltInCommands![/]",
            name.data());
        return;
    }

    RegisterCommand(std::wstring(name), func); // Forward to the core registration
}

/**
 * @brief Core registration: autocomplete list + dispatch table.
 *
 * The command name is stored as wchar_t* in the Commands autocomplete vector,
 * and as a std::wstring key (uppercased) in DispatchTable.
 *
 * @param name  Wide command name (any case).
 * @param func  Callable to invoke on execution.
 */
void Console::RegisterCommand(const std::wstring& name, std::function<void()> func) {
    // ----------------------------------------------------------------
    // 1. Add to autocomplete Commands if not already present
    // ----------------------------------------------------------------

    wchar_t* cmd_wcs = Wcsdup(name.c_str()); // Wide heap copy for autocomplete
    bool     already_exists = false;

    for(int i = 0; i < Commands.Size; i++) {
        if(Wcsicmp(Commands[i], cmd_wcs) == 0) // Case-insensitive duplicate check
        {
            already_exists = true;
            break;
        }
    }

    if(!already_exists)
        Commands.push_back(cmd_wcs);  // Store in autocomplete vector
    else
        ImGui::MemFree(cmd_wcs);      // Free duplicate — already in the list

    // ----------------------------------------------------------------
    // 2. Uppercase the key for the dispatch table
    // ----------------------------------------------------------------

    std::wstring key = name;
    std::transform(key.begin(), key.end(), key.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });

    // ----------------------------------------------------------------
    // 3. Insert or overwrite the dispatch table entry
    // ----------------------------------------------------------------

    DispatchTable[key] = func;
}

/**
 * @brief Registers a command with a wide help description.
 *
 * @param name  Wide command name.
 * @param desc  Wide description shown by HELP.
 * @param func  Callable to invoke on execution.
 */
void Console::RegisterCommand(const std::wstring& name,
    const std::wstring& desc,
    std::function<void()> func) {
    RegisterCommand(name, func); // Register logic + autocomplete first

    std::wstring key = name;
    std::transform(key.begin(), key.end(), key.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });

    HelpDescriptions[key] = desc; // Store the wide description
}

// ============================================================================
//  Constructor / Destructor
// ============================================================================

/**
 * @brief Initialises the console and registers the four built-in commands.
 */
Console::Console() {
    ClearLog();
    wmemset(InputBuf, L'\0', IM_COUNTOF(InputBuf));      // Zero the wide input buffer
    memset(InputBufUtf8, '\0', sizeof(InputBufUtf8));    // Zero the UTF-8 staging buffer
    HistoryPos = -1;
    AutoScroll = true;
    ScrollToBottom = false;

    AddLog(L"Bem-vindo ao Console ImGui!");
    AddLog(L"Digite '[yellow]HELP[/]' para ver a lista de comandos.");

    // CLEAR
    RegisterBuiltIn(L"CLEAR", [this]() { this->ClearLog(); });

    // HELP
    RegisterBuiltIn(L"HELP", [this]() {
        AddLog(L"--- [yellow]Comandos Internos[/] ---");
        for(const auto& cmd : BuiltInCommands)
            AddLog(L"- [yellow]%ls[/]: %ls", cmd.name.data(), cmd.description.data());

        AddLog(L"\n--- [cyan]Comandos do Sistema[/] ---");
        for(const auto& pair : DispatchTable) {
            const std::wstring& cmd_name = pair.first; // Already UPPERCASE

            bool is_builtin = false;
            for(const auto& b : BuiltInCommands)
                if(b.name == cmd_name) { is_builtin = true; break; }

            if(!is_builtin) {
                auto it = HelpDescriptions.find(cmd_name);
                if(it != HelpDescriptions.end())
                    AddLog(L"- [cyan]%ls[/]: %ls", cmd_name.c_str(), it->second.c_str());
                else
                    AddLog(L"- [cyan]%ls[/]", cmd_name.c_str());
            }
        }
    });

    // HISTORY
    RegisterBuiltIn(L"HISTORY", [this]() {
        int first = History.Size - 10; // Show at most the last 10 entries
        for(int i = first > 0 ? first : 0; i < History.Size; i++)
            AddLog(L"%3d: %ls\n", i, History[i]);
    });

    // EXIT (placeholder — override from main.cpp)
    RegisterBuiltIn(L"EXIT", [this]() {
        AddLog(L"[error]A logica de saida deve ser sobrescrita no main.cpp![/]");
    });
}

/**
 * @brief Frees all heap-allocated wide strings in Items and History.
 */
Console::~Console() {
    ClearLog(); // Frees every Items[i] via ImGui::MemFree

    for(int i = 0; i < History.Size; i++)
        ImGui::MemFree(History[i]); // Each entry was allocated with Wcsdup
}

// ============================================================================
//  Log management
// ============================================================================

/**
 * @brief Frees all log items and clears the Items vector.
 */
void Console::ClearLog() {
    for(int i = 0; i < Items.Size; i++)
        ImGui::MemFree(Items[i]); // Wcsdup-allocated — must use MemFree

    Items.clear();
}

/**
 * @brief Formats a wide string (vswprintf-style) and appends it to the log.
 *
 * The formatted wide string is duplicated onto ImGui's heap (via Wcsdup)
 * so it persists between frames.
 *
 * @param fmt  Wide printf format string, e.g. L"Score: %d  Player: %ls".
 * @param ...  Variadic arguments matching the format string.
 */
void Console::AddLog(const wchar_t* fmt, ...) {
    wchar_t buf[1024]; // Stack-allocated wide buffer for the formatted text
    va_list args;

    va_start(args, fmt);
    vswprintf(buf, IM_COUNTOF(buf), fmt, args); // Wide-char safe printf
    buf[IM_COUNTOF(buf) - 1] = L'\0';           // Guarantee wide NUL termination
    va_end(args);

    Items.push_back(Wcsdup(buf)); // Heap-duplicate and store
}

// ============================================================================
//  Command execution
// ============================================================================

/**
 * @brief Parses and executes a wide-char command string.
 *
 * Steps:
 *  1. Echo the raw input to the log (prefixed with L'#').
 *  2. Deduplicate and update History (wide strings).
 *  3. Strip trailing L' ', uppercase, look up in DispatchTable.
 *  4. Call the registered lambda or log "unknown command".
 *
 * @param command_line  Wide command typed by the user.
 */
void Console::ExecCommand(const wchar_t* command_line) {
    AddLog(L"# %ls\n", command_line); // Echo with wide '#' prefix

    // ------------------------------------------------------------------
    // 1. History: remove duplicate entry, then append at the end
    // ------------------------------------------------------------------

    HistoryPos = -1; // Reset browse cursor

    for(int i = History.Size - 1; i >= 0; i--) {
        if(Wcsicmp(History[i], command_line) == 0) {
            ImGui::MemFree(History[i]);           // Free the duplicate wide string
            History.erase(History.begin() + i);  // Remove from vector
            break;
        }
    }

    History.push_back(Wcsdup(command_line)); // Append fresh wide copy

    // ------------------------------------------------------------------
    // 2. Build the dispatch key: strip trailing spaces, uppercase
    // ------------------------------------------------------------------

    std::wstring cmd = command_line;

    size_t last = cmd.find_last_not_of(L' '); // Last non-space character index
    if(last != std::wstring::npos)
        cmd = cmd.substr(0, last + 1);        // Remove trailing spaces

    std::transform(cmd.begin(), cmd.end(), cmd.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });

    // ------------------------------------------------------------------
    // 3. O(log n) map lookup and dispatch
    // ------------------------------------------------------------------

    auto it = DispatchTable.find(cmd);

    if(it != DispatchTable.end())
        it->second();                               // Call the registered lambda
    else
        AddLog(L"Comando desconhecido: '%ls'\n", command_line);

    ScrollToBottom = true; // Scroll to show the command result
}

// ============================================================================
//  ImGui rendering
// ============================================================================

/**
 * @brief Renders the console window.  Call once per frame.
 *
 * ImGui boundary:
 *  - The window title is converted from wchar_t* to UTF-8 for ImGui::Begin.
 *  - Each Items[i] (wchar_t*) is converted to UTF-8 for ImGui::TextUnformatted.
 *  - InputText uses InputBufUtf8 (char[]); on submit the result is converted
 *    back to wide and stored in InputBuf before ExecCommand is called.
 *
 * @param title   Wide window title.
 * @param p_open  Pointer to a bool controlling window visibility.
 */
void Console::Draw(const wchar_t* title, bool* p_open) {
    ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);

    // Convert wide title to UTF-8 so ImGui::Begin can use it
    std::string title_utf8 = WideToUtf8(title);

    if(!ImGui::Begin(title_utf8.c_str(), p_open)) {
        ImGui::End();
        return;
    }

    // Right-click title bar → close menu
    if(ImGui::BeginPopupContextItem()) {
        if(ImGui::MenuItem("Close Console")) *p_open = false;
        ImGui::EndPopup();
    }

    // ---- Toolbar -------------------------------------------------------

    if(ImGui::SmallButton("Clear"))  ClearLog();   // Wipe the log
    ImGui::SameLine();
    bool copy_to_clipboard = ImGui::SmallButton("Copy");

    ImGui::SameLine();
    Filter.Draw("Filter", 180); // ImGui filter widget (UTF-8 internally)

    ImGui::Separator();

    // ---- Scrolling log area --------------------------------------------

    const float footer_height = ImGui::GetStyle().ItemSpacing.y
        + ImGui::GetFrameHeightWithSpacing();

    if(ImGui::BeginChild("ScrollingRegion",
        ImVec2(0, -footer_height), false,
        ImGuiWindowFlags_HorizontalScrollbar)) {
        if(ImGui::BeginPopupContextWindow()) {
            if(ImGui::Selectable("Clear")) ClearLog();
            ImGui::EndPopup();
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Compact rows

        if(copy_to_clipboard) ImGui::LogToClipboard();

        for(wchar_t* item : Items) {
            // Convert each wide log line to UTF-8 for ImGui
            std::string item_utf8 = WideToUtf8(item);

            if(!Filter.PassFilter(item_utf8.c_str())) // Filter operates on UTF-8
                continue;

            // RenderTermicolor processes [color]…[/] tags in the wide string
            RenderTermicolor(item);
        }

        if(copy_to_clipboard) ImGui::LogFinish();

        if(ScrollToBottom ||
            (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
            ImGui::SetScrollHereY(1.0f);

        ScrollToBottom = false;
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    ImGui::Separator();

    // ---- Command input -------------------------------------------------

    bool reclaim_focus = false;

    ImGuiInputTextFlags input_flags =
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_EscapeClearsAll |
        ImGuiInputTextFlags_CallbackCompletion |
        ImGuiInputTextFlags_CallbackHistory;

    // InputText requires char* (UTF-8).  We use InputBufUtf8 as the staging
    // buffer and convert to/from InputBuf (wchar_t[]) at the boundary.
    if(ImGui::InputText("Input", InputBufUtf8, IM_COUNTOF(InputBufUtf8),
        input_flags, &TextEditCallbackStub,
        std::bit_cast<void*>(this))) {
        // Convert the UTF-8 result back to wide for all internal processing
        std::wstring wide_input = Utf8ToWide(InputBufUtf8);

        // Copy into the wide InputBuf (truncate if longer than the buffer)
        wcsncpy(InputBuf, wide_input.c_str(), IM_COUNTOF(InputBuf) - 1);
        InputBuf[IM_COUNTOF(InputBuf) - 1] = L'\0'; // Ensure NUL termination

        Wcstrim(InputBuf);        // Strip trailing wide spaces
        if(InputBuf[0] != L'\0')
            ExecCommand(InputBuf); // Execute the wide command string

        // Clear both buffers after submission
        wmemset(InputBuf, L'\0', IM_COUNTOF(InputBuf));
        memset(InputBufUtf8, '\0', sizeof(InputBufUtf8));
        reclaim_focus = true;
    }

    ImGui::SetItemDefaultFocus();
    if(reclaim_focus) ImGui::SetKeyboardFocusHere(-1);

    ImGui::End();
}

// ============================================================================
//  InputText callbacks
// ============================================================================

/**
 * @brief Static trampoline: recovers the Console* from UserData and forwards.
 */
int Console::TextEditCallbackStub(ImGuiInputTextCallbackData* data) {
    Console* console = static_cast<Console*>(data->UserData);
    return console->TextEditCallback(data);
}

/**
 * @brief Handles TAB-completion and Up/Down history navigation.
 *
 * The callback operates on data->Buf which is a UTF-8 char* buffer (ImGui's
 * internal representation for InputText).  To compare against our wide
 * Commands list we convert candidate prefixes to wide on the fly via
 * Utf8ToWide / Wcsnicmp.  History entries are converted to UTF-8 for
 * insertion back into data->Buf.
 *
 * @param data  Mutable InputText widget state.
 * @return      0 (ImGui ignores the return value here).
 */
int Console::TextEditCallback(ImGuiInputTextCallbackData* data) {
    switch(data->EventFlag) {
        // ==============================================================
        // TAB — autocomplete
        // ==============================================================
        case ImGuiInputTextFlags_CallbackCompletion:
            {
                // 1. Locate the word under the cursor in the UTF-8 buffer
                const char* word_end = data->Buf + data->CursorPos;
                const char* word_start = word_end;

                while(word_start > data->Buf) {
                    // Cast byte to unsigned char before widening to avoid signed UB
                    wchar_t wc = static_cast<wchar_t>(
                        static_cast<unsigned char>(word_start[-1]));

                    // Delimiters are all ASCII — safe to compare as wchar_t
                    if(wc == L' ' || wc == L'\t' || wc == L',' || wc == L';')
                        break;
                    word_start--;
                }

                // 2. Convert the partial word to wide for comparison with Commands[]
                std::string  prefix_utf8(word_start, word_end);
                std::wstring prefix_wide = Utf8ToWide(prefix_utf8.c_str());

                int prefix_len = static_cast<int>(prefix_wide.size()); // Wide char count

                // 3. Collect matching wide commands
                ImVector<const wchar_t*> candidates;
                for(int i = 0; i < Commands.Size; i++) {
                    if(Wcsnicmp(Commands[i], prefix_wide.c_str(), prefix_len) == 0)
                        candidates.push_back(Commands[i]);
                }

                if(candidates.Size == 0) {
                    AddLog(L"Nenhuma correspondencia para \"%ls\"!\n", prefix_wide.c_str());
                } else if(candidates.Size == 1) {
                    // Exact match: replace the partial word (UTF-8 bytes) with the
                    // full command name (converted back to UTF-8)
                    std::string full_utf8 = WideToUtf8(candidates[0]);

                    data->DeleteChars(static_cast<int>(word_start - data->Buf),
                        static_cast<int>(word_end - word_start));
                    data->InsertChars(data->CursorPos, full_utf8.c_str()); // Full name
                    data->InsertChars(data->CursorPos, " ");               // Trailing space
                } else {
                    // Multiple matches: find the longest common wide prefix
                    int match_len = prefix_len; // Start from what is already matched

                    for(;;) {
                        wchar_t ref_wc = L'\0'; // Reference wide char at position match_len
                        bool    all_match = true;

                        for(int i = 0; i < candidates.Size && all_match; i++) {
                            wchar_t wc = static_cast<wchar_t>(
                                towupper(static_cast<wint_t>(candidates[i][match_len])));

                            if(i == 0)
                                ref_wc = wc;                    // Set reference from first candidate
                            else if(ref_wc == L'\0' || ref_wc != wc)
                                all_match = false;              // Mismatch or end of a candidate
                        }

                        if(!all_match) break;
                        match_len++; // All candidates share one more wide character
                    }

                    // Replace partial word with the longest common wide prefix
                    if(match_len > prefix_len) {
                        // Slice the common prefix and convert to UTF-8 for insertion
                        std::wstring common_wide(candidates[0], candidates[0] + match_len);
                        std::string  common_utf8 = WideToUtf8(common_wide.c_str());

                        data->DeleteChars(static_cast<int>(word_start - data->Buf),
                            static_cast<int>(word_end - word_start));
                        data->InsertChars(data->CursorPos, common_utf8.c_str());
                    }

                    // List all candidates in the wide log
                    AddLog(L"Candidatos possiveis:");
                    for(int i = 0; i < candidates.Size; i++)
                        AddLog(L"- %ls", candidates[i]);
                }

                break;
            }

            // ==============================================================
            // Up / Down arrow — history navigation
            // ==============================================================
        case ImGuiInputTextFlags_CallbackHistory:
            {
                const int prev_pos = HistoryPos;

                if(data->EventKey == ImGuiKey_UpArrow) {
                    if(HistoryPos == -1)
                        HistoryPos = History.Size - 1; // Jump to last entry
                    else if(HistoryPos > 0)
                        HistoryPos--;                  // Step back one entry
                } else if(data->EventKey == ImGuiKey_DownArrow) {
                    if(HistoryPos != -1 && ++HistoryPos >= History.Size)
                        HistoryPos = -1; // Past the end → back to blank
                }

                if(prev_pos != HistoryPos) {
                    // History entries are wchar_t*; convert to UTF-8 for InputText
                    const wchar_t* wide_entry = (HistoryPos >= 0)
                        ? History[HistoryPos] : L"";

                    std::string utf8_entry = WideToUtf8(wide_entry);

                    data->DeleteChars(0, data->BufTextLen);         // Clear current UTF-8 bytes
                    data->InsertChars(0, utf8_entry.c_str());       // Insert UTF-8 history entry
                }

                break;
            }
    }

    return 0;
}

// ============================================================================
//  Color-tag renderer  (Termícolor) — wide-char edition
// ============================================================================

/**
 * @brief Converts a wide color name or hex string to an ImVec4 RGBA color.
 *
 * Supported formats:
 *  - Wide named colors: L"red", L"green", L"blue", L"yellow", L"orange",
 *                       L"gray", L"purple", L"cyan", L"error"
 *  - Wide hex: L"#RRGGBB"  (e.g. L"#FF8800")
 *  - Default (unknown): white (1,1,1,1)
 *
 * @param start  Pointer to the first wide char of the color key (after L'[').
 * @param end    Pointer one-past the last wide char (before L']').
 * @return       RGBA color as ImVec4.
 */
ImVec4 Console::ParseColor(const wchar_t* start, const wchar_t* end) {
    std::wstring key(start, end - start); // Extract wide key substring

    // ---- Hex format: [#RRGGBB] ------------------------------------------

    if(!key.empty() && key[0] == L'#') {
        unsigned int r = 0, g = 0, b = 0;
        // swscanf_s reads 2 hex digits per channel from the wide string
        if(swscanf_s(key.c_str() + 1, L"%02x%02x%02x", &r, &g, &b) == 3)
            return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    }

    // ---- Named colors ---------------------------------------------------

    if(key == L"red")     return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
    if(key == L"green")   return ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
    if(key == L"blue")    return ImVec4(0.4f, 0.6f, 1.0f, 1.0f);
    if(key == L"yellow")  return ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
    if(key == L"orange")  return ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
    if(key == L"gray")    return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    if(key == L"purple")  return ImVec4(0.8f, 0.4f, 0.8f, 1.0f);
    if(key == L"cyan")    return ImVec4(0.4f, 1.0f, 1.0f, 1.0f);
    if(key == L"error")   return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // Default: white
}

/**
 * @brief Parses and renders a wide string with inline [color]…[/] tags.
 *
 * The function walks the wide string, splitting at L'[' and L']'.  Because
 * wchar_t elements are individual codepoints (not multi-byte sequences), the
 * search is a simple pointer walk — no wcschr / strchr ambiguity.
 *
 * Text segments are converted to UTF-8 before being passed to
 * ImGui::TextUnformatted (which requires char*).
 *
 *  - [color] pushes a color onto ImGui's style stack.
 *  - [/] pops the last pushed color.
 *
 * Any unclosed color pushes are cleaned up at the end.
 *
 * @param text  Null-terminated wide string with optional [color] tags.
 */
void Console::RenderTermicolor(const wchar_t* text) {
    const wchar_t* p = text;                   // Current parse position
    const wchar_t* text_end = text + wcslen(text);    // One past the last wchar_t
    int            color_stack = 0;                    // Depth of pushed style colors

    while(p < text_end) {
        // Find the next L'[' wide character
        const wchar_t* tag_start = p;
        while(tag_start < text_end && *tag_start != L'[')
            tag_start++;

        if(tag_start >= text_end) {
            // No more tags — draw the remaining text
            if(p != text) ImGui::SameLine(0, 0); // Attach to previous segment
            std::string seg = WideToUtf8(p);       // Wide → UTF-8 for ImGui
            ImGui::TextUnformatted(seg.c_str());
            break;
        }

        // Find the matching L']'
        const wchar_t* tag_end = tag_start + 1;
        while(tag_end < text_end && *tag_end != L']')
            tag_end++;

        if(tag_end >= text_end) {
            // Found L'[' but no L']' — treat the rest as plain text
            if(p != text) ImGui::SameLine(0, 0);
            std::string seg = WideToUtf8(p);
            ImGui::TextUnformatted(seg.c_str());
            break;
        }

        // ---- Draw the text segment BEFORE the tag ------------------

        if(tag_start > p) {
            if(p != text) ImGui::SameLine(0, 0); // No gap between segments

            // Slice the wide segment and convert to UTF-8
            std::wstring wide_seg(p, tag_start - p);
            std::string  utf8_seg = WideToUtf8(wide_seg.c_str());
            ImGui::TextUnformatted(utf8_seg.c_str());
        }

        // ---- Process the tag ----------------------------------------

        // Closing tag: [/] → length between '[' and ']' is 1 (just '/')
        // or []          → length is 0 (empty tag)
        ptrdiff_t inner_len = tag_end - tag_start - 1; // chars between '[' and ']'

        bool is_close = (inner_len == 1 && tag_start[1] == L'/')  // [/]
            || (inner_len == 0);                          // []

        if(is_close) {
            if(color_stack > 0) {
                ImGui::PopStyleColor(); // Restore previous text color
                color_stack--;
            }
        } else {
            // Opening tag: parse the wide color name/hex
            ImVec4 col = ParseColor(tag_start + 1, tag_end);
            ImGui::PushStyleColor(ImGuiCol_Text, col); // Push wide color
            color_stack++;
        }

        p = tag_end + 1; // Advance past the L']'
    }

    // Safety: pop any colors that were never closed with [/]
    while(color_stack > 0) {
        ImGui::PopStyleColor();
        color_stack--;
    }
}

// ============================================================================
//  Emoji helpers
// ============================================================================

/**
 * @brief Appends a log line prefixed with a wide-char emoji.
 *
 * Because all log storage is wchar_t, the emoji is simply prepended as a
 * wide character — no encoding conversion needed at this point.
 *
 * @param emoji  Wide-char emoji, e.g. L"🚀" or L"\U0001F680".
 * @param fmt    Wide printf format string for the message body.
 * @param ...    Variadic arguments.
 */
void Console::AddLogWithEmoji(const wchar_t* emoji, const wchar_t* fmt, ...) {
    wchar_t buf[1024]; // Wide buffer for the formatted message body
    va_list args;

    va_start(args, fmt);
    vswprintf(buf, IM_COUNTOF(buf), fmt, args); // Wide vprintf
    va_end(args);

    // Concatenate emoji + space + message as a single wide string
    std::wstring msg = std::wstring(emoji) + L" " + std::wstring(buf);
    AddLog(L"%ls", msg.c_str()); // Delegate to AddLog (which calls Wcsdup)
}

/**
 * @brief Returns a random wide-char emoji from a fixed internal table.
 *
 * Uses wide string literals (L"…" or escape sequences L"\Uxxxxxxxx") so that
 * emoji are stored as native wchar_t codepoints — no UTF-8 decoding needed
 * anywhere in the internal pipeline.
 *
 * Requires Console::LoadEmojiFont() to have been called so ImGui can render
 * the glyphs.
 *
 * @return  A std::wstring containing one emoji, or L"" if the table is empty.
 */
std::wstring Console::GetRandomEmoji() const {
    // Static table: populated once, lives for program lifetime
    static std::vector<std::wstring> emojis;
    static bool initialized = false;

    if(!initialized) {
        // Wide string literals — each L"…" is stored as native wchar_t codepoints.
        // The Unicode escape \Uxxxxxxxx form is used for supplementary characters
        // (> U+FFFF) because some compilers / source encodings may not pass them
        // through raw wide literals reliably.
        emojis.push_back(L"\u2713");       // U+2713  CHECK MARK
        emojis.push_back(L"\u2717");       // U+2717  BALLOT X
        emojis.push_back(L"\u26A0");       // U+26A0  WARNING SIGN
        emojis.push_back(L"\u2139");       // U+2139  INFORMATION SOURCE
        emojis.push_back(L"\u26A1");       // U+26A1  HIGH VOLTAGE SIGN
        emojis.push_back(L"\U0001F527");   // U+1F527 WRENCH            🔧
        emojis.push_back(L"\U0001F680");   // U+1F680 ROCKET            🚀
        emojis.push_back(L"\U0001F4DD");   // U+1F4DD MEMO              📝
        emojis.push_back(L"\U0001F4CA");   // U+1F4CA BAR CHART         📊
        emojis.push_back(L"\U0001F3AF");   // U+1F3AF DIRECT HIT        🎯
        emojis.push_back(L"\U0001F4A1");   // U+1F4A1 LIGHT BULB        💡
        emojis.push_back(L"\U0001F50D");   // U+1F50D MAGNIFYING GLASS  🔍
        emojis.push_back(L"\u23F1");       // U+23F1  STOPWATCH         ⏱
        emojis.push_back(L"\U0001F4E6");   // U+1F4E6 PACKAGE           📦
        emojis.push_back(L"\U0001F31F");   // U+1F31F GLOWING STAR      🌟
        emojis.push_back(L"\U0001F4BE");   // U+1F4BE FLOPPY DISK       💾
        emojis.push_back(L"\U0001F4E5");   // U+1F4E5 INBOX TRAY        📥
        emojis.push_back(L"\U0001F4E4");   // U+1F4E4 OUTBOX TRAY       📤
        emojis.push_back(L"\U0001F510");   // U+1F510 CLOSED LOCK+KEY   🔐
        emojis.push_back(L"\U0001F511");   // U+1F511 KEY               🔑

        initialized = true;
    }

    if(emojis.empty()) return L"";

    return emojis[rand() % emojis.size()]; // Random wide emoji string
}
