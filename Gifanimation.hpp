#pragma once
#include "pch.hpp"

/**
 * @file GifAnimation.hpp
 * @brief Animação GIF com decode assíncrono e upload GPU totalmente não-bloqueante.
 *
 * ============================================================
 *  POR QUE O "A CARREGAR GIF..." DEMORAVA TANTO
 * ============================================================
 *
 *  O ecrã de loading fica visível durante todo o estado Decoding.
 *  O Decoding inclui:
 *
 *    1. ifstream → ler ficheiro para buffer           ← rápido
 *    2. stbi_load_gif_from_memory                     ← lento (LZW, N frames)
 *    3. Loop de cópia por frame:                      ← DESNECESSÁRIO e lento
 *         for each frame:
 *           decoded[i].pixels.assign(src, src + frame_bytes)
 *           ↑ aloca vector + memcpy de frame_bytes por cada frame
 *
 *  Para um GIF de 100 frames × 400×300px × 4 bytes:
 *    - 100 alocações de heap separadas
 *    - 100 × 480 000 bytes = ~48 MB de cópias
 *    - ~50-200ms adicionais em cima do stbi_load_gif_from_memory
 *
 *  SOLUÇÃO: guardar o buffer contíguo do stbi directamente.
 *
 *    stbi devolve [frame0][frame1]...[frameN] num único bloco alocado.
 *    Em vez de copiar cada frame para um vector separado, guardamos
 *    o buffer inteiro (std::move) e calculamos o offset de cada frame
 *    em PrepareChunk: `raw_pixels.data() + gi * frame_bytes`.
 *
 *    Cópias em DecodeGif depois da fix: 0 (zero).
 *    O único memcpy é o que já existia em PrepareChunk para o staging buffer.
 *
 * ============================================================
 *  PIPELINE COMPLETO
 * ============================================================
 *
 *  BACKGROUND THREAD                      MAIN THREAD (TickUpload)
 *  ─────────────────────────────────────  ──────────────────────────────────
 *  DecodeGif:
 *    ifstream → file_buf
 *    GifDecoder::Decode → on_frame callbacks → raw_pixels (preenchido por frame)
 *    on_frame copia cada frame para o offset correcto sob mutex
 *    m_state = Uploading
 *                                           join thread
 *                                           PrepareChunk(frames 0..7):
 *                                             vkCreateImage × 8
 *                                             memcpy(staging, raw + gi*stride)
 *                                             grava cmd buffer
 *                                           SubmitChunk(fence) → retorna
 *                                           ← GPU copia em paralelo
 *                                           vkGetFenceStatus → NOT_READY → retorna
 *                                           vkGetFenceStatus → SUCCESS
 *                                           FinalizeChunk → descriptor sets
 *                                           HasAnyFrame() == true ← 1.º chunk pronto
 *                                           PrepareChunk(frames 8..15) → ...
 *
 * ============================================================
 *  MÁQUINA DE ESTADOS
 * ============================================================
 *
 *   Idle → Decoding → Uploading → Loaded
 *                              ↘ Failed
 *
 * ============================================================
 *  THREAD SAFETY
 * ============================================================
 *
 *   m_state          → std::atomic — sem lock
 *   m_pending        → m_pending_mutex — escrito pela thread, lido em PrepareChunk
 *   Todas as calls Vulkan → APENAS na main thread
 */
class GifAnimation {
public:

    // =========================================================================
    // Estado
    // =========================================================================

    enum class State : int {
        Idle,      ///< Sem dados
        Decoding,  ///< Background thread a ler + decodificar
        Uploading, ///< Chunks em voo com VkFence
        Loaded,    ///< Todos os frames na VRAM
        Failed     ///< Erro irrecuperável
    };

    // =========================================================================
    // Constantes
    // =========================================================================

    /// Delay mínimo por frame em ms — normaliza delays 0 de encoders buggy
    static constexpr int MIN_FRAME_DELAY_MS = 10;

    /// Frames preparados e submetidos por chunk
    static constexpr uint32_t FRAMES_PER_CHUNK = 8;

    // =========================================================================
    // Ciclo de vida
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
     * @brief Inicia o decode numa background thread — retorna imediatamente.
     * Chame TickUpload() a cada frame para avançar o upload.
     * @param path  Caminho UTF-8 do ficheiro .gif.
     */
    void LoadAsync(const char* path);

