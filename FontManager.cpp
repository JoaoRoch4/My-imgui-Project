X/**
 * @file FontManager.cpp
 * @brief Font loading and management for ImGui — with per-font emoji merging.
 *
 * THE CORRECT WAY TO MERGE EMOJI INTO MULTIPLE FONTS
 * ---------------------------------------------------
 * ImGui's MergeMode=true in ImFontConfig does NOT mean "add glyphs to every
 * font."  It means: add the glyphs from this .ttf into the SAME ImFont object
 * that the most recent NON-merge AddFont* call created.
 *
 * In other words, each (base font + emoji merge) pair must be loaded
 * consecutively:
 *
 *   AddFontFromFileTTF("Roboto.ttf",    size)               // creates slot A
 *   AddFontFromFileTTF("seguiemj.ttf",  size, MergeMode=1)  // merges INTO slot A
 *   AddFontFromFileTTF("OpenSans.ttf",  size)               // creates slot B
 *   AddFontFromFileTTF("seguiemj.ttf",  size, MergeMode=1)  // merges INTO slot B
 *
 * Any attempt to retroactively patch slots after the fact (swapping Fonts[]
 * pointers, etc.) does not work because MergeMode is controlled by the
 * ConfigData list, not the Fonts pointer list.
 *
 * CONSEQUENCE FOR THIS CLASS
 * --------------------------
 * LoadAllFonts() now accepts an optional emoji_path parameter.
 * When provided, every base font is immediately followed by an emoji merge.
 * LoadEmojiFromSystemFonts() / LoadEmojiAuto() locate the emoji .ttf and then
 * call LoadAllFonts() again, which reloads all base fonts WITH emoji merged.
 *
 * CALL ORDER IN main.cpp (unchanged)
 * ------------------------------------
 *   font_manager.LoadAllFonts(size);                    // base fonts only
 *   font_manager.LoadEmojiFromSystemFonts(size);        // reloads all + merges
 * OR the simpler one-shot:
 *   font_manager.LoadAllFontsWithEmoji(size);           // does both in one call
 */

#include "pch.hpp"
#include "FontManager.hpp"


namespace fs = std::filesystem;

// ============================================================================
//  File-scope helper: WideToUtf8
//
//  Converts up to `count` wchar_t elements to a UTF-8 std::string.
//  Used to convert L"" wide emoji literals for ImGui (which requires char*).
//
//  On Windows, wchar_t is 16-bit UTF-16.
//  On Linux/macOS, wchar_t is 32-bit UCS-4.
//  Either way, \UxxxxxxXX escape sequences produce the correct codepoint.
//
//  @param wstr   Wide string to convert.
//  @param count  Number of wchar_t elements to convert (default: whole string).
// ============================================================================
static std::string WideToUtf8(const wchar_t* wstr, size_t count = static_cast<size_t>(-1)) {
    if(!wstr) return "";

#ifdef _WIN32
    // Windows: WideCharToMultiByte handles UTF-16 surrogates automatically
    int len = (count == static_cast<size_t>(-1))
        ? -1                              // null-terminated
        : static_cast<int>(count);        // exact count

    int needed = WideCharToMultiByte(CP_UTF8, 0, wstr, len, nullptr, 0, nullptr, nullptr);
    if(needed <= 0) return "";

    // WideCharToMultiByte includes the NUL when len == -1; exclude it
    int result_len = (len == -1) ? needed - 1 : needed;
    std::string result(result_len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, len, &result[0], needed, nullptr, nullptr);
    return result;
#else
    // Linux/macOS: manual UTF-8 encoding of 32-bit codepoints
    std::string result;
    size_t i = 0;
    while(*wstr && (count == static_cast<size_t>(-1) || i < count)) {
        uint32_t cp = static_cast<uint32_t>(*wstr++);
        ++i;

        if(cp < 0x80u)
            result += static_cast<char>(cp);
        else if(cp < 0x800u) {
            result += static_cast<char>(0xC0u | (cp >> 6));
            result += static_cast<char>(0x80u | (cp & 0x3Fu));
        } else if(cp < 0x10000u) {
            result += static_cast<char>(0xE0u | (cp >> 12));
            result += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
            result += static_cast<char>(0x80u | (cp & 0x3Fu));
        } else {
            result += static_cast<char>(0xF0u | (cp >> 18));
            result += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
            result += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
            result += static_cast<char>(0x80u | (cp & 0x3Fu));
        }
    }
    return result;
#endif
}

