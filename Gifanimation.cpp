/**
 * @file GifAnimation.cpp
 * @brief Animação GIF multi-frame com pool Vulkan dedicado por instância.
 *
 * POR QUE UM POOL PRIVADO?
 * -------------------------
 * ImGui_ImplVulkan_AddTexture() aloca VkDescriptorSet do pool global do ImGui.
 * Desde julho 2023 esse pool tem tamanho mínimo — um GIF com muitos frames
 * estouraria o pool silenciosamente ou causaria VK_ERROR_OUT_OF_POOL_MEMORY.
 *
 * Cada GifAnimation cria o SEU PRÓPRIO VkDescriptorPool:
 *
 *   VkDescriptorPoolCreateInfo {
 *       maxSets        = frame_count   // exactamente o necessário
 *       poolSizeCount  = 1
 *       pPoolSizes     = { COMBINED_IMAGE_SAMPLER, frame_count }
 *   }
 *
 * Ao destruir:
 *   vkDestroyDescriptorPool(device, m_desc_pool)
 *   → liberta TODOS os sets de uma vez, 1 chamada Vulkan
 *   → sem necessidade de vkFreeDescriptorSets por frame
 *
 * LAYOUT DO DESCRIPTOR SET
 * -------------------------
 * ImGui usa um layout fixo para texturas:
 *   binding 0 → VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, stageFlags = FRAGMENT
 *
 * Recuperamos esse layout via ImGui_ImplVulkan_GetDescriptorSetLayout() —
 * disponível no ImGui moderno (1.90+). Sem criar um layout próprio.
 *
 * PIPELINE DE CARREGAMENTO POR FRAME
 * ------------------------------------
 * Para cada frame i do GIF:
 *  1.  vkCreateImage (DEVICE_LOCAL, SAMPLED | TRANSFER_DST)
 *  2.  vkCreateImageView
 *  3.  vkCreateSampler (LINEAR)
 *  4.  vkAllocateDescriptorSets (do pool privado)
 *  5.  vkUpdateDescriptorSets   (bind sampler + view ao set)
 *  6.  vkCreateBuffer (HOST_VISIBLE, TRANSFER_SRC)
 *  7.  memcpy pixels → buffer mapeado + vkFlushMappedMemoryRanges
 *  8.  command buffer: barrier + vkCmdCopyBufferToImage + barrier
 *  9.  vkQueueSubmit + vkDeviceWaitIdle
 *
 * O upload buffer é mantido até Unload() — mesmo padrão do ImGui example.
 *
 * stb_image — sem STB_IMAGE_IMPLEMENTATION aqui
 * -----------------------------------------------
 * A implementação (STB_IMAGE_IMPLEMENTATION) está definida em Image.cpp.
 * GifAnimation.cpp apenas inclui o header para usar stbi_load_gif_from_memory.
 */

#include "pch.hpp"
#include "GifAnimation.hpp"

#include <stb_image.h>  // apenas declarações — implementação em Image.cpp

#include "Memory.hpp"
#include "VulkanContext_Wrapper.hpp"

#include <fstream>  // std::ifstream

// ============================================================================
// Move semântico
// ============================================================================

/**
 * @brief Move constructor — transfere posse de todos os recursos.
 *
 * O estado da origem é zerado para que o destrutor não liberte nada.
 */
GifAnimation::GifAnimation(GifAnimation&& o) noexcept
    : m_frames(std::move(o.m_frames))
    , m_delays_ms(std::move(o.m_delays_ms))
    , m_desc_pool(o.m_desc_pool)
    , m_width(o.m_width)
    , m_height(o.m_height)
    , m_current_frame(o.m_current_frame)
    , m_elapsed_ms(o.m_elapsed_ms)
    , m_paused(o.m_paused)
{
    // Zera a origem — o destrutor de o não deve libertar os recursos
    o.m_desc_pool     = VK_NULL_HANDLE;
    o.m_width         = 0;
    o.m_height        = 0;
    o.m_current_frame = 0;
    o.m_elapsed_ms    = 0.0f;
}