    /**
     * @brief Carregamento síncrono (uso fora do render loop / testes).
     * @param path  Caminho UTF-8 do ficheiro .gif.
     * @return true se pelo menos 1 frame foi carregado.
     */
    bool Load(const char* path);

    /**
     * @brief Avança o upload — chame UMA VEZ por frame na main thread.
     *
     * Nunca bloqueia mais que ~microsegundos (apenas vkGetFenceStatus).
     * @return true no tick em que o último chunk foi finalizado.
     */
    bool TickUpload();

    /**
     * @brief Liberta recursos Vulkan e aguarda a thread.
     * Se há chunk em voo, faz vkWaitForFences antes de destruir.
     */
    void Unload();

    // =========================================================================
    // Animação
    // =========================================================================

    /** @brief Avança o temporizador. @param delta_ms DeltaTime × 1000. */
    void Update(float delta_ms);

    /** @brief Pausa / retoma. */
    void SetPaused(bool paused) noexcept { m_paused = paused; }

    /** @brief Reinicia no frame 0. */
    void Reset() noexcept;

    // =========================================================================
    // Consulta
    // =========================================================================

    [[nodiscard]] State GetState()   const noexcept { return m_state.load(); }
    [[nodiscard]] bool  IsLoaded()   const noexcept { return m_state.load() == State::Loaded;  }
    [[nodiscard]] bool  IsFailed()   const noexcept { return m_state.load() == State::Failed;  }
    [[nodiscard]] bool  IsAnimated() const noexcept { return m_frames_loaded > 1; }
    [[nodiscard]] bool  IsPaused()   const noexcept { return m_paused; }
    [[nodiscard]] int   GetWidth()   const noexcept { return m_width;  }
    [[nodiscard]] int   GetHeight()  const noexcept { return m_height; }

    [[nodiscard]] bool IsDecoding() const noexcept {
        const State s = m_state.load();
        return s == State::Decoding || s == State::Uploading;
    }

    [[nodiscard]] int   GetFrameCount()       const noexcept { return static_cast<int>(m_total_frames); }
    [[nodiscard]] int   GetLoadedFrameCount() const noexcept { return static_cast<int>(m_frames_loaded); }
    [[nodiscard]] bool  HasAnyFrame()         const noexcept { return m_frames_loaded > 0; }
    [[nodiscard]] int   GetCurrentFrameIndex()const noexcept { return m_current_frame; }

    [[nodiscard]] float GetUploadProgress() const noexcept {
        if(m_total_frames == 0) return 0.0f;
        return static_cast<float>(m_frames_loaded) /
               static_cast<float>(m_total_frames);
    }

    /** @brief ImTextureID do frame activo para ImGui::Image(). */
    [[nodiscard]] ImTextureID GetCurrentID() const noexcept;

private:

    // =========================================================================
    // PendingGif — resultado do decode, sem cópias por frame
    // =========================================================================

    /**
     * @brief Buffer contíguo do stbi + metadados — produzido em DecodeGif().
     *
     * stbi_load_gif_from_memory devolve um único bloco de memória:
     *   [frame0_RGBA][frame1_RGBA]...[frameN_RGBA]
     *
     * Em vez de copiar cada frame para um vector separado (versão anterior),
     * guardamos o bloco inteiro com std::move e calculamos offsets em PrepareChunk.
     *
     * frame_bytes = width × height × 4
     * Offset do frame i = raw_pixels.data() + i × frame_bytes
     *
     * Libertado em Unload() ou quando todos os chunks foram preparados.
     */
    struct PendingGif
    {
        std::vector<uint8_t> raw_pixels; ///< Buffer contíguo RGBA — preenchido incrementalmente por on_frame
        std::vector<int>     delays_ms;  ///< Delay em ms por frame (index 0..N-1)
        std::size_t          frame_bytes = 0; ///< Bytes por frame = width × height × 4
    };

    // =========================================================================
    // GifFrame — recursos Vulkan de um frame na VRAM
    // =========================================================================

