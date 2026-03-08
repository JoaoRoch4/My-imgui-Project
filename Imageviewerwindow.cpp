/**
 * @file ImageViewerWindow.cpp
 * @brief Janela ImGui com pan, zoom, GIF e botão de nova imagem.
 *
 * PAN COM ARRASTAR (botão esquerdo)
 * ----------------------------------
 * O child region usa ImGuiWindowFlags_NoScrollWithMouse para desactivar
 * o scroll automático do ImGui com a roda. Tudo é gerido manualmente:
 *
 *   Drag esquerdo  → SetScrollX/Y(GetScrollX/Y() - MouseDelta.x/y)
 *                    MouseDelta é o deslocamento em píxeis desde o frame anterior
 *                    Subtraímos porque arrastar para a direita = ver mais à direita
 *
 *   Roda           → SetScrollY(GetScrollY() - MouseWheel * 32.0f)
 *   Shift + roda   → SetScrollX(GetScrollX() - MouseWheel * 32.0f)
 *   Ctrl  + roda   → zoom (consome MouseWheel)
 *
 * O cursor muda para ImGuiMouseCursor_ResizeAll durante o drag e
 * ImGuiMouseCursor_Hand em hover — feedback visual imediato.
 *
 * BOTÃO [+ Nova imagem]
 * ----------------------
 * O factory passa [this]{ OpenFileDialog(); } no construtor da janela.
 * A janela não conhece o factory — só conhece a callback.
 * Se a callback estiver vazia (std::function vazia), o botão não aparece.
 */

#include "pch.hpp"
#include "ImageViewerWindow.hpp"

// ============================================================================
// WideToUtf8
// ============================================================================

/**
 * @brief Converte wchar_t* para UTF-8 via WideCharToMultiByte.
 *
 * @param wstr  String wide terminada em nulo; nullptr devolve "".
 * @return      std::string codificada em UTF-8.
 */
std::string ImageViewerWindow::WideToUtf8(const wchar_t* wstr)
{
    if(!wstr) return "";

    // Primeira chamada: consulta quantos bytes são necessários (inclui NUL)
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);

    if(needed <= 1) return "";

    std::string result(static_cast<std::size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], needed, nullptr, nullptr);
    return result;
}

// ============================================================================
// HasGifExtension
// ============================================================================

/**
 * @brief Verifica se o caminho termina em ".gif" (case-insensitive).
 *
 * Converte os últimos 4 caracteres para lowercase e compara com L".gif".
 *
 * @param filepath  Caminho wide a verificar.
 * @return          true se a extensão for .gif
 */
bool ImageViewerWindow::HasGifExtension(const std::wstring& filepath) noexcept
{
    if(filepath.size() < 4) return false;

    std::wstring ext = filepath.substr(filepath.size() - 4);

    for(wchar_t& c : ext)
        c = static_cast<wchar_t>(towlower(c)); // lowercase para comparação

    return ext == L".gif";
}

// ============================================================================
// Construtor
// ============================================================================

/**
 * @brief Inicializa a janela, detecta o tipo e carrega para a VRAM.
 *
 * HasGifExtension() decide entre GifAnimation e Image.
 * O título é formatado como "filename.ext##id" — o sufixo "##id" é
 * invisível ao utilizador mas garante chaves ImGui únicas.
 *
 * @param id            ID único atribuído pelo factory.
 * @param filepath      Caminho wide completo do ficheiro.
 * @param open_callback Callback para [+ Nova imagem]; pode ser vazia.
 */
