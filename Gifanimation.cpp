/**
 * @file GifAnimation.cpp
 * @brief Animação GIF com decode assíncrono e upload GPU totalmente não-bloqueante.
 *
 * ============================================================
 *  DECODE — GifDecoder puro C++ (sem stb)
 * ============================================================
 *
 *  Decode delega para GifDecoder::Decode() — sem stb_image, sem malloc.
 *  GifDecoder usa std::ifstream + standard library e suporta:
 *    - LZW variable-width codes (2-12 bits), bit reader LE
 *    - Interlacing (4 passes GIF89a)
 *    - Disposal methods (DoNotDispose/RestoreBackground/RestorePrevious)
 *    - Transparência por índice de cor (Graphic Control Extension)
 *    - Global e Local Color Tables
 *
 *  Resultado: GifDecodeResult com buffer RGBA contíguo [frame0]...[frameN].
 *  PrepareChunk acede directamente por offset: raw_pixels.data() + gi*frame_bytes.
 */

#include "pch.hpp"
#include "GifAnimation.hpp"
#include "GifDecoder.hpp"  // decoder GIF puro C++ — sem stb

#include "Memory.hpp"
#include "VulkanContext_Wrapper.hpp"

// ============================================================================
// Move semântico
// ============================================================================

/**
 * @brief Move constructor — transfere posse de todos os recursos.
 * A origem fica em Idle com todos os handles a VK_NULL_HANDLE.
 */
GifAnimation::GifAnimation(GifAnimation&& o) noexcept
    : m_frames(std::move(o.m_frames))
    , m_delays_ms(std::move(o.m_delays_ms))
    , m_desc_pool(o.m_desc_pool)
    , m_desc_layout(o.m_desc_layout)
    , m_upload_cmd_pool(o.m_upload_cmd_pool)
    , m_upload_cmd(o.m_upload_cmd)
    , m_chunk_fence(o.m_chunk_fence)
    , m_width(o.m_width)
    , m_height(o.m_height)
    , m_current_frame(o.m_current_frame)
    , m_elapsed_ms(o.m_elapsed_ms)
    , m_paused(o.m_paused)
    , m_total_frames(o.m_total_frames)
    , m_frames_loaded(o.m_frames_loaded)
    , m_chunk_in_flight(o.m_chunk_in_flight)
    , m_chunk_begin(o.m_chunk_begin)
    , m_chunk_end(o.m_chunk_end)
    , m_decode_thread(std::move(o.m_decode_thread))
    , m_pending(std::move(o.m_pending))
{
    m_state.store(o.m_state.load());

    o.m_desc_pool        = VK_NULL_HANDLE;
    o.m_desc_layout      = VK_NULL_HANDLE;
    o.m_upload_cmd_pool  = VK_NULL_HANDLE;
    o.m_upload_cmd       = VK_NULL_HANDLE;
    o.m_chunk_fence      = VK_NULL_HANDLE;
    o.m_chunk_in_flight  = false;
    o.m_width            = 0;
    o.m_height           = 0;
    o.m_current_frame    = 0;
    o.m_elapsed_ms       = 0.0f;
    o.m_total_frames     = 0;
    o.m_frames_loaded    = 0;
    o.m_chunk_begin      = 0;
    o.m_chunk_end        = 0;
    o.m_state.store(State::Idle);
}

GifAnimation& GifAnimation::operator=(GifAnimation&& o) noexcept
{
    if(this != &o)
    {
        Unload();

        m_frames           = std::move(o.m_frames);
        m_delays_ms        = std::move(o.m_delays_ms);
        m_desc_pool        = o.m_desc_pool;
        m_desc_layout      = o.m_desc_layout;
        m_upload_cmd_pool  = o.m_upload_cmd_pool;
        m_upload_cmd       = o.m_upload_cmd;
        m_chunk_fence      = o.m_chunk_fence;
        m_width            = o.m_width;
        m_height           = o.m_height;
        m_current_frame    = o.m_current_frame;
        m_elapsed_ms       = o.m_elapsed_ms;
        m_paused           = o.m_paused;
        m_total_frames     = o.m_total_frames;
        m_frames_loaded    = o.m_frames_loaded;
        m_chunk_in_flight  = o.m_chunk_in_flight;
        m_chunk_begin      = o.m_chunk_begin;
        m_chunk_end        = o.m_chunk_end;
        m_decode_thread    = std::move(o.m_decode_thread);
        m_pending          = std::move(o.m_pending);
        m_state.store(o.m_state.load());

        o.m_desc_pool       = VK_NULL_HANDLE;
        o.m_desc_layout     = VK_NULL_HANDLE;
        o.m_upload_cmd_pool = VK_NULL_HANDLE;
        o.m_upload_cmd      = VK_NULL_HANDLE;
        o.m_chunk_fence     = VK_NULL_HANDLE;
        o.m_chunk_in_flight = false;
        o.m_width           = 0;
        o.m_height          = 0;
        o.m_current_frame   = 0;
        o.m_elapsed_ms      = 0.0f;
        o.m_total_frames    = 0;
        o.m_frames_loaded   = 0;
        o.m_frames_decoded.store(0);   // reset streaming — novo GIF
        o.m_chunk_begin     = 0;
        o.m_chunk_end       = 0;
        o.m_state.store(State::Idle);
    }
    return *this;
}

