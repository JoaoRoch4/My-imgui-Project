/**
 * @file VulkanContext_Wrapper.cpp
 * @brief Implementação do VulkanContext: instância, device, swapchain e frames Vulkan.
 *
 * CORREÇÃO DE BUG — SetVSync() não atualizava m_VSyncEnabled
 * -----------------------------------------------------------
 * RebuildSwapChain() lê m_VSyncEnabled para escolher o present mode.
 * O código original nunca escrevia nesse campo em SetVSync(), então
 * qualquer rebuild revertia silenciosamente a mudança de VSync.
 * Correção: m_VSyncEnabled = enabled; como primeira linha de SetVSync().
 */

#include "pch.hpp"
#include "VulkanContext_Wrapper.hpp"

 // ============================================================================
 // Construtor / Destrutor
 // ============================================================================

 /**
  * @brief Inicializa todos os membros com valores nulos/padrão seguros.
  *
  * Handles Vulkan começam como VK_NULL_HANDLE para que Cleanup() possa
  * detectar com segurança quais objetos foram realmente criados.
  * m_VSyncEnabled começa true → swapchain inicial usa FIFO (VSync ligado).
  */
VulkanContext::VulkanContext()
    : m_Allocator(nullptr)                      // sem allocator customizado (usa default do driver)
    , m_Instance(VK_NULL_HANDLE)                // instância Vulkan
    , m_PhysicalDevice(VK_NULL_HANDLE)          // GPU física
    , m_Device(VK_NULL_HANDLE)                  // device lógico
    , m_QueueFamily((uint32_t) -1)               // -1 indica "não selecionado"
    , m_Queue(VK_NULL_HANDLE)                   // fila gráfica
    , m_PipelineCache(VK_NULL_HANDLE)           // cache de pipelines (não usado ativamente)
    , m_DescriptorPool(VK_NULL_HANDLE)          // pool de descriptor sets para ImGui
    , m_PresentMode(VK_PRESENT_MODE_FIFO_KHR)  // espelho do modo desejado (começa FIFO)
    , m_MinImageCount(2)                        // mínimo de imagens na swapchain
    , m_SwapChainRebuild(false)                 // sem rebuild pendente no início
    , m_VSyncEnabled(false)                      // VSync ligado por padrão
#ifdef APP_USE_VULKAN_DEBUG_REPORT
    , m_DebugMessenger(VK_NULL_HANDLE)          // messenger da validation layer
#endif
{
}

/**
 * @brief Destrutor vazio — recursos Vulkan devem ser liberados via Cleanup()/CleanupWindow().
 *
 * A ordem importa: CleanupWindow() (surface) antes de Cleanup() (device/instance).
 * O Memory singleton garante essa ordem em DestroyVulkan().
 */
VulkanContext::~VulkanContext() {}

// ============================================================================
// CheckVkResult
// ============================================================================

/**
 * @brief Verifica o resultado de uma chamada Vulkan; aborta em erros fatais.
 *
 * Erros negativos são fatais (device lost, out of memory, etc.) e chamam abort().
 * Erros positivos são informativos (VK_SUBOPTIMAL_KHR) e apenas imprimem.
 * VK_SUCCESS (0) passa sem custo.
 *
 * @param err  Código retornado por uma função Vulkan.
 */
void VulkanContext::CheckVkResult(VkResult err) {
    if(err == VK_SUCCESS)
        return; // caminho feliz — sem overhead

    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err); // exibe o código numérico

    if(err < 0)
        abort(); // erro fatal: estado irrecuperável, termina o processo
}

// ============================================================================
// DebugCallback
// ============================================================================