GifAnimation& GifAnimation::operator=(GifAnimation&& o) noexcept
{
    if(this != &o)
    {
        Unload(); // liberta recursos actuais

        m_frames        = std::move(o.m_frames);
        m_delays_ms     = std::move(o.m_delays_ms);
        m_desc_pool     = o.m_desc_pool;
        m_width         = o.m_width;
        m_height        = o.m_height;
        m_current_frame = o.m_current_frame;
        m_elapsed_ms    = o.m_elapsed_ms;
        m_paused        = o.m_paused;

        o.m_desc_pool     = VK_NULL_HANDLE;
        o.m_width         = 0;
        o.m_height        = 0;
        o.m_current_frame = 0;
        o.m_elapsed_ms    = 0.0f;
    }
    return *this;
}

// ============================================================================
// GifFrame::GetID
// ============================================================================

/**
 * @brief Converte VkDescriptorSet → ImTextureID.
 *
 * VkDescriptorSet é um handle não-dispatchable (uint64_t em Windows 64-bit).
 * Não existe conversão implícita para ImTextureID (struct no ImGui moderno).
 * reinterpret_cast via ImU64 é a abordagem correcta — igual ao ImGui internamente.
 */
ImTextureID GifAnimation::GifFrame::GetID() const noexcept
{
    if(ds == VK_NULL_HANDLE)
        return ImTextureID{};

    // VkDescriptorSet → ImU64 (uint64_t) → ImTextureID
    const ImU64 handle = reinterpret_cast<ImU64>(ds);
    return ImTextureID{ handle };
}

// ============================================================================
// FindMemoryType
// ============================================================================

/**
 * @brief Encontra o índice do tipo de memória que satisfaz os requisitos.
 *
 * Equivalente a findMemoryType() do exemplo oficial do ImGui.
 *
 * @param phys_dev     GPU física.
 * @param type_filter  memoryRequirements.memoryTypeBits — máscara de tipos compatíveis.
 * @param properties   Flags desejadas (DEVICE_LOCAL, HOST_VISIBLE, etc.).
 * @return             Índice do tipo, ou 0xFFFFFFFF se não encontrado.
 */