// ============================================================================
// GifFrame::GetID
// ============================================================================

/**
 * @brief Converte VkDescriptorSet → ImTextureID.
 *
 * VkDescriptorSet é um handle não-dispatchable — representado como uint64_t
 * no Windows 64-bit. ImTextureID é ImU64 internamente.
 * reinterpret_cast é necessário porque não existe conversão implícita
 * entre handles Vulkan opacos e ImTextureID.
 */
ImTextureID GifAnimation::GifFrame::GetID() const noexcept
{
    if(ds == VK_NULL_HANDLE)
        return ImTextureID{};

    const ImU64 handle = reinterpret_cast<ImU64>(ds);
    return ImTextureID{ handle };
}

// ============================================================================
// FindMemoryType
// ============================================================================

/**
 * @brief Encontra o índice do tipo de memória Vulkan que satisfaz os requisitos.
 *
 * @param phys_dev     GPU física.
 * @param type_filter  memoryRequirements.memoryTypeBits — bitmask dos tipos válidos.
 * @param properties   Flags desejadas (DEVICE_LOCAL, HOST_VISIBLE, etc.).
 * @return             Índice compatível ou 0xFFFFFFFF se não encontrado.
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
        const bool flags_ok =
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties;

        if(type_ok && flags_ok)
            return i;
    }

    return 0xFFFFFFFF;
}

// ============================================================================
// CreateUploadCommandPool
// ============================================================================

/**
 * @brief Cria o VkCommandPool dedicado para operações de upload GIF.
 *
 * Pool separado do ImGui e do frame renderer:
 *   - O command buffer pode ficar válido entre ticks enquanto o fence pende
 *   - Reset individual com RESET_COMMAND_BUFFER_BIT sem destruir o pool
 *   - Sem interferência com os pools do render loop
 */
bool GifAnimation::CreateUploadCommandPool(VkDevice device, uint32_t queue_family)
{
    VkCommandPoolCreateInfo info{};
    info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.queueFamilyIndex = queue_family;
    info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    const VkResult err = vkCreateCommandPool(device, &info, nullptr, &m_upload_cmd_pool);
    VulkanContext::CheckVkResult(err);
    if(err != VK_SUCCESS) return false;

    VkCommandBufferAllocateInfo alloc{};
    alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool        = m_upload_cmd_pool;
    alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;

    const VkResult err2 = vkAllocateCommandBuffers(device, &alloc, &m_upload_cmd);
    VulkanContext::CheckVkResult(err2);
    return (err2 == VK_SUCCESS);
}

// ============================================================================
// CreateDescriptorPool
// ============================================================================

/**
 * @brief Cria o VkDescriptorPool privado dimensionado ao total de frames.
 * maxSets = total_frames — sem overhead, sem impacto no pool do ImGui.
 */
bool GifAnimation::CreateDescriptorPool(VkDevice device, uint32_t frame_count)
{
    VkDescriptorPoolSize pool_size{};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = frame_count;

    VkDescriptorPoolCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    info.maxSets       = frame_count;
    info.poolSizeCount = 1;
    info.pPoolSizes    = &pool_size;

    const VkResult err = vkCreateDescriptorPool(device, &info, nullptr, &m_desc_pool);
    VulkanContext::CheckVkResult(err);
    return (err == VK_SUCCESS);
}

// ============================================================================
// CreateDescriptorSetLayout
// ============================================================================

/**
 * @brief Cria o layout que espelha o descriptor set interno do ImGui.
 * binding 0 → COMBINED_IMAGE_SAMPLER, FRAGMENT, count 1.
 */
bool GifAnimation::CreateDescriptorSetLayout(VkDevice device)
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings    = &binding;

    const VkResult err = vkCreateDescriptorSetLayout(device, &info, nullptr, &m_desc_layout);
    VulkanContext::CheckVkResult(err);
    return (err == VK_SUCCESS);
}

// ============================================================================
// CreateFence
// ============================================================================

/**
 * @brief Cria o VkFence reutilizável não-sinalizado para sincronização de chunks.
 *
 * Não-sinalizado (sem VK_FENCE_CREATE_SIGNALED_BIT):
 *   vkResetFences antes de cada SubmitChunk → não-sinalizado
 *   vkGetFenceStatus → VK_NOT_READY enquanto GPU trabalha
 *   vkGetFenceStatus → VK_SUCCESS após GPU terminar
 */