// ============================================================================
//  File-scope helper: canonical emoji glyph range table
//
//  Every range listed here is baked into the atlas texture.
//  Any codepoint NOT in this table renders as '?' even if the .ttf has it.
//
//  Ranges:
//   0x0020-0x00FF  Basic Latin + Latin-1 Supplement  (normal text)
//   0x2600-0x27BF  Misc Symbols + Dingbats            (✓ ✗ ⚠ ⚡ ☀ ★)
//   0x2B00-0x2BFF  Misc Symbols and Arrows
//   0x1F300-0x1F5FF  Misc Symbols and Pictographs     (🌍 🎃 🔑)
//   0x1F600-0x1F64F  Emoticons                        (😀 😔 😊 🤣) ← was MISSING
//   0x1F680-0x1F6FF  Transport and Map                (🚀 🚗 ✈)
//   0x1F900-0x1F9FF  Supplemental Symbols             (🤣 🧠 🦷)
//
//  SENTINEL: two consecutive zeros — ImGui reads pairs until it hits {0,0}.
//  STATIC:   pointer must remain valid until after ImFontAtlas::Build().
// ============================================================================
static const ImWchar* GetEmojiRanges() {
    static const ImWchar ranges[] = {
        0x0020, 0x00FF,    // Basic Latin + Latin-1 Supplement
        0x2600, 0x27BF,    // Misc Symbols and Dingbats
        0x2B00, 0x2BFF,    // Misc Symbols and Arrows
        0x1F300, 0x1F5FF,  // Misc Symbols and Pictographs
        0x1F600, 0x1F64F,  // Emoticons  (😀 😔 😊 — was missing, caused ?????)
        0x1F680, 0x1F6FF,  // Transport and Map  (🚀 🚗 ✈)
        0x1F900, 0x1F9FF,  // Supplemental Symbols and Pictographs
        0, 0               // sentinel — TWO zeros required by ImGui
    };
    return ranges;
}

// ============================================================================
//  Constructor / Destructor
// ============================================================================

FontManager::FontManager()
    : m_SelectedFontIndex(0), m_emoji_font(nullptr) {
#ifdef IMGUI_FONTS_FOLDER
    m_folder_path = ResolveFolderPath(IMGUI_FONTS_FOLDER);
#endif
}

FontManager::~FontManager() = default;

// ============================================================================
//  Path helpers
// ============================================================================

std::string FontManager::ResolveFolderPath(const std::string& relative_path) {
    try {
        fs::path current_dir = fs::current_path();
        fs::path resolved = current_dir / relative_path;
        return fs::absolute(resolved).string();
    } catch(const std::exception&) {
        return relative_path;
    }
}

std::vector<std::string> FontManager::GetFontFilesFromFolder() {
    std::vector<std::string> files;
    try {
        if(!fs::exists(m_folder_path) || !fs::is_directory(m_folder_path))
            return files;

        for(const auto& entry : fs::directory_iterator(m_folder_path)) {
            if(entry.is_regular_file()) {
                std::string fname = entry.path().filename().string();
                if(IsFontFile(fname))
                    files.push_back(entry.path().string());
            }
        }
        std::sort(files.begin(), files.end());
    } catch(const std::exception&) {}
    return files;
}

bool FontManager::IsFontFile(const std::string& filename) const {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return std::tolower(c); });

    static const std::vector<std::string> exts = {
        ".ttf", ".otf", ".ttc", ".otc", ".woff"
    };
    for(const auto& ext : exts)
        if(lower.size() >= ext.size() &&
            lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0)
            return true;
    return false;
}

bool FontManager::IsEmojiFontFile(const std::string& filename) const {
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return std::tolower(c); });

    static const std::vector<std::string> emoji_names = {
        "emoji", "noto color emoji", "noto emoji",
        "apple color emoji", "segoe color emoji", "twemoji", "emojicc"
    };
    for(const auto& name : emoji_names)
        if(lower.find(name) != std::string::npos)
            return IsFontFile(filename);
    return false;
}

std::string FontManager::GetWindowsFontsFolder() const {
    const char* windir = std::getenv("WINDIR");
    if(windir) return std::string(windir) + "\\Fonts\\";
    return "C:\\Windows\\Fonts\\";
}

std::string FontManager::ExtractFontName(const char* descriptor) const {
    if(!descriptor) return "Unknown";
    std::string name = descriptor;
    size_t slash = name.find_last_of("/\\");
    if(slash != std::string::npos) name = name.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if(dot != std::string::npos) name = name.substr(0, dot);
    return name;
}

