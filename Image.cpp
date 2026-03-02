/**
 * @file Image.cpp
 * @brief Wrapper de imagem Vulkan — baseado no exemplo oficial do ImGui.
 *
 * DIFERENÇAS EM RELAÇÃO AO EXEMPLO ORIGINAL
 * ------------------------------------------
 * Original                         │ Esta classe
 * ─────────────────────────────────┼───────────────────────────────────────
 * Variáveis globais g_Device,      │ g_App->g_Vulkan->GetDevice() etc.
 * g_PhysicalDevice, g_Queue,       │ (mesmo resultado, sem globais soltos)
 * g_Allocator, g_MainWindowData    │
 *                                  │
 * MyTextureData (struct)           │ Image (classe com RAII + helpers Draw)
 *                                  │
 * LoadTextureFromFile(path, &tex)  │ image.Load(path)
 * RemoveTexture(&tex)              │ image.Unload() / destrutor automático
 *                                  │
 * tex.DS como ImTextureID          │ image.GetID() retorna o mesmo DS
 *                                  │
 * Comandos no pool do frame atual  │ Idem — g_App->g_Vulkan->GetMainWindowData()
 *
 * POR QUE stb_image.h VEM DEPOIS DE pch.hpp?
 * -------------------------------------------
 * MSVC com /Yu"pch.hpp" injeta o PCH antes de qualquer linha do .cpp,
 * incluindo #define e #include que aparecem antes de #include "pch.hpp".
 * Se colocarmos STB_IMAGE_IMPLEMENTATION antes do pch, o compilador
 * processa o stb_image.h sem o define → funções não são emitidas →
 * C3861 "identifier not found".
 * Solução: incluir stb_image.h APÓS pch.hpp, com o define imediatamente antes.
 *
 * O UPLOAD BUFFER É MANTIDO ATÉ UNLOAD
 * --------------------------------------
 * O exemplo oficial mantém UploadBuffer e UploadBufferMemory até RemoveTexture().
 * Esta classe segue o mesmo padrão — DestroyResources() libera tudo junto.
 */

#include "pch.hpp"       // SEMPRE PRIMEIRO com MSVC /Yu — PCH injeta antes de tudo

// stb_image DEVE vir depois do pch para que o define seja visível ao compilador
#define STB_IMAGE_IMPLEMENTATION // emite a implementação das funções stbi_*
#include "stb_image.h"           // stbi_load, stbi_load_from_memory, stbi_image_free

#include "Image.hpp"
#include "App.hpp"   // g_App → g_Vulkan (device, queue, window data)
#include "VulkanContext_Wrapper.hpp"
#include "ImGuiContext_Wrapper.hpp"

// ============================================================================
// Construtor
// ============================================================================

/**
 * @brief Inicializa todos os handles com zero/VK_NULL_HANDLE via memset.
 *
 * Mesma abordagem do MyTextureData() original:
 *   MyTextureData() { memset(this, 0, sizeof(*this)); }
 *
 * Equivalente a inicializar cada campo individualmente, porém garante que
 * nenhum campo seja esquecido caso novos campos sejam adicionados no futuro.
 */
Image::Image() {
    memset(this, 0, sizeof(*this)); // zera todos os campos, incluindo os Vulkan handles
}

// ============================================================================
// Destrutor
// ============================================================================

/**
 * @brief Destrói automaticamente os recursos Vulkan ao sair do escopo.
 * RAII — evita Unload() manual na maioria dos casos.
 */
Image::~Image() {
    Unload();
}

// ============================================================================
// Move construtor
// ============================================================================

/**
 * @brief Transfere a posse dos handles Vulkan para o novo objeto.
 * O objeto de origem é zerado (memset) para evitar double-free no destrutor.
 */
Image::Image(Image&& o) noexcept {
    memcpy(this, &o, sizeof(*this)); // copia todos os handles e estados
    memset(&o,  0,  sizeof(o));      // zera a origem — destrutor não fará nada
}

/**
 * @brief Move assignment — libera recursos atuais e assume os da origem.
 */
Image& Image::operator=(Image&& o) noexcept {
    if(this != &o) {
        Unload();                        // libera o que esta instância possui
        memcpy(this, &o, sizeof(*this)); // assume os handles da origem
        memset(&o,   0,  sizeof(o));     // zera a origem
    }
    return *this;
}