bool GifAnimation::CreateFence(VkDevice device)
{
    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.flags = 0; // não-sinalizado

    const VkResult err = vkCreateFence(device, &info, nullptr, &m_chunk_fence);
    VulkanContext::CheckVkResult(err);
    return (err == VK_SUCCESS);
}

// ============================================================================
// DestroyFrame
// ============================================================================

/**
 * @brief Liberta os recursos Vulkan de um GifFrame, excepto o descriptor set.
 * O VkDescriptorSet pertence ao pool — destruído em Unload() de uma só vez.
 */
void GifAnimation::DestroyFrame(VkDevice device, GifFrame& frame)
{
    if(frame.upload_mem   != VK_NULL_HANDLE) vkFreeMemory(device, frame.upload_mem, nullptr);
    if(frame.upload_buf   != VK_NULL_HANDLE) vkDestroyBuffer(device, frame.upload_buf, nullptr);
    if(frame.sampler      != VK_NULL_HANDLE) vkDestroySampler(device, frame.sampler, nullptr);
    if(frame.image_view   != VK_NULL_HANDLE) vkDestroyImageView(device, frame.image_view, nullptr);
    if(frame.image        != VK_NULL_HANDLE) vkDestroyImage(device, frame.image, nullptr);
    if(frame.image_memory != VK_NULL_HANDLE) vkFreeMemory(device, frame.image_memory, nullptr);

    frame = GifFrame{};
}

// ============================================================================
// DecodeGif — background thread (streaming por frame)
// ============================================================================

/**
 * @brief Decodifica o GIF frame a frame e disponibiliza cada um imediatamente.
 *
 * Usa GifDecoder::Decode() com dois callbacks:
 *
 *  on_header — chamado uma vez após o Parse (Pass 1):
 *    - Define m_total_frames, m_width, m_height
 *    - Pré-aloca m_pending.raw_pixels (total_frames × frame_bytes)
 *    - Pré-aloca m_pending.delays_ms
 *    - Chamado ANTES do primeiro on_frame — TickUpload pode inicializar
 *      os recursos Vulkan (DescriptorPool, etc.) com o total exacto
 *
 *  on_frame — chamado por frame em ordem (0, 1, 2, ..., N-1):
 *    - Copia o canvas RGBA para m_pending.raw_pixels[frame_idx * frame_bytes]
 *    - Incrementa m_frames_decoded (atomic)
 *    - Se frame_idx == 0: store(Uploading) → TickUpload começa no próximo tick
 *
 * A thread de decode corre concorrentemente com os ticks do render loop:
 *   - TickUpload só lê frames até m_frames_decoded (atomic)
 *   - PrepareChunk limita chunk_end a m_frames_decoded
 *   - O mutex protege apenas o acesso a m_pending.raw_pixels e delays_ms
 *
 * @param path  Cópia do caminho — a thread possui a string.
 */
void GifAnimation::DecodeGif(std::string path)
{
    bool ok = GifDecoder::Decode(
        std::filesystem::path(path),

        // ---- on_header: metadados exactos antes de qualquer frame -----------
        // Chamado logo após o Parse — permite pré-alocar tudo de uma vez
        [this](int w, int h, int total_frames, std::size_t frame_bytes)
        {
            {
                std::lock_guard<std::mutex> lock(m_pending_mutex);

                // Pré-aloca o buffer contíguo com capacidade para TODOS os frames
                // Será preenchido incrementalmente por on_frame via memcpy de offset
                m_pending.raw_pixels.resize(
                    static_cast<std::size_t>(total_frames) * frame_bytes);

                // Pré-aloca delays — on_frame escreve delays_ms[frame_idx] directamente
                m_pending.delays_ms.resize(
                    static_cast<std::size_t>(total_frames), 100);

                m_pending.frame_bytes = frame_bytes;
                m_width               = w;
                m_height              = h;
            }

            // m_total_frames definido ANTES de qualquer frame chegar —
            // TickUpload usa-o para dimensionar DescriptorPool e m_frames[]
            m_total_frames = static_cast<uint32_t>(total_frames);
        },

        // ---- on_frame: chamado por frame assim que composto ------------------
        // canvas_span é válido apenas durante esta chamada — copiar imediatamente
        [this](int frame_idx, std::span<const uint8_t> canvas_span, int delay_ms)
        {
            {
                std::lock_guard<std::mutex> lock(m_pending_mutex);

                // Calcula o offset deste frame no buffer contíguo
                const std::size_t offset =
                    static_cast<std::size_t>(frame_idx) * m_pending.frame_bytes;

                // Copia canvas RGBA → raw_pixels[frame_idx]
                // O buffer foi pré-alocado em on_header — sem realloc aqui
                std::memcpy(
                    m_pending.raw_pixels.data() + offset,
                    canvas_span.data(),
                    m_pending.frame_bytes);

                m_pending.delays_ms[static_cast<std::size_t>(frame_idx)] = delay_ms;
            }

            // Incrementa o contador de frames disponíveis (atomic — sem lock)
            // PrepareChunk lê este valor para saber até onde pode fazer upload
            ++m_frames_decoded;

            // Frame 0 pronto → sinaliza TickUpload para começar o upload GPU
            // Os frames seguintes chegam enquanto o upload do frame 0 corre
            if(frame_idx == 0)
                m_state.store(State::Uploading);
        }
    );

    // Se o decode falhou sem entregar nenhum frame
    if(!ok || m_frames_decoded.load() == 0)
        m_state.store(State::Failed);
}