uint32_t GifAnimation::FindMemoryType(VkPhysicalDevice phys_dev,
                                       uint32_t type_filter,
                                       VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(phys_dev, &mem_props);

    for(uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
    {
        const bool type_ok  = (type_filter & (1u << i)) != 0;
        const bool flags_ok = (mem_props.memoryTypes[i].propertyFlags & properties) == properties;
        if(type_ok && flags_ok) return i;
    }
    return 0xFFFFFFFF; // não encontrado
}

// ============================================================================
// CreateDescriptorPool
// ============================================================================

/**
 * @brief Cria o VkDescriptorPool privado dimensionado a frame_count sets.
 *
 * Tipo: COMBINED_IMAGE_SAMPLER — exactamente o que ImGui usa para texturas.
 * maxSets = frame_count — sem desperdício, sem risco de estouro.
 *
 * FREE_DESCRIPTOR_SET_BIT: permite libertar sets individualmente se necessário.
 * Na prática destruímos o pool inteiro em Unload() — mais eficiente.
 *
 * @param device       Device Vulkan.
 * @param frame_count  Número de frames — define o tamanho do pool.
 * @return             true se o pool foi criado com sucesso.
 */
bool GifAnimation::CreateDescriptorPool(VkDevice device, uint32_t frame_count)
{
    // Um slot de COMBINED_IMAGE_SAMPLER por frame
    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = frame_count;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets       = frame_count; // exactamente o necessário
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes    = &pool_size;

    const VkResult err = vkCreateDescriptorPool(device, &pool_info, nullptr, &m_desc_pool);
    VulkanContext::CheckVkResult(err);
    return (err == VK_SUCCESS);
}

// ============================================================================
// CreateDescriptorSetLayout
// ============================================================================

/**
 * @brief Cria o VkDescriptorSetLayout que espelha o layout de textura do ImGui.
 *
 * O ImGui Vulkan backend usa internamente um layout com:
 *   binding 0 → VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
 *               stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
 *               descriptorCount = 1
 *
 * Não existe função pública no ImGui para obter esse layout, por isso
 * criamos o nosso próprio com a mesma especificação. Os sets alocados
 * com este layout são compatíveis com o shader interno do ImGui.
 *
 * O layout é criado uma vez em Load() e destruído em Unload().
 *
 * @param device  Device Vulkan.
 * @return        true se o layout foi criado com sucesso.
 */
bool GifAnimation::CreateDescriptorSetLayout(VkDevice device)
{
    // Binding 0: sampler combinado — textura + sampler num único descriptor
    // Usado pelo fragment shader do ImGui para amostrar a textura da imagem
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;                                          // binding 0 no shader
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // sampler + view
    binding.descriptorCount = 1;                                          // 1 textura por set
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;              // só o fragment shader lê

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;        // um único binding
    info.pBindings    = &binding;

    const VkResult err = vkCreateDescriptorSetLayout(device, &info, nullptr, &m_desc_layout);
    VulkanContext::CheckVkResult(err);
    return (err == VK_SUCCESS);
}

// ============================================================================
// UploadFrame
// ============================================================================

/**
 * @brief Faz o upload de um frame GIF para a VRAM e configura o descriptor.
 *
 * SEQUÊNCIA (igual ao exemplo oficial do ImGui):
 *  1.  vkCreateImage + vkAllocateMemory + vkBindImageMemory (DEVICE_LOCAL)
 *  2.  vkCreateImageView
 *  3.  vkCreateSampler (LINEAR)
 *  4.  vkAllocateDescriptorSets (do pool privado m_desc_pool)
 *  5.  vkUpdateDescriptorSets   (COMBINED_IMAGE_SAMPLER binding 0)
 *  6.  vkCreateBuffer + vkAllocateMemory + vkBindBufferMemory (HOST_VISIBLE)
 *  7.  vkMapMemory + memcpy + vkFlushMappedMemoryRanges + vkUnmapMemory
 *  8.  vkAllocateCommandBuffers + vkBeginCommandBuffer
 *  9.  barrier UNDEFINED → TRANSFER_DST_OPTIMAL
 * 10.  vkCmdCopyBufferToImage
 * 11.  barrier TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
 * 12.  vkEndCommandBuffer + vkQueueSubmit + vkDeviceWaitIdle
 *
 * @param device    Device Vulkan.
 * @param queue     Fila gráfica.
 * @param cmd_pool  Command pool do frame actual do swapchain.
 * @param pixels    Array RGBA 8bpp — width * height * 4 bytes.
 * @param w         Largura em píxeis.
 * @param h         Altura em píxeis.
 * @param frame     Estrutura GifFrame a preencher.
 * @return          true se todos os recursos foram criados com sucesso.
 */
bool GifAnimation::UploadFrame(VkDevice device, VkQueue queue,
                                VkCommandPool cmd_pool,
                                const uint8_t* pixels, int w, int h,
                                GifFrame& frame)
{
    VkPhysicalDevice phys = Memory::Get()->GetVulkan()->GetPhysicalDevice();
    const std::size_t img_size = static_cast<std::size_t>(w * h * 4); // RGBA

    VkResult err;

    // ---- 1. VkImage DEVICE_LOCAL -------------------------------------------
    {
        VkImageCreateInfo info{};
        info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType     = VK_IMAGE_TYPE_2D;
        info.format        = VK_FORMAT_R8G8B8A8_UNORM;
        info.extent        = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        info.mipLevels     = 1;
        info.arrayLayers   = 1;
        info.samples       = VK_SAMPLE_COUNT_1_BIT;
        info.tiling        = VK_IMAGE_TILING_OPTIMAL;       // layout optimizado para GPU
        info.usage         = VK_IMAGE_USAGE_SAMPLED_BIT     // lida por shader
                           | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // destino da cópia
        info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;     // transitamos no command buffer

        err = vkCreateImage(device, &info, nullptr, &frame.image);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;

        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device, frame.image, &req);

        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = req.size;
        alloc.memoryTypeIndex = FindMemoryType(phys, req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); // memória privada da GPU

        err = vkAllocateMemory(device, &alloc, nullptr, &frame.image_memory);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;

        err = vkBindImageMemory(device, frame.image, frame.image_memory, 0);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;
    }

    // ---- 2. VkImageView ----------------------------------------------------
    {
        VkImageViewCreateInfo info{};
        info.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image                       = frame.image;
        info.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        info.format                      = VK_FORMAT_R8G8B8A8_UNORM;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.layerCount = 1;

        err = vkCreateImageView(device, &info, nullptr, &frame.image_view);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;
    }

    // ---- 3. VkSampler LINEAR -----------------------------------------------
    {
        VkSamplerCreateInfo info{};
        info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter    = VK_FILTER_LINEAR;
        info.minFilter    = VK_FILTER_LINEAR;
        info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.minLod       = -1000.0f;
        info.maxLod       =  1000.0f;
        info.maxAnisotropy = 1.0f;

        err = vkCreateSampler(device, &info, nullptr, &frame.sampler);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;
    }

    // ---- 4. VkDescriptorSet do pool privado --------------------------------
    // Usa m_desc_layout criado em Load() — espelha o layout interno do ImGui
    // (binding 0, COMBINED_IMAGE_SAMPLER, FRAGMENT). Sem chamar AddTexture().
    {
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool     = m_desc_pool;    // pool privado desta GifAnimation
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts        = &m_desc_layout; // layout que espelha o do ImGui

        err = vkAllocateDescriptorSets(device, &alloc, &frame.ds);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;
    }

    // ---- 5. vkUpdateDescriptorSets — associa sampler + view ao set ---------
    // Binding 0 = COMBINED_IMAGE_SAMPLER — mesmo binding usado pelo shader ImGui
    {
        VkDescriptorImageInfo image_info{};
        image_info.sampler     = frame.sampler;
        image_info.imageView   = frame.image_view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = frame.ds;
        write.dstBinding      = 0; // binding 0 = textura no shader ImGui
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &image_info;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    // ---- 6. Upload buffer HOST_VISIBLE -------------------------------------
    // HOST_VISIBLE: CPU pode mapear e escrever via vkMapMemory.
    // Não é HOST_COHERENT → flush manual necessário (passo 7).
    {
        VkBufferCreateInfo info{};
        info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size        = static_cast<VkDeviceSize>(img_size);
        info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; // fonte da cópia
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        err = vkCreateBuffer(device, &info, nullptr, &frame.upload_buf);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(device, frame.upload_buf, &req);

        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = req.size;
        alloc.memoryTypeIndex = FindMemoryType(phys, req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT); // mapeável pela CPU

        err = vkAllocateMemory(device, &alloc, nullptr, &frame.upload_mem);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;

        err = vkBindBufferMemory(device, frame.upload_buf, frame.upload_mem, 0);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;
    }

    // ---- 7. Copia pixels CPU → upload buffer --------------------------------
    {
        void* map = nullptr;
        err = vkMapMemory(device, frame.upload_mem, 0,
            static_cast<VkDeviceSize>(img_size), 0, &map);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;

        std::memcpy(map, pixels, img_size); // pixels do frame → memória mapeada

        // Flush: garante visibilidade das escritas CPU pela GPU
        // (memória HOST_VISIBLE mas não HOST_COHERENT)
        VkMappedMemoryRange range{};
        range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = frame.upload_mem;
        range.size   = static_cast<VkDeviceSize>(img_size);

        err = vkFlushMappedMemoryRanges(device, 1, &range);
        VulkanContext::CheckVkResult(err);

        vkUnmapMemory(device, frame.upload_mem);
    }

    // ---- 8. Command buffer -------------------------------------------------
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    {
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandPool        = cmd_pool;
        alloc.commandBufferCount = 1;

        err = vkAllocateCommandBuffers(device, &alloc, &cmd);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        err = vkBeginCommandBuffer(cmd, &begin);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;
    }

    // ---- 9. Barrier UNDEFINED → TRANSFER_DST_OPTIMAL -----------------------
    // Transita o layout da imagem para aceitar a cópia do buffer.
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = frame.image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.layerCount     = 1;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_HOST_BIT,     // CPU terminou de escrever no buffer
            VK_PIPELINE_STAGE_TRANSFER_BIT, // GPU pode começar a cópia
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // ---- 10. vkCmdCopyBufferToImage ----------------------------------------
    {
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };

        vkCmdCopyBufferToImage(cmd,
            frame.upload_buf,
            frame.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region);
    }

    // ---- 11. Barrier TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL -------
    // Transita para o layout que o sampler do fragment shader espera.
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = frame.image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.layerCount     = 1;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,        // cópia terminou
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, // shader pode ler
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // ---- 12. Submit + wait -------------------------------------------------
    {
        err = vkEndCommandBuffer(cmd);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;

        VkSubmitInfo submit{};
        submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cmd;

        err = vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;

        // Bloqueante — upload único por frame, fora do loop de render
        err = vkDeviceWaitIdle(device);
        VulkanContext::CheckVkResult(err);
    }

    return true;
}