// ============================================================================
// Load — arquivo
// ============================================================================

/**
 * @brief Carrega uma imagem a partir de um arquivo em disco.
 *
 * Passa diretamente para stbi_load(), que abre o arquivo internamente.
 * Isso evita uma cópia extra em buffer temporário.
 *
 * @param path  Caminho para o arquivo (PNG, JPEG, BMP, TGA…).
 */
bool Image::Load(const char* path) {
    if(m_Loaded) Unload(); // recarrega se já havia uma imagem

    m_Channels = 4; // força RGBA — formato mais fácil para o Vulkan

    // stbi_load: decodifica o arquivo → array RGBA de 4 bytes/pixel na CPU
    unsigned char* pixels = stbi_load(path,
        &m_Width, &m_Height,
        nullptr,    // canais originais — não precisamos, forçamos 4
        m_Channels);

    if(!pixels) {
        fprintf(stderr, "[Image] stbi_load falhou (%s): %s\n",
            path, stbi_failure_reason());
        return false;
    }

    bool ok = UploadToGPU(pixels, m_Width, m_Height);
    stbi_image_free(pixels); // GPU já tem a cópia — libera a memória CPU
    return ok;
}

// ============================================================================
// Load — memória
// ============================================================================

/**
 * @brief Carrega uma imagem a partir de um buffer em memória.
 *
 * Útil para imagens embutidas como arrays de bytes no executável.
 *
 * @param data       Ponteiro para os bytes do arquivo (não pixels crus).
 * @param data_size  Tamanho do buffer em bytes.
 */
bool Image::Load(const void* data, size_t data_size) {
    if(m_Loaded) Unload();

    m_Channels = 4;

    unsigned char* pixels = stbi_load_from_memory(
        static_cast<const unsigned char*>(data),
        static_cast<int>(data_size),
        &m_Width, &m_Height,
        nullptr,   // canais originais
        m_Channels);

    if(!pixels) {
        fprintf(stderr, "[Image] stbi_load_from_memory falhou: %s\n",
            stbi_failure_reason());
        return false;
    }

    bool ok = UploadToGPU(pixels, m_Width, m_Height);
    stbi_image_free(pixels);
    return ok;
}

// ============================================================================
// Unload
// ============================================================================

/**
 * @brief Libera todos os recursos Vulkan e reseta o estado.
 *
 * Seguro chamar múltiplas vezes — se !m_Loaded, retorna imediatamente.
 * NÃO chame entre NewFrame() e Render(): o descriptor set ainda pode estar
 * em uso naquele frame pela GPU.
 */
void Image::Unload() {
    if(!m_Loaded) return;
    DestroyResources();
    // Reseta estado — campos Vulkan já foram zerados em DestroyResources()
    m_Width    = 0;
    m_Height   = 0;
    m_Channels = 0;
    m_Loaded   = false;
}

// ============================================================================
// GetAspect
// ============================================================================

/**
 * @brief Retorna razão largura/altura (ex.: 16/9 ≈ 1.777).
 * Retorna 1.0f se a imagem não estiver carregada (evita divisão por zero).
 */
float Image::GetAspect() const {
    if(m_Height == 0) return 1.0f;
    return static_cast<float>(m_Width) / static_cast<float>(m_Height);
}

// ============================================================================
// GetID
// ============================================================================

/**
 * @brief Retorna o ImTextureID para uso com ImGui::Image / ImageButton.
 *
 * ImTextureID no ImGui moderno (1.90+) é uma struct, não uint64_t.
 * Para o backend Vulkan, ele armazena internamente um VkDescriptorSet.
 *
 * VkDescriptorSet é um handle não-dispatchable: no Windows 64-bit é um
 * uint64_t (com VK_DEFINE_NON_DISPATCHABLE_HANDLE), portanto não pode
 * ser convertido diretamente com static_cast para ImTextureID (struct).
 *
 * Solução correta: reinterpret_cast via ImU64 (uint64_t), depois constrói
 * ImTextureID. Isso é o mesmo que o ImGui faz internamente para Vulkan.
 *
 * Retorna ImTextureID{} (nulo) se não estiver carregado.
 */
