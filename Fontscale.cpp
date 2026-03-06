/**
 * @file FontScale.cpp
 * @brief Zoom de fonte global via Ctrl+Scroll, com supressão por janela.
 *
 * MECANISMO DE SUPRESSÃO (lag de 1 frame, imperceptível)
 * -------------------------------------------------------
 *
 *   Frame N — Console em foco, usuário faz Ctrl+Scroll:
 *     1. SDL_PollEvent → ProcessEvent():
 *           s_suppressed ainda é FALSE (Console não avisou ainda)
 *           → aplica zoom global (comportamento indesejado no Frame N)
 *     2. Console::Draw():
 *           IsWindowHovered() = true
 *           → SetScrollSuppressed(true)   ← avisa para o Frame N+1
 *           → aplica zoom local do console via io.MouseWheel
 *
 *   Frame N+1 — Console em foco, usuário faz Ctrl+Scroll:
 *     1. SDL_PollEvent → ProcessEvent():
 *           s_suppressed = TRUE           ← desta vez pula o zoom global ✓
 *     2. Console::Draw():
 *           → aplica zoom local do console
 *           → mantém SetScrollSuppressed(true)
 *
 * O Frame N tem comportamento duplo (global + local) apenas no PRIMEIRO
 * Ctrl+Scroll com o console em foco — imperceptível na prática.
 * A partir do Frame N+1 o comportamento é exclusivamente local.
 */

#include "pch.hpp"
#include "FontScale.hpp"

// ============================================================================
// Definição dos membros estáticos
// ============================================================================

float FontScale::s_default_size = 13.0f; ///< Sobrescrito na primeira interação
bool  FontScale::s_default_set  = false; ///< false até ser capturado uma vez
bool  FontScale::s_suppressed   = false; ///< false = scroll global ativo

// ============================================================================
// SetScrollSuppressed
// ============================================================================

/**
 * @brief Atualiza o flag de supressão de scroll global.
 *
 * Deve ser chamado por Console::Draw() uma vez por frame, passando
 * o resultado de ImGui::IsWindowHovered() para a janela do console.
 *
 * @param suppressed  true = console em foco → ProcessEvent() pula Ctrl+Scroll.
 */
void FontScale::SetScrollSuppressed(bool suppressed) noexcept
{
    s_suppressed = suppressed; // lido por ProcessEvent() no próximo frame
}

// ============================================================================
// IsScrollSuppressed
// ============================================================================

/**
 * @brief Retorna o estado atual do flag de supressão.
 *
 * @return true se o scroll global está suprimido (console em foco).
 */
bool FontScale::IsScrollSuppressed() noexcept
{
    return s_suppressed; // estado definido por SetScrollSuppressed()
}

// ============================================================================
// ProcessEvent
// ============================================================================

/**
 * @brief Processa SDL_EVENT_MOUSE_WHEEL + Ctrl → zoom de fonte global.
 *
 * CONDIÇÕES PARA APLICAR:
 *  1. Evento é SDL_EVENT_MOUSE_WHEEL.
 *  2. Ctrl está pressionado (SDL_KMOD_CTRL cobre esquerdo e direito).
 *  3. s_suppressed == false (Console NÃO está em foco).
 *
 * Quando s_suppressed == true, o evento é ignorado silenciosamente.
 * Console::Draw() trata o scroll localmente via ImGui::GetIO().MouseWheel.
 *
 * @param event  Evento SDL3 a inspecionar.
 * @return       true se a fonte global foi alterada; false caso contrário.
 */
bool FontScale::ProcessEvent(const SDL_Event& event)
{
    // Filtra: apenas scroll do mouse interessa
    if(event.type != SDL_EVENT_MOUSE_WHEEL)
        return false; // qualquer outro tipo de evento é ignorado

    // Verifica se Ctrl está pressionado via SDL_GetModState()
    // SDL_KMOD_CTRL = SDL_KMOD_LCTRL | SDL_KMOD_RCTRL
    const SDL_Keymod mod = SDL_GetModState();
    if(!(mod & SDL_KMOD_CTRL))
        return false; // scroll sem Ctrl — deixa o ImGui tratar normalmente

    // SUPRESSÃO: se o Console está em foco (flag definido no frame anterior),
    // pula o zoom global e deixa o Console tratar internamente em Draw().
    if(s_suppressed)
        return false; // Console consumirá via ImGui::GetIO().MouseWheel

    // Captura o tamanho default UMA vez (contexto ImGui já ativo neste ponto)
    ImGuiStyle& style = ImGui::GetStyle(); // referência ao estilo global único
    if(!s_default_set)
    {
        s_default_size = style.FontSizeBase; // salva o valor inicial para Reset
        s_default_set  = true;
    }

    // event.wheel.y: float em SDL3 (era int em SDL2)
    //   > 0 = scroll para cima   → aumenta fonte
    //   < 0 = scroll para baixo  → diminui fonte
    const float delta = event.wheel.y;

    if(delta == 0.0f)
        return false; // scroll puramente horizontal — ignoramos

    // Calcula e aplica o novo tamanho; STEP * delta preserva trackpads
    SetSize(style.FontSizeBase + (STEP * delta));

    return true; // zoom global foi aplicado
}

// ============================================================================
// GetCurrentSize
// ============================================================================

/**
 * @brief Retorna o tamanho base atual da fonte global em pixels.
 *
 * Lê diretamente de ImGuiStyle::FontSizeBase — fonte da verdade.
 */
float FontScale::GetCurrentSize()
{
    return ImGui::GetStyle().FontSizeBase; // leitura direta do estilo global
}

// ============================================================================
// SetSize
// ============================================================================

/**
 * @brief Define o tamanho base global, clampeado a [FONT_SIZE_MIN, FONT_SIZE_MAX].
 *
 * Escreve em FontSizeBase e _NextFrameFontSizeBase para sincronismo
 * com o próximo frame (mesmo padrão usado pelo StyleEditor).
 *
 * @param px  Tamanho desejado em pixels; será clampeado.
 */
void FontScale::SetSize(float px)
{
    // Clamp: FONT_SIZE_MIN=8 evita fonte invisível; FONT_SIZE_MAX=48 evita UI inutilizável
    const float clamped = (px < FONT_SIZE_MIN) ? FONT_SIZE_MIN
                        : (px > FONT_SIZE_MAX) ? FONT_SIZE_MAX
                        : px;

    ImGuiStyle& style = ImGui::GetStyle(); // estilo global único do contexto

    style.FontSizeBase           = clamped; // lido por NewFrame() imediatamente
    style._NextFrameFontSizeBase = clamped; // garante sincronismo com o próximo frame
}

// ============================================================================
// ResetToDefault
// ============================================================================

/**
 * @brief Restaura o tamanho ativo na primeira chamada a ProcessEvent.
 *
 * Se ProcessEvent nunca foi chamado, s_default_size = 13.0f (fallback seguro).
 */
void FontScale::ResetToDefault()
{
    SetSize(s_default_size); // volta ao valor capturado no primeiro zoom
}