#pragma once

#include "pch.hpp"

/**
 * @file MicaTheme.h
 * @brief Tema Windows 11 Mica para ImGui com persistência via AppSettings.
 *
 * MUDANÇA DE PERSISTÊNCIA
 * ------------------------
 * Antes: MicaTheme::LoadThemeFromFile / SaveThemeToFile gerenciavam um JSON
 * próprio (ex.: "mica_theme.json").
 *
 * Agora: ThemeConfig é um campo de AppSettings (AppSettings::mica_theme) e
 * é serializado junto com tudo no settings.json via App::SaveConfig().
 * As funções LoadThemeFromFile / SaveThemeToFile são mantidas para uso
 * independente (testes, utilitários), mas não são mais chamadas pela App.
 *
 * SOBRECARGA ApplyMicaTheme(ThemeConfig, ImGuiStyle&)
 * ----------------------------------------------------
 * Necessária para:
 *  1. ColorSettings::ColorSettings() — aplica Mica em um ImGuiStyle TEMPORÁRIO
 *     sem afetar o contexto global (GImGui pode ainda não existir neste ponto).
 *  2. App::ApplyStyleToImGui() — aplica o Mica salvo ao estilo global.
 *
 * A versão original ApplyMicaTheme(ThemeConfig) continua delegando para a
 * sobrecarga com ImGui::GetStyle() — sem quebra de compatibilidade.
 */
namespace MicaTheme {

// ============================================================================
// Color — representação RGBA serializável
// ============================================================================

/**
 * @brief Cor RGBA como quatro floats — reflect-cpp compatível.
 *
 * Usado em ThemeConfig para que todas as cores do tema sejam editáveis
 * diretamente no settings.json ou via futura UI de customização.
 */
struct Color {
    float r = 1.0f; ///< Componente vermelho [0,1]
    float g = 1.0f; ///< Componente verde    [0,1]
    float b = 1.0f; ///< Componente azul     [0,1]
    float a = 1.0f; ///< Alpha               [0,1]

    /** @brief Converte para ImVec4 para uso direto nas APIs do ImGui. */
    ImVec4 toImVec4() const { return ImVec4(r, g, b, a); }
};

// ============================================================================
// ThemeConfig — configuração completa do tema Mica
// ============================================================================

/**
 * @brief Todos os parâmetros do tema Mica — cores e dimensões de estilo.
 *
 * Serializado como AppSettings::mica_theme em settings.json.
 * Defaults calibrados para o visual Windows 11 Mica escuro.
 *
 * CORES DE SUPERFÍCIE:
 *   surface_primary   — fundo de janelas (semitransparente para efeito Mica)
 *   surface_secondary — fundo de popups e barra de menu
 *
 * CORES DE INTERAÇÃO:
 *   accent    — azul Windows 11 (#0092F8 ≈ {0.004, 0.576, 0.976})
 *   hover     — cinza levemente mais claro para hover
 *   active    — azul mais opaco para estado ativo (não exposto diretamente)
 *
 * CORES DE TEXTO:
 *   text_primary   — branco suave (#F2F2F2)
 *   text_secondary — cinza médio para texto desabilitado/secundário
 */
struct ThemeConfig {
    // ---- Superfícies ---------------------------------------------------
    Color surface_primary   = { 0.129f, 0.129f, 0.129f, 0.78f  }; ///< Fundo de janelas
    Color surface_secondary = { 0.157f, 0.157f, 0.157f, 0.85f  }; ///< Popups e menu bar

    // ---- Accent e UI ---------------------------------------------------
    Color accent        = { 0.004f, 0.576f, 0.976f, 1.0f  }; ///< Azul Windows 11
    Color text_primary  = { 0.949f, 0.949f, 0.949f, 1.0f  }; ///< Texto principal
    Color text_secondary= { 0.698f, 0.698f, 0.702f, 1.0f  }; ///< Texto secundário/disabled
    Color border        = { 0.329f, 0.329f, 0.329f, 0.4f  }; ///< Bordas e separadores
    Color hover         = { 0.212f, 0.212f, 0.212f, 0.9f  }; ///< Fundo de hover
    Color active        = { 0.004f, 0.576f, 0.976f, 0.9f  }; ///< Estado ativo (não usado diretamente)

