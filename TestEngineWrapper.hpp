#pragma once
#include "pch.hpp"


// Forward declarations do ImGui Test Engine
struct ImGuiTestEngine;

/**
 * @class TestEngineWrapper
 * @brief Encapsula o ciclo de vida e registro de testes do ImGui Test Engine.
 */
class TestEngineWrapper {
public:
    TestEngineWrapper();
    ~TestEngineWrapper();

    /**
     * @brief Inicializa a engine e a vincula ao contexto atual do ImGui.
     */
    void Init();

    /**
     * @brief Processa os testes. Deve ser chamado após ImGui::Render().
     */
    void PostRender();

    /**
     * @brief Destrói o contexto da engine.
     */
    void Shutdown();

    /**
     * @brief Registra os scripts de teste automatizados.
     */
    void RegisterTests();

    ImGuiTestEngine* GetPtr() { return m_Engine; }

private:
    ImGuiTestEngine* m_Engine;
};