// ============================================================================
// PrepareChunk — FASE 1
// ============================================================================

/**
 * @brief Cria recursos GPU + memcpy (offset directo) + grava o command buffer.
 *
 * Para cada frame i no chunk [m_chunk_begin, m_chunk_end):
 *   1. vkCreateImage DEVICE_LOCAL + memória + bind
 *   2. vkCreateImageView
 *   3. vkCreateSampler LINEAR
 *   4. vkCreateBuffer HOST_VISIBLE (staging) + memória + bind
 *   5. vkMapMemory + memcpy(staging, raw_pixels + gi*frame_bytes) + flush
 *   6. Preenche barriers copy_barriers[i] e use_barriers[i]
 *
 * O memcpy em 5 usa o OFFSET directamente no buffer contíguo do GifDecoder —
 * sem cópia intermédia, sem alocação adicional de heap.
 *
 * Após o loop, grava o command buffer:
 *   vkResetCommandBuffer
 *   vkCmdPipelineBarrier(N × UNDEFINED→TRANSFER_DST) — batch
 *   vkCmdCopyBufferToImage × N
 *   vkCmdPipelineBarrier(N × TRANSFER_DST→SHADER_READ_ONLY) — batch
 *   vkEndCommandBuffer
 */
bool GifAnimation::PrepareChunk(VkDevice device, VkPhysicalDevice phys)
{
    m_chunk_begin = m_frames_loaded;

    // Limita o chunk aos frames que já têm pixels prontos em m_pending.
    // m_frames_decoded é incrementado pela background thread a cada on_frame.
    // Nunca fazemos upload de um frame cujo memcpy ainda não terminou.
    const uint32_t frames_ready =
        std::min(m_frames_decoded.load(), m_total_frames);

    m_chunk_end = std::min(m_chunk_begin + FRAMES_PER_CHUNK, frames_ready);


    const uint32_t chunk_size = m_chunk_end - m_chunk_begin;
    if(chunk_size == 0) return true;

    // Lê frame_bytes sob lock — escrito pela thread em DecodeGif
    std::size_t frame_bytes = 0;
    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        frame_bytes = m_pending.frame_bytes;
    }

    VkResult err;

    std::vector<VkImageMemoryBarrier> copy_barriers(chunk_size);
    std::vector<VkImageMemoryBarrier> use_barriers(chunk_size);

    for(uint32_t ci = 0; ci < chunk_size; ++ci)
    {
        const uint32_t gi    = m_chunk_begin + ci;
        GifFrame&      frame = m_frames[gi];

        // ---- VkImage DEVICE_LOCAL + memória + bind -------------------------
        {
            VkImageCreateInfo info{};
            info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            info.imageType     = VK_IMAGE_TYPE_2D;
            info.format        = VK_FORMAT_R8G8B8A8_UNORM;
            info.extent        = {
                static_cast<uint32_t>(m_width),
                static_cast<uint32_t>(m_height), 1
            };
            info.mipLevels     = 1;
            info.arrayLayers   = 1;
            info.samples       = VK_SAMPLE_COUNT_1_BIT;
            info.tiling        = VK_IMAGE_TILING_OPTIMAL;
            info.usage         = VK_IMAGE_USAGE_SAMPLED_BIT
                               | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            err = vkCreateImage(device, &info, nullptr, &frame.image);
            VulkanContext::CheckVkResult(err);
            if(err != VK_SUCCESS) return false;

            VkMemoryRequirements req;
            vkGetImageMemoryRequirements(device, frame.image, &req);

            VkMemoryAllocateInfo alloc{};
            alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize  = req.size;
            alloc.memoryTypeIndex = FindMemoryType(phys, req.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            err = vkAllocateMemory(device, &alloc, nullptr, &frame.image_memory);
            VulkanContext::CheckVkResult(err);
            if(err != VK_SUCCESS) return false;

            err = vkBindImageMemory(device, frame.image, frame.image_memory, 0);
            VulkanContext::CheckVkResult(err);
            if(err != VK_SUCCESS) return false;
        }

        // ---- VkImageView ---------------------------------------------------
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

        // ---- VkSampler LINEAR ----------------------------------------------
        {
            VkSamplerCreateInfo info{};
            info.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            info.magFilter     = VK_FILTER_LINEAR;
            info.minFilter     = VK_FILTER_LINEAR;
            info.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            info.addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            info.addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            info.addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            info.minLod        = -1000.0f;
            info.maxLod        =  1000.0f;
            info.maxAnisotropy = 1.0f;

            err = vkCreateSampler(device, &info, nullptr, &frame.sampler);
            VulkanContext::CheckVkResult(err);
            if(err != VK_SUCCESS) return false;
        }

        // ---- VkBuffer HOST_VISIBLE (staging) + memória + bind -------------
        {
            VkBufferCreateInfo info{};
            info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            info.size        = static_cast<VkDeviceSize>(frame_bytes);
            info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
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
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

            err = vkAllocateMemory(device, &alloc, nullptr, &frame.upload_mem);
            VulkanContext::CheckVkResult(err);
            if(err != VK_SUCCESS) return false;

            err = vkBindBufferMemory(device, frame.upload_buf, frame.upload_mem, 0);
            VulkanContext::CheckVkResult(err);
            if(err != VK_SUCCESS) return false;
        }

        // ---- memcpy directo com offset no buffer contíguo -----------------
        {
            void* map = nullptr;
            err = vkMapMemory(device, frame.upload_mem, 0,
                static_cast<VkDeviceSize>(frame_bytes), 0, &map);
            VulkanContext::CheckVkResult(err);
            if(err != VK_SUCCESS) return false;

            // AQUI ESTÁ O GANHO PRINCIPAL:
            // Em vez de ler de decoded[gi].pixels.data() (vector separado),
            // calculamos o offset directamente no buffer contíguo do GifDecoder.
            // Sem indireções de heap extra — cache-friendly, um único bloco.
            {
                std::lock_guard<std::mutex> lock(m_pending_mutex);
                const uint8_t* src =
                    m_pending.raw_pixels.data() +                // início do buffer
                    static_cast<std::ptrdiff_t>(gi) *            // frame index global
                    static_cast<std::ptrdiff_t>(frame_bytes);    // bytes por frame

                // Único memcpy por frame — inevitável (CPU→staging buffer Vulkan)
                std::memcpy(map, src, frame_bytes);

                // Copia delay para m_delays_ms enquanto temos o lock
                m_delays_ms[gi] = m_pending.delays_ms[gi];
            }

            VkMappedMemoryRange range{};
            range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = frame.upload_mem;
            range.size   = static_cast<VkDeviceSize>(frame_bytes);
            vkFlushMappedMemoryRanges(device, 1, &range);

            vkUnmapMemory(device, frame.upload_mem);
        }

        // ---- Preenche barriers UNDEFINED→TRANSFER_DST ---------------------
        copy_barriers[ci]                             = VkImageMemoryBarrier{};
        copy_barriers[ci].sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        copy_barriers[ci].dstAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
        copy_barriers[ci].oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
        copy_barriers[ci].newLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        copy_barriers[ci].srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        copy_barriers[ci].dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        copy_barriers[ci].image                       = frame.image;
        copy_barriers[ci].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_barriers[ci].subresourceRange.levelCount = 1;
        copy_barriers[ci].subresourceRange.layerCount = 1;

        // ---- Preenche barriers TRANSFER_DST→SHADER_READ_ONLY --------------
        use_barriers[ci]                             = VkImageMemoryBarrier{};
        use_barriers[ci].sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        use_barriers[ci].srcAccessMask               = VK_ACCESS_TRANSFER_WRITE_BIT;
        use_barriers[ci].dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;
        use_barriers[ci].oldLayout                   = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        use_barriers[ci].newLayout                   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        use_barriers[ci].srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        use_barriers[ci].dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        use_barriers[ci].image                       = frame.image;
        use_barriers[ci].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        use_barriers[ci].subresourceRange.levelCount = 1;
        use_barriers[ci].subresourceRange.layerCount = 1;
    }

    // ---- Grava o command buffer único para o chunk -------------------------

    // Reset individual — RESET_COMMAND_BUFFER_BIT no pool permite isto
    err = vkResetCommandBuffer(m_upload_cmd, 0);
    VulkanContext::CheckVkResult(err);
    if(err != VK_SUCCESS) return false;

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    err = vkBeginCommandBuffer(m_upload_cmd, &begin_info);
    VulkanContext::CheckVkResult(err);
    if(err != VK_SUCCESS) return false;

    // Batch de N barriers UNDEFINED→TRANSFER_DST numa única chamada
    vkCmdPipelineBarrier(m_upload_cmd,
        VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr,
        chunk_size, copy_barriers.data());

    // N cópias buffer→imagem
    for(uint32_t ci = 0; ci < chunk_size; ++ci)
    {
        const uint32_t gi = m_chunk_begin + ci;

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {
            static_cast<uint32_t>(m_width),
            static_cast<uint32_t>(m_height), 1
        };

        vkCmdCopyBufferToImage(m_upload_cmd,
            m_frames[gi].upload_buf,
            m_frames[gi].image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region);
    }

    // Batch de N barriers TRANSFER_DST→SHADER_READ_ONLY numa única chamada
    vkCmdPipelineBarrier(m_upload_cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr,
        chunk_size, use_barriers.data());

    err = vkEndCommandBuffer(m_upload_cmd);
    VulkanContext::CheckVkResult(err);
    return (err == VK_SUCCESS);
}