ImageViewerWindow::ImageViewerWindow(int id, std::wstring filepath,
                                     std::function<void()> open_callback)
    : m_id(id)
    , m_filepath(std::move(filepath))
    , m_is_gif(HasGifExtension(m_filepath))
    , m_open(true)
    , m_zoom(1.0f)
    , m_open_callback(std::move(open_callback))
{
    // ---- Título ImGui --------------------------------------------------
    // rfind encontra o último separador de directório para extrair só o nome
    const std::wstring::size_type sep = m_filepath.find_last_of(L"\\/");
    const std::wstring filename = (sep == std::wstring::npos)
        ? m_filepath
        : m_filepath.substr(sep + 1);

    // "filename.ext##42" — "##42" é chave interna (invisível na aba)
    m_title      = filename + L"##" + std::to_wstring(m_id);
    m_title_utf8 = WideToUtf8(m_title.c_str()); // converte uma vez no construtor

    // ---- Upload para VRAM ----------------------------------------------
    const std::string path_utf8 = WideToUtf8(m_filepath.c_str());

    if(m_is_gif)
      m_gif.LoadAsync(path_utf8.c_str());   // todos os frames → VkImage por frame
    else
        m_image.Load(path_utf8.c_str()); // textura única   → VkImage
}

// ============================================================================
// Helpers de consulta
// ============================================================================

bool ImageViewerWindow::IsLoaded() const noexcept
{
    return m_is_gif ? m_gif.IsLoaded() : m_image.IsLoaded();
}

/** @brief Largura em píxeis — delega para GIF ou Image. */
int ImageViewerWindow::GetContentWidth() const noexcept
{
    return m_is_gif ? m_gif.GetWidth() : m_image.GetWidth();
}

/** @brief Altura em píxeis — delega para GIF ou Image. */
int ImageViewerWindow::GetContentHeight() const noexcept
{
    return m_is_gif ? m_gif.GetHeight() : m_image.GetHeight();
}

/**
 * @brief ImTextureID do frame activo.
 *
 * GIF:      m_gif.GetCurrentID()  — muda a cada Update()
 * Estático: m_image.GetID()       — sempre o mesmo descriptor set
 */
ImTextureID ImageViewerWindow::GetCurrentTextureID() const noexcept
{
    return m_is_gif ? m_gif.GetCurrentID() : m_image.GetID();
}

// ============================================================================
// Draw
// ============================================================================

/**
 * @brief Desenha a janela completa. Chame uma vez por frame.
 *
 * Avança a animação GIF com DeltaTime antes de desenhar —
 * garante velocidade correcta independentemente do framerate.
 */
