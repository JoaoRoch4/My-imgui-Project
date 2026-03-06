/**
 * @file ImageViewerWindow.cpp
 * @brief Implementação de uma janela ImGui de visualização de imagem.
 *
 * FLUXO DE RENDERIZAÇÃO POR FRAME
 * ---------------------------------
 *  1. ImGui::Begin()           — abre a janela flutuante redimensionável
 *  2. DrawToolbar()            — botões Fit / 1:1 / zoom% / dimensões
 *  3. DrawImage(available)     — child region com scroll + zoom
 *     3a. IsWindowHovered()    — detecta se o mouse está na área da imagem
 *     3b. KeyCtrl + MouseWheel → ajusta m_zoom
 *     3c. PushFont(…, size)    — aplica zoom via tamanho de fonte (não usado aqui)
 *     3d. ImGui::Image(id, sz) — exibe a textura Vulkan
 *  4. ImGui::End()             — fecha a janela
 *
 * SCROLL DE ZOOM (Ctrl + Scroll)
 * --------------------------------
 * Ao contrário do console (que modifica a escala de fonte), aqui o zoom
 * é puramente geométrico: multiplica as dimensões da imagem por m_zoom.
 * io.MouseWheel é consumido (= 0.0f) apenas se o cursor está sobre esta
 * janela, para não interferir com outras janelas.
 *
 * CHILD REGION COM SCROLL
 * -------------------------
 * A imagem é desenhada dentro de um BeginChild("##img_N") com
 * ImGuiWindowFlags_HorizontalScrollbar. Quando o zoom faz a imagem
 * ultrapassar a área disponível, as barras de scroll aparecem automaticamente.
 *
 * ID ÚNICO
 * ---------
 * O título ImGui é formatado como "filename.png##42" onde 42 é o m_id.
 * O sufixo "##" faz o ImGui usar apenas o número como chave interna,
 * permitindo títulos idênticos (mesma imagem aberta duas vezes) sem conflito.
 */

#include "pch.hpp"
#include "ImageViewerWindow.hpp"

// ============================================================================
// WideToUtf8 (helper local)
// ============================================================================

/**
 * @brief Converte wchar_t* para std::string UTF-8 via WideCharToMultiByte.
 *
 * @param wstr  String wide terminada em nulo; nullptr retorna "".
 * @return      std::string codificada em UTF-8.
 */
std::string ImageViewerWindow::WideToUtf8(const wchar_t* wstr)
{
    if(!wstr) return ""; // guarda contra nullptr

    const int needed = WideCharToMultiByte(
        CP_UTF8, 0,     // página de código alvo
        wstr, -1,       // fonte wide terminada em NUL
        nullptr, 0,     // primeira chamada: consulta bytes necessários
        nullptr, nullptr);

    if(needed <= 1) return ""; // string vazia ou erro

    std::string result(static_cast<std::size_t>(needed - 1), '\0'); // sem NUL
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], needed, nullptr, nullptr);
    return result;
}

// ============================================================================
// Construtor
// ============================================================================

/**
 * @brief Inicializa a janela, carrega a imagem e monta o título ImGui.
 *
 * TÍTULO FORMAT: "filename.ext##id"
 *   - "filename.ext" = parte visível na aba/título da janela
 *   - "##id"         = chave interna do ImGui (invisível ao usuário)
 *
 * A imagem é carregada imediatamente no construtor. Se falhar,
 * IsLoaded() retorna false e Draw() exibe uma mensagem de erro
 * no lugar da imagem.
 *
 * @param id        ID único atribuído pelo ImageViewerFactory.
 * @param filepath  Caminho completo da imagem (wide-char).
 */
