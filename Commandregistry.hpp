#pragma once
#include "pch.hpp"

class Console;
class App;

/**
 * @file CommandRegistry.hpp
 * @brief Classe responsável exclusivamente pelo registro de comandos do Console.
 *
 * REGRA OBRIGATÓRIA — NOMES SEM ESPAÇOS
 * ----------------------------------------
 * ExecCommand divide a linha no PRIMEIRO espaço para obter a chave de despacho.
 * Qualquer espaço num nome de comando impede o lookup correto.
 *
 *   ERRADO : L"Test Emojis"   → chave = L"TEST"         → não encontrado
 *   CORRETO: L"test_emojis"   → chave = L"TEST_EMOJIS"  → encontrado ✓
 *
 * Nomes compostos DEVEM usar '_' como separador.
 * O espaço é reservado para separar nome de argumentos:
 *
 *   "system dir /b"  → chave = L"SYSTEM", args = { L"dir", L"/b" }
 *
 * GRUPOS DE REGISTRO
 * -------------------
 *   RegisterLifecycle() — EXIT, QUIT, BREAK, forceexit, abort, system_pause, cpp_pause
 *   RegisterSystem()    — SPECS, VSYNC, NOVIEWPORTS, FONTRESET, system [cmd]
 *   RegisterTheme()     — MICA, NOMICA, theme [dark|light|classic]
 *   RegisterDemo()      — implot, implot3d, test_emojis
 */
class CommandRegistry {
public:

    /**
     * @brief Constrói o registry com referências não-possuidoras.
     *
     * @param app      Instância de App em execução.
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
     * @return MyResult::ok sempre (falhas individuais logadas no console).
     */
    [[nodiscard]] class MyResult RegisterAll();

private:

    class App*     m_app;     ///< Ponteiro não-possuidor para a instância de App
    class Console* m_console; ///< Ponteiro não-possuidor para o Console ImGui

    /** @brief EXIT, QUIT, BREAK, forceexit, abort, system_pause, cpp_pause */
    void RegisterLifecycle();

    /** @brief SPECS, VSYNC, NOVIEWPORTS, FONTRESET, system [cmd] */
    void RegisterSystem();

    /** @brief MICA, NOMICA, theme [dark|light|classic] */
    void RegisterTheme();

    /** @brief implot, implot3d, test_emojis */
    void RegisterDemo();
};