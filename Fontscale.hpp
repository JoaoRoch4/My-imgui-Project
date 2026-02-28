#pragma once

#include "pch.hpp"

class FontScale {
public:

    /** @brief Limites em pixels para o tamanho base da fonte. */
    static constexpr float FONT_SIZE_MIN = 8.0f;   ///< Menor tamanho permitido (px)
    static constexpr float FONT_SIZE_MAX = 48.0f;  ///< Maior tamanho permitido (px)
    static constexpr float STEP = 1.0f;   ///< Incremento por tick de scroll

    /**
     * @brief Processa um SDL_Event e aplica zoom de fonte se Ctrl+Scroll for detectado.
     *
     * Chame UMA VEZ por evento, dentro do loop SDL_PollEvent, ANTES de
     * ImGui_ImplSDL3_ProcessEvent — ou depois, tanto faz, pois lemos
     * apenas o scroll e o estado do teclado, sem consumir o evento.
     *
     * @param event  Referência ao evento SDL3 corrente.
     * @return       true se o evento foi um Ctrl+Scroll e a fonte foi alterada.
     */
    static bool ProcessEvent(const SDL_Event& event);

    /** @brief Retorna o tamanho base atual da fonte em pixels. */
    static float GetCurrentSize();

    /** @brief Define o tamanho base diretamente, respeitando os limites. */
    static void  SetSize(float px);

    /** @brief Restaura o tamanho que estava ativo na inicialização do programa. */
    static void  ResetToDefault();

private:
    static float s_default_size; ///< Tamanho capturado na primeira chamada a ProcessEvent
    static bool  s_default_set;  ///< true após s_default_size ser capturado uma vez
};