#ifdef APP_USE_VULKAN_DEBUG_REPORT
/**
 * @brief Callback invocado pela validation layer para cada mensagem de debug.
 *
 * VKAPI_ATTR / VKAPI_CALL garantem a convenção de chamada correta em todas
 * as plataformas (necessário para que o driver consiga chamar a função).
 * Retornar VK_FALSE indica que a chamada original não deve ser abortada.
 *
 * @param messageSeverity  Gravidade: ERROR, WARNING, INFO ou VERBOSE.
 * @param messageType      Categoria: GENERAL, VALIDATION ou PERFORMANCE.
 * @param pCallbackData    Dados da mensagem: ID, nome e texto completo.
 * @param pUserData        Ponteiro customizado passado no registro (não usado).
 */
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    (void) pUserData; // suprime warning de variável não utilizada

    // Converte o tipo de mensagem em string legível
    const char* type_str = "";
    if(messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)    type_str = "[GENERAL]";
    else if(messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) type_str = "[VALIDATION]";
    else if(messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)type_str = "[PERFORMANCE]";

    // Roteia para stderr (erros/warnings) ou stdout (info/verbose)
    if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        fprintf(stderr, "[ERROR] %s %s - %s\n", type_str, pCallbackData->pMessageIdName, pCallbackData->pMessage);
    else if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        fprintf(stderr, "[WARNING] %s %s - %s\n", type_str, pCallbackData->pMessageIdName, pCallbackData->pMessage);
    else if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        fprintf(stdout, "[INFO] %s %s - %s\n", type_str, pCallbackData->pMessageIdName, pCallbackData->pMessage);
    else if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
        fprintf(stdout, "[VERBOSE] %s %s - %s\n", type_str, pCallbackData->pMessageIdName, pCallbackData->pMessage);

    return VK_FALSE; // não aborta a chamada Vulkan original
}
#endif

// ============================================================================
// IsExtensionAvailable
// ============================================================================

/**
 * @brief Verifica se uma extensão está presente na lista enumerada pelo Vulkan.
 *
 * Usada antes de push_back() para não causar VK_ERROR_EXTENSION_NOT_PRESENT.
 *
 * @param properties  Lista de VkExtensionProperties de vkEnumerate*ExtensionProperties.
 * @param extension   Nome da extensão a procurar (ex.: "VK_KHR_portability_enumeration").
 * @return            true se a extensão estiver disponível.
 */
bool VulkanContext::IsExtensionAvailable(
    const std::vector<VkExtensionProperties>& properties,
    const char* extension) {
    for(const VkExtensionProperties& p : properties)
        if(strcmp(p.extensionName, extension) == 0) // comparação exata byte a byte
            return true;
    return false;
}

// ============================================================================
// Initialize
// ============================================================================

/**
 * @brief Cria instância Vulkan, seleciona GPU e cria o logical device.
 *
 * ORDEM:
 *  1. vkCreateInstance            → instância global (+ validation layers se habilitado)
 *  2. SelectPhysicalDevice        → melhor GPU disponível
 *  3. SelectQueueFamilyIndex      → família de filas com suporte gráfico
 *  4. vkCreateDevice              → device lógico com uma fila gráfica
 *  5. vkCreateDescriptorPool      → pool de descriptors para o ImGui Vulkan backend
 *
 * NÃO cria swapchain — isso é feito em SetupWindow().
 *
 * @param instance_extensions  Extensões exigidas pelo SDL (VK_KHR_surface, etc.).
 * @return                     true se todos os objetos Vulkan foram criados com sucesso.
 */
