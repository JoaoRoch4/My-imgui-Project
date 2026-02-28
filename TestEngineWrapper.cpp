#include "pch.hpp"
//#include "TestEngineWrapper.hpp"
//
//// Headers da Engine 
//#include "imgui_test_engine/imgui_te_engine.h"
//#include "imgui_test_engine/imgui_te_context.h"
//#include "imgui_internal.h" 
//#include <imgui_te_engine.cpp>
//#include <imgui_te_perftool.cpp>
//
//
//TestEngineWrapper::TestEngineWrapper() : m_Engine(nullptr) {}
//
//TestEngineWrapper::~TestEngineWrapper() { Shutdown(); }
//
//void TestEngineWrapper::Init() {
//    // 1. Cria o contexto
//    m_Engine = ImGuiTestEngine_CreateContext();
//
//    // 2. Vincula ao contexto do ImGui (importante: deve ser chamado após ImGui::CreateContext)
//    ImGuiTestEngine_BindImGuiContext(m_Engine, ImGui::GetCurrentContext());
//
//    // 3. Configurações de IO - REMOVIDOS os campos que deram erro
//    // Se precisar de velocidade, a Engine já vem com um padrão aceitável.
//    // ImGuiTestEngineIO& io = ImGuiTestEngine_GetIO(m_Engine);
//}
//
//void TestEngineWrapper::PostRender() {
//    if (m_Engine) {
//        ImGuiTestEngine_PostRender(m_Engine, ImGui::GetCurrentContext());
//    }
//}
//
//void TestEngineWrapper::Shutdown() {
//    if (m_Engine) {
//        ImGuiTestEngine_DestroyContext(m_Engine);
//        m_Engine = nullptr;
//    }
//}
//
//void TestEngineWrapper::RegisterTests() {
//    // Registro de um teste simples para validar a compilação
//    ImGuiTest* t = ImGuiTestEngine_RegisterTest(m_Engine, "Sistema", "Teste_Login");
//    
//    t->TestFunc = [](ImGuiTestContext* ctx) {
//        ctx->LogInfo("Test Engine operacional no Vulkan!");
//        
//        // Exemplo de interação genérica que funciona em quase qualquer versão:
//        ctx->WindowFocus("Debug Console"); 
//        
//        // Se o seu console tiver um input, ele tentará clicar
//        if (ctx->ItemExists("##InputText")) {
//            ctx->ItemClick("##InputText");
//            ctx->KeyChars("HELP");
//            ctx->KeyPress(ImGuiKey_Enter);
//        }
//    };
//}