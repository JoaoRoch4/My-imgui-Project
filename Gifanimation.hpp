#pragma once
#include "pch.hpp"

/**
 * @file GifAnimation.hpp
 * @brief Animação GIF multi-frame com backend Vulkan dedicado.
 *
 * PROBLEMA DO POOL DO IMGUI
 * --------------------------
 * ImGui_ImplVulkan_AddTexture() aloca VkDescriptorSet do pool passado em
 * ImGui_ImplVulkan_InitInfo::DescriptorPool. Desde 2023-07-29 os exemplos
 * ImGui criam pools de tamanho mínimo (IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE).
 * Um GIF com 60 frames precisaria de 60 sets — estouraria esse pool.
 *
 * SOLUÇÃO — POOL PRIVADO POR INSTÂNCIA
 * --------------------------------------
 * Cada GifAnimation cria o SEU PRÓPRIO VkDescriptorPool dimensionado a
 * exactamente frame_count sets (+ margem de 1).
 *
 *   m_desc_pool → VkDescriptorPool com maxSets = frame_count
 *
 * Vantagens:
 *   - Zero impacto no pool do ImGui
 *   - Destruir o pool liberta TODOS os sets de uma vez (1 chamada Vulkan)
 *   - Sem fragmentação — o pool é destruído com a animação
 *
 * ARQUITECTURA DOS FRAMES
 * ------------------------
 * Cada frame é um GifFrame — struct interna com recursos Vulkan brutos:
 *
 *   GifFrame {
 *       VkImage, VkDeviceMemory  → imagem DEVICE_LOCAL na VRAM
 *       VkImageView              → view para o sampler
 *       VkSampler                → filtro LINEAR
 *       VkBuffer, VkDeviceMemory → upload buffer HOST_VISIBLE (mantido até Unload)
 *       VkDescriptorSet          → alocado do pool privado
 *   }
 *
 * GetCurrentID() devolve o VkDescriptorSet do frame activo como ImTextureID.
 *
 * ANIMAÇÃO
 * ---------
 * Update(delta_ms) acumula tempo e avança m_current_frame quando o delay
 * do frame actual expira. Loop infinito com wrap-around automático.
 * Delays de 0 ms são normalizados para MIN_FRAME_DELAY_MS.
 */
class GifAnimation {
public:

    // =========================================================================
    // Constantes
    // =========================================================================

    /// Delay mínimo por frame — normaliza valores 0 emitidos por alguns encoders
    static constexpr int MIN_FRAME_DELAY_MS = 10;

    // =========================================================================
    // Construtor / Destrutor
    // =========================================================================

    GifAnimation()  = default;
    ~GifAnimation() { Unload(); }

    GifAnimation(const GifAnimation&)            = delete;
    GifAnimation& operator=(const GifAnimation&) = delete;

    GifAnimation(GifAnimation&& o) noexcept;
    GifAnimation& operator=(GifAnimation&& o) noexcept;

    // =========================================================================
    // Carregamento
    // =========================================================================

    /**
     * @brief Carrega todos os frames de um ficheiro GIF.
     *
     * Cria um VkDescriptorPool privado dimensionado a frame_count sets,
     * depois faz o upload de cada frame para a VRAM individualmente.
     *
     * @param path  Caminho UTF-8 do ficheiro .gif.
     * @return      true se pelo menos 1 frame foi carregado com sucesso.
     */
    bool Load(const char* path);

    /**
     * @brief Liberta todos os recursos Vulkan.
     *
     * Destrói o VkDescriptorPool (liberta todos os sets de uma vez),
     * depois destrói os recursos de cada frame.
     * DEVE ser chamado fora de um frame em construção (após PostFrameCleanup).
     */
    void Unload();

    // =========================================================================
    // Animação
    // =========================================================================

    /**
     * @brief Avança o temporizador da animação.
     * @param delta_ms  Tempo desde o último Update() em ms (DeltaTime * 1000).
     */
    void Update(float delta_ms);

    /** @brief Pausa ou retoma a animação. */
    void SetPaused(bool paused) noexcept { m_paused = paused; }

    /** @brief Reinicia no frame 0 sem libertar recursos. */
    void Reset() noexcept;

    // =========================================================================
    // Consulta
    // =========================================================================

    [[nodiscard]] bool IsLoaded()   const noexcept { return !m_frames.empty(); }
    [[nodiscard]] bool IsAnimated() const noexcept { return m_frames.size() > 1; }
    [[nodiscard]] bool IsPaused()   const noexcept { return m_paused; }
    [[nodiscard]] int  GetWidth()   const noexcept { return m_width;  }
    [[nodiscard]] int  GetHeight()  const noexcept { return m_height; }
    [[nodiscard]] int  GetFrameCount()        const noexcept { return static_cast<int>(m_frames.size()); }
    [[nodiscard]] int  GetCurrentFrameIndex() const noexcept { return m_current_frame; }

