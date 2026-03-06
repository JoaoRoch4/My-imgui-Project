#pragma once
#include "pch.hpp"
#include "ImageViewerWindow.hpp"

/**
 * @file ImageViewerFactory.hpp
 * @brief Factory e gerenciador do ciclo de vida de janelas de imagem.
 *
 * PADRÃO FACTORY
 * ---------------
 * ImageViewerFactory é o único ponto de criação de ImageViewerWindow.
 * Cada chamada a OpenFileDialog() ou OpenFile() produz uma nova janela
 * independente — mesma imagem pode ser aberta múltiplas vezes.
 *
 * O factory:
 *  - Abre o explorador do Windows via IFileOpenDialog (COM moderno)
 *  - Cria uma nova ImageViewerWindow com ID único crescente
 *  - Armazena a janela em m_windows (std::vector de unique_ptr)
 *  - Chama Draw() em todas as janelas abertas a cada frame
 *  - Remove janelas fechadas (IsOpen() == false) após cada frame
 *
 * INTEGRAÇÃO COM MyWindows
 * -------------------------
 * Em MyWindows.hpp: adicione "ImageViewerFactory m_image_viewer_factory;"
 * Em MyWindows::CreateWindows(): chame "m_image_viewer_factory.DrawAll();"
 * Em alguma janela: adicione "m_image_viewer_factory.DrawOpenButton();"
 *
 * DIALOGO DE ARQUIVO  (IFileOpenDialog)
 * ----------------------------------------
 * Usa a API COM moderna (Windows Vista+) em vez de GetOpenFileNameW.
 * Vantagens: visual nativo do Windows 10/11, suporte a caminhos longos,
 * múltiplos tipos de arquivo via SetFileTypes().
 *
 * CoInitializeEx / CoUninitialize são chamados apenas se necessário —
 * a classe verifica se COM já foi inicializado na thread.
 */
class ImageViewerFactory {
public:

    // =========================================================================
    // Formatos suportados (compile-time)
    // =========================================================================

    /** @brief Filtros exibidos no diálogo. Pares {nome, extensões}. */
    static constexpr std::array<std::pair<const wchar_t*, const wchar_t*>, 5>
    FILE_FILTERS = { {
        { L"Imagens",                  L"*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif" },
        { L"PNG (*.png)",              L"*.png"  },
        { L"JPEG (*.jpg, *.jpeg)",     L"*.jpg;*.jpeg" },
        { L"BMP (*.bmp)",              L"*.bmp"  },
        { L"TGA (*.tga)",              L"*.tga"  }
    } };

    // =========================================================================
    // Construtor / Destrutor
    // =========================================================================

    ImageViewerFactory()  noexcept;
    ~ImageViewerFactory() noexcept;

    // Não copiável — gerencia recursos COM e janelas com posse única
    ImageViewerFactory(const ImageViewerFactory&)            = delete;
    ImageViewerFactory& operator=(const ImageViewerFactory&) = delete;

    // Movível
    ImageViewerFactory(ImageViewerFactory&&)            = default;
    ImageViewerFactory& operator=(ImageViewerFactory&&) = default;

    // =========================================================================
    // API pública
    // =========================================================================

    /**
     * @brief Abre o explorador do Windows e cria uma nova janela para a
     *        imagem selecionada.
     *
     * Usa IFileOpenDialog (COM). Bloqueante até o usuário confirmar ou cancelar.
     * Se o usuário cancelar, nenhuma janela é criada.
     *
     * @return true se uma imagem foi selecionada e a janela criada.
     */
    bool OpenFileDialog();

    /**
     * @brief Cria uma nova janela de visualização para um arquivo já conhecido.
     *
     * Útil para abrir imagens programaticamente (ex.: drag-and-drop futuro).
     *
     * @param filepath  Caminho wide completo do arquivo.
     * @return          Ponteiro não-possuidor para a janela criada;
     *                  nullptr se o caminho estiver vazio.
     */
    ImageViewerWindow* OpenFile(std::wstring filepath);

    /**
     * @brief Desenha TODAS as janelas abertas e remove as fechadas.
     *
     * Chame uma vez por frame em MyWindows::CreateWindows().
     * A remoção de janelas fechadas ocorre no final do frame — seguro
     * chamar Draw() de todas as janelas antes de remover.
     */
    void DrawAll();

    /**
     * @brief Desenha um botão "Abrir Imagem..." que abre o diálogo ao clicar.
     *
     * Conveniente para embutir o botão em qualquer janela ImGui existente
     * (ex.: toolbar da janela WindowControls ou uma janela dedicada).
     */
    void DrawOpenButton();

    /**
     * @brief Fecha e remove todas as janelas de imagem abertas.
     */
    void CloseAll();

    /** @brief Número de janelas de imagem atualmente abertas. */
    [[nodiscard]] std::size_t GetOpenCount() const noexcept;

private:

    // =========================================================================
    // Estado interno
    // =========================================================================

    /// Janelas de imagem abertas — cada uma possui sua Image Vulkan
    std::vector<std::unique_ptr<ImageViewerWindow>> m_windows;

    int  m_next_id;    ///< Próximo ID a ser atribuído (começa em 1, incrementa)
    bool m_com_init;   ///< true se CoInitializeEx foi chamado por esta instância

    // =========================================================================
    // Helpers privados
    // =========================================================================

    /**
     * @brief Remove da lista as janelas com IsOpen() == false.
     *
     * Chamado no final de DrawAll() — após Draw() de todas as janelas,
     * para que nenhuma janela seja destruída enquanto está sendo desenhada.
     *
     * Usa erase-remove idiom com unique_ptr: as janelas removidas têm seus
     * destruidores chamados aqui, o que por sua vez chama Image::Unload()
     * e libera os recursos Vulkan.
     */
    void RemoveClosed();
};