#pragma once
#include "pch.hpp"

#include <optional>    // std::optional
#include <span>        // std::span
#include <string>      // std::wstring
#include <string_view> // std::wstring_view

/**
 * @file InitArgs.hpp
 * @brief Parser e armazenamento do lpCmdLine recebido em wWinMain.
 */

/// Parser e contentor dos argumentos de linha de comandos do wWinMain.
class InitArgs
{
public:

    InitArgs()  = default;
    ~InitArgs() = default;

    InitArgs(const InitArgs&)            = delete;
    InitArgs& operator=(const InitArgs&) = delete;
    InitArgs(InitArgs&&)                 = default;
    InitArgs& operator=(InitArgs&&)      = default;

    /**
     * @brief Inicializa a partir do lpCmdLine do wWinMain.
     * @param lp_cmd_line  Ponteiro wide-string do wWinMain (pode ser nullptr ou vazio).
     */
    void Init(LPWSTR lp_cmd_line);

    /** @brief Vista de todos os argumentos individuais. */
    [[nodiscard]] std::span<const std::wstring> GetArgs()  const noexcept;

    /** @brief Número de argumentos (excluindo o executável). */
    [[nodiscard]] int                           GetCount() const noexcept;

    /** @brief Linha de comandos raw original. */
    [[nodiscard]] std::wstring_view             GetRaw()   const noexcept;

    /**
     * @brief Argumento no índice dado (0-based, relativo a argv[1]).
     * @return O argumento ou std::nullopt se índice inválido.
     */
    [[nodiscard]] std::optional<std::wstring_view> GetArg(std::size_t index) const noexcept;

    /**
     * @brief Verifica se um flag está presente (ex: L"--noviewports").
     * @param flag  Wide-string a procurar na lista de argumentos.
     */
    [[nodiscard]] bool HasFlag(std::wstring_view flag) const noexcept;

    /**
     * @brief Devolve o valor imediatamente a seguir a um flag (ex: L"--file" L"foo.gif").
     * @param flag  Flag cujo próximo argumento se quer obter.
     * @return O argumento seguinte ou std::nullopt se flag não encontrado ou sem valor.
     */
    [[nodiscard]] std::optional<std::wstring_view> GetValue(std::wstring_view flag) const noexcept;

private:

    std::wstring              m_raw;  ///< lpCmdLine original (cópia)
    std::vector<std::wstring> m_args; ///< Argumentos individuais após parse com CommandLineToArgvW
};