// ============================================================================
//  Core internal loader
//
//  Loads every base font from m_folder_path.  If emoji_path is non-empty,
//  immediately after each base font an emoji merge is added — this is the
//  ONLY correct way to get emoji into every font slot with the public API.
//
//  WHY CONSECUTIVE PAIRS ARE REQUIRED
//  ------------------------------------
//  ImGui tracks font configuration in io.Fonts->ConfigData (an array of
//  ImFontConfig structs, one per AddFont* call).  When MergeMode=true, ImGui
//  merges the new glyphs into the ImFont that owns the PREVIOUS ConfigData
//  entry — i.e., the font added by the immediately preceding non-merge call.
//
//  Reordering Fonts[] pointers after the fact has no effect on ConfigData
//  order and therefore cannot change which slot MergeMode targets.
//
//  The only correct approach: add base font → add emoji merge → add next base
//  font → add emoji merge → … in strict alternation.
// ============================================================================
static ImFont* AddEmojiMerge(ImGuiIO& io,
    const std::string& emoji_path,
    float size) {
    // Called immediately after a base font AddFontFromFileTTF.
    // MergeMode=true merges into that just-added base font slot.
    ImFontConfig cfg;
    cfg.MergeMode = true;  // merge into the preceding base font slot
    cfg.PixelSnapH = true;
    cfg.OversampleH = 1;     // 1x is sufficient for emoji; saves atlas memory
    cfg.OversampleV = 1;
    // Fonte de texto — hinting padrão (sem flags) = máxima qualidade
cfg.FontLoaderFlags = 0;

// Fonte de emoji — só LoadColor
cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LoadColor;
    return io.Fonts->AddFontFromFileTTF(
        emoji_path.c_str(),
        size,
        &cfg,
        GetEmojiRanges()  // codepoint ranges to bake — includes 0x1F600 Emoticons
    );
}

// ============================================================================
//  LoadAllFonts  (public)
// ============================================================================

bool FontManager::LoadAllFonts(float base_size) {
    std::vector<float> sizes = { base_size };
    return LoadAllFonts(sizes);
}

bool FontManager::LoadAllFonts(const std::vector<float>& sizes) {
    // Load base fonts only — no emoji.
    // Call LoadEmojiFromSystemFonts() or LoadAllFontsWithEmoji() afterwards.
    return LoadAllFontsInternal(sizes, "");
}

// ============================================================================
//  LoadAllFontsWithEmoji  (public convenience)
//
//  Finds the system emoji font and loads all base fonts with emoji merged.
//  Replaces the two-step LoadAllFonts + LoadEmojiFromSystemFonts pattern.
// ============================================================================
bool FontManager::LoadAllFontsWithEmoji(float base_size) {
    // Locate the emoji font file first
    std::string emoji_path = FindSystemEmojiFont();
    if(emoji_path.empty()) {
        printf("[FontManager] LoadAllFontsWithEmoji: no emoji font found, "
            "loading base fonts only.\n");
    }

    std::vector<float> sizes = { base_size };
    return LoadAllFontsInternal(sizes, emoji_path);
}

// ============================================================================
//  LoadAllFontsInternal  (private)
// ============================================================================
bool FontManager::LoadAllFontsInternal(const std::vector<float>& sizes,
    const std::string& emoji_path) {
    if(m_folder_path.empty()) return false;

    auto font_files = GetFontFilesFromFolder();
    if(font_files.empty()) return false;

    ImGuiIO& io = ImGui::GetIO();
    int loaded_count = 0;
    m_Fonts.clear();   // rebuild the list from scratch

    for(const auto& font_path : font_files) {
        // Skip emoji font files in the fonts folder — they are handled via
        // the emoji_path parameter and must not create standalone slots.
        std::string fname = fs::path(font_path).filename().string();
        if(IsEmojiFontFile(fname))
            continue;

        for(float size : sizes) {
            // ── Step 1: add the base font (creates a new ImFont slot) ──────
            ImFont* font = io.Fonts->AddFontFromFileTTF(font_path.c_str(), size);
            if(!font) continue;

            FontInfo info;
            info.font = font;
            info.name = ExtractFontName(font_path.c_str());
            info.size = size;
            m_Fonts.push_back(info);
            loaded_count++;

            // ── Step 2: immediately merge emoji INTO this slot ───────────
            // This must happen right here, before any other base font is added.
            // MergeMode targets the most recent non-merge slot — which is the
            // font we just added above.
            if(!emoji_path.empty()) {
                ImFont* em = AddEmojiMerge(io, emoji_path, size);
                if(em) {
                    m_emoji_font = em; // keep the last successful merge as sentinel
                    printf("[FontManager]   %s (%.0fpx) + emoji OK\n",
                        info.name.c_str(), size);
                } else {
                    printf("[FontManager]   %s (%.0fpx) loaded, emoji merge FAILED\n",
                        info.name.c_str(), size);
                }
            }
        }
    }

    // DO NOT call io.Fonts->Build() here.
    // The Vulkan/SDL backend must do exactly one Build() + texture upload after
    // ALL AddFont* calls are complete.  Let the backend's init sequence do it.
    return loaded_count > 0;
}

