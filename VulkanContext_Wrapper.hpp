#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "imgui.h"
#include "imgui_impl_vulkan.h"

#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
#include <volk.h>
#endif

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    // Initialize Vulkan instance, physical device and logical device
    bool Initialize(const std::vector<const char*>& instance_extensions);

    // Create window surface and swapchain
    bool SetupWindow(SDL_Window* window, int width, int height);

    // Cleanup resources (call CleanupWindow before Cleanup)
    void Cleanup();
    void CleanupWindow();

    // ---- Getters for Vulkan objects ----------------------------------------
    VkInstance                GetInstance()        const { return m_Instance; }
    VkPhysicalDevice          GetPhysicalDevice()  const { return m_PhysicalDevice; }
    VkDevice                  GetDevice()          const { return m_Device; }
    VkQueue                   GetQueue()           const { return m_Queue; }
    uint32_t                  GetQueueFamily()     const { return m_QueueFamily; }
    VkDescriptorPool          GetDescriptorPool()  const { return m_DescriptorPool; }
    VkPipelineCache           GetPipelineCache()   const { return m_PipelineCache; }
    ImGui_ImplVulkanH_Window* GetMainWindowData() { return &m_MainWindowData; }

    // ---- Frame operations --------------------------------------------------
    void FrameRender(ImDrawData* draw_data);
    void FramePresent();

    // ---- Swapchain management ----------------------------------------------
    void RebuildSwapChain(int width, int height);
    bool NeedsSwapChainRebuild()           const { return m_SwapChainRebuild; }
    void SetSwapChainRebuild(bool rebuild) { m_SwapChainRebuild = rebuild; }

    // ---- VSync control -----------------------------------------------------
    void SetVSync(bool enabled);
    bool GetVSync() const { return m_VSyncEnabled; }

    // ---- Error checking ----------------------------------------------------
    static void CheckVkResult(VkResult err);

private:
    VkAllocationCallbacks* m_Allocator;       ///< Custom allocator (nullptr = default)
    VkInstance               m_Instance;        ///< Vulkan instance
    VkPhysicalDevice         m_PhysicalDevice;  ///< Selected GPU
    VkDevice                 m_Device;          ///< Logical device
    uint32_t                 m_QueueFamily;     ///< Graphics queue family index
    VkQueue                  m_Queue;           ///< Graphics queue handle
    VkPipelineCache          m_PipelineCache;   ///< Pipeline cache (unused, kept for future use)
    VkDescriptorPool         m_DescriptorPool;  ///< Descriptor pool for ImGui

    VkPresentModeKHR         m_PresentMode;     ///< Informational mirror of current desired mode
    ImGui_ImplVulkanH_Window m_MainWindowData;  ///< Swapchain, render pass, framebuffers
    uint32_t                 m_MinImageCount;   ///< Minimum swapchain image count
    bool                     m_SwapChainRebuild;///< True when swapchain must be recreated
    bool                     m_VSyncEnabled;    ///< Authoritative VSync flag (read by RebuildSwapChain)

#ifdef APP_USE_VULKAN_DEBUG_REPORT
    VkDebugUtilsMessengerEXT m_DebugMessenger;  ///< Validation layer messenger
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT  messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT         messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
#endif

    static bool IsExtensionAvailable(
        const std::vector<VkExtensionProperties>& properties,
        const char* extension);
};
