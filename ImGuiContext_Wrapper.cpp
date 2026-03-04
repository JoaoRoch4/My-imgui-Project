/**
 * @file ImGuiContext_Wrapper.cpp
 * @brief ImGui context initialization, frame management, and font defaults.
 *
 * DEFAULT FONT WORKFLOW
 * ---------------------
 * ImGui uses io.FontDefault for every window that does not call PushFont().
 * To make Roboto (or any other font) the global default:
 *
 *  1. Load fonts via FontManager BEFORE the first NewFrame():
 *       font_manager.LoadAllFontsWithEmoji(size);
 *
 *  2. After loading, call SetDefaultFont() with the desired ImFont*:
 *       imgui_context.SetDefaultFont(
 *           imgui_context.FindDefaultFont("Roboto"));
 *
 *  3. The next NewFrame() will upload the atlas (if not yet built) and
 *     io.FontDefault will already be set.
 *
 * FindDefaultFont() searches ConfigData[0].Name in every atlas slot for a
 * case-insensitive substring match, so "roboto", "Roboto", "Roboto-Regular"
 * all find the same font.
 */

#include "pch.hpp"
#include "ImGuiContext_Wrapper.hpp"
#include "MicaTheme.h"
#include "VulkanContext_Wrapper.hpp"


 // ============================================================================
 //  Constructor / Destructor
 // ============================================================================

 /**
  * @brief Initializes members to safe defaults before Initialize() is called.
  */
ImGuiContext_Wrapper::ImGuiContext_Wrapper()
    : m_Initialized(false)
    , m_DefaultFont(nullptr)  // nullptr = use ProggyClean until SetDefaultFont() is called
{
}

ImGuiContext_Wrapper::~ImGuiContext_Wrapper() {}

// ============================================================================
//  Initialize
// ============================================================================

/**
 * @brief Creates the ImGui context, configures IO flags, applies the Mica
 *        theme, initializes SDL3 and Vulkan backends.
 *
 * Font loading happens AFTER this call (via FontManager), so io.FontDefault
 * is not set here.  Call SetDefaultFont() once FontManager has loaded fonts.
 *
 * @param window           SDL3 window handle.
 * @param vulkan_context   Initialized VulkanContext.
 * @param scale            DPI content scale from SDL_GetDisplayContentScale().
 * @return true on success.
 */
bool ImGuiContext_Wrapper::Initialize(SDL_Window* window,
    VulkanContext* vulkan_context,
    float           scale) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
        ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;   // keyboard navigation
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;    // gamepad navigation
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;        // dockable windows
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;     // multi-viewport
    io.ConfigFlags |= ImGuiViewportFlags_TopMost;
    io.ConfigFlags |= ImGuiViewportFlags_IsMinimized;
    io.ConfigFlags |= ImGuiViewportFlags_IsFocused;
    // Apply Windows 11 Mica theme; fall back to default if file is missing
    MicaTheme::ThemeConfig theme = MicaTheme::LoadThemeFromFile("mica_theme.json");
    MicaTheme::ApplyMicaTheme(theme);

    // DPI scaling — applied to all built-in sizes and font rendering
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    // Multi-viewport: keep platform windows visually consistent
    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 8.0f;   // match Mica rounded corners
        style.Colors[ImGuiCol_WindowBg].w = 0.95f; // slight transparency
    }

    // SDL3 + Vulkan backend initialization
    ImGui_ImplSDL3_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = vulkan_context->GetInstance();
    init_info.PhysicalDevice = vulkan_context->GetPhysicalDevice();
    init_info.Device = vulkan_context->GetDevice();
    init_info.QueueFamily = vulkan_context->GetQueueFamily();
    init_info.Queue = vulkan_context->GetQueue();
    init_info.PipelineCache = vulkan_context->GetPipelineCache();
    init_info.DescriptorPool = vulkan_context->GetDescriptorPool();
    init_info.MinImageCount = 2;
    init_info.ImageCount = vulkan_context->GetMainWindowData()->ImageCount;
    init_info.Allocator = nullptr;
    init_info.PipelineInfoMain.RenderPass = vulkan_context->GetMainWindowData()->RenderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.CheckVkResultFn = VulkanContext::CheckVkResult;

    ImGui_ImplVulkan_Init(&init_info);

    m_Initialized = true;
    return true;
}

// ============================================================================
//  Default font
// ============================================================================

/**
 * @brief Sets io.FontDefault so ImGui uses @p font for all unstyled windows.
 *
 * Call this after FontManager::LoadAllFontsWithEmoji() and before the first
 * NewFrame().  The backend builds the atlas on the first NewFrame() call, at
 * which point io.FontDefault is already set.
 *
 * Passing nullptr resets to ImGui's built-in ProggyClean (slot 0).
 *
 * @param font  ImFont* from io.Fonts->Fonts[i] or FontManager::GetFont(i).
 */