// ============================================================================
//  FindSystemEmojiFont  (private helper)
//
//  Returns the absolute path of the first emoji font found on disk,
//  or "" if none is available.
// ============================================================================
std::string FontManager::FindSystemEmojiFont() const {
    std::string sys = GetWindowsFontsFolder();

    static const std::vector<std::string> candidates = {
        "arialuni.ttf",       // Arial Unicode MS — monochrome, older Windows
        "seguiemj.ttf",        // Segoe UI Emoji — color, Windows 8.1+
        "segoeuiemj.ttf",      // alternate name seen on some Windows builds
        "seguisym.ttf",        // Segoe UI Symbol — monochrome, older Windows
        "NotoColorEmoji.ttf",  // Noto Color Emoji
        "AppleColorEmoji.ttf", // Apple Color Emoji
        "Twemoji.ttf",
    };

    for(const auto& name : candidates) {
        std::string fp = sys + name;
        if(fs::exists(fp)) return fp;
    }
    return "";
}

// ============================================================================
//  LoadEmojiFromSystemFonts  (public)
//
//  The correct approach when called AFTER LoadAllFonts():
//  Clears the atlas and reloads every base font with emoji merged consecutively.
//
//  Why reload rather than patch?
//  ImGui ConfigData order cannot be changed after the fact; the only way to
//  merge emoji into every existing slot is to re-add them in the correct
//  base+merge pair sequence.  Since Build() hasn't been called yet (we
//  removed it from LoadAllFonts), clearing and reloading is cheap — it only
//  manipulates CPU-side config lists, not GPU resources.
// ============================================================================
ImFont* FontManager::LoadEmojiFromSystemFonts(float size) {
    std::string emoji_path = FindSystemEmojiFont();
    if(emoji_path.empty()) {
        printf("[FontManager] WARNING: No system emoji font found. "
            "Emoji will show as '?'.\n");
        return nullptr;
    }

    printf("[FontManager] Emoji font: %s\n", emoji_path.c_str());

    // Clear the atlas and reload all base fonts + emoji in consecutive pairs.
    // This is safe because Build() has not been called yet — no GPU texture
    // has been created from this atlas data.
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();  // discard all previously queued AddFont* calls

    // Also add ProggyClean (ImGui's built-in default) back, with emoji merged,
    // so slot 0 works correctly too.
    io.Fonts->AddFontDefault();  // slot 0: ProggyClean (ASCII)
    ImFont* em0 = AddEmojiMerge(io, emoji_path, size);  // merge emoji into slot 0
    if(em0) printf("[FontManager]   ProggyClean (%.0fpx) + emoji OK\n", size);

    // Reload all user fonts, each immediately followed by an emoji merge.
    std::vector<float> sizes = { size };
    bool ok = LoadAllFontsInternal(sizes, emoji_path);

    if(m_emoji_font) {
        printf("[FontManager] Emoji merged into all %d font slot(s).\n",
            io.Fonts->Fonts.Size / 2);  // each slot has base+merge = 2 ConfigData
    } else {
        printf("[FontManager] Emoji merge produced no results.\n");
    }

    (void) ok;
    return m_emoji_font;
}

// ============================================================================
//  LoadEmojis / LoadEmojiAuto  (from misc/fonts/ folder)
// ============================================================================

