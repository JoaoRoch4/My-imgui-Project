#pragma once

#include "pch.hpp"

/**
 * @file Image.hpp
 * @brief Wrapper de imagem Vulkan para exibição no ImGui.
 *
 * USO BÁSICO
 * ----------
 * @code
 *   // Fora do loop (uma vez):
 *   Image logo;
 *   logo.Load("assets/logo.png");
 *
 *   // Dentro do loop, após NewFrame():
 *   logo.Draw(200.0f, 100.0f);
 *   logo.DrawFitted(400.0f, 300.0f);
 *   logo.DrawCentered(200.0f, 100.0f);
 *   if (logo.DrawButton("##logo_btn", {64, 64})) { ... }
 * @endcode
 *
 * COMO FUNCIONA (baseado no exemplo oficial do ImGui para Vulkan)
 * --------------------------------------------------------------
 *  1. stb_image decodifica o arquivo → array RGBA 8bpp na CPU
 *  2. VkImage DEVICE_LOCAL é criado na VRAM (GPU-only, alta velocidade)
 *  3. VkImageView e VkSampler descrevem como o shader lê a imagem
 *  4. ImGui_ImplVulkan_AddTexture() registra a textura → VkDescriptorSet
 *  5. UploadBuffer (HOST_VISIBLE) recebe os pixels via memcpy + flush
 *  6. Command buffer do frame atual executa:
 *       barrier UNDEFINED → TRANSFER_DST_OPTIMAL
 *       vkCmdCopyBufferToImage  (UploadBuffer → VkImage)
 *       barrier TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
 *  7. vkQueueSubmit + vkDeviceWaitIdle (bloqueante — upload único)
 *
 * POR QUE O UploadBuffer NÃO É LIBERADO APÓS O UPLOAD?
 * ------------------------------------------------------
 * No exemplo oficial do ImGui o UploadBuffer e sua memória são mantidos
 * até RemoveTexture(). Isso é seguro porque o upload ocorre uma única vez
 * fora do loop de render. A classe segue o mesmo padrão.
 *
 * DESTRUIÇÃO
 * ----------
 * Unload() (chamado pelo destrutor) destrói na ordem correta:
 *   UploadBufferMemory → UploadBuffer → Sampler → ImageView → Image
 *   → ImageMemory → ImGui_ImplVulkan_RemoveTexture(DS)
 * NÃO chame Unload() entre NewFrame() e Render() — o descriptor set
 * ainda pode estar em uso naquele frame.
 */
class Image {
public:

    Image();
    ~Image();

    // Não copiável — recursos Vulkan têm posse única
    Image(const Image&)            = delete;
    Image& operator=(const Image&) = delete;

    // Movível — transfere posse dos handles sem copiar
    Image(Image&& o) noexcept;
    Image& operator=(Image&& o) noexcept;

    // =========================================================================
    // Carregamento
    // =========================================================================

    /**
     * @brief Carrega uma imagem a partir de um arquivo em disco.
     * Formatos suportados: PNG, JPEG, BMP, TGA, GIF (stb_image).
     * Pode ser chamado múltiplas vezes — faz Unload() antes de recarregar.
     * @param path  Caminho para o arquivo.
     * @return      true se carregou com sucesso.
     */
    bool Load(const char* path);

    /**
     * @brief Carrega uma imagem a partir de um buffer em memória.
     * Útil para imagens embutidas no executável como arrays de bytes.
     * @param data       Ponteiro para os bytes do arquivo (não pixels crus).
     * @param data_size  Tamanho do buffer em bytes.
     * @return           true se carregou com sucesso.
     */
    bool Load(const void* data, size_t data_size);

    /**
     * @brief Libera todos os recursos Vulkan desta imagem.
     * Seguro chamar mesmo se a imagem não estiver carregada.
     */
    void Unload();

    // =========================================================================
    // Estado
    // =========================================================================

    bool  IsLoaded()  const { return m_Loaded;  } ///< true após Load() bem-sucedido
    int   GetWidth()  const { return m_Width;   } ///< Largura original em pixels
    int   GetHeight() const { return m_Height;  } ///< Altura original em pixels
    float GetAspect() const;                       ///< Largura / Altura (1.0 se não carregado)