    struct GifFrame
    {
        VkImage         image        = VK_NULL_HANDLE; ///< Imagem DEVICE_LOCAL
        VkDeviceMemory  image_memory = VK_NULL_HANDLE; ///< Memória da imagem
        VkImageView     image_view   = VK_NULL_HANDLE; ///< View para sampler
        VkSampler       sampler      = VK_NULL_HANDLE; ///< Filtro LINEAR
        VkBuffer        upload_buf   = VK_NULL_HANDLE; ///< Staging buffer HOST_VISIBLE
        VkDeviceMemory  upload_mem   = VK_NULL_HANDLE; ///< Memória do staging
        VkDescriptorSet ds           = VK_NULL_HANDLE; ///< Set do pool privado

        [[nodiscard]] ImTextureID GetID() const noexcept;
    };

    // =========================================================================
    // Recursos Vulkan
    // =========================================================================

    std::vector<GifFrame> m_frames;    ///< Todos os frames — pré-alocados após decode
    std::vector<int>      m_delays_ms; ///< Delays em ms por frame

    VkDescriptorPool      m_desc_pool        = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_desc_layout      = VK_NULL_HANDLE;
    VkCommandPool         m_upload_cmd_pool  = VK_NULL_HANDLE; ///< Pool dedicado para upload
    VkCommandBuffer       m_upload_cmd       = VK_NULL_HANDLE; ///< Command buffer reutilizável
    VkFence               m_chunk_fence      = VK_NULL_HANDLE; ///< Fence por chunk

    int   m_width         = 0;
    int   m_height        = 0;
    int   m_current_frame = 0;
    float m_elapsed_ms    = 0.0f;
    bool  m_paused        = false;

    // =========================================================================
    // Controlo fence-based
    // =========================================================================

    uint32_t m_total_frames   = 0;     ///< Total de frames — definido por on_header antes do 1.º frame
    std::atomic<uint32_t> m_frames_decoded{ 0 }; ///< Frames com pixels prontos em m_pending (incrementado por on_frame)
    uint32_t m_frames_loaded  = 0;     ///< Frames finalizados na VRAM
    bool     m_chunk_in_flight = false; ///< true enquanto o fence não sinalizou
    uint32_t m_chunk_begin    = 0;     ///< Primeiro frame do chunk em voo
    uint32_t m_chunk_end      = 0;     ///< Um após o último frame do chunk em voo

    // =========================================================================
    // Estado assíncrono (decode)
    // =========================================================================

    std::atomic<State> m_state{ State::Idle };

    std::thread m_decode_thread; ///< Background thread — join em Unload()
    std::mutex  m_pending_mutex; ///< Protege m_pending

    PendingGif m_pending; ///< Resultado do decode — sem cópias por frame

    // =========================================================================
    // Helpers privados
    // =========================================================================

    /** @brief Lê ficheiro + stbi_load_gif_from_memory — corre na background thread. */
    void DecodeGif(std::string path);

    /** @brief [FASE 1] Cria recursos GPU + memcpy offset + grava command buffer. */
    [[nodiscard]] bool PrepareChunk(VkDevice device, VkPhysicalDevice phys);

    /** @brief [FASE 2] vkQueueSubmit com fence — NÃO BLOQUEIA. */
    [[nodiscard]] bool SubmitChunk(VkQueue queue);

    /** @brief [FASE 3] Após fence sinalizado: aloca descriptor sets. */
    [[nodiscard]] bool FinalizeChunk(VkDevice device);

    /** @brief Cria o command pool dedicado para uploads. */
    [[nodiscard]] bool CreateUploadCommandPool(VkDevice device, uint32_t queue_family);

    /** @brief Cria o VkDescriptorPool privado dimensionado a total_frames. */
    [[nodiscard]] bool CreateDescriptorPool(VkDevice device, uint32_t frame_count);

    /** @brief Cria o VkDescriptorSetLayout: binding 0 COMBINED_IMAGE_SAMPLER FRAGMENT. */
    [[nodiscard]] bool CreateDescriptorSetLayout(VkDevice device);

    /** @brief Cria o VkFence reutilizável (não-sinalizado inicialmente). */
    [[nodiscard]] bool CreateFence(VkDevice device);

    /** @brief Liberta recursos de um GifFrame (sem descriptor set — pertence ao pool). */
    static void DestroyFrame(VkDevice device, GifFrame& frame);

    /** @brief Encontra índice de tipo de memória Vulkan compatível. */
    [[nodiscard]] static uint32_t FindMemoryType(VkPhysicalDevice phys_dev,
                                                  uint32_t type_filter,
                                                  VkMemoryPropertyFlags properties);
};