ImageViewerWindow::ImageViewerWindow(int id, std::wstring filepath)
    : m_id(id)               // ID único para o sufixo ImGui "##N"
    , m_filepath(std::move(filepath)) // caminho completo armazenado para exibição
    , m_open(true)           // começa aberta
    , m_zoom(1.0f)           // zoom pixel-perfeito na abertura
{
    // ---- Extrai apenas o nome do arquivo para o título -------------------
    // rfind(L'\\') ou rfind(L'/') localiza o último separador de diretório.
    // npos indica raiz sem diretório → usa o caminho inteiro como título.
    const std::wstring::size_type sep = m_filepath.find_last_of(L"\\/");
    const std::wstring filename = (sep == std::wstring::npos)
        ? m_filepath                         // sem diretório
        : m_filepath.substr(sep + 1);        // apenas "filename.ext"

    // Monta "filename.ext##42" — o "##42" torna a chave ImGui única
    m_title  = filename + L"##" + std::to_wstring(m_id);
    m_title_utf8 = WideToUtf8(m_title.c_str()); // converte uma vez só

    // ---- Carrega a imagem Vulkan -----------------------------------------
    // Image::Load() executa o upload para a VRAM via command buffer.
    // Converte o caminho wide para UTF-8 — stb_image exige char*.
    const std::string path_utf8 = WideToUtf8(m_filepath.c_str());
    m_image.Load(path_utf8.c_str());
}

// ============================================================================
// Draw
// ============================================================================

/**
 * @brief Desenha a janela ImGui desta imagem. Chame uma vez por frame.
 *
 * Se a imagem não foi carregada (Load() falhou), exibe mensagem de erro
 * com o caminho do arquivo problemático e um botão para fechar.
 *
 * A janela é livremente redimensionável. O título usa m_title_utf8 já
 * convertido no construtor — sem conversão por frame.
 */
void ImageViewerWindow::Draw()
{
    // Tamanho inicial: baseado nas dimensões da imagem, clampeado a 800×600.
    // ImGuiCond_Once: aplica apenas na primeira vez que a janela aparece.
    const float init_w = static_cast<float>(
        std::min(m_image.GetWidth()  + 16, 820));  // margem de 16px
    const float init_h = static_cast<float>(
        std::min(m_image.GetHeight() + 80, 660));  // margem para toolbar

    ImGui::SetNextWindowSize(ImVec2(init_w, init_h), ImGuiCond_Once);

    // m_open é passado por ponteiro — o X da janela ImGui o zera.
    // Quando m_open == false, IsOpen() retorna false e o factory a remove.
    if(!ImGui::Begin(m_title_utf8.c_str(), &m_open))
    {
        ImGui::End(); // End() obrigatório mesmo quando colapsada
        return;
    }

    // ---- Erro de carregamento -------------------------------------------
    if(!m_image.IsLoaded())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
            "Falha ao carregar a imagem:");
        ImGui::TextWrapped("%s", WideToUtf8(m_filepath.c_str()).c_str());

        if(ImGui::Button("Fechar"))
            m_open = false; // marca para remoção pelo factory

        ImGui::End();
        return;
    }

    // ---- Toolbar (Fit, 1:1, zoom%, dimensões) ---------------------------
    DrawToolbar();

    ImGui::Separator();

    // ---- Área disponível após toolbar + separator -----------------------
    const ImVec2 available = ImGui::GetContentRegionAvail();

    // ---- Imagem com scroll e zoom ---------------------------------------
    DrawImage(available);

    ImGui::End(); // SEMPRE pareado com Begin()
}

// ============================================================================
// DrawToolbar
// ============================================================================

/**
 * @brief Toolbar interna com botões de zoom e informações da imagem.
 *
 * Botões:
 *  [Fit]    — recalcula m_zoom para a imagem caber na área disponível
 *  [1:1]    — zoom = 1.0 (pixel perfeito)
 *  [+] [-]  — incrementa / decrementa zoom em ZOOM_STEP
 *  "NNNxMMM (ZZZ%)" — dimensões originais e zoom atual (texto não interativo)
 */