ImTextureID Image::GetID() const {
    if(!m_Loaded) return ImTextureID{};
    // Passo 1: VkDescriptorSet → ImU64 via reinterpret_cast
    //   VkDescriptorSet é uint64_t em 64-bit (handle não-dispatchable)
    //   static_cast não compila porque não há conversão implícita definida
    ImU64 handle = reinterpret_cast<ImU64>(m_DS);
    // Passo 2: ImU64 → ImTextureID via construtor da struct
    return ImTextureID{ handle };
}

// ============================================================================
// Helpers de desenho
// ============================================================================

/**
 * @brief Exibe a imagem com tamanho e UVs especificados.
 *
 * ImGui::Image(tex_id, size, uv0, uv1):
 *   tex_id = VkDescriptorSet como ImTextureID
 *   size   = tamanho em pixels da tela
 *   uv0    = canto superior esquerdo da sub-região (0,0 = imagem inteira)
 *   uv1    = canto inferior direito   da sub-região (1,1 = imagem inteira)
 */
void Image::Draw(ImVec2 size, ImVec2 uv0, ImVec2 uv1) const {
    if(!m_Loaded) return;
    ImGui::Image(GetID(), size, uv0, uv1);
}

/**
 * @brief Atalho para Draw com largura e altura separadas.
 */
void Image::Draw(float w, float h) const {
    Draw(ImVec2(w, h));
}

/**
 * @brief Exibe a imagem cabendo em max_w × max_h mantendo o aspect ratio.
 *
 * Calcula qual dimensão está mais apertada e escala proporcionalmente.
 * Não amplia imagens menores que a caixa (scale ≤ 1.0).
 */
void Image::DrawFitted(float max_w, float max_h) const {
    if(!m_Loaded) return;

    float src_w = static_cast<float>(m_Width);
    float src_h = static_cast<float>(m_Height);

    // Escala necessária para cada eixo — usa a menor para caber em ambos
    float scale = std::min(max_w / src_w, max_h / src_h);

    // Não amplia imagens menores que a caixa
    if(scale > 1.0f) scale = 1.0f;

    Draw(src_w * scale, src_h * scale);
}

/**
 * @brief Exibe a imagem centralizada horizontalmente na janela atual.
 *
 * GetContentRegionAvail().x retorna a largura disponível na janela.
 * SetCursorPosX empurra o cursor para o offset calculado.
 */
void Image::DrawCentered(float w, float h) const {
    if(!m_Loaded) return;

    float available = ImGui::GetContentRegionAvail().x; // largura disponível
    float offset    = (available - w) * 0.5f;           // margem para centralizar

    if(offset > 0.0f)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);

    Draw(w, h);
}

/**
 * @brief Exibe a imagem como botão clicável, retorna true se clicado.
 *
 * ImGui::ImageButton(id, tex_id, size):
 *   id     = string para evitar conflitos entre múltiplos botões na janela
 *   tex_id = VkDescriptorSet como ImTextureID
 *   size   = tamanho do botão em pixels
 */
bool Image::DrawButton(const char* id, ImVec2 size) const {
    if(!m_Loaded) return false;
    return ImGui::ImageButton(id, GetID(), size);
}

// ============================================================================
// FindMemoryType
// ============================================================================

/**
 * @brief Encontra o índice do tipo de memória Vulkan que satisfaz os requisitos.
 *
 * Equivalente direto ao findMemoryType() do exemplo oficial.
 * vkGetPhysicalDeviceMemoryProperties retorna os tipos de memória disponíveis
 * na GPU. type_filter é uma máscara de bits indicando quais tipos são
 * compatíveis com o buffer/imagem em questão.
 *
 * @param type_filter  memoryRequirements.memoryTypeBits (máscara de compatibilidade).
 * @param properties   Flags desejadas (HOST_VISIBLE, DEVICE_LOCAL, etc.).
 * @return             Índice do tipo de memória, ou 0xFFFFFFFF se não encontrado.
 */
uint32_t Image::FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(
        g_App->g_Vulkan->GetPhysicalDevice(), // GPU física
        &mem_properties);

    for(uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        bool type_ok  = (type_filter & (1u << i)) != 0; // tipo é compatível?
        bool props_ok = (mem_properties.memoryTypes[i].propertyFlags & properties) == properties;
        if(type_ok && props_ok) return i;
    }

    return 0xFFFFFFFF; // não encontrado — vkAllocateMemory vai falhar
}

