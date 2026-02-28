/**
 * @file FontScale.cpp
 * @brief Zoom de fonte global no ImGui via Ctrl + Scroll do mouse.
 *
 * COMO O ZOOM DE FONTE FUNCIONA NO IMGUI MODERNO
 * -----------------------------------------------
 * O ImGui tem dois parâmetros que controlam o tamanho visual do texto:
 *
 *  ImGuiStyle::FontSizeBase   (float, pixels)
 *    → O tamanho "base" da fonte.  O ImGui multiplica este valor por
 *      FontScaleMain e FontScaleDpi para obter o tamanho final em cada frame.
 *    → Alterar FontSizeBase em runtime NÃO reconstrói o atlas de texturas.
 *      O ImGui re-rasteriza a fonte usando o FontLoader (FreeType ou stb_truetype)
 *      dentro do próprio NewFrame(), de forma transparente.
 *    → É a API recomendada para zoom dinâmico desde que o backend suporte
 *      ImGuiBackendFlags_RendererHasTextures (o backend Vulkan/ImGui suporta).
 *
 *  ImGuiStyle::FontScaleMain  (float, multiplicador)
 *    → Fator de escala aplicado sobre FontSizeBase.  1.0 = sem escala.
 *    → Menos indicado para zoom do usuário porque afeta cálculos de layout.
 *
 * POR QUE NÃO RECARREGAR O ATLAS?
 * ---------------------------------
 * A abordagem antiga era:
 *   io.Fonts->Clear() → AddFontFromFileTTF(novo_tamanho) → Build() → upload GPU
 * Isso exige sincronização com o frame Vulkan e é frágil.
 * Com FontSizeBase + FontLoader ativo, o ImGui cuida de tudo internamente.
 *
 * DETECÇÃO DO CTRL+SCROLL
 * -----------------------
 * SDL3 entrega scroll como SDL_EVENT_MOUSE_WHEEL com campo y (float):
 *   y > 0 → scroll para cima    → aumenta fonte
 *   y < 0 → scroll para baixo   → diminui fonte
 *
 * O estado do Ctrl é lido via SDL_GetModState() no momento do evento:
 *   SDL_KMOD_CTRL  cobre tanto Ctrl esquerdo quanto direito.
 *
 * NÃO usamos io.KeyCtrl do ImGui porque ProcessEvent() pode ser chamado
 * antes de ImGui_ImplSDL3_ProcessEvent() processar o frame corrente.
 *
 * INTEGRAÇÃO EM main.cpp
 * ----------------------
 * Dentro do loop SDL_PollEvent, adicione UMA linha:
 * @code
 *   while(SDL_PollEvent(&event)) {
 *       FontScale::ProcessEvent(event);        // ← adicione aqui
 *       ImGui_ImplSDL3_ProcessEvent(&event);
 *       // ... resto do tratamento de eventos
 *   }
 * @endcode
 *
 * Opcionalmente, registre um comando no Console para resetar:
 * @code
 *   con->RegisterCommand(L"FONTRESET",
 *       L"Restaura o tamanho original da fonte (Ctrl+Scroll para zoom).",
 *       []() { FontScale::ResetToDefault(); });
 * @endcode
 */

#include "pch.hpp"     // Windows.h, SDL3, imgui.h, etc.
#include "FontScale.hpp"

 // ============================================================================
 // Inicialização dos membros estáticos
 // ============================================================================

float FontScale::s_default_size = 13.0f; ///< Será sobrescrito na primeira chamada
bool  FontScale::s_default_set = false; ///< false até ser capturado uma vez

// ============================================================================
// ProcessEvent
// ============================================================================

/**
 * @brief Processa um SDL_Event e aplica zoom de fonte se Ctrl+Scroll for detectado.
 *
 * FLUXO INTERNO:
 *  1. Filtra: apenas SDL_EVENT_MOUSE_WHEEL interessa.
 *  2. Filtra: SDL_GetModState() deve conter SDL_KMOD_CTRL.
 *  3. Na primeira chamada captura ImGuiStyle::FontSizeBase como default.
 *  4. Calcula o novo tamanho: atual ± STEP * |delta_y|.
 *  5. Aplica com SetSize() que valida os limites e escreve em FontSizeBase.
 *
 * Por que multiplicar STEP por |delta_y| e não só pelo sinal?
 *   Trackpads e mouses de alta resolução entregam deltas fracionários (ex.: 0.25).
 *   Acumular o valor real — e não só +1/-1 — torna o zoom proporcional à
 *   velocidade de rolagem, assim como zoom em browsers e editores de texto.
 *
 * @param event  Evento SDL3 a inspecionar (qualquer tipo; filtramos internamente).
 * @return       true se a fonte foi alterada nesta chamada.
 */
