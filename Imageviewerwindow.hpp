#pragma once
#include "pch.hpp"
#include "Image.hpp"

/**
 * @file ImageViewerWindow.hpp
 * @brief Uma janela ImGui independente que exibe uma única imagem Vulkan.
 *
 * PADRÃO FACTORY
 * ---------------
 * ImageViewerWindow não é construído diretamente pelo código de UI.
 * ImageViewerFactory é o único ponto de criação — ele atribui o ID único
 * que evita conflitos de janelas no ImGui e mantém o ciclo de vida via
 * std::unique_ptr.
 *
 * ZOOM
 * -----
 * Ctrl+Scroll dentro da janela altera m_zoom [ZOOM_MIN, ZOOM_MAX].
 * O botão "Fit" recalcula o zoom para caber a imagem na área disponível.
 * O botão "1:1" restaura zoom = 1.0 (pixel perfeito).
 *
 * RESIZE
 * -------
 * A janela ImGui é livremente redimensionável pelo usuário.
 * ImGuiWindowFlags_None deixa o ImGui gerenciar o tamanho; a imagem
 * é desenhada com o tamanho resultante de aplicar m_zoom sobre as
 * dimensões originais da imagem.
 *
 * CICLO DE VIDA
 * --------------
 * m_open começa true. Quando o usuário clica no X da janela ImGui,
 * m_open é zerado. ImageViewerFactory::RemoveClosed() recolhe e
 * destrói janelas com m_open == false a cada frame.
 */
class ImageViewerWindow {
public:

    // =========================================================================
    // Constantes de zoom
    // =========================================================================

    static constexpr float ZOOM_MIN  = 0.05f; ///< Zoom mínimo (5%)
    static constexpr float ZOOM_MAX  = 16.0f; ///< Zoom máximo (1600%)
    static constexpr float ZOOM_STEP = 0.1f;  ///< Incremento por tick de scroll

    // =========================================================================
    // Construtor / Destrutor
    // =========================================================================

    /**
     * @brief Constrói a janela, carrega a imagem e define o título.
     *
     * @param id        ID único inteiro — integrado ao título via "##id" para
     *                  evitar colisões de IDs no ImGui.
     * @param filepath  Caminho wide do arquivo de imagem (PNG, JPG, BMP, TGA).
     */
    ImageViewerWindow(int id, std::wstring filepath);

    ~ImageViewerWindow() = default;

    // Não copiável — Image contém handles Vulkan de posse única
    ImageViewerWindow(const ImageViewerWindow&)            = delete;
    ImageViewerWindow& operator=(const ImageViewerWindow&) = delete;

    // Movível
    ImageViewerWindow(ImageViewerWindow&&)            = default;
    ImageViewerWindow& operator=(ImageViewerWindow&&) = default;

    // =========================================================================
    // API pública
    // =========================================================================

    /**
     * @brief Desenha a janela ImGui. Chame uma vez por frame.
     *
     * Retorna sem desenhar nada se a imagem não foi carregada com sucesso.
     * Após o usuário clicar no X, m_open é false — verifique IsOpen().
     */
    void Draw();

    /** @brief true enquanto o usuário não fechar a janela com o X. */
    [[nodiscard]] bool IsOpen()    const noexcept { return m_open;    }

    /** @brief true se a imagem Vulkan foi carregada com sucesso. */
    [[nodiscard]] bool IsLoaded()  const noexcept { return m_image.IsLoaded(); }

    /** @brief Caminho completo do arquivo aberto. */
    [[nodiscard]] const std::wstring& GetFilepath() const noexcept { return m_filepath; }

    /** @brief ID único desta janela (atribuído pelo factory). */
    [[nodiscard]] int  GetID()     const noexcept { return m_id;      }

private:

    // =========================================================================
    // Estado interno
    // =========================================================================

    int          m_id;       ///< ID único para título ImGui "##N"
    std::wstring m_filepath; ///< Caminho completo do arquivo
    std::wstring m_title;    ///< Título wide da janela (filename + " ##id")
    std::string  m_title_utf8; ///< Título em UTF-8 para ImGui::Begin
    Image        m_image;    ///< Imagem Vulkan — posse exclusiva desta janela
    bool         m_open;     ///< false quando o X foi clicado
    float        m_zoom;     ///< Fator de zoom atual [ZOOM_MIN, ZOOM_MAX]

    // =========================================================================
    // Métodos auxiliares de renderização
    // =========================================================================

    /** @brief Desenha a toolbar interna (Fit, 1:1, zoom%, info). */
    void DrawToolbar();

    /**
     * @brief Desenha a imagem com scroll e zoom no child region.
     * @param available  Tamanho disponível na janela após a toolbar.
     */
    void DrawImage(ImVec2 available);

    /**
     * @brief Recalcula m_zoom para a imagem caber na área disponível.
     * @param available  Área disponível em pixels de tela.
     */
    void FitToWindow(ImVec2 available);

    /**
     * @brief Converte wchar_t* para UTF-8 — fronteira com ImGui.
     * @param wstr  String wide terminada em nulo.
     * @return      std::string UTF-8.
     */
    [[nodiscard]] static std::string WideToUtf8(const wchar_t* wstr);
};