    // ---- Frames e campos de entrada ------------------------------------
    Color frame_bg        = { 0.176f, 0.176f, 0.176f, 0.545f }; ///< Fundo de inputs/frames
    Color frame_bg_hovered= { 0.212f, 0.212f, 0.212f, 0.9f   }; ///< Fundo de input em hover
    Color frame_bg_active = { 0.067f, 0.341f, 0.608f, 0.588f }; ///< Fundo de input ativo

    // ---- Dimensões de estilo -------------------------------------------
    float frame_rounding    = 4.0f;  ///< ThemeConfig::frame_rounding → style.FrameRounding
    float window_rounding   = 8.0f;  ///< ThemeConfig::window_rounding → style.WindowRounding
    float popup_rounding    = 4.0f;  ///< → style.PopupRounding
    float tab_rounding      = 4.0f;  ///< → style.TabRounding
    float grab_rounding     = 2.0f;  ///< → style.GrabRounding
    float frame_border_size = 1.0f;  ///< → style.FrameBorderSize
    float window_border_size= 1.0f;  ///< → style.WindowBorderSize
    float popup_border_size = 1.0f;  ///< → style.PopupBorderSize
    float scrollbar_size    = 14.0f; ///< → style.ScrollbarSize
    float grab_min_size     = 10.0f; ///< → style.GrabMinSize
};

// ============================================================================
// Funções
// ============================================================================

/**
 * @brief Retorna um ThemeConfig com os valores default do Mica.
 *
 * Útil para construção de ColorSettings sem criar um ThemeConfig manualmente.
 */
ThemeConfig GetDefaultTheme();

/**
 * @brief Carrega um ThemeConfig de um arquivo JSON via reflect-cpp.
 *
 * Mantido para uso independente. A App não chama mais esta função —
 * o ThemeConfig é lido como parte de AppSettings em App::LoadConfig().
 *
 * @param filepath  Caminho do arquivo JSON.
 * @return          ThemeConfig carregado, ou GetDefaultTheme() em caso de falha.
 */
ThemeConfig LoadThemeFromFile(const std::string& filepath);

/**
 * @brief Salva um ThemeConfig em um arquivo JSON via reflect-cpp.
 *
 * Mantido para uso independente. A App não chama mais esta função —
 * o ThemeConfig é gravado como parte de AppSettings em App::SaveConfig().
 *
 * @param config    ThemeConfig a serializar.
 * @param filepath  Caminho do arquivo JSON de destino.
 */
void SaveThemeToFile(const ThemeConfig& config, const std::string& filepath);

/**
 * @brief Aplica o tema Mica a um ImGuiStyle fornecido externamente.
 *
 * SOBRECARGA PRINCIPAL — usada em dois contextos:
 *
 *  1. ColorSettings::ColorSettings()
 *     Recebe um ImGuiStyle TEMPORÁRIO (stack) para capturar as cores Mica sem
 *     precisar de um contexto ImGui ativo (GImGui pode ser nullptr aqui).
 *
 *  2. App::ApplyStyleToImGui()
 *     Recebe ImGui::GetStyle() para aplicar o tema Mica ao estilo global após
 *     restaurar StyleSettings e ColorSettings do settings.json.
 *
 * @param theme  Configuração do tema (cores + dimensões).
 * @param style  ImGuiStyle de destino (modificado in-place).
 */
void ApplyMicaTheme(const ThemeConfig& theme, ImGuiStyle& style);

/**
 * @brief Aplica o tema Mica ao ImGuiStyle GLOBAL (ImGui::GetStyle()).
 *
 * Sobrecarga de conveniência — delega para ApplyMicaTheme(theme, ImGui::GetStyle()).
 * Requer contexto ImGui ativo (GImGui != nullptr).
 *
 * @param theme  Configuração do tema.
 */
void ApplyMicaTheme(const ThemeConfig& theme);

/**
 * @brief Aplica o tema Mica padrão ao ImGuiStyle global.
 *
 * Equivalente a ApplyMicaTheme(GetDefaultTheme()).
 * Mantida para compatibilidade com código existente que chame ApplyMicaThemeDefault().
 */
inline void ApplyMicaThemeDefault() {
    ApplyMicaTheme(GetDefaultTheme()); // delega para a sobrecarga com ThemeConfig
}

} // namespace MicaTheme