    /** @brief ImTextureID do frame activo — passa directamente a ImGui::Image(). */
    [[nodiscard]] ImTextureID GetCurrentID() const noexcept;

private:

    // =========================================================================
    // GifFrame — recursos Vulkan de um único frame
    // =========================================================================

    /**
     * @brief Recursos Vulkan de um frame GIF.
     *
     * Não é uma classe RAII — o ciclo de vida é gerido por GifAnimation.
     * Todos os handles são inicializados a VK_NULL_HANDLE no construtor.
     * DestroyFrame() liberta os recursos sem tocar no descriptor set
     * (o pool é destruído separadamente de uma vez).
     */
    struct GifFrame
    {
        VkImage         image        = VK_NULL_HANDLE; ///< Imagem DEVICE_LOCAL na VRAM
        VkDeviceMemory  image_memory = VK_NULL_HANDLE; ///< Memória da imagem
        VkImageView     image_view   = VK_NULL_HANDLE; ///< View para o sampler
        VkSampler       sampler      = VK_NULL_HANDLE; ///< Sampler LINEAR
        VkBuffer        upload_buf   = VK_NULL_HANDLE; ///< Upload buffer HOST_VISIBLE
        VkDeviceMemory  upload_mem   = VK_NULL_HANDLE; ///< Memória do upload buffer
        VkDescriptorSet ds           = VK_NULL_HANDLE; ///< Set alocado do pool privado

        /** @brief ImTextureID para ImGui::Image() — converte VkDescriptorSet. */
        [[nodiscard]] ImTextureID GetID() const noexcept;
    };

    // =========================================================================
    // Estado interno
    // =========================================================================

    std::vector<GifFrame> m_frames;     ///< Um GifFrame por frame GIF
    std::vector<int>      m_delays_ms;  ///< Delay de cada frame em ms

    VkDescriptorPool      m_desc_pool   = VK_NULL_HANDLE; ///< Pool privado — 1 set por frame
    VkDescriptorSetLayout m_desc_layout = VK_NULL_HANDLE; ///< Layout criado por nós (binding 0 = COMBINED_IMAGE_SAMPLER)
    int              m_width        = 0;
    int              m_height       = 0;
    int              m_current_frame = 0;
    float            m_elapsed_ms   = 0.0f;
    bool             m_paused       = false;

    // =========================================================================
    // Helpers privados
    // =========================================================================

    /**
     * @brief Cria o VkDescriptorPool privado dimensionado a frame_count sets.
     *
     * @param device       Device Vulkan.
     * @param frame_count  Número de frames — maxSets do pool.
     * @return             true se o pool foi criado com sucesso.
     */
    [[nodiscard]] bool CreateDescriptorPool(VkDevice device, uint32_t frame_count);

    /**
     * @brief Cria o VkDescriptorSetLayout que espelha o layout de textura do ImGui.
     *
     * binding 0 → COMBINED_IMAGE_SAMPLER, stageFlags = FRAGMENT.
     * Criado uma vez em Load(), destruído em Unload().
     *
     * @param device  Device Vulkan.
     * @return        true se o layout foi criado com sucesso.
     */
    [[nodiscard]] bool CreateDescriptorSetLayout(VkDevice device);

    /**
     * @brief Faz o upload de um frame para a VRAM e aloca o descriptor.
     *
     * @param device   Device Vulkan.
     * @param queue    Fila gráfica para o command buffer.
     * @param cmd_pool Command pool do frame actual.
     * @param pixels   Pixels RGBA 8bpp do frame.
     * @param w        Largura.
     * @param h        Altura.
     * @param frame    Frame a preencher.
     * @return         true se o upload foi bem-sucedido.
     */
    [[nodiscard]] bool UploadFrame(VkDevice device, VkQueue queue,
                                   VkCommandPool cmd_pool,
                                   const uint8_t* pixels, int w, int h,
                                   GifFrame& frame);

    /**
     * @brief Liberta os recursos Vulkan de um frame (sem o descriptor set).
     *
     * O descriptor set é libertado pela destruição do pool em Unload().
     *
     * @param device  Device Vulkan.
     * @param frame   Frame a destruir.
     */
    static void DestroyFrame(VkDevice device, GifFrame& frame);

    /**
     * @brief Encontra o índice do tipo de memória Vulkan compatível.
     *
     * @param phys_dev     GPU física.
     * @param type_filter  memoryRequirements.memoryTypeBits.
     * @param properties   Flags desejadas.
     * @return             Índice ou 0xFFFFFFFF se não encontrado.
     */
    [[nodiscard]] static uint32_t FindMemoryType(VkPhysicalDevice phys_dev,
                                                  uint32_t type_filter,
                                                  VkMemoryPropertyFlags properties);
};