// ============================================================================
// UploadToGPU — núcleo do carregamento (espelho do LoadTextureFromFile original)
// ============================================================================

/**
 * @brief Cria todos os recursos Vulkan e faz o upload dos pixels para a GPU.
 *
 * Segue exatamente o fluxo do exemplo oficial do ImGui:
 *
 *  1.  VkImage DEVICE_LOCAL         — imagem na VRAM da GPU
 *  2.  VkImageView                  — como o shader enxerga a imagem
 *  3.  VkSampler                    — filtro e wrapping
 *  4.  ImGui_ImplVulkan_AddTexture  — registra → DS (ImTextureID)
 *  5.  UploadBuffer HOST_VISIBLE    — memória acessível pela CPU
 *  6.  memcpy + vkFlushMappedMemoryRanges — copia pixels para o buffer
 *  7.  Command buffer do frame atual:
 *        barrier UNDEFINED → TRANSFER_DST_OPTIMAL
 *        vkCmdCopyBufferToImage
 *        barrier TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
 *  8.  vkQueueSubmit + vkDeviceWaitIdle (bloqueante — upload único)
 *
 * @param pixels  Array RGBA 8bpp (4 bytes/pixel).
 * @param w       Largura da imagem.
 * @param h       Altura da imagem.
 */
bool Image::UploadToGPU(unsigned char* pixels, int w, int h) {
    VkDevice         device    = g_App->g_Vulkan->GetDevice();
    VkPhysicalDevice phys_dev  = g_App->g_Vulkan->GetPhysicalDevice();
    VkQueue          queue     = g_App->g_Vulkan->GetQueue();
    VkAllocationCallbacks* alloc = nullptr; // usa o alocador do VulkanContext se existir

    size_t image_size = static_cast<size_t>(w * h * 4); // 4 bytes por pixel RGBA

    VkResult err;

    // ---- 1. VkImage (DEVICE_LOCAL) ------------------------------------------
    // DEVICE_LOCAL = memória privada da GPU, sem acesso CPU, máxima largura de banda
    // usage: SAMPLED (lida por shader) + TRANSFER_DST (destino da cópia)
    {
        VkImageCreateInfo info = {};
        info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType     = VK_IMAGE_TYPE_2D;
        info.format        = VK_FORMAT_R8G8B8A8_UNORM; // 4 bytes por pixel
        info.extent.width  = static_cast<uint32_t>(w);
        info.extent.height = static_cast<uint32_t>(h);
        info.extent.depth  = 1;
        info.mipLevels     = 1;
        info.arrayLayers   = 1;
        info.samples       = VK_SAMPLE_COUNT_1_BIT; // sem multisampling
        info.tiling        = VK_IMAGE_TILING_OPTIMAL; // layout otimizado para acesso GPU
        info.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // será transitado pelo command buffer

        err = vkCreateImage(device, &info, alloc, &m_Image);
        VulkanContext::CheckVkResult(err);

        // Consulta os requisitos de memória para esta imagem
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device, m_Image, &req);

        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize  = req.size;
        alloc_info.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); // memória privada da GPU

        err = vkAllocateMemory(device, &alloc_info, alloc, &m_ImageMemory);
        VulkanContext::CheckVkResult(err);

        err = vkBindImageMemory(device, m_Image, m_ImageMemory, 0); // vincula imagem à memória
        VulkanContext::CheckVkResult(err);
    }

    // ---- 2. VkImageView -----------------------------------------------------
    // A view descreve como o sampler interpreta os dados da imagem
    {
        VkImageViewCreateInfo info = {};
        info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image                           = m_Image;
        info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        info.format                          = VK_FORMAT_R8G8B8A8_UNORM;
        info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.levelCount     = 1;
        info.subresourceRange.layerCount     = 1;

        err = vkCreateImageView(device, &info, alloc, &m_ImageView);
        VulkanContext::CheckVkResult(err);
    }

    // ---- 3. VkSampler -------------------------------------------------------
    // LINEAR: interpola pixels vizinhos para resultado suave
    // REPEAT: pixels fora [0,1] repetem a textura (igual ao exemplo original)
    {
        VkSamplerCreateInfo info = {};
        info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter    = VK_FILTER_LINEAR;
        info.minFilter    = VK_FILTER_LINEAR;
        info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.minLod       = -1000.0f;
        info.maxLod       =  1000.0f;
        info.maxAnisotropy= 1.0f;

        err = vkCreateSampler(device, &info, alloc, &m_Sampler);
        VulkanContext::CheckVkResult(err);
    }

    // ---- 4. ImGui_ImplVulkan_AddTexture → VkDescriptorSet ------------------
    // Registra sampler + view no pool de descritores do ImGui.
    // O VkDescriptorSet retornado é o ImTextureID que passamos ao ImGui::Image.
    m_DS = ImGui_ImplVulkan_AddTexture(
        m_Sampler,
        m_ImageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // layout final após a cópia

    // ---- 5. UploadBuffer (HOST_VISIBLE) -------------------------------------
    // HOST_VISIBLE = CPU pode mapear e escrever via vkMapMemory.
    // Não é HOST_COHERENT → precisamos de vkFlushMappedMemoryRanges (passo 6).
    {
        VkBufferCreateInfo info = {};
        info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size        = static_cast<VkDeviceSize>(image_size);
        info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; // fonte de transferência
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        err = vkCreateBuffer(device, &info, alloc, &m_UploadBuffer);
        VulkanContext::CheckVkResult(err);

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(device, m_UploadBuffer, &req);

        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize  = req.size;
        alloc_info.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // memória mapeável pela CPU

        err = vkAllocateMemory(device, &alloc_info, alloc, &m_UploadBufferMemory);
        VulkanContext::CheckVkResult(err);

        err = vkBindBufferMemory(device, m_UploadBuffer, m_UploadBufferMemory, 0);
        VulkanContext::CheckVkResult(err);
    }

    // ---- 6. Copia pixels CPU → UploadBuffer ---------------------------------
    {
        void* map = nullptr;
        err = vkMapMemory(device, m_UploadBufferMemory, 0,
            static_cast<VkDeviceSize>(image_size), 0, &map);
        VulkanContext::CheckVkResult(err);

        memcpy(map, pixels, image_size); // CPU → memória mapeada

        // Flush: garante que as escritas CPU são visíveis para a GPU
        // (necessário porque a memória NÃO é HOST_COHERENT)
        VkMappedMemoryRange range = {};
        range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = m_UploadBufferMemory;
        range.size   = static_cast<VkDeviceSize>(image_size);

        err = vkFlushMappedMemoryRanges(device, 1, &range);
        VulkanContext::CheckVkResult(err);

        vkUnmapMemory(device, m_UploadBufferMemory); // desmapeia após o flush
    }

    // ---- 7. Command buffer do frame atual -----------------------------------
    // Usa o command pool do frame atual — mesma abordagem do exemplo oficial.
    // g_MainWindowData.Frames[FrameIndex].CommandPool
    ImGui_ImplVulkanH_Window* wd = g_App->g_Vulkan->GetMainWindowData();
    VkCommandPool   cmd_pool     = wd->Frames[wd->FrameIndex].CommandPool;
    VkCommandBuffer cmd_buf      = VK_NULL_HANDLE;

    {
        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandPool        = cmd_pool;
        alloc_info.commandBufferCount = 1;

        err = vkAllocateCommandBuffers(device, &alloc_info, &cmd_buf);
        VulkanContext::CheckVkResult(err);

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // usado uma vez

        err = vkBeginCommandBuffer(cmd_buf, &begin_info);
        VulkanContext::CheckVkResult(err);
    }

    // ---- Barrier 1: UNDEFINED → TRANSFER_DST_OPTIMAL -----------------------
    // Prepara a imagem para receber a cópia do buffer.
    // srcStageMask HOST_BIT: dados escritos pela CPU já chegaram ao buffer.
    // dstStageMask TRANSFER_BIT: a cópia começa depois desta barrier.
    {
        VkImageMemoryBarrier copy_barrier = {};
        copy_barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        copy_barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
        copy_barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
        copy_barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copy_barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        copy_barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        copy_barrier.image                           = m_Image;
        copy_barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_barrier.subresourceRange.levelCount     = 1;
        copy_barrier.subresourceRange.layerCount     = 1;

        vkCmdPipelineBarrier(cmd_buf,
            VK_PIPELINE_STAGE_HOST_BIT,      // CPU terminou de escrever no buffer
            VK_PIPELINE_STAGE_TRANSFER_BIT,  // GPU pode começar a transferência
            0, 0, nullptr, 0, nullptr,
            1, &copy_barrier);
    }

    // ---- Cópia: UploadBuffer → VkImage ---------------------------------------
    {
        VkBufferImageCopy region = {};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width           = static_cast<uint32_t>(w);
        region.imageExtent.height          = static_cast<uint32_t>(h);
        region.imageExtent.depth           = 1;

        vkCmdCopyBufferToImage(cmd_buf,
            m_UploadBuffer,                        // fonte: buffer HOST_VISIBLE
            m_Image,                               // destino: imagem DEVICE_LOCAL
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,  // layout atual da imagem
            1, &region);
    }

    // ---- Barrier 2: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL --------
    // Após a cópia, transita para o layout que o sampler espera.
    // srcStageMask TRANSFER_BIT: a cópia terminou.
    // dstStageMask FRAGMENT_SHADER_BIT: o shader pode ler a partir daqui.
    {
        VkImageMemoryBarrier use_barrier = {};
        use_barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        use_barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
        use_barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
        use_barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        use_barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        use_barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        use_barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        use_barrier.image                           = m_Image;
        use_barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        use_barrier.subresourceRange.levelCount     = 1;
        use_barrier.subresourceRange.layerCount     = 1;

        vkCmdPipelineBarrier(cmd_buf,
            VK_PIPELINE_STAGE_TRANSFER_BIT,          // transferência terminou
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,   // fragment shader pode ler
            0, 0, nullptr, 0, nullptr,
            1, &use_barrier);
    }

    // ---- 8. Submit + wait ---------------------------------------------------
    {
        VkSubmitInfo end_info = {};
        end_info.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        end_info.commandBufferCount = 1;
        end_info.pCommandBuffers    = &cmd_buf;

        err = vkEndCommandBuffer(cmd_buf); // finaliza a gravação
        VulkanContext::CheckVkResult(err);

        err = vkQueueSubmit(queue, 1, &end_info, VK_NULL_HANDLE); // envia à GPU
        VulkanContext::CheckVkResult(err);

        err = vkDeviceWaitIdle(device); // espera GPU terminar (bloqueante — upload único)
        VulkanContext::CheckVkResult(err);
    }

    m_Width  = w;
    m_Height = h;
    m_Loaded = true;
    return true;
}

