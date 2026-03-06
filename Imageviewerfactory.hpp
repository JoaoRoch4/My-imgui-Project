#pragma once
#include "pch.hpp"
#include "ImageViewerWindow.hpp"

#include <winrt/base.h>   // winrt::com_ptr
#include <wil/resource.h> // wil::unique_cotaskmem_string

/**
 * @file ImageViewerFactory.hpp
 * @brief Factory e gerenciador do ciclo de vida de janelas de imagem.
 *
 * SEPARAÇÃO DRAW / CLEANUP — PORQUÊ É OBRIGATÓRIA
 * -------------------------------------------------
 * O loop de renderização Vulkan + ImGui tem uma ordem rígida:
 *
 *   [CPU — construção do frame]
 *     ImGui::NewFrame()
 *     DrawAll()              ← só ImGui::Begin/Image/End, SEM Vulkan
 *     ImGui::Render()
 *
 *   [GPU — submissão]
 *     FrameRender()          ← vkBeginCommandBuffer ... vkQueueSubmit
 *     FramePresent()         ← vkQueuePresentKHR
 *
 *   [Cleanup — seguro]
 *     PostFrameCleanup()     ← vkDeviceWaitIdle + destruição de recursos
 *
 * Chamar vkDeviceWaitIdle DENTRO de DrawAll() (entre NewFrame e Render)
 * corrompe o estado do Vulkan e causa VK_ERROR_DEVICE_LOST no vkQueueSubmit
 * seguinte.  DrawAll() não deve tocar em nenhuma API Vulkan — só ImGui.
 *
 * PostFrameCleanup() é chamado DEPOIS de FramePresent(), quando a GPU
 * terminou o frame e é seguro destruir qualquer recurso.
 *
 * INTEGRAÇÃO NO LOOP PRINCIPAL (MyWindows / App)
 * ------------------------------------------------
 *   // Fase 1 — construção ImGui (sem Vulkan)
 *   m_factory.DrawAll();
 *
 *   // Fase 2 — submissão GPU
 *   g_VulkanContext->FrameRender(draw_data);
 *   g_VulkanContext->FramePresent();
 *
 *   // Fase 3 — cleanup seguro, após submit+present
 *   m_factory.PostFrameCleanup();
 */
class ImageViewerFactory {
public:

    // =========================================================================
    // Formatos suportados (compile-time)
    // =========================================================================

    /** @brief Filtros exibidos no diálogo. Pares {nome, extensões}. */
    static constexpr std::array<std::pair<const wchar_t*, const wchar_t*>, 5>
    FILE_FILTERS = { {
        { L"Imagens",               L"*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif" },
        { L"PNG (*.png)",           L"*.png"        },
        { L"JPEG (*.jpg, *.jpeg)",  L"*.jpg;*.jpeg" },
        { L"BMP (*.bmp)",           L"*.bmp"        },
        { L"TGA (*.tga)",           L"*.tga"        }
    } };

    // =========================================================================
    // Construtor / Destrutor
    // =========================================================================

    ImageViewerFactory()  noexcept;
    ~ImageViewerFactory() noexcept;

    ImageViewerFactory(const ImageViewerFactory&)            = delete;
    ImageViewerFactory& operator=(const ImageViewerFactory&) = delete;
    ImageViewerFactory(ImageViewerFactory&&)                 = default;
    ImageViewerFactory& operator=(ImageViewerFactory&&)      = default;

    // =========================================================================
    // API pública
    // =========================================================================

    /**
     * @brief Abre o explorador do Windows e cria janelas para as imagens
     *        seleccionadas (suporta multi-seleção).
     * @return true se pelo menos uma imagem foi aberta.
     */
    bool OpenFileDialog();

    /**
     * @brief Cria uma nova janela para um caminho já conhecido.
     * @param filepath  Caminho wide completo.
     * @return          Ponteiro não-possuidor; nullptr se caminho vazio.
     */
    ImageViewerWindow* OpenFile(std::wstring filepath);

    /**
     * @brief Fase 1 — só ImGui, zero Vulkan.
     *
     * Chame entre ImGui::NewFrame() e ImGui::Render().
     * Marca janelas fechadas em m_pending_destroy mas NÃO as destrói.
     * A destruição de recursos Vulkan acontece em PostFrameCleanup().
     */
    void DrawAll();

    /**
     * @brief Fase 2 — cleanup seguro após FramePresent().
     *
     * Chame DEPOIS de FrameRender() + FramePresent(), nunca antes.
     * Se houver janelas pendentes de destruição, faz vkDeviceWaitIdle
     * e liberta os recursos Vulkan.
     *
     * Chamar antes do submit causa VK_ERROR_DEVICE_LOST.
     */
    void PostFrameCleanup();

    /**
     * @brief Botão "Abrir Imagem..." embutível em qualquer janela ImGui.
     */
    void DrawOpenButton();

    /**
     * @brief Fecha todas as janelas imediatamente (com GPU sync).
     * Seguro chamar apenas fora do loop de render (ex.: shutdown).
     */
    void CloseAll();

    /** @brief Número de janelas actualmente abertas. */
    [[nodiscard]] std::size_t GetOpenCount() const noexcept;

private:

    std::vector<std::unique_ptr<ImageViewerWindow>> m_windows;        ///< Janelas vivas
    std::vector<std::unique_ptr<ImageViewerWindow>> m_pending_destroy; ///< Aguardam GPU idle

    int  m_next_id;  ///< Próximo ID único (começa em 1)
    bool m_com_init; ///< true se CoInitializeEx foi chamado por esta instância
};