// ============================================================================
// SubmitChunk — FASE 2
// ============================================================================

/**
 * @brief Submete o command buffer com o fence — NÃO BLOQUEIA.
 *
 * vkQueueSubmit: GPU começa as cópias.
 * A main thread retorna IMEDIATAMENTE — zero vkDeviceWaitIdle.
 * O fence é sinalizado quando a GPU terminar o chunk.
 */
bool GifAnimation::SubmitChunk(VkQueue queue)
{
    VkDevice device = Memory::Get()->GetVulkan()->GetDevice();

    // Reset do fence ANTES do submit — estado não-sinalizado
    const VkResult reset_err = vkResetFences(device, 1, &m_chunk_fence);
    VulkanContext::CheckVkResult(reset_err);
    if(reset_err != VK_SUCCESS) return false;

    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &m_upload_cmd;

    // Submit com fence — GPU sinaliza quando terminar, sem bloquear a CPU
    const VkResult err = vkQueueSubmit(queue, 1, &submit, m_chunk_fence);
    VulkanContext::CheckVkResult(err);
    if(err != VK_SUCCESS) return false;

    m_chunk_in_flight = true;
    return true;
}

// ============================================================================
// FinalizeChunk — FASE 3
// ============================================================================

/**
 * @brief Após fence sinalizado: aloca descriptor sets para o chunk.
 *
 * A GPU terminou — imagens em SHADER_READ_ONLY_OPTIMAL.
 * Aloca e actualiza os VkDescriptorSet para [m_chunk_begin, m_chunk_end).
 * Após retornar, GetCurrentID() pode devolver estes frames com segurança.
 *
 * Quando o último chunk é finalizado, liberta m_pending.raw_pixels:
 * todos os pixels já estão na VRAM — RAM CPU recuperada.
 */
