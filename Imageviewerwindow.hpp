#pragma once
#include "pch.hpp"
#include "Image.hpp"
#include "GifAnimation.hpp"

/**
 * @file ImageViewerWindow.hpp
 * @brief Janela ImGui que exibe imagem estática ou GIF animado.
 *
 * FUNCIONALIDADES
 * ----------------
 *  - Zoom: Ctrl+Scroll, botões Fit / 1:1 / + / -
 *  - Pan:  arrastar com botão esquerdo move a imagem
 *  - Scroll: roda do rato (vertical), Shift+roda (horizontal)
 *  - Controlos de animação para GIFs (Play/Pause/Reset/frame N/M)
 *  - Botão [+ Nova imagem] na toolbar — abre nova janela via callback
 *
 * PAN
 * ----
 * O child usa ImGuiWindowFlags_NoScrollWithMouse para desactivar o
 * scroll automático do ImGui. O drag é detectado com IsMouseDragging
 * e o scroll ajustado via SetScrollX/Y manualmente.
 * Cursor muda para Hand em hover e ResizeAll durante o drag.
 *
 * BOTÃO [+ Nova imagem]
 * ----------------------
 * ImageViewerWindow não conhece o factory — usa std::function<void()>
 * passada no construtor. O factory passa [this]{ OpenFileDialog(); }.
 * Se a callback estiver vazia, o botão não é exibido.
 */
class ImageViewerWindow {
public:

    static constexpr float ZOOM_MIN  = 0.05f; ///< Zoom mínimo (5%)
    static constexpr float ZOOM_MAX  = 16.0f; ///< Zoom máximo (1600%)
    static constexpr float ZOOM_STEP = 0.1f;  ///< Passo por tick de scroll

    /**
     * @brief Constrói a janela e carrega o conteúdo.
     *
     * @param id            ID único do factory (sufixo ImGui "##id").
     * @param filepath      Caminho wide do ficheiro (PNG, JPG, BMP, TGA, GIF).
     * @param open_callback Invocada ao clicar [+ Nova imagem]; pode ser vazia.
     */
    ImageViewerWindow(int id, std::wstring filepath,
                      std::function<void()> open_callback = {});

    ~ImageViewerWindow() = default;

    ImageViewerWindow(const ImageViewerWindow&)            = delete;
    ImageViewerWindow& operator=(const ImageViewerWindow&) = delete;
    ImageViewerWindow(ImageViewerWindow&&)                 = default;
    ImageViewerWindow& operator=(ImageViewerWindow&&)      = default;

    /** @brief Desenha a janela. Chame uma vez por frame. */
    void Draw();

    /** @brief true enquanto a janela não for fechada. */
    [[nodiscard]] bool IsOpen()   const noexcept { return m_open; }

    /** @brief true se a imagem/GIF foi carregado com sucesso. */
    [[nodiscard]] bool IsLoaded() const noexcept;

    /** @brief true se o ficheiro é um GIF. */
    [[nodiscard]] bool IsGif()    const noexcept { return m_is_gif; }

    /** @brief Caminho completo do ficheiro. */
    [[nodiscard]] const std::wstring& GetFilepath() const noexcept { return m_filepath; }

    /** @brief ID único desta janela. */
    [[nodiscard]] int GetID() const noexcept { return m_id; }

private:

    int          m_id;           ///< ID único — sufixo ImGui "##N"
    std::wstring m_filepath;     ///< Caminho completo do ficheiro
    std::wstring m_title;        ///< "filename.ext##id" (wide)
    std::string  m_title_utf8;   ///< "filename.ext##id" (UTF-8 para ImGui)

    Image        m_image;        ///< Textura estática (se !m_is_gif)
    GifAnimation m_gif;          ///< Animação GIF     (se  m_is_gif)
    bool         m_is_gif;       ///< true se extensão == .gif

    bool  m_open; ///< false quando X clicado
    float m_zoom; ///< Zoom actual [ZOOM_MIN, ZOOM_MAX]

    /// Callback para [+ Nova imagem] — invocada ao clicar o botão.
    /// Passada pelo factory; pode ser vazia (botão não aparece).
    std::function<void()> m_open_callback;

    /** @brief Toolbar: Fit, 1:1, +, -, info, [+ Nova imagem], controlos GIF. */
    void DrawToolbar();

    /**
     * @brief Child scrollável com pan, zoom e tooltip de coordenadas.
     * @param available  Área disponível após toolbar + separator.
     */
    void DrawImage(ImVec2 available);

    /** @brief Recalcula m_zoom para a imagem caber na área. */
    void FitToWindow(ImVec2 available);

    [[nodiscard]] int         GetContentWidth()     const noexcept;
    [[nodiscard]] int         GetContentHeight()    const noexcept;
    [[nodiscard]] ImTextureID GetCurrentTextureID() const noexcept;

    [[nodiscard]] static std::string WideToUtf8(const wchar_t* wstr);
    [[nodiscard]] static bool HasGifExtension(const std::wstring& filepath) noexcept;
};