void ImageViewerWindow::Draw()
{
    // Tamanho inicial baseado nas dimensões do conteúdo, clampeado a 820×660
    const float init_w = static_cast<float>(std::min(GetContentWidth()  + 16, 820));
    const float init_h = static_cast<float>(std::min(GetContentHeight() + 80, 660));
    ImGui::SetNextWindowSize(ImVec2(init_w, init_h), ImGuiCond_Once);

    if(!ImGui::Begin(m_title_utf8.c_str(), &m_open))
    {
        ImGui::End();
        return;
    }

    // Polling do upload assíncrono (custo zero quando já está pronto)
    if(m_is_gif)
        m_gif.TickUpload();

    if(!IsLoaded())
    {
        if(m_is_gif && m_gif.IsFailed())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Falha ao carregar GIF:");
            ImGui::TextWrapped("%s", WideToUtf8(m_filepath.c_str()).c_str());
            if(ImGui::Button("Fechar")) m_open = false;
            ImGui::End();
            return;
        }

        if(m_is_gif && m_gif.IsDecoding())
        {
            const float t = static_cast<float>(ImGui::GetTime());
            ImGui::Text("A carregar GIF");
            constexpr int   DOTS  = 8;
            constexpr float STEP  = 6.2831853f / DOTS;
            for(int d = 0; d < DOTS; ++d)
            {
                const float alpha = std::max(0.1f, 1.0f - std::fmod(t * 4.0f - d * STEP * 0.5f, 6.2831853f) / 6.2831853f);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, alpha), ".");
            }
            ImGui::End();
            return;
        }

        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Falha ao carregar:");
        ImGui::TextWrapped("%s", WideToUtf8(m_filepath.c_str()).c_str());
        if(ImGui::Button("Fechar")) m_open = false;
        ImGui::End();
        return;
    }

    if(m_is_gif)
        m_gif.Update(ImGui::GetIO().DeltaTime * 1000.0f);

    DrawToolbar();
    ImGui::Separator();
    DrawImage(ImGui::GetContentRegionAvail());

    ImGui::End();

    // ---- Erro de carregamento ------------------------------------------
    if(!IsLoaded())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Falha ao carregar:");
        ImGui::TextWrapped("%s", WideToUtf8(m_filepath.c_str()).c_str());
        if(ImGui::Button("Fechar")) m_open = false;
        ImGui::End();
        return;
    }

    // ---- Avança animação GIF -------------------------------------------
    // DeltaTime em segundos → converte para ms para o temporizador
    // ---- Polling do upload assíncrono (GIF) --------------------------------
    // TickUpload() só actua quando o decode terminou (ReadyToUpload).
    // Na maioria dos frames é uma leitura atómica + return false — custo zero.
    if(m_is_gif)
        m_gif.TickUpload();

    // ---- Estado de carregamento --------------------------------------------
    if(!IsLoaded())
    {
        if(m_is_gif && m_gif.IsFailed())
        {
            // Decode ou upload falhou — mostra erro
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Falha ao carregar GIF:");
            ImGui::TextWrapped("%s", WideToUtf8(m_filepath.c_str()).c_str());
            if(ImGui::Button("Fechar")) m_open = false;
            ImGui::End();
            return;
        }

        if(m_is_gif && m_gif.IsDecoding())
        {
            // Animação de espera enquanto a thread decodifica
            // ImGui::GetTime() oscila entre 0 e 2π — dá rotação suave
            const float t = static_cast<float>(ImGui::GetTime());
            ImGui::Text("A carregar GIF");

            // Spinner simples: 8 pontos com brilho rotativo
            // Cada ponto tem alpha proporcional à sua distância do "ponteiro"
            constexpr int   DOTS  = 8;
            constexpr float STEP  = 6.2831853f / DOTS; // 2π / 8
            for(int d = 0; d < DOTS; ++d)
            {
                // Fracção de brilho: 1.0 no ponto activo, decresce nos outros
                const float alpha = std::max(0.1f, 1.0f - std::fmod(t * 4.0f - d * STEP * 0.5f, 6.2831853f) / 6.2831853f);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, alpha), ".");
            }

            ImGui::End();
            return; // não renderiza imagem ainda
        }

        // Imagem estática que falhou ao carregar
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Falha ao carregar:");
        ImGui::TextWrapped("%s", WideToUtf8(m_filepath.c_str()).c_str());
        if(ImGui::Button("Fechar")) m_open = false;
        ImGui::End();
        return;
    }

   
}

// ============================================================================
// DrawToolbar
// ============================================================================

/**
 * @brief Toolbar: Fit, 1:1, +, -, info, [+ Nova imagem], controlos GIF.
 *
 * [+ Nova imagem] só aparece se m_open_callback não for vazia.
 * Ao clicar, invoca a callback — o factory abre o diálogo e cria
 * uma nova janela independentemente desta.
 */
void ImageViewerWindow::DrawToolbar()
{
    // ---- Fit ------------------------------------------------------------
    if(ImGui::Button("Fit"))
    {
        // Reserva 30px para o separator — evita barra de scroll vertical desnecessária
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        FitToWindow(ImVec2(avail.x, avail.y - 30.0f));
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Ajustar a janela");

    ImGui::SameLine();

    // ---- 1:1 ------------------------------------------------------------
    if(ImGui::Button("1:1")) m_zoom = 1.0f;
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("100%% — pixel perfeito");

    ImGui::SameLine();

    // ---- + / - ----------------------------------------------------------
    if(ImGui::Button("+"))
        m_zoom = std::min(m_zoom + ZOOM_STEP, ZOOM_MAX);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Aumentar zoom");

    ImGui::SameLine();

    if(ImGui::Button("-"))
        m_zoom = std::max(m_zoom - ZOOM_STEP, ZOOM_MIN);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Diminuir zoom");

    ImGui::SameLine();

    // ---- Info dimensões + zoom ------------------------------------------
    ImGui::TextDisabled("%dx%d  (%.0f%%)",
        GetContentWidth(), GetContentHeight(), m_zoom * 100.0f);

    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", WideToUtf8(m_filepath.c_str()).c_str());

    // ---- Botão nova imagem ---------------------------------------------
    // Só exibido se o factory forneceu uma callback válida no construtor
    if(m_open_callback)
    {
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); // divisor visual vertical
        ImGui::SameLine();

        if(ImGui::Button("+ Nova imagem"))
            m_open_callback(); // invoca OpenFileDialog() no factory

        if(ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Abre uma nova janela de visualizacao.\n"
                "Suporta seleccao multipla (Ctrl/Shift + Clique).");
    }

    // ---- Controlos GIF (só para GIFs com mais de 1 frame) --------------
    if(!m_is_gif || !m_gif.IsAnimated())
        return;

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Botão play/pause
    const bool paused = m_gif.IsPaused();
    if(ImGui::Button(paused ? "Play" : "Pause"))
        m_gif.SetPaused(!paused);
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip(paused ? "Retomar animacao" : "Pausar animacao");

    ImGui::SameLine();

    // Botão reiniciar
    if(ImGui::Button("Reset"))
        m_gif.Reset();
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Reiniciar no frame 0");

    ImGui::SameLine();

    // Contador de frames (1-based para exibição)
    ImGui::TextDisabled("frame %d/%d",
        m_gif.GetCurrentFrameIndex() + 1,
        m_gif.GetFrameCount());
}