bool GifAnimation::FinalizeChunk(VkDevice device)
{
    const uint32_t chunk_size = m_chunk_end - m_chunk_begin;

    for(uint32_t ci = 0; ci < chunk_size; ++ci)
    {
        const uint32_t gi    = m_chunk_begin + ci;
        GifFrame&      frame = m_frames[gi];

        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool     = m_desc_pool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts        = &m_desc_layout;

        const VkResult err = vkAllocateDescriptorSets(device, &alloc, &frame.ds);
        VulkanContext::CheckVkResult(err);
        if(err != VK_SUCCESS) return false;

        VkDescriptorImageInfo image_info{};
        image_info.sampler     = frame.sampler;
        image_info.imageView   = frame.image_view;
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = frame.ds;
        write.dstBinding      = 0;
        write.descriptorCount = 1;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo      = &image_info;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    m_frames_loaded   = m_chunk_end;
    m_chunk_in_flight = false;

    // Quando todos os frames foram finalizados, liberta o buffer raw do GifDecoder
    // Todos os pixels já estão na VRAM — raw_pixels já não é necessário
    if(m_frames_loaded >= m_total_frames)
    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        m_pending.raw_pixels.clear();
        m_pending.raw_pixels.shrink_to_fit();
        m_pending.delays_ms.clear();
    }

    return true;
}

// ============================================================================
// LoadAsync
// ============================================================================

/**
 * @brief Inicia o decode numa background thread — retorna imediatamente.
 * TickUpload() detecta Decoding → Uploading e começa os chunks.
 */
void GifAnimation::LoadAsync(const char* path)
{
    Unload();

    m_frames_loaded   = 0;
    m_frames_decoded.store(0);   // reset streaming — novo GIF
    m_total_frames    = 0;
    m_chunk_begin     = 0;
    m_chunk_end       = 0;
    m_chunk_in_flight = false;
    m_state.store(State::Decoding);

    m_decode_thread = std::thread(&GifAnimation::DecodeGif, this, std::string(path));
}

// ============================================================================
// Load — síncrono
// ============================================================================