// ============================================================================
// DestroyResources — espelho direto do RemoveTexture() original
// ============================================================================

/**
 * @brief Destrói todos os recursos Vulkan na ordem correta.
 *
 * Ordem idêntica ao RemoveTexture() do exemplo oficial:
 *   1. vkFreeMemory    (UploadBufferMemory)
 *   2. vkDestroyBuffer (UploadBuffer)
 *   3. vkDestroySampler
 *   4. vkDestroyImageView
 *   5. vkDestroyImage
 *   6. vkFreeMemory    (ImageMemory)
 *   7. ImGui_ImplVulkan_RemoveTexture (DS)
 *
 * Handles nulos (VK_NULL_HANDLE / zero) são ignorados pelo Vulkan —
 * o memset do construtor garante que handles não inicializados são zero.
 */
void Image::DestroyResources() {
    // Verifica se o App ainda está ativo (não chame após Close())
    if(!g_App || !g_App->g_Vulkan) return;

    VkDevice device = g_App->g_Vulkan->GetDevice();
    if(device == VK_NULL_HANDLE) return;

    // Ordem: buffer de upload primeiro, depois imagem, por último o descriptor set
    vkFreeMemory    (device, m_UploadBufferMemory, nullptr);
    vkDestroyBuffer (device, m_UploadBuffer,       nullptr);
    vkDestroySampler(device, m_Sampler,            nullptr);
    vkDestroyImageView(device, m_ImageView,        nullptr);
    vkDestroyImage  (device, m_Image,              nullptr);
    vkFreeMemory    (device, m_ImageMemory,        nullptr);

    // Remove o descriptor set do pool de descritores do ImGui
    if(m_DS != VK_NULL_HANDLE)
        ImGui_ImplVulkan_RemoveTexture(m_DS);

    // Zera todos os handles para evitar double-free
    memset(this, 0, sizeof(*this));
}