ImFont* FontManager::LoadEmojis(const std::string& emoji_font_filename, float size) {
    if(m_folder_path.empty()) return nullptr;
    try {
        std::string full_path = (fs::path(m_folder_path) / emoji_font_filename).string();
        if(!fs::exists(full_path)) return nullptr;

        // Same pattern: clear atlas, re-add default font + emoji, then all
        // user fonts each paired with an emoji merge.
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();

        io.Fonts->AddFontDefault();
        AddEmojiMerge(io, full_path, size);

        LoadAllFontsInternal({ size }, full_path);
        return m_emoji_font;
    } catch(const std::exception&) {}
    return nullptr;
}

ImFont* FontManager::LoadEmojiAuto(float size) {
    if(m_folder_path.empty()) return nullptr;
    try {
        if(!fs::exists(m_folder_path) || !fs::is_directory(m_folder_path))
            return nullptr;

        for(const auto& entry : fs::directory_iterator(m_folder_path)) {
            if(entry.is_regular_file()) {
                std::string fname = entry.path().filename().string();
                if(IsEmojiFontFile(fname))
                    return LoadEmojis(fname, size);
            }
        }
    } catch(const std::exception&) {}
    return nullptr;
}

// ============================================================================
//  LoadFont  (single font, public)
// ============================================================================
ImFont* FontManager::LoadFont(const std::string& filename, float size) {
    if(m_folder_path.empty()) return nullptr;
    try {
        std::string full_path = (fs::path(m_folder_path) / filename).string();
        if(!fs::exists(full_path)) return nullptr;

        ImGuiIO& io = ImGui::GetIO();
        ImFont* font = io.Fonts->AddFontFromFileTTF(full_path.c_str(), size);
        if(font) {
            FontInfo info;
            info.font = font;
            info.name = ExtractFontName(filename.c_str());
            info.size = size;
            m_Fonts.push_back(info);
            return font;
        }
    } catch(const std::exception&) {}
    return nullptr;
}

// ============================================================================
//  Utilities
// ============================================================================

void FontManager::CollectLoadedFonts() {
    m_Fonts.clear();
    ImGuiIO& io = ImGui::GetIO();
    for(int i = 0; i < io.Fonts->Fonts.Size; i++) {
        FontInfo info;
        info.font = io.Fonts->Fonts[i];
        info.name = "Font " + std::to_string(i);
        info.size = 13.0f;
        m_Fonts.push_back(info);
    }
}

ImFont* FontManager::GetFont(int index) const {
    if(index >= 0 && index < (int) m_Fonts.size())
        return m_Fonts[index].font;
    return nullptr;
}

