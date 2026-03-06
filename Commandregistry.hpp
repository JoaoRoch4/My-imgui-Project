#pragma once
#include "pch.hpp"

// Forward declarations — evita incluir headers pesados no .hpp
class Console;
class App;

/**
 * @file CommandRegistry.hpp
 * @brief Classe responsável exclusivamente pelo registro de comandos do Console.
 *
 * RESPONSABILIDADE ÚNICA
 * -----------------------
 * App::RegisterCommands() delegava para este objeto, mantendo App.cpp enxuto.
 * Cada comando é registrado em um método privado separado, agrupado por tema:
 *
 *   RegisterLifecycle()   — EXIT, QUIT, BREAK, forceexit, Abort, pauses
 *   RegisterSystem()      — SPECS, VSYNC, NOVIEWPORTS, FONTRESET
 *   RegisterTheme()       — MICA, NOMICA, theme [dark|light|classic]
 *   RegisterDemo()        — implot, implot3d, Test Emojis
 *
 * USO EM App::RegisterCommands()
 * --------------------------------
 * @code
 *   MyResult App::RegisterCommands() {
 *       if(!g_Console || !g_Vulkan)
 *           return MR_MSGBOX_ERR_END_LOC("Console ou Vulkan nulos.");
 *
 *       CommandRegistry reg(this, g_Console);
 *       return reg.RegisterAll();
 *   }
 * @endcode
 *
 * ACESSO AO ESTADO DE App
 * ------------------------
 * CommandRegistry recebe um ponteiro não-possuidor para App.
 * Acessa apenas membros públicos de App (g_Done, g_Vulkan, g_Settings, etc.).
 * Não armazena o ponteiro além do escopo de RegisterAll() — seguro contra
 * use-after-free porque os lambdas capturam os ponteiros por valor.
 */
class CommandRegistry {
public:

    /**
     * @brief Constrói o registry com referências não-possuidoras.
     *
     * @param app      Instância de App em execução (membros públicos acessíveis).
     * @param console  Console ImGui onde os comandos serão registrados.
     */
    CommandRegistry(App* app, Console* console) noexcept;

    ~CommandRegistry() = default;

    // Não copiável nem movível — vive apenas durante RegisterAll()
    CommandRegistry(const CommandRegistry&)            = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;
    CommandRegistry(CommandRegistry&&)                 = delete;
    CommandRegistry& operator=(CommandRegistry&&)      = delete;

    /**
     * @brief Registra todos os grupos de comandos no Console.
     *
     * Chama internamente RegisterLifecycle(), RegisterSystem(),
     * RegisterTheme() e RegisterDemo() nessa ordem.
     *
     * @return MyResult::ok sempre (falhas individuais são logadas no console).
     */
    [[nodiscard]] class MyResult RegisterAll();

private:

    class App*     m_app;     ///< Ponteiro não-possuidor para a instância de App
    class Console* m_console; ///< Ponteiro não-possuidor para o Console ImGui

    // =========================================================================
    // Grupos de registro — um método por tema
    // =========================================================================

    /**
     * @brief Registra comandos de ciclo de vida da aplicação.
     *
     * Comandos: EXIT, QUIT, BREAK, forceexit, Abort, "System Pause", "Cpp Pause"
     */
    void RegisterLifecycle();

    /**
     * @brief Registra comandos de sistema e hardware.
     *
     * Comandos: SPECS, VSYNC, NOVIEWPORTS, FONTRESET
     */
    void RegisterSystem();

    /**
     * @brief Registra comandos de tema visual.
     *
     * Comandos: MICA, NOMICA, theme [dark|light|classic]
     */
    void RegisterTheme();

    /**
     * @brief Registra comandos de demonstração e testes.
     *
     * Comandos: implot, implot3d, "Test Emojis"
     */
    void RegisterDemo();
};