/**
 * @brief Carregamento síncrono — bloqueia até tudo estar na VRAM.
 * Decode na thread actual + loop TickUpload com vkWaitForFences.
 */
bool GifAnimation::Load(const char* path)
{
    Unload();

    m_frames_loaded   = 0;
    m_frames_decoded.store(0);   // reset streaming — novo GIF
    m_total_frames    = 0;
    m_chunk_begin     = 0;
    m_chunk_end       = 0;
    m_chunk_in_flight = false;
    m_state.store(State::Decoding);

    DecodeGif(std::string(path));

    if(m_state.load() != State::Uploading)
        return false;

    VkDevice device = Memory::Get()->GetVulkan()->GetDevice();

    while(m_state.load() == State::Uploading)
    {
        if(!TickUpload())
        {
            if(m_state.load() == State::Failed) return false;

            // Modo síncrono: espera o fence em vez de polling
            if(m_chunk_in_flight && m_chunk_fence != VK_NULL_HANDLE)
            {
                constexpr uint64_t TIMEOUT_NS = 5'000'000'000ULL;
                vkWaitForFences(device, 1, &m_chunk_fence, VK_TRUE, TIMEOUT_NS);
            }
        }
    }

    return m_state.load() == State::Loaded;
}

// ============================================================================
// TickUpload
// ============================================================================

/**
 * @brief Avança o upload fence-based — chame UMA VEZ por frame na main thread.
 *
 * Estados:
 *   Decoding       → return false (thread ainda a correr)
 *   Uploading, chunk em voo:
 *     vkGetFenceStatus NOT_READY → return false (~1µs)
 *     vkGetFenceStatus SUCCESS   → FinalizeChunk → PrepareChunk + SubmitChunk
 *   Uploading, sem chunk em voo → PrepareChunk + SubmitChunk → return false
 *   Loaded → return true (último chunk finalizado neste tick)
 *
 * @return true no tick em que o último chunk foi finalizado.
 */
bool GifAnimation::TickUpload()
{
    if(m_state.load() != State::Uploading)
        return false;

    // NÃO fazemos join aqui — a background thread ainda pode estar a correr
    // (a entregar frames via on_frame enquanto o upload do frame 0 começa).
    // O join é feito em Unload() que aguarda correctamente a thread terminar.

    VulkanContext*   vk     = Memory::Get()->GetVulkan();
    VkDevice         device = vk->GetDevice();
    VkPhysicalDevice phys   = vk->GetPhysicalDevice();
    VkQueue          queue  = vk->GetQueue();

    // ---- Inicialização única (primeiro tick em Uploading) ------------------
    if(m_frames_loaded == 0 && !m_chunk_in_flight)
    {
        if(!CreateDescriptorSetLayout(device))            { m_state.store(State::Failed); return false; }
        if(!CreateDescriptorPool(device, m_total_frames)) { m_state.store(State::Failed); return false; }
        if(!CreateUploadCommandPool(device, vk->GetQueueFamily())) { m_state.store(State::Failed); return false; }
        if(!CreateFence(device))                          { m_state.store(State::Failed); return false; }

        m_frames.resize(m_total_frames);
        m_delays_ms.resize(m_total_frames, 100);
    }

    // ---- Verifica chunk em voo (fence-based) -------------------------------
    if(m_chunk_in_flight)
    {
        // NÃO BLOQUEIA — apenas lê o estado do fence (~microsegundos)
        const VkResult status = vkGetFenceStatus(device, m_chunk_fence);

        if(status == VK_NOT_READY)
            return false; // GPU ainda a trabalhar — render loop continua normalmente

        if(status != VK_SUCCESS)
        {
            VulkanContext::CheckVkResult(status);
            m_state.store(State::Failed);
            return false;
        }

        // Fence sinalizado — GPU terminou este chunk
        if(!FinalizeChunk(device))
        {
            m_state.store(State::Failed);
            return false;
        }

        // Verifica se todos os frames foram finalizados
        if(m_frames_loaded >= m_total_frames)
        {
            m_state.store(State::Loaded);
            return true; // animação totalmente pronta
        }

        // Há mais chunks — PrepareChunk do próximo imediatamente neste tick
    }

    // ---- Prepara e submete o próximo chunk ---------------------------------
    if(!PrepareChunk(device, phys)) { m_state.store(State::Failed); return false; }
    if(!SubmitChunk(queue))         { m_state.store(State::Failed); return false; }

    return false; // chunk submetido — próximo tick verifica o fence
}

// ============================================================================
// Unload
// ============================================================================