bool VulkanContext::Initialize(const std::vector<const char*>& instance_extensions) {
    VkResult err;

#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
    volkInitialize(); // carrega ponteiros de função Vulkan via volk em vez do loader padrão
#endif

    // ---- Instância Vulkan ---------------------------------------------------
    {
        VkInstanceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

        // Enumera extensões disponíveis no loader (2 chamadas: count depois dados)
        uint32_t properties_count;
        std::vector<VkExtensionProperties> properties;
        vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.data());
        CheckVkResult(err);

          std::vector<const char*> extensions = instance_extensions; // começa com as do SDL

        // VK_KHR_get_physical_device_properties2: necessário para features avançadas de device
        if(IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
            extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

        // VK_KHR_portability_enumeration: necessário em macOS/MoltenVK
    #ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if(IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
    #endif

        // Validation layers + debug messenger (somente em builds de debug)
    #ifdef APP_USE_VULKAN_DEBUG_REPORT
        const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = layers;
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); // obrigatória para o messenger

        // Configura messenger encadeado via pNext para capturar erros DA criação da instância
        VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {};
        debug_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        debug_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug_create_info.pfnUserCallback = DebugCallback;
        debug_create_info.pUserData = nullptr;
        create_info.pNext = &debug_create_info; // encadeado: ativo só durante vkCreateInstance
    #endif

        create_info.enabledExtensionCount = (uint32_t) extensions.size();
        create_info.ppEnabledExtensionNames = extensions.data();
        err = vkCreateInstance(&create_info, m_Allocator, &m_Instance);
        CheckVkResult(err);

    #ifdef IMGUI_IMPL_VULKAN_USE_VOLK
        volkLoadInstance(m_Instance); // carrega funções específicas desta instância via volk
    #endif

        // Cria o messenger permanente (ativo durante toda a vida da instância)
    #ifdef APP_USE_VULKAN_DEBUG_REPORT
        auto vkCreateDebugUtilsMessengerEXT =
            (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
                m_Instance, "vkCreateDebugUtilsMessengerEXT");
        IM_ASSERT(vkCreateDebugUtilsMessengerEXT != nullptr);

        VkDebugUtilsMessengerCreateInfoEXT messenger_info = {};
        messenger_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messenger_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
        messenger_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messenger_info.pfnUserCallback = DebugCallback;
        messenger_info.pUserData = nullptr;

        err = vkCreateDebugUtilsMessengerEXT(m_Instance, &messenger_info, m_Allocator, &m_DebugMessenger);
        CheckVkResult(err);
    #endif
    }

    // ---- Physical device (GPU) ----------------------------------------------

    // ImGui helper prefere GPU dedicada sobre integrada
    m_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(m_Instance);
    if(m_PhysicalDevice == VK_NULL_HANDLE)
        return false; // nenhuma GPU com suporte Vulkan encontrada

    // ---- Família de filas gráficas ------------------------------------------

    m_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(m_PhysicalDevice);
    if(m_QueueFamily == (uint32_t) -1)
        return false; // GPU não tem fila gráfica

    // ---- Logical device -----------------------------------------------------
    {
        ImVector<const char*> device_extensions;
        device_extensions.push_back("VK_KHR_swapchain"); // obrigatória para apresentação

        // Enumera extensões de device disponíveis nesta GPU
        uint32_t properties_count;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &properties_count, nullptr);
        properties.resize(properties_count);
        vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &properties_count, properties.Data);

        // VK_KHR_portability_subset: obrigatória se presente (MoltenVK no macOS)
    #ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if(IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
            device_extensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
    #endif

        const float queue_priority[] = { 1.0f }; // prioridade máxima para a fila gráfica

        VkDeviceQueueCreateInfo queue_info[1] = {};
        queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[0].queueFamilyIndex = m_QueueFamily; // família selecionada acima
        queue_info[0].queueCount = 1;             // uma fila é suficiente para ImGui
        queue_info[0].pQueuePriorities = queue_priority;

        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
        create_info.pQueueCreateInfos = queue_info;
        create_info.enabledExtensionCount = (uint32_t) device_extensions.Size;
        create_info.ppEnabledExtensionNames = device_extensions.Data;

        err = vkCreateDevice(m_PhysicalDevice, &create_info, m_Allocator, &m_Device);
        CheckVkResult(err);

        vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue); // índice 0 dentro da família
    }

    // ---- Descriptor pool ----------------------------------------------------
    {
        // Pool com combined image samplers: suficiente para o backend ImGui Vulkan
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE },
        };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // permite liberar sets individualmente
        pool_info.maxSets = 0;
        for(VkDescriptorPoolSize& s : pool_sizes)
            pool_info.maxSets += s.descriptorCount; // maxSets = soma total dos descritores
        pool_info.poolSizeCount = (uint32_t) IM_COUNTOF(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        err = vkCreateDescriptorPool(m_Device, &pool_info, m_Allocator, &m_DescriptorPool);
        CheckVkResult(err);
    }

    return true;
}

// ============================================================================
// SetupWindow
// ============================================================================

/**
 * @brief Cria a surface Vulkan e a swapchain inicial para a janela SDL3.
 *
 * Deve ser chamado APÓS Initialize() e ANTES de ImGui_ImplVulkan_Init().
 * O present mode inicial é determinado por m_VSyncEnabled, que já reflete
 * qualquer chamada a SetVSync() feita antes de SetupWindow().
 *
 * @param window  Janela SDL3 já criada com SDL_WINDOW_VULKAN.
 * @param width   Largura inicial em pixels.
 * @param height  Altura inicial em pixels.
 * @return        true se surface e swapchain foram criadas com sucesso.
 */
