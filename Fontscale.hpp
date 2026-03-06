#pragma once
#include "pch.hpp"

/**
 * @file FontScale.hpp
 * @brief Zoom de fonte global via Ctrl+Scroll — com supressão por janela.
 *
 * SUPRESSÃO DE SCROLL
 * --------------------
 * Quando o Console ImGui está em foco, o Ctrl+Scroll deve afetar apenas
 * a escala interna do console (m_font_scale), não a fonte global.
 *
 * O problema de ordem de execução num frame:
 *
 *   SDL_PollEvent → FontScale::ProcessEvent()   ← aqui não sabemos o hover
 *   Windows()     → Console::Draw()             ← aqui sabemos o hover
 *
 * Solução: Console::Draw() chama FontScale::SetScrollSuppressed() com o
 * estado de hover do frame ATUAL. No PRÓXIMO frame, ProcessEvent() lê o flag
 * e pula o scroll global. O lag de 1 frame é imperceptível para o usuário.
 *
 * FLUXO COMPLETO:
 *
 *   Frame N:
 *     1. SDL_PollEvent → ProcessEvent() lê s_suppressed (= false no início)
 *        → aplica zoom global normalmente
 *     2. Console::Draw() detecta hover → SetScrollSuppressed(true)
 *
 *   Frame N+1:
 *     1. SDL_PollEvent → ProcessEvent() lê s_suppressed (= true)
 *        → PULA o zoom global
 *     2. Console::Draw() lê io.MouseWheel → aplica zoom local do console
 *     3. Console::Draw() atualiza SetScrollSuppressed(true/false) para Frame N+2
 */
class FontScale {
public:

    // =========================================================================
    // Limites e incremento
    // =========================================================================

    static constexpr float FONT_SIZE_MIN = 8.0f;  ///< Menor tamanho global permitido (px)
    static constexpr float FONT_SIZE_MAX = 48.0f; ///< Maior tamanho global permitido (px)
    static constexpr float STEP          = 1.0f;  ///< Incremento por tick de scroll

    // =========================================================================
    // Supressão de scroll global
    // =========================================================================

    /**
     * @brief Informa ao FontScale se o scroll global deve ser suprimido.
     *
     * Chamado por Console::Draw() uma vez por frame, com o estado de hover
     * da janela do console.  Quando true, ProcessEvent() ignora Ctrl+Scroll
     * para que o console possa tratar o evento internamente.
     *
     * @param suppressed  true = console está em foco; false = scroll global ativo.
     */
    static void SetScrollSuppressed(bool suppressed) noexcept;

    /**
     * @brief Retorna true se o scroll global está suprimido neste frame.
     *
     * Usado internamente por ProcessEvent(); exposto para testes/debug.
     */
    [[nodiscard]] static bool IsScrollSuppressed() noexcept;

    // =========================================================================
    // API principal
    // =========================================================================

    /**
     * @brief Processa um SDL_Event e aplica zoom de fonte global se:
     *        (a) é um SDL_EVENT_MOUSE_WHEEL,
     *        (b) Ctrl está pressionado,
     *        (c) o scroll NÃO está suprimido (console não está em foco).
     *
     * @param event  Evento SDL3 a inspecionar.
     * @return       true se a fonte global foi alterada nesta chamada.
     */
    static bool ProcessEvent(const SDL_Event& event);

    /** @brief Retorna o tamanho base atual da fonte global em pixels. */
    [[nodiscard]] static float GetCurrentSize();

    /** @brief Define o tamanho base global, respeitando FONT_SIZE_MIN/MAX. */
    static void SetSize(float px);

    /** @brief Restaura o tamanho capturado na primeira chamada a ProcessEvent. */
    static void ResetToDefault();

private:

    static float s_default_size;  ///< Tamanho capturado no primeiro zoom (reset target)
    static bool  s_default_set;   ///< true após s_default_size ser capturado uma vez
    static bool  s_suppressed;    ///< true quando Console está em foco (scroll suprimido)
};