/**
 * @brief Liberta todos os recursos e aguarda a thread.
 *
 * Ordem:
 *  1. join() — aguarda thread de decode
 *  2. vkWaitForFences — se chunk em voo, aguarda GPU terminar
 *  3. Liberta m_pending (pixels CPU)
 *  4. DestroyFrame × N
 *  5. vkDestroyDescriptorPool
 *  6. vkDestroyDescriptorSetLayout
 *  7. vkDestroyCommandPool (liberta m_upload_cmd automaticamente)
 *  8. vkDestroyFence
 *  9. Reset do estado
 */
void GifAnimation::Unload()
{
    if(m_decode_thread.joinable())
        m_decode_thread.join();

    const bool has_vk =
        !m_frames.empty()                    ||
        m_desc_pool       != VK_NULL_HANDLE  ||
        m_desc_layout     != VK_NULL_HANDLE  ||
        m_upload_cmd_pool != VK_NULL_HANDLE  ||
        m_chunk_fence     != VK_NULL_HANDLE;

    if(!has_vk)
    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        m_pending = PendingGif{};
        m_total_frames  = 0;
        m_frames_loaded = 0;
        m_chunk_in_flight = false;
        m_state.store(State::Idle);
        return;
    }

    VkDevice device = Memory::Get()->GetVulkan()->GetDevice();

    // Se há chunk em voo, aguarda GPU antes de destruir recursos
    if(m_chunk_in_flight && m_chunk_fence != VK_NULL_HANDLE)
    {
        constexpr uint64_t TIMEOUT_NS = 5'000'000'000ULL;
        vkWaitForFences(device, 1, &m_chunk_fence, VK_TRUE, TIMEOUT_NS);
        m_chunk_in_flight = false;
    }

    {
        std::lock_guard<std::mutex> lock(m_pending_mutex);
        m_pending = PendingGif{};
    }

    for(auto& frame : m_frames)
        DestroyFrame(device, frame);

    m_frames.clear();
    m_delays_ms.clear();

    if(m_desc_pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, m_desc_pool, nullptr);
        m_desc_pool = VK_NULL_HANDLE;
    }

    if(m_desc_layout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, m_desc_layout, nullptr);
        m_desc_layout = VK_NULL_HANDLE;
    }

    // Destrói o command pool — liberta m_upload_cmd automaticamente
    if(m_upload_cmd_pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, m_upload_cmd_pool, nullptr);
        m_upload_cmd_pool = VK_NULL_HANDLE;
        m_upload_cmd      = VK_NULL_HANDLE;
    }

    if(m_chunk_fence != VK_NULL_HANDLE)
    {
        vkDestroyFence(device, m_chunk_fence, nullptr);
        m_chunk_fence = VK_NULL_HANDLE;
    }

    m_width           = 0;
    m_height          = 0;
    m_current_frame   = 0;
    m_elapsed_ms      = 0.0f;
    m_paused          = false;
    m_total_frames    = 0;
    m_frames_loaded   = 0;
    m_frames_decoded.store(0);   // reset streaming — novo GIF
    m_chunk_begin     = 0;
    m_chunk_end       = 0;
    m_chunk_in_flight = false;
    m_state.store(State::Idle);
}

// ============================================================================
// Update
// ============================================================================

/**
 * @brief Avança o temporizador e muda de frame quando o delay expira.
 *
 * Seguro durante Uploading — clampeia a m_frames_loaded - 1.
 * Animação começa logo que o 1.º chunk é finalizado (HasAnyFrame()).
 *
 * @param delta_ms  DeltaTime × 1000 (millisegundos).
 */
void GifAnimation::Update(float delta_ms)
{
    if(m_paused || m_frames_loaded <= 1)
        return;

    m_elapsed_ms += delta_ms;

    const int available = static_cast<int>(m_frames_loaded);

    // Clamp defensivo — durante upload m_frames_loaded cresce a cada chunk
    if(m_current_frame >= available)
        m_current_frame = 0;

    while(m_elapsed_ms >=
          static_cast<float>(m_delays_ms[static_cast<std::size_t>(m_current_frame)]))
    {
        m_elapsed_ms -= static_cast<float>(
            m_delays_ms[static_cast<std::size_t>(m_current_frame)]);

        // Wrap apenas nos frames já disponíveis
        m_current_frame = (m_current_frame + 1) % available;
    }
}

// ============================================================================
// Reset
// ============================================================================

void GifAnimation::Reset() noexcept
{
    m_current_frame = 0;
    m_elapsed_ms    = 0.0f;
}

// ============================================================================
// GetCurrentID
// ============================================================================

/**
 * @brief ImTextureID do frame activo — passa directamente a ImGui::Image().
 * Clampeia a m_frames_loaded durante upload progressivo.
 */
ImTextureID GifAnimation::GetCurrentID() const noexcept
{
    if(m_frames_loaded == 0)
        return ImTextureID{};

    const std::size_t available = m_frames_loaded;
    const std::size_t idx       =
        static_cast<std::size_t>(m_current_frame) % available;

    return m_frames[idx].GetID();
}