// ============================================================================
// DestroyFrame
// ============================================================================

/**
 * @brief Liberta os recursos Vulkan de um frame, sem o descriptor set.
 *
 * O descriptor set NÃO é libertado aqui — pertence ao pool privado
 * que é destruído em bloco em Unload() com vkDestroyDescriptorPool.
 * Destruir o pool liberta automaticamente todos os sets alocados nele.
 *
 * @param device  Device Vulkan.
 * @param frame   Frame a destruir.
 */
void GifAnimation::DestroyFrame(VkDevice device, GifFrame& frame)
{
    // Upload buffer — mantido até agora para o caso de reload futuro,
    // libertado aqui em conjunto com os outros recursos
    if(frame.upload_mem  != VK_NULL_HANDLE) vkFreeMemory(device, frame.upload_mem, nullptr);
    if(frame.upload_buf  != VK_NULL_HANDLE) vkDestroyBuffer(device, frame.upload_buf, nullptr);
    if(frame.sampler     != VK_NULL_HANDLE) vkDestroySampler(device, frame.sampler, nullptr);
    if(frame.image_view  != VK_NULL_HANDLE) vkDestroyImageView(device, frame.image_view, nullptr);
    if(frame.image       != VK_NULL_HANDLE) vkDestroyImage(device, frame.image, nullptr);
    if(frame.image_memory!= VK_NULL_HANDLE) vkFreeMemory(device, frame.image_memory, nullptr);
    // frame.ds NÃO é libertado — destruído pelo pool

    frame = GifFrame{}; // zera todos os handles
}