// ============================================================================
// DrawImage
// ============================================================================

/**
 * @brief Child scrollável com pan (arrastar), zoom (Ctrl+scroll) e tooltip.
 *
 * PAN — COMO FUNCIONA:
 *   NoScrollWithMouse desactiva o scroll automático do ImGui.
 *   IsMouseDragging(Left, 0.0f) detecta qualquer movimento com botão esquerdo.
 *   io.MouseDelta é o deslocamento do rato em píxeis desde o frame anterior.
 *
 *   SetScrollX(GetScrollX() - delta.x):
 *     delta.x > 0 (rato foi para a direita) → scroll vai para a direita → imagem move para a esquerda ✓
 *     delta.x < 0 (rato foi para a esquerda) → scroll vai para a esquerda → imagem move para a direita ✓
 *
 * SCROLL MANUAL:
 *   Roda para cima   → scroll sobe   (MouseWheel > 0 → subtraímos → ScrollY diminui → sobe)
 *   Roda para baixo  → scroll desce
 *   Shift + roda     → scroll horizontal
 *   Ctrl  + roda     → zoom (consome MouseWheel para não fazer scroll)
 *
 * @param available  Área disponível na janela após toolbar + separator.
 */
void ImageViewerWindow::DrawImage(ImVec2 available)
{
    // ID único do child para evitar colisões quando múltiplos viewers estão abertos
    const std::string child_id = "##img_" + std::to_string(m_id);

    // NoScrollWithMouse: desactiva scroll automático — gerimos manualmente
    if(!ImGui::BeginChild(child_id.c_str(), available, false,
        ImGuiWindowFlags_HorizontalScrollbar |
        ImGuiWindowFlags_NoMove              |
        ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::EndChild(); // EndChild() obrigatório mesmo quando invisível
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Hovered: verifica se o cursor está sobre esta child window
    const bool hovered = ImGui::IsWindowHovered(
        ImGuiHoveredFlags_RootAndChildWindows);

    // ---- Ctrl+Scroll = zoom --------------------------------------------
    if(hovered && io.KeyCtrl && io.MouseWheel != 0.0f)
    {
        m_zoom        = std::clamp(m_zoom + ZOOM_STEP * io.MouseWheel, ZOOM_MIN, ZOOM_MAX);
        io.MouseWheel = 0.0f; // consome — evita scroll simultâneo ao zoom
    }

    // ---- Scroll manual com roda (sem Ctrl) -----------------------------
    if(hovered && !io.KeyCtrl && io.MouseWheel != 0.0f)
    {
        if(io.KeyShift)
            // Shift + roda = scroll horizontal
            ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheel * 32.0f);
        else
            // Roda simples = scroll vertical
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * 32.0f);

        io.MouseWheel = 0.0f; // consome
    }

    // ---- Pan com arrastar (botão esquerdo) -----------------------------
    // IsMouseDragging(Left, 0.0f): threshold 0.0f = qualquer movimento conta
    if(hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
    {
        // Subtrai o delta: arrastar para a direita → ScrollX aumenta → imagem move para a esquerda
        ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseDelta.x);
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);

        // Cursor "mover" durante o drag — feedback visual claro
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    else if(hovered)
    {
        // Cursor "mão" em hover — indica que pode arrastar
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // ---- Dimensões com zoom -------------------------------------------
    const float img_w = static_cast<float>(GetContentWidth())  * m_zoom;
    const float img_h = static_cast<float>(GetContentHeight()) * m_zoom;

    // ---- Centraliza quando menor que a área disponível ----------------
    // GetContentRegionAvail() dentro do child desconta as barras de scroll
    const ImVec2 region   = ImGui::GetContentRegionAvail();
    const float  offset_x = (img_w < region.x) ? (region.x - img_w) * 0.5f : 0.0f;
    const float  offset_y = (img_h < region.y) ? (region.y - img_h) * 0.5f : 0.0f;

    if(offset_x > 0.0f || offset_y > 0.0f)
    {
        // Avança o cursor de layout para centrar a imagem
        ImGui::SetCursorPos(ImVec2(
            ImGui::GetCursorPosX() + offset_x,
            ImGui::GetCursorPosY() + offset_y));
    }

    // ---- Renderiza a imagem / frame GIF activo -------------------------
    // GIF: GetCurrentTextureID() muda automaticamente depois de Update()
    ImGui::Image(GetCurrentTextureID(), ImVec2(img_w, img_h),
        ImVec2(0.0f, 0.0f),  // uv0: canto superior esquerdo
        ImVec2(1.0f, 1.0f)); // uv1: canto inferior direito

    // ---- Tooltip com coordenadas do pixel -----------------------------
    // Só quando não está a arrastar — durante drag o cursor é ResizeAll
    if(ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const ImVec2 mouse   = ImGui::GetMousePos();
        const ImVec2 img_pos = ImGui::GetItemRectMin(); // canto sup-esq da imagem na tela

        // UV [0,1] do ponto sob o cursor → coordenadas do pixel original
        const float uv_x = (mouse.x - img_pos.x) / img_w;
        const float uv_y = (mouse.y - img_pos.y) / img_h;

        const int px_x = static_cast<int>(uv_x * static_cast<float>(GetContentWidth()));
        const int px_y = static_cast<int>(uv_y * static_cast<float>(GetContentHeight()));

        if(m_is_gif && m_gif.IsAnimated())
        {
            ImGui::SetTooltip("Pixel: %d, %d\nZoom: %.0f%%\nFrame: %d/%d",
                px_x, px_y, m_zoom * 100.0f,
                m_gif.GetCurrentFrameIndex() + 1,
                m_gif.GetFrameCount());
        }
        else
        {
            ImGui::SetTooltip("Pixel: %d, %d\nZoom: %.0f%%",
                px_x, px_y, m_zoom * 100.0f);
        }
    }

    ImGui::EndChild(); // SEMPRE após BeginChild, independente do retorno
}

// ============================================================================
// FitToWindow
// ============================================================================

/**
 * @brief Recalcula m_zoom para a imagem caber na área disponível.
 *
 * Usa a escala mínima entre X e Y para preservar o aspect ratio.
 * Resultado é clampeado entre ZOOM_MIN e ZOOM_MAX.
 *
 * @param available  Área alvo em píxeis de tela.
 */
void ImageViewerWindow::FitToWindow(ImVec2 available)
{
    if(GetContentWidth()  == 0 ||
       GetContentHeight() == 0 ||
       available.x <= 0.0f    ||
       available.y <= 0.0f)
        return; // evita divisão por zero

    // Escala que faz a imagem caber em cada eixo separadamente
    const float scale_x = available.x / static_cast<float>(GetContentWidth());
    const float scale_y = available.y / static_cast<float>(GetContentHeight());

    // Usa a menor para garantir que a imagem cabe nos dois eixos (preserva aspect ratio)
    m_zoom = std::clamp(std::min(scale_x, scale_y), ZOOM_MIN, ZOOM_MAX);
}