void FontManager::ShowFontSelector(const char* label, ImFont** current_font) {
    if(m_Fonts.empty()) return;
    int current_idx = 0;
    for(int i = 0; i < (int) m_Fonts.size(); i++)
        if(m_Fonts[i].font == *current_font) { current_idx = i; break; }

    std::string preview = m_Fonts[current_idx].name + " (" +
        std::to_string((int) m_Fonts[current_idx].size) + "px)";
    if(ImGui::BeginCombo(label, preview.c_str())) {
        for(int i = 0; i < (int) m_Fonts.size(); i++) {
            bool sel = (current_idx == i);
            ImGui::PushFont(m_Fonts[i].font, m_Fonts[i].size);
            std::string item = m_Fonts[i].name + " (" +
                std::to_string((int) m_Fonts[i].size) + "px)";
            if(ImGui::Selectable(item.c_str(), sel)) {
                *current_font = m_Fonts[i].font;
                current_idx = i;
            }
            ImGui::PopFont();
            if(sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void FontManager::ShowFontDemoWindow(bool* p_open) {
    if(!ImGui::Begin("Font Gallery", p_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }
    ImGui::Text("Total Fonts Loaded: %d", GetFontCount());
    if(m_emoji_font) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Emoji Font Loaded");
    }
    ImGui::Separator();
    for(int i = 0; i < (int) m_Fonts.size(); i++) {
        ImGui::PushID(i);
        ImGui::Text("[%d] %s (%.1f px)", i, m_Fonts[i].name.c_str(), m_Fonts[i].size);
        ImGui::PushFont(m_Fonts[i].font, m_Fonts[i].size);
        ImGui::TextWrapped("The quick brown fox jumps over the lazy dog");
        ImGui::TextWrapped("0123456789  !@#$%%^&*()_+-={}[]|:;<>?,./");
        {
            // L"" wide literal converted to UTF-8 for ImGui::TextUnformatted
            // WideToUtf8 is defined below as a file-scope helper
            std::string emoji_test = WideToUtf8(
                L"Emoji test: \U0001F600 \U0001F680 \u26A0 \u2713");
            ImGui::TextUnformatted(emoji_test.c_str());
        }
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::End();
}

void FontManager::ShowEmojiPickerWindow(bool* p_open) {
    if(!ImGui::Begin("Emoji Picker", p_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End(); return;
    }
    if(!m_emoji_font) {
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "No emoji font loaded!");
        ImGui::End(); return;
    }
    ImGui::TextWrapped("Click an emoji to copy to clipboard");
    ImGui::Separator();

    // Wide string literals (L"") used as the source for emoji text.
    // Each entry is {button label (narrow), wide emoji string}.
    // At render time the wchar_t* is converted to UTF-8 for ImGui.
    static const std::vector<std::pair<const char*, const wchar_t*>> cats = {
        { "Smileys", L"\U0001F600\U0001F601\U0001F602\U0001F923\U0001F603\U0001F604\U0001F605\U0001F606\U0001F609\U0001F60A\U0001F60B\U0001F60D\U0001F60E\U0001F60F\U0001F612\U0001F61E\U0001F614\U0001F61F\U0001F620\U0001F621\U0001F608\U0001F480\U0001F4A9\U0001F921" },
        { "Hands", L"\U0001F44B\U0001F91A\U0001F590\u270B\U0001F596\U0001F44C\u270C\U0001F91E\U0001F44D\U0001F44E\u270A\U0001F44A\U0001F44F\U0001F64C\U0001F91D\U0001F64F" },
        { "Animals", L"\U0001F436\U0001F431\U0001F42D\U0001F430\U0001F98A\U0001F43B\U0001F43C\U0001F428\U0001F42F\U0001F981\U0001F42E\U0001F437\U0001F438\U0001F435\U0001F414\U0001F427\U0001F986\U0001F985\U0001F989\U0001F987\U0001F434\U0001F984\U0001F41D\U0001F98B\U0001F422\U0001F40D\U0001F42C\U0001F433\U0001F988" },
        { "Food", L"\U0001F34F\U0001F34E\U0001F34A\U0001F34B\U0001F34C\U0001F349\U0001F347\U0001F353\U0001F352\U0001F351\U0001F34D\U0001F951\U0001F346\U0001F345\U0001F33D\U0001F35E\U0001F950\U0001F355\U0001F354\U0001F32E\U0001F35C\U0001F363\U0001F366\U0001F382\U0001F370\U0001F36B\U0001F37F\U0001F369\U0001F36A" },
        { "Travel", L"\u2708\U0001F6EB\U0001F681\U0001F680\U0001F682\U0001F684\U0001F687\U0001F68C\U0001F691\U0001F692\U0001F693\U0001F695\U0001F697\U0001F699\U0001F69C\U0001F3CE\U0001F6B2\U0001F6F4\U0001F6A2\u2693\U0001F5FA\U0001F30D\U0001F5FC\U0001F3D4\U0001F30B\U0001F3D5\U0001F3D6\U0001F3D9" },
    };

    ImGui::PushFont(m_emoji_font, m_emoji_font->LegacySize);
    static int sel_cat = 0;
    for(int i = 0; i < (int) cats.size(); i++) {
        if(ImGui::Button(cats[i].first, ImVec2(80, 0))) sel_cat = i;
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::Separator();

    if(sel_cat >= 0 && sel_cat < (int) cats.size()) {
        // cats[i].second is const wchar_t*.  Each wchar_t is one codepoint,
        // so we walk one element at a time, convert each to UTF-8 for ImGui.
        const wchar_t* emojis = cats[sel_cat].second;
        ImVec2         btn_sz = ImVec2(50, 50);
        float          avail = ImGui::GetContentRegionAvail().x;
        int            per_row = std::max(1, (int) (avail / (btn_sz.x + ImGui::GetStyle().ItemSpacing.x)));
        int count = 0;

        for(const wchar_t* wc = emojis; *wc; ++wc) {
            // Convert this single wchar_t codepoint to a UTF-8 std::string
            // so ImGui::Button (which requires const char*) can display it.
            std::string emoji = WideToUtf8(wc, 1);

            ImGui::PushID(count);
            if(ImGui::Button(emoji.c_str(), btn_sz))
                ImGui::SetClipboardText(emoji.c_str());
            ImGui::PopID();
            count++;
            if(count % per_row != 0) ImGui::SameLine();
        }
    }
    ImGui::PopFont();
    ImGui::End();
}