// ============================================================================
// Load
// ============================================================================

/**
 * @brief Carrega todos os frames de um ficheiro GIF.
 *
 * LEITURA DO FICHEIRO:
 *   stbi_load_gif_from_memory requer o buffer completo em memória.
 *   Lemos com ifstream(ate|binary) para determinar o tamanho sem tellg loop.
 *
 * DECODIFICAÇÃO:
 *   stbi_load_gif_from_memory(buf, len, &delays, &w, &h, &frames, &comp, 4)
 *   Devolve buffer RGBA: [frame0: w*h*4][frame1: w*h*4]...
 *   delays[] em ms, 1 por frame — alocado por stbi, libertado com stbi_image_free.
 *
 * POOL PRIVADO:
 *   CreateDescriptorPool(device, frame_count) antes do loop de upload.
 *   Cada frame chama UploadFrame() que aloca 1 set do pool privado.
 *
 * @param path  Caminho UTF-8 do ficheiro .gif.
 * @return      true se pelo menos 1 frame foi carregado com sucesso.
 */
bool GifAnimation::Load(const char* path)
{
    Unload(); // liberta estado anterior

    // ---- Lê o ficheiro inteiro para CPU ------------------------------------
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if(!file.is_open()) return false;

    const std::streamsize file_size = file.tellg();
    if(file_size <= 0) return false;

    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> file_buf(static_cast<std::size_t>(file_size));
    if(!file.read(reinterpret_cast<char*>(file_buf.data()), file_size))
        return false;

    // ---- Decodifica todos os frames ----------------------------------------
    int frame_count = 0;
    int width       = 0;
    int height      = 0;
    int channels    = 0;
    int* raw_delays = nullptr;

    // Deleter nomeado — lambda falha no MSVC (tipos distintos por instância)
    struct StbiDeleter {
        void operator()(stbi_uc* p) const noexcept { stbi_image_free(p); }
        void operator()(int*     p) const noexcept { stbi_image_free(p); }
    };

    stbi_uc* raw_pixels = stbi_load_gif_from_memory(
        file_buf.data(),
        static_cast<int>(file_size),
        &raw_delays,
        &width, &height,
        &frame_count,
        &channels,
        4); // força RGBA — VK_FORMAT_R8G8B8A8_UNORM

    if(!raw_pixels || frame_count <= 0)
        return false;

    // RAII — stbi_image_free ao sair do escopo mesmo em caso de falha
    std::unique_ptr<stbi_uc, StbiDeleter> pixels_guard(raw_pixels);
    std::unique_ptr<int,     StbiDeleter> delays_guard(raw_delays);

    // ---- Contexto Vulkan ---------------------------------------------------
    VulkanContext* vk     = Memory::Get()->GetVulkan();
    VkDevice       device = vk->GetDevice();
    VkQueue        queue  = vk->GetQueue();

    // Command pool do frame actual do swapchain
    ImGui_ImplVulkanH_Window* wd = vk->GetMainWindowData();
    VkCommandPool cmd_pool =
        wd->Frames[static_cast<uint32_t>(wd->FrameIndex)].CommandPool;

    // ---- Cria o layout de descriptor (uma vez por animação) ----------------
    // Espelha o layout interno do ImGui — binding 0, COMBINED_IMAGE_SAMPLER.
    // Deve existir antes de CreateDescriptorPool e de UploadFrame.
    if(!CreateDescriptorSetLayout(device))
        return false;

    // ---- Cria o pool privado dimensionado a frame_count --------------------
    if(!CreateDescriptorPool(device, static_cast<uint32_t>(frame_count)))
        return false;

    // ---- Prepara vectores --------------------------------------------------
    m_frames.resize(static_cast<std::size_t>(frame_count));
    m_delays_ms.resize(static_cast<std::size_t>(frame_count));
    m_width  = width;
    m_height = height;

    // Bytes de um único frame: width * height * 4 canais RGBA
    const std::size_t frame_bytes =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * 4;

    // ---- Upload de cada frame para a VRAM ---------------------------------
    for(int i = 0; i < frame_count; ++i)
    {
        // Ponteiro para o início dos píxeis deste frame no buffer contíguo
        const uint8_t* frame_pixels =
            raw_pixels + static_cast<std::ptrdiff_t>(i) *
                         static_cast<std::ptrdiff_t>(frame_bytes);

        if(!UploadFrame(device, queue, cmd_pool,
                        frame_pixels, width, height,
                        m_frames[static_cast<std::size_t>(i)]))
        {
            Unload(); // liberta o que foi carregado até agora
            return false;
        }

        // Normaliza delays: 0 ms → MIN_FRAME_DELAY_MS (evita animação a 0 fps)
        const int raw_d = (raw_delays != nullptr) ? raw_delays[i] : 100;
        m_delays_ms[static_cast<std::size_t>(i)] =
            (raw_d < MIN_FRAME_DELAY_MS) ? MIN_FRAME_DELAY_MS : raw_d;
    }

    // pixels_guard e delays_guard libertam os buffers stbi aqui ✓
    return true;
}