bool VulkanContext::SetupWindow(SDL_Window* window, int width, int height) {
    // Cria a VkSurfaceKHR que conecta Vulkan à janela SDL3
    VkSurfaceKHR surface;
    if(SDL_Vulkan_CreateSurface(window, m_Instance, m_Allocator, &surface) == 0) {
        fprintf(stderr, "Failed to create Vulkan surface.\n");
        return false;
    }

    // Verifica suporte WSI: a fila gráfica selecionada deve poder apresentar nesta surface
    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(m_PhysicalDevice, m_QueueFamily, surface, &res);
    if(res != VK_TRUE) {
        fprintf(stderr, "Error no WSI support on physical device 0\n");
        return false;
    }

    // Seleciona o formato de surface — preferência por BGRA8 UNORM sRGB
    const VkFormat requestSurfaceImageFormat[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM,
        VK_FORMAT_R8G8B8_UNORM
    };
    const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;

    m_MainWindowData.Surface = surface;
    m_MainWindowData.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        m_PhysicalDevice,
        m_MainWindowData.Surface,
        requestSurfaceImageFormat,
        (size_t) IM_COUNTOF(requestSurfaceImageFormat),
        requestSurfaceColorSpace);

    // Monta lista de present modes com base em m_VSyncEnabled
    VkPresentModeKHR present_modes[3];
    uint32_t         present_mode_count;

    if(m_VSyncEnabled) {
        present_modes[0] = VK_PRESENT_MODE_FIFO_KHR; // VSync ON: FIFO garantido pelo spec
        present_mode_count = 1;
    } else {
        present_modes[0] = VK_PRESENT_MODE_MAILBOX_KHR;   // VSync OFF: preferido (triple buffer)
        present_modes[1] = VK_PRESENT_MODE_IMMEDIATE_KHR; // VSync OFF: alternativo
        present_modes[2] = VK_PRESENT_MODE_FIFO_KHR;      // fallback sempre disponível
        present_mode_count = 3;
    }

    // Deixa o ImGui escolher o melhor modo suportado pelo hardware
    m_MainWindowData.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        m_PhysicalDevice,
        m_MainWindowData.Surface,
        present_modes,
        present_mode_count);

    // Cria swapchain, render pass e framebuffers iniciais
    IM_ASSERT(m_MinImageCount >= 2); // sanidade: swapchain precisa de ao menos 2 imagens
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        m_Instance, m_PhysicalDevice, m_Device,
        &m_MainWindowData, m_QueueFamily, m_Allocator,
        width, height, m_MinImageCount, 0);

    return true;
}

// ============================================================================
// SetVSync  (CORRIGIDA)
// ============================================================================

/**
 * @brief Liga ou desliga VSync e agenda rebuild da swapchain.
 *
 * CORREÇÃO APLICADA:
 *   m_VSyncEnabled = enabled; é a única linha que faltava no código original.
 *   Sem ela, RebuildSwapChain() sempre lia o valor antigo e recriava
 *   a swapchain com o modo errado, revertendo a mudança silenciosamente.
 *
 * Esta função apenas registra a intenção. A seleção concreta do present mode
 * (com fallback por hardware) é feita em RebuildSwapChain() via
 * ImGui_ImplVulkanH_SelectPresentMode().
 *
 * @param enabled  true = VSync ON (FIFO); false = VSync OFF (MAILBOX/IMMEDIATE).
 */
void VulkanContext::SetVSync(bool enabled) {
    m_VSyncEnabled = enabled; // BUG CORRIGIDO: flag autoritativa lida por RebuildSwapChain()

    // Calcula o modo preferido para o espelho m_PresentMode (informativo)
    const VkPresentModeKHR desired = enabled
        ? VK_PRESENT_MODE_FIFO_KHR    // VSync ON: FIFO é o único modo necessário
        : VK_PRESENT_MODE_MAILBOX_KHR; // VSync OFF: preferência (pode não ser suportado)

    if(m_PresentMode != desired) {
        m_PresentMode = desired; // atualiza espelho informativo
        m_SwapChainRebuild = true;    // sinaliza o MainLoop para chamar RebuildSwapChain()
    }
}

// ============================================================================
// RebuildSwapChain
// ============================================================================

/**
 * @brief Recria a swapchain com o present mode determinado por m_VSyncEnabled.
 *
 * Chamado pelo MainLoop quando NeedsSwapChainRebuild() retorna true.
 * Isso ocorre após SetVSync(), redimensionamento de janela, ou
 * VK_ERROR_OUT_OF_DATE_KHR/VK_SUBOPTIMAL_KHR retornados por FrameRender/FramePresent.
 *
 * @param width   Largura atual da janela em pixels (obtida via SDL_GetWindowSize).
 * @param height  Altura atual da janela em pixels.
 */
