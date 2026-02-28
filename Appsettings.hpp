#pragma once
#include "pch.hpp"

/**
 * @file AppSettings.hpp
 * @brief Configurações persistidas entre sessões via settings.json.
 *
 * Extraído em header próprio porque App.hpp, MenuBar.cpp e main.cpp
 * precisam do tipo completo — não apenas de forward declaration.
 */
struct AppSettings {
    std::vector<float> clear_color  = { 0.45f, 0.55f, 0.60f, 1.00f }; ///< Cor de fundo RGBA
    bool               show_console = false; ///< Visibilidade do console ImGui interno

    /// Multiplicador global de escala de fontes — aplicado via io.FontGlobalScale.
    /// 1.0 = tamanho original do atlas. 0.5 = metade, 2.0 = dobro.
    /// Não requer reconstrução do atlas — tem efeito imediato em runtime.
    float              font_scale   = 1.0f;
};