// ============================================================================
// Unload
// ============================================================================

/**
 * @brief Liberta todos os recursos Vulkan.
 *
 * ORDEM:
 *  1. DestroyFrame() por cada frame — liberta image, memory, view, sampler, buffers
 *  2. vkDestroyDescriptorPool — liberta TODOS os sets de uma vez (1 chamada)
 *
 * NOTA: chamar apenas quando a GPU não está a usar os recursos.
 * O factory garante isso com vkDeviceWaitIdle em PostFrameCleanup().
 */
void GifAnimation::Unload()
{
    if(m_frames.empty() && m_desc_pool == VK_NULL_HANDLE)
        return; // nada a libertar

    VkDevice device = Memory::Get()->GetVulkan()->GetDevice();

    // Liberta recursos de cada frame (sem o descriptor set)
    for(auto& frame : m_frames)
        DestroyFrame(device, frame);

    m_frames.clear();
    m_delays_ms.clear();

    // Destrói o pool — liberta todos os VkDescriptorSet de uma vez
    if(m_desc_pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, m_desc_pool, nullptr);
        m_desc_pool = VK_NULL_HANDLE;
    }

    // Destrói o layout — criado em Load(), já não é necessário
    if(m_desc_layout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, m_desc_layout, nullptr);
        m_desc_layout = VK_NULL_HANDLE;
    }

    m_width         = 0;
    m_height        = 0;
    m_current_frame = 0;
    m_elapsed_ms    = 0.0f;
    m_paused        = false;
}