    /**
     * @brief Retorna o ImTextureID para uso direto com ImGui::Image / ImageButton.
     * Internamente é um VkDescriptorSet obtido de ImGui_ImplVulkan_AddTexture().
     */
    ImTextureID GetID() const;

    // =========================================================================
    // Helpers de desenho — chame dentro de ImGui::Begin / End
    // =========================================================================

    /**
     * @brief Exibe a imagem com tamanho fixo.
     * @param size  Tamanho em pixels na tela.
     * @param uv0   UV do canto superior esquerdo (default 0,0 = inteira).
     * @param uv1   UV do canto inferior direito  (default 1,1 = inteira).
     */
    void Draw(ImVec2 size,
              ImVec2 uv0 = ImVec2(0.0f, 0.0f),
              ImVec2 uv1 = ImVec2(1.0f, 1.0f)) const;

    /** @brief Atalho para Draw com largura e altura separadas. */
    void Draw(float w, float h) const;

    /**
     * @brief Exibe a imagem cabendo em max_w × max_h mantendo o aspect ratio.
     * A imagem não é ampliada se for menor que a caixa.
     */
    void DrawFitted(float max_w, float max_h) const;

    /**
     * @brief Exibe a imagem centralizada horizontalmente na janela atual.
     */
    void DrawCentered(float w, float h) const;

    /**
     * @brief Exibe a imagem como botão clicável.
     * @param id    ID único do botão (ex.: "##btn_logo").
     * @param size  Tamanho do botão.
     * @return      true no frame em que foi clicado.
     */
    bool DrawButton(const char* id, ImVec2 size) const;

private:

    // =========================================================================
    // Recursos Vulkan — espelho direto do MyTextureData do exemplo oficial
    // =========================================================================
    class App* g_App;

    VkDescriptorSet m_DS;                ///< Descriptor set retornado por AddTexture — é o ImTextureID
    VkImageView     m_ImageView;         ///< View da imagem para o sampler
    VkImage         m_Image;             ///< Imagem Vulkan na VRAM (DEVICE_LOCAL)
    VkDeviceMemory  m_ImageMemory;       ///< Memória da imagem na VRAM
    VkSampler       m_Sampler;           ///< Sampler LINEAR / CLAMP
    VkBuffer        m_UploadBuffer;      ///< Buffer de upload HOST_VISIBLE (mantido até Unload)
    VkDeviceMemory  m_UploadBufferMemory;///< Memória do upload buffer

    int   m_Width;    ///< Largura em pixels
    int   m_Height;   ///< Altura em pixels
    int   m_Channels; ///< Canais (sempre 4 = RGBA)
    bool  m_Loaded;   ///< true após upload bem-sucedido

    // =========================================================================
    // Funções privadas
    // =========================================================================

    /**
     * @brief Recebe os pixels RGBA crus e executa todo o pipeline de upload Vulkan.
     * Chamado por ambas as sobrecargas de Load().
     * @param pixels  Array RGBA 8bpp (4 bytes/pixel) retornado pelo stb_image.
     * @param w       Largura da imagem.
     * @param h       Altura da imagem.
     * @return        true se todos os recursos foram criados com sucesso.
     */
    bool UploadToGPU(unsigned char* pixels, int w, int h);

    /**
     * @brief Destrói todos os recursos Vulkan na ordem correta.
     * Chamado por Unload() e pelo destrutor.
     */
    void DestroyResources();

    /**
     * @brief Encontra o índice do tipo de memória Vulkan compatível.
     * Equivalente a ImGui_ImplVulkan_MemoryType() do backend do ImGui.
     * @param type_filter  memoryRequirements.memoryTypeBits.
     * @param properties   Flags desejadas (ex.: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).
     * @return             Índice do tipo, ou 0xFFFFFFFF se não encontrado.
     */
    static uint32_t FindMemoryType(uint32_t              type_filter,
                                   VkMemoryPropertyFlags properties);
};
