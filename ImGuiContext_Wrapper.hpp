#pragma once
#include "pch.hpp"
/**
 * @brief Wraps the ImGui context, SDL3 and Vulkan backends.
 *
 * DEFAULT FONT
 * ------------
 * After Initialize() the active font is whatever FontManager loaded first.
 * Call SetDefaultFont() with any ImFont* to change it:
 *
 *   font_manager.LoadAllFontsWithEmoji(size);
 *   imgui_context.SetDefaultFont(font_manager.GetFont(0)); // first = Roboto
 *
 * Or use the convenience helper FindDefaultFont() which searches by name:
 *
 *   imgui_context.SetDefaultFont(
 *       imgui_context.FindDefaultFont("Roboto"));
 */
class ImGuiContext_Wrapper {
public:
    ImGuiContext_Wrapper();
    ~ImGuiContext_Wrapper();

    // Initialize ImGui with SDL and Vulkan backends.
    bool Initialize(SDL_Window* window, VulkanContext* vulkan_context, float scale);

    // -------------------------------------------------------------------------
    // Default font selection
    // -------------------------------------------------------------------------

    /**
     * @brief Sets the default (fallback) font used by ImGui::Begin and all
     *        windows that do not explicitly call PushFont().
     *
     * Must be called AFTER the font atlas has been built by the backend
     * (i.e., after the first NewFrame() or after manually triggering a rebuild).
     *
     * @param font  Any ImFont* returned by io.Fonts->Fonts[i] or FontManager::GetFont().
     *              Passing nullptr resets to ImGui's built-in ProggyClean.
     */
    void SetDefaultFont(ImFont* font);

    /**
     * @brief Searches io.Fonts->Fonts for a font whose ConfigData name contains
     *        @p name_substring (case-insensitive).  Returns the first match, or
     *        nullptr if not found.
     *
     * Use this to select Roboto without needing to know its slot index:
     * @code
     *   imgui_context.SetDefaultFont(imgui_context.FindDefaultFont("Roboto"));
     * @endcode
     *
     * @param name_substring  Partial font name to search for, e.g. "Roboto".
     */
    ImFont* FindDefaultFont(const char* name_substring) const;

    /** @brief Returns the current default font (nullptr = ProggyClean). */
    ImFont* GetDefaultFont() const { return m_DefaultFont; }

    // -------------------------------------------------------------------------
    // Frame operations
    // -------------------------------------------------------------------------

    void NewFrame();
    void Render();
    void RenderPlatformWindows();
    void ProcessEvent(SDL_Event* event);

    // -------------------------------------------------------------------------
    // Font helpers
    // -------------------------------------------------------------------------

    /** @brief Adds a single font to the atlas. Does NOT call Build(). */
    ImFont* LoadFont(const char* filepath, float size = 16.0f);

    // -------------------------------------------------------------------------
    // Cleanup
    // -------------------------------------------------------------------------

    void Shutdown();

    // -------------------------------------------------------------------------
    // Getters
    // -------------------------------------------------------------------------

    ImGuiIO& GetIO() { return ImGui::GetIO(); }
    bool     WantsViewports() const;

private:
    bool    m_Initialized; ///< True after Initialize() succeeds
    ImFont* m_DefaultFont; ///< The font set as io.FontDefault (nullptr = ProggyClean)
};
