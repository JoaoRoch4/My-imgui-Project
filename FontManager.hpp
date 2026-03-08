#pragma once
#include "pch.hpp"

/**
 * @brief Manages ImGui font loading from a folder, with per-font emoji merging.
 *
 * KEY USAGE — call LoadAllFontsWithEmoji() instead of the two-step pattern:
 *
 *   font_manager.LoadAllFontsWithEmoji(13.0f * main_scale);
 *
 * This loads every base font and immediately merges seguiemj.ttf into each
 * one consecutively, which is the only correct way to get emoji in every slot.
 */
class FontManager {
public:
    FontManager();
    ~FontManager();

    // -------------------------------------------------------------------------
    // Primary API — use this
    // -------------------------------------------------------------------------

    /**
     * @brief Loads all fonts from IMGUI_FONTS_FOLDER, merging the system emoji
     *        font into each one immediately after it is loaded.
     *
     * This is the one-shot replacement for LoadAllFonts + LoadEmojiFromSystemFonts.
     * Do NOT call both — call this alone.
     *
     * @param base_size  Pixel size for all fonts.
     * @return true if at least one base font was loaded.
     */
    bool LoadAllFontsWithEmoji(float base_size = 13.0f);

    // -------------------------------------------------------------------------
    // Legacy API — kept for compatibility
    // -------------------------------------------------------------------------

    /** @brief Loads base fonts only (no emoji). Follow with LoadEmojiFromSystemFonts(). */
    bool LoadAllFonts(float base_size = 13.0f);
    bool LoadAllFonts(const std::vector<float>& sizes);

    /** @brief Loads a single font by filename from the configured folder. */
    ImFont* LoadFont(const std::string& filename, float size = 13.0f);

    /**
     * @brief Finds seguiemj.ttf, clears the atlas, and reloads all fonts with
     *        emoji merged into each.  Only call this after LoadAllFonts() and
     *        BEFORE the backend builds the atlas (before the first NewFrame).
     */
    ImFont* LoadEmojiFromSystemFonts(float size = 13.0f);

    /** @brief Loads emoji from a specific file in the fonts folder. */
    ImFont* LoadEmojis(const std::string& emoji_font_filename, float size = 13.0f);

    /** @brief Auto-detects an emoji font in the fonts folder and loads it. */
    ImFont* LoadEmojiAuto(float size = 13.0f);

    // -------------------------------------------------------------------------
    // Queries and UI
    // -------------------------------------------------------------------------

    void    CollectLoadedFonts();
    void    ShowFontSelector(const char* label, ImFont** current_font);
    void    ShowFontDemoWindow(bool* p_open = nullptr);
    void    ShowEmojiPickerWindow(bool* p_open = nullptr);

    int     GetFontCount()   const { return (int) m_Fonts.size(); }
    ImFont* GetFont(int idx) const;
    ImFont* GetEmojiFont()   const { return m_emoji_font; }
    bool    HasEmojiFont()   const { return m_emoji_font != nullptr; }
    const std::string& GetFolderPath() const { return m_folder_path; }

     /**
     * @brief Empurra uma fonte pelo nome para a pilha de fontes do ImGui.
     *
     * Procura em m_Fonts a primeira entrada cujo FontInfo::name contenha
     * font_name (comparação case-insensitive). Se encontrada, chama
     * ImGui::PushFont() com o ImFont* correspondente.
     *
     * Deve ser emparelhado com PopFont() — exactamente como ImGui::PushFont.
     *
     * @param font_name  Substring do nome da fonte (ex: "Roboto", "OpenSans").
     * @return           true se a fonte foi encontrada e empurrada;
     *                   false se não foi encontrada (nenhum PushFont é feito).
     */
    bool PushFontByName(const std::string& font_name);

    /**
     * @brief Retira a fonte do topo da pilha do ImGui.
     *
     * Wrapper directo de ImGui::PopFont() — disponível aqui para simetria
     * com PushFontByName() e para centralizar a lógica de font switching
     * no FontManager.
     */
    void PopFont();

private:
    struct FontInfo {
        ImFont* font;
        std::string name;
        float       size;
    };

    std::vector<FontInfo> m_Fonts;
    ImFont* m_emoji_font;
    int                   m_SelectedFontIndex;
    std::string           m_folder_path;

    // Internal: load all fonts, optionally merging emoji_path after each one
    bool LoadAllFontsInternal(const std::vector<float>& sizes,
        const std::string& emoji_path);

    // Finds the first available system emoji font path, or ""
    std::string FindSystemEmojiFont() const;
    std::string GetWindowsFontsFolder() const;
    std::string ResolveFolderPath(const std::string& relative_path);
    std::string ExtractFontName(const char* descriptor) const;

    std::vector<std::string> GetFontFilesFromFolder();
    bool IsFontFile(const std::string& filename) const;
    bool IsEmojiFontFile(const std::string& filename) const;
};