bool FontScale::ProcessEvent(const SDL_Event& event) {
    // Filtra: só interessa scroll do mouse
    if(event.type != SDL_EVENT_MOUSE_WHEEL)
        return false; // Ignora qualquer outro tipo de evento

    // Verifica se Ctrl está pressionado agora via SDL_GetModState()
    // SDL_KMOD_CTRL = SDL_KMOD_LCTRL | SDL_KMOD_RCTRL (ambos os lados)
    const SDL_Keymod mod = SDL_GetModState();
    if(!(mod & SDL_KMOD_CTRL))
        return false; // Scroll sem Ctrl — deixa o ImGui tratar normalmente

    // Captura o tamanho default UMA vez, na primeira interação do usuário
    // (neste ponto o contexto ImGui já está ativo, FontSizeBase já foi definido)
    ImGuiStyle& style = ImGui::GetStyle(); // referência ao estilo global único
    if(!s_default_set) {
        s_default_size = style.FontSizeBase; // salva o valor inicial para Reset
        s_default_set = true;
    }

    // event.wheel.y: positivo = scroll para cima, negativo = para baixo
    // Usamos o valor float diretamente para suportar trackpads de alta resolução
    const float delta = event.wheel.y; // float em SDL3 (era int em SDL2)

    if(delta == 0.0f)
        return false; // Scroll puramente horizontal com Ctrl — ignoramos

    // Calcula o novo tamanho: adiciona STEP por unidade de scroll
    const float current = style.FontSizeBase;           // tamanho atual em pixels
    const float proposed = current + (STEP * delta);     // proposta: ±1px por tick

    SetSize(proposed); // valida os limites e aplica

    return true; // evento foi consumido pelo zoom de fonte
}

// ============================================================================
// GetCurrentSize
// ============================================================================

/**
 * @brief Retorna o tamanho base atual da fonte em pixels.
 *
 * Lê diretamente de ImGuiStyle::FontSizeBase, que é a fonte verdade:
 * qualquer alteração feita por SetSize() ou pelo StyleEditor aparece aqui.
 *
 * @return Tamanho em pixels (ex.: 13.0f, 16.0f, 24.0f).
 */
float FontScale::GetCurrentSize() {
    return ImGui::GetStyle().FontSizeBase; // leitura direta do estilo ImGui
}

// ============================================================================
// SetSize
// ============================================================================

/**
 * @brief Define o tamanho base da fonte, respeitando SIZE_MIN e SIZE_MAX.
 *
 * Escreve em dois lugares sincronizados:
 *
 *  style.FontSizeBase
 *    → Lido pelo ImGui em NewFrame() para calcular o tamanho final.
 *    → O FontLoader re-rasteriza a fonte automaticamente quando este valor muda,
 *      desde que ImGuiBackendFlags_RendererHasTextures esteja ativo (Vulkan ✓).
 *
 *  style._NextFrameFontSizeBase
 *    → Campo interno que o ImGui usa para aplicar a mudança no PRÓXIMO frame,
 *      evitando inconsistências caso SetSize() seja chamado DURANTE um frame.
 *    → O StyleEditor do projeto também escreve neste campo (veja StyleEditor.cpp:
 *      `if(ImGui::DragFloat("FontSizeBase", ...) style._NextFrameFontSizeBase = ...`).
 *
 * @param px  Tamanho desejado em pixels. Será clampado a [SIZE_MIN, SIZE_MAX].
 */
void FontScale::SetSize(float px) {
    // Clamp: garante que a fonte nunca sai do intervalo seguro
    // SIZE_MIN=8 evita fonte invisível; SIZE_MAX=48 evita UI inutilizável
    const float clamped = (px < FONT_SIZE_MIN) ? FONT_SIZE_MIN
        : (px > FONT_SIZE_MAX) ? FONT_SIZE_MAX
        : px;

    ImGuiStyle& style = ImGui::GetStyle(); // único estilo global do contexto

    style.FontSizeBase = clamped; // valor lido por NewFrame() imediatamente
    style._NextFrameFontSizeBase = clamped; // garante sincronismo com o próximo frame
    //      ^^ campo interno (prefixo _) documentado em imgui.h como "for internal use"
    //         mas é exatamente o que o StyleEditor usa, então é a forma correta
}

// ============================================================================
// ResetToDefault
// ============================================================================

/**
 * @brief Restaura o tamanho que estava ativo na primeira chamada a ProcessEvent.
 *
 * Se ProcessEvent ainda não tiver sido chamado (usuário não usou o zoom),
 * o s_default_size ainda é 13.0f (valor do membro estático).
 * Nesse caso, o reset é para 13px — tamanho razoável em qualquer setup.
 *
 * Útil como comando no Console:
 * @code
 *   con->RegisterCommand(L"FONTRESET", L"Restaura tamanho original da fonte.",
 *       []() { FontScale::ResetToDefault(); });
 * @endcode
 */
void FontScale::ResetToDefault() {
    SetSize(s_default_size); // s_default_size = valor capturado no primeiro zoom
}