void ImageViewerWindow::DrawToolbar()
{
    // ---- Botão Fit -------------------------------------------------------
    // Calcula a área disponível AGORA (antes de desenhar a imagem).
    // GetContentRegionAvail() retorna o espaço restante após a toolbar.
    // Subtraímos uma estimativa da altura da toolbar (22px) para o fit
    // ser preciso na primeira chamada.
    if(ImGui::Button("Fit"))
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        FitToWindow(ImVec2(avail.x, avail.y - 30.0f)); // reserva altura do separator
    }
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("Ajustar à janela");

    ImGui::SameLine();

    // ---- Botão 1:1 -------------------------------------------------------
    if(ImGui::Button("1:1"))
        m_zoom = 1.0f; // pixel perfeito — sem escala
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("Zoom 100%% (pixel perfeito)");

    ImGui::SameLine();

    // ---- Botão + ---------------------------------------------------------
    if(ImGui::Button("+"))
    {
        m_zoom += ZOOM_STEP;
        if(m_zoom > ZOOM_MAX) m_zoom = ZOOM_MAX; // clamp no limite superior
    }
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("Aumentar zoom");

    ImGui::SameLine();

    // ---- Botão - ---------------------------------------------------------
    if(ImGui::Button("-"))
    {
        m_zoom -= ZOOM_STEP;
        if(m_zoom < ZOOM_MIN) m_zoom = ZOOM_MIN; // clamp no limite inferior
    }
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("Diminuir zoom");

    ImGui::SameLine();

    // ---- Informações (não interativas) -----------------------------------
    // TextDisabled usa ImGuiCol_TextDisabled (cinza) — visualmente secundário.
    ImGui::TextDisabled("%dx%d  (%.0f%%)",
        m_image.GetWidth(),
        m_image.GetHeight(),
        m_zoom * 100.0f); // ex.: "1920x1080  (75%)"

    ImGui::SameLine();

    // ---- Caminho completo como tooltip do texto de info -----------------
    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", WideToUtf8(m_filepath.c_str()).c_str());
}

// ============================================================================
// DrawImage
// ============================================================================

/**
 * @brief Desenha a imagem dentro de um child scrollável com zoom.
 *
 * CHILD REGION:
 *   BeginChild("##img_N", available, false, HorizontalScrollbar)
 *   Quando a imagem ampliada ultrapassa a área, o ImGui exibe as
 *   barras de scroll automaticamente.
 *
 * CTRL + SCROLL:
 *   io.MouseWheel acumulado pelo ImGui a partir dos eventos SDL do frame.
 *   Consumimos (= 0.0f) apenas quando o cursor está sobre esta child window
 *   para não interferir com scroll de outras janelas.
 *
 * CENTRALIZAÇÃO:
 *   Quando a imagem é MENOR que a área disponível, usamos SetCursorPos()
 *   para centralizá-la. Quando é maior, o scroll cuida do offset.
 *
 * @param available  Tamanho disponível na janela após toolbar + separator.
 */