void VulkanContext::RebuildSwapChain(int width, int height) {
    if(m_Device == VK_NULL_HANDLE)
        return; // segurança: nada a fazer se o device não foi criado

    // Aguarda a GPU terminar todos os frames em voo antes de destruir a swapchain.
    // Destruir recursos em uso causa VK_ERROR_DEVICE_LOST.
    vkDeviceWaitIdle(m_Device);

    // Monta lista de present modes com base em m_VSyncEnabled (agora sempre correto)
    VkPresentModeKHR present_modes[3];
    uint32_t         present_mode_count;

    if(m_VSyncEnabled) {
        present_modes[0] = VK_PRESENT_MODE_FIFO_KHR; // VSync ON: FIFO garantido
        present_mode_count = 1;
    } else {
        present_modes[0] = VK_PRESENT_MODE_MAILBOX_KHR;   // triple buffer, sem tearing
        present_modes[1] = VK_PRESENT_MODE_IMMEDIATE_KHR; // sem espera, pode ter tearing
        present_modes[2] = VK_PRESENT_MODE_FIFO_KHR;      // fallback garantido pelo spec
        present_mode_count = 3;
    }

    // Seleciona o melhor modo suportado pelo hardware (percorre a lista em ordem)
    m_MainWindowData.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        m_PhysicalDevice,
        m_MainWindowData.Surface,
        present_modes,
        present_mode_count);

    // Informa o backend ImGui sobre a contagem mínima de imagens da nova swapchain
    ImGui_ImplVulkan_SetMinImageCount(m_MinImageCount);

    // Recria swapchain, render pass e framebuffers
    // ImGui_ImplVulkanH_CreateOrResizeWindow destrói a swapchain antiga internamente
    ImGui_ImplVulkanH_CreateOrResizeWindow(
        m_Instance, m_PhysicalDevice, m_Device,
        &m_MainWindowData, m_QueueFamily, m_Allocator,
        width, height, m_MinImageCount, 0);

    m_MainWindowData.FrameIndex = 0;    // reseta para o início do novo ciclo de frames
    m_SwapChainRebuild = false; // limpa o flag — evita rebuild desnecessário no próximo frame
}

// ============================================================================
// FrameRender
// ============================================================================

/**
 * @brief Grava e submete os comandos de renderização do frame atual.
 *
 * SEQUÊNCIA POR FRAME:
 *  1. vkAcquireNextImageKHR     → obtém índice da próxima imagem da swapchain
 *  2. vkWaitForFences           → aguarda a GPU terminar o uso anterior desta imagem
 *  3. vkResetFences             → reseta a fence para o próximo submit
 *  4. vkResetCommandPool        → libera comandos do frame anterior
 *  5. vkBeginCommandBuffer      → inicia gravação de novos comandos
 *  6. vkCmdBeginRenderPass      → define framebuffer alvo e clear color
 *  7. ImGui_ImplVulkan_RenderDrawData → grava primitivos ImGui
 *  8. vkCmdEndRenderPass        → encerra o render pass
 *  9. vkEndCommandBuffer        → finaliza a gravação
 * 10. vkQueueSubmit             → envia para a GPU executar
 *
 * @param draw_data  Dados de renderização obtidos via ImGui::GetDrawData().
 */