// ============================================================================
// Update
// ============================================================================

/**
 * @brief Avança o temporizador e muda de frame quando o delay expira.
 *
 * Loop while: necessário para saltar múltiplos frames se delta_ms for grande
 * (ex.: janela minimizada vários segundos — evita animação "em câmara lenta").
 *
 * @param delta_ms  Milissegundos desde o último Update() (DeltaTime * 1000).
 */
void GifAnimation::Update(float delta_ms)
{
    if(m_paused || m_frames.size() <= 1)
        return;

    m_elapsed_ms += delta_ms;

    // Avança frames enquanto o tempo acumulado superar o delay actual
    while(m_elapsed_ms >= static_cast<float>(
              m_delays_ms[static_cast<std::size_t>(m_current_frame)]))
    {
        // Desconta o delay do frame que terminou
        m_elapsed_ms -= static_cast<float>(
            m_delays_ms[static_cast<std::size_t>(m_current_frame)]);

        // Avança para o próximo frame com wrap-around (loop infinito)
        m_current_frame = (m_current_frame + 1) % static_cast<int>(m_frames.size());
    }
}

// ============================================================================
// Reset
// ============================================================================

/** @brief Reinicia a animação no frame 0 sem libertar recursos. */
void GifAnimation::Reset() noexcept
{
    m_current_frame = 0;
    m_elapsed_ms    = 0.0f;
}

// ============================================================================
// GetCurrentID
// ============================================================================

/**
 * @brief Devolve o ImTextureID do frame activo.
 *
 * Se não houver frames, devolve ImTextureID{} (nulo).
 * ImGui::Image com ImTextureID{} não renderiza nada — sem crash.
 */
ImTextureID GifAnimation::GetCurrentID() const noexcept
{
    if(m_frames.empty())
        return ImTextureID{};

    // Clamp defensivo — m_current_frame nunca deve sair do intervalo
    const std::size_t idx =
        static_cast<std::size_t>(m_current_frame) % m_frames.size();

    return m_frames[idx].GetID();
}