void ImageViewerWindow::DrawImage(ImVec2 available)
{
    // ID único do child: "##img_N" onde N = m_id
    // Evita conflito se dois viewers estiverem abertos ao mesmo tempo.
    const std::string child_id = "##img_" + std::to_string(m_id);

    // Abre o child scrollável — false = sem borda interna
    if(ImGui::BeginChild(child_id.c_str(), available, false,
        ImGuiWindowFlags_HorizontalScrollbar |
        ImGuiWindowFlags_NoMove))              // deixa drag na janela pai
    {
        // ---- Ctrl+Scroll = zoom -------------------------------------------
        ImGuiIO& io = ImGui::GetIO(); // IO global — acesso ao MouseWheel

        // IsWindowHovered com RootAndChildWindows cobre esta child region
        const bool hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_RootAndChildWindows);

        if(hovered && io.KeyCtrl && io.MouseWheel != 0.0f)
        {
            // io.MouseWheel > 0 = scroll para cima = zoom in
            m_zoom += ZOOM_STEP * io.MouseWheel;

            // Clamp nos limites
            if(m_zoom < ZOOM_MIN) m_zoom = ZOOM_MIN;
            if(m_zoom > ZOOM_MAX) m_zoom = ZOOM_MAX;

            // Consume o wheel — evita que o child role enquanto dá zoom
            io.MouseWheel = 0.0f;
        }

        // ---- Calcula tamanho da imagem com zoom --------------------------
        const float img_w = static_cast<float>(m_image.GetWidth())  * m_zoom;
        const float img_h = static_cast<float>(m_image.GetHeight()) * m_zoom;

        // ---- Centraliza quando menor que a área disponível ---------------
        // GetContentRegionAvail() dentro do child já desconta as scrollbars.
        const ImVec2 region = ImGui::GetContentRegionAvail();

        // Offset horizontal: máximo de 0 (quando imagem > área = scroll ativo)
        const float offset_x = (img_w < region.x)
            ? (region.x - img_w) * 0.5f  // centraliza
            : 0.0f;                       // imagem maior — scroll ativo

        // Offset vertical: análogo ao horizontal
        const float offset_y = (img_h < region.y)
            ? (region.y - img_h) * 0.5f
            : 0.0f;

        // Aplica o offset de cursor se necessário
        if(offset_x > 0.0f || offset_y > 0.0f)
        {
            ImGui::SetCursorPos(ImVec2(
                ImGui::GetCursorPosX() + offset_x,
                ImGui::GetCursorPosY() + offset_y));
        }

        // ---- Desenha a imagem Vulkan -------------------------------------
        // Image::GetID() retorna o VkDescriptorSet como ImTextureID.
        // ImGui::Image(tex_id, size, uv0, uv1) — uv0/uv1 = imagem inteira.
        ImGui::Image(
            m_image.GetID(),                    // ImTextureID = VkDescriptorSet
            ImVec2(img_w, img_h),               // tamanho com zoom aplicado
            ImVec2(0.0f, 0.0f),                 // uv0: canto superior esquerdo
            ImVec2(1.0f, 1.0f));                // uv1: canto inferior direito

        // ---- Tooltip com informações ao passar o mouse na imagem --------
        if(ImGui::IsItemHovered())
        {
            // Calcula a posição do pixel sob o cursor
            const ImVec2 mouse     = ImGui::GetMousePos();
            const ImVec2 img_pos   = ImGui::GetItemRectMin();

            // Coordenadas UV [0,1] do ponto sob o cursor
            const float uv_x = (mouse.x - img_pos.x) / img_w;
            const float uv_y = (mouse.y - img_pos.y) / img_h;

            // Coordenadas do pixel original (antes do zoom)
            const int px_x = static_cast<int>(uv_x * static_cast<float>(m_image.GetWidth()));
            const int px_y = static_cast<int>(uv_y * static_cast<float>(m_image.GetHeight()));

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
 * @brief Recalcula m_zoom para a imagem caber dentro de @p available.
 *
 * Usa a escala mínima entre os dois eixos para preservar o aspect ratio.
 * Nunca amplia além de 1:1 (não aumenta imagens menores que a área).
 *
 * @param available  Área disponível em pixels de tela.
 */
void ImageViewerWindow::FitToWindow(ImVec2 available)
{
    if(m_image.GetWidth()  == 0 ||
       m_image.GetHeight() == 0 ||
       available.x <= 0.0f      ||
       available.y <= 0.0f)
    {
        return; // evita divisão por zero
    }

    // Escala que faz a imagem caber em cada eixo
    const float scale_x = available.x / static_cast<float>(m_image.GetWidth());
    const float scale_y = available.y / static_cast<float>(m_image.GetHeight());

    // Usa a menor escala para caber nos dois eixos simultaneamente
    const float fit_zoom = (scale_x < scale_y) ? scale_x : scale_y;

    // Clamp: não amplia além de ZOOM_MAX, não reduz abaixo de ZOOM_MIN
    m_zoom = (fit_zoom < ZOOM_MIN) ? ZOOM_MIN
           : (fit_zoom > ZOOM_MAX) ? ZOOM_MAX
           : fit_zoom;
}