void VulkanContext::FrameRender(ImDrawData* draw_data) {
    ImGui_ImplVulkanH_Window* wd = &m_MainWindowData;

    // Semáforos para sincronizar aquisição de imagem e conclusão de renderização
    VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

    // Adquire a próxima imagem disponível da swapchain (UINT64_MAX = sem timeout)
    VkResult err = vkAcquireNextImageKHR(
        m_Device, wd->Swapchain, UINT64_MAX,
        image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);

    // OUT_OF_DATE ou SUBOPTIMAL: swapchain desatualizada, agenda rebuild
    if(err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        m_SwapChainRebuild = true;
    if(err == VK_ERROR_OUT_OF_DATE_KHR)
        return; // não podemos renderizar com swapchain inválida

    if(err != VK_SUBOPTIMAL_KHR)
        CheckVkResult(err); // SUBOPTIMAL não é fatal — continuamos este frame

    ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex]; // dados deste frame

    // Aguarda a fence: garante que a GPU terminou de usar este frame anteriormente
    err = vkWaitForFences(m_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);
    CheckVkResult(err);

    // Reseta a fence para não-sinalizado (pronta para o próximo vkQueueSubmit)
    err = vkResetFences(m_Device, 1, &fd->Fence);
    CheckVkResult(err);

    // Libera todos os command buffers deste command pool para reutilização
    err = vkResetCommandPool(m_Device, fd->CommandPool, 0);
    CheckVkResult(err);

    // Inicia a gravação: ONE_TIME_SUBMIT = gravado e submetido uma única vez por frame
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    err = vkBeginCommandBuffer(fd->CommandBuffer, &begin_info);
    CheckVkResult(err);

    // Inicia o render pass: define o framebuffer alvo, área de renderização e clear color
    VkRenderPassBeginInfo rp_info = {};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass = wd->RenderPass;
    rp_info.framebuffer = fd->Framebuffer;
    rp_info.renderArea.extent.width = wd->Width;
    rp_info.renderArea.extent.height = wd->Height;
    rp_info.clearValueCount = 1;
    rp_info.pClearValues = &wd->ClearValue; // cor de fundo (RGBA)
    vkCmdBeginRenderPass(fd->CommandBuffer, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

    // Grava os triângulos, texturas e primitivos ImGui no command buffer
    ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

    vkCmdEndRenderPass(fd->CommandBuffer); // encerra o render pass

    // Finaliza a gravação do command buffer
    err = vkEndCommandBuffer(fd->CommandBuffer);
    CheckVkResult(err);

    // Submete o command buffer para execução na fila gráfica
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &image_acquired_semaphore;  // espera a imagem estar pronta
    submit_info.pWaitDstStageMask = &wait_stage;                // estágio que precisa esperar
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &fd->CommandBuffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &render_complete_semaphore; // sinalizado ao terminar

    err = vkQueueSubmit(m_Queue, 1, &submit_info, fd->Fence); // fence sinalizada quando a GPU terminar
    CheckVkResult(err);
}

// ============================================================================
// FramePresent
// ============================================================================

/**
 * @brief Apresenta o frame renderizado na tela via vkQueuePresentKHR.
 *
 * Deve ser chamado imediatamente após FrameRender() no mesmo frame.
 * Avança o índice de semáforo (circular) para o próximo frame.
 * Se m_SwapChainRebuild for true, retorna sem apresentar.
 */
void VulkanContext::FramePresent() {
    if(m_SwapChainRebuild)
        return; // swapchain inválida — não tenta apresentar

    ImGui_ImplVulkanH_Window* wd = &m_MainWindowData;
    VkSemaphore render_complete_semaphore =
        wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;

    // Apresenta a imagem renderizada na surface (janela visível)
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore; // espera renderização terminar
    info.swapchainCount = 1;
    info.pSwapchains = &wd->Swapchain;
    info.pImageIndices = &wd->FrameIndex; // índice adquirido em FrameRender

    VkResult err = vkQueuePresentKHR(m_Queue, &info);

    if(err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
        m_SwapChainRebuild = true; // swapchain desatualizada — agenda rebuild
    if(err == VK_ERROR_OUT_OF_DATE_KHR)
        return; // saímos sem avançar o índice de semáforo

    if(err != VK_SUBOPTIMAL_KHR)
        CheckVkResult(err);

    // Avança para o próximo semáforo em ciclo circular pelo array de semáforos
    wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount;
}

// ============================================================================
// CleanupWindow / Cleanup
// ============================================================================

/**
 * @brief Destrói a swapchain e a surface Vulkan.
 *
 * Deve ser chamado ANTES de Cleanup() porque a surface pertence à instância.
 * Destruir a instância antes da surface causa crash ou validation error.
 */
void VulkanContext::CleanupWindow() {
    ImGui_ImplVulkanH_DestroyWindow(m_Instance, m_Device, &m_MainWindowData, m_Allocator);
    vkDestroySurfaceKHR(m_Instance, m_MainWindowData.Surface, m_Allocator);
}

/**
 * @brief Destrói descriptor pool, device lógico e instância Vulkan.
 *
 * Deve ser chamado APÓS CleanupWindow() e APÓS vkDeviceWaitIdle().
 */
void VulkanContext::Cleanup() {
    vkDestroyDescriptorPool(m_Device, m_DescriptorPool, m_Allocator);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
    // Carrega a função de destruição do messenger dinamicamente (é uma extensão)
    auto vkDestroyDebugUtilsMessengerEXT =
        (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
            m_Instance, "vkDestroyDebugUtilsMessengerEXT");
    vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, m_Allocator);
#endif

    vkDestroyDevice(m_Device, m_Allocator);     // destrói o device lógico
    vkDestroyInstance(m_Instance, m_Allocator); // destrói a instância (último recurso Vulkan)
}