void ImGuiContext_Wrapper::SetDefaultFont(ImFont* font) {
    m_DefaultFont = font;        // remember for GetDefaultFont()
    ImGui::GetIO().FontDefault = font; // nullptr is valid — means ProggyClean

    if(font)
        printf("[ImGuiContext] Default font set (ptr=%p)\n",
            static_cast<void*>(font));
    else
        printf("[ImGuiContext] Default font reset to ProggyClean\n");
}

/**
 * @brief Finds the first atlas slot whose file path contains @p name_substring
 *        (case-insensitive).
 *
 * ImGui stores the source file path in ImFontConfig::Name for each slot.
 * We lowercase both strings and search for the substring, so "roboto" matches
 * "Roboto-Regular.ttf", "roboto_medium.ttf", etc.
 *
 * @code
 *   // In main.cpp, after LoadAllFontsWithEmoji():
 *   imgui_context.SetDefaultFont(
 *       imgui_context.FindDefaultFont("Roboto"));  // finds Roboto-Regular.ttf
 * @endcode
 *
 * @param name_substring  Case-insensitive substring to search in font names.
 * @return Matching ImFont*, or nullptr if not found.
 */
ImFont* ImGuiContext_Wrapper::FindDefaultFont(const char* name_substring) const {
    if(!name_substring) return nullptr;

    // Build a lowercase version of the search term
    std::string needle = name_substring;
    for(char& c : needle)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    ImGuiIO& io = ImGui::GetIO();

    // Walk every slot in the atlas
    for(int i = 0; i < io.Fonts->Fonts.Size; i++) {
        ImFont* slot = io.Fonts->Fonts[i];
        if(!slot) continue;

        // ImFont::GetDebugName() returns the name ImGui stores internally —
        // this is the file path passed to AddFontFromFileTTF, or "(default)"
        // for AddFontDefault().  It is always a valid const char*.
        std::string haystack = slot->GetDebugName();
        for(char& c : haystack)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if(haystack.find(needle) != std::string::npos) {
            printf("[ImGuiContext] FindDefaultFont(\"%s\") -> slot %d (%s)\n",
                name_substring, i, slot->GetDebugName());
            return slot;
        }
    }

    printf("[ImGuiContext] FindDefaultFont(\"%s\"): not found in %d slot(s)\n",
        name_substring, io.Fonts->Fonts.Size);
    return nullptr;
}

// ============================================================================
//  Frame operations
// ============================================================================

/**
 * @brief Begins a new ImGui frame.  Call once per render loop iteration.
 *
 * On the first call the Vulkan backend builds the font atlas and uploads the
 * texture — this is why all AddFont* calls must happen before the first
 * NewFrame(), and io.FontDefault must be set before then too.
 */
void ImGuiContext_Wrapper::NewFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

/** @brief Finalizes the ImGui frame.  Call after all ImGui:: draw calls. */
void ImGuiContext_Wrapper::Render() {
    ImGui::Render();
}

/** @brief Updates and renders secondary platform windows (multi-viewport). */
void ImGuiContext_Wrapper::RenderPlatformWindows() {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
}

/** @brief Forwards an SDL event to the ImGui SDL3 backend. */
void ImGuiContext_Wrapper::ProcessEvent(SDL_Event* event) {
    ImGui_ImplSDL3_ProcessEvent(event);
}

/** @brief Returns true if the multi-viewport flag is active. */
bool ImGuiContext_Wrapper::WantsViewports() const {
    return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
}

// ============================================================================
//  Font helpers
// ============================================================================

/**
 * @brief Adds a single font file to the atlas.
 *
 * Does NOT call io.Fonts->Build() — the Vulkan backend does that on the first
 * NewFrame().  Call this before the render loop starts.
 *
 * @param filepath  Absolute or relative path to a .ttf / .otf file.
 * @param size      Pixel size.
 * @return          ImFont* on success, nullptr if the file cannot be opened.
 */
ImFont* ImGuiContext_Wrapper::LoadFont(const char* filepath, float size) {
    if(!filepath) return nullptr;

    // Verify the file exists before handing it to ImGui
    FILE* file = nullptr;
    errno_t err{};
    err = fopen_s(&file, filepath, "rb");
    if(!file) return nullptr;
    fclose(file);

    // Add to atlas — Build() will be triggered by the backend on first NewFrame()
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(filepath, size);
}

// ============================================================================
//  Cleanup
// ============================================================================

/**
 * @brief Shuts down backends and destroys the ImGui context.
 *
 * Safe to call even if Initialize() was never called (m_Initialized guard).
 */
void ImGuiContext_Wrapper::Shutdown() {
    if(m_Initialized) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
            ImPlot::DestroyContext();
        ImGui::DestroyContext();
        m_Initialized = false;
        m_DefaultFont = nullptr;
    }
}
