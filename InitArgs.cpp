/**
 * @file InitArgs.cpp
 * @brief Parser e armazenamento do lpCmdLine recebido em wWinMain.
 *
 * ============================================================
 *  PORQUÊ CommandLineToArgvW E NÃO PARSING MANUAL
 * ============================================================
 *
 *  lpCmdLine é uma wide-string bruta do shell do Windows, por exemplo:
 *    L"--noviewports --file \"meu arquivo.gif\" --scale 2"
 *
 *  Fazer splitting manual por espaços falha em:
 *    - Argumentos com espaços dentro de aspas ("meu arquivo.gif")
 *    - Aspas escapadas (\"valor\")
 *    - Tabulações como separadores
 *
 *  CommandLineToArgvW (Shell32) implementa exactamente as mesmas regras
 *  que o CRT usa para __wargv — o resultado é idêntico ao que terias
 *  com argv em main(). Trata aspas, espaços e escapes correctamente.
 *
 *  LOCALAPI: CommandLineToArgvW devolve um LPWSTR* alocado via LocalAlloc.
 *  Libertamos com LocalFree(argv) após copiar para std::vector<std::wstring>.
 *  Sem new/malloc — apenas std::wstring que gere o seu próprio heap.
 *
 * ============================================================
 *  ESTRUTURA DO lpCmdLine
 * ============================================================
 *
 *  Em wWinMain, lpCmdLine NÃO inclui o nome do executável — ao contrário
 *  de GetCommandLineW() que o inclui como argv[0].
 *
 *  CommandLineToArgvW(lpCmdLine, &argc):
 *    L"--noviewports --file foo.gif"
 *      → argc = 3
 *      → argv[0] = L"--noviewports"
 *      → argv[1] = L"--file"
 *      → argv[2] = L"foo.gif"
 *
 *  (Se lpCmdLine for L"" ou nullptr, argc = 0 e não há argumentos.)
 *
 * ============================================================
 *  EXEMPLOS DE USO
 * ============================================================
 *
 *  InitArgs* args = Memory::Get()->GetInitArgs();
 *
 *  bool no_viewports = args->HasFlag(L"--noviewports");
 *
 *  auto file = args->GetValue(L"--file");
 *  if(file) OpenGif(*file);
 *
 *  for(auto& a : args->GetArgs())
 *      wprintf(L"arg: %s\n", a.c_str());
 */

#include "pch.hpp"
#include "InitArgs.hpp"

// ============================================================================
// Init
// ============================================================================

/**
 * @brief Inicializa a partir do lpCmdLine do wWinMain.
 *
 * PASSO 1 — Guarda a string raw
 *   Se lp_cmd_line for nullptr (subsistema CONSOLE sem args, ou vazio),
 *   m_raw fica vazio e m_args fica vazio — sem crash.
 *
 * PASSO 2 — CommandLineToArgvW
 *   Usa a Shell32 API para partir lpCmdLine pelos mesmos critérios
 *   que o CRT usa para __wargv: aspas, espaços, tabs, escapes.
 *   Devolve LPWSTR* alocado via LocalAlloc — libertamos com LocalFree.
 *
 * PASSO 3 — Cópia para std::vector<std::wstring>
 *   Cada elemento do array temporário do Windows é copiado para
 *   um std::wstring próprio. O vector fica na heap gerida pelo RAII.
 *   LocalFree(argv) liberta o array do Windows logo após a cópia.
 *
 * @param lp_cmd_line  LPWSTR de wWinMain — pode ser nullptr ou L"".
 */
void InitArgs::Init(LPWSTR lp_cmd_line)
{
    // ---- Passo 1: guarda a linha raw original --------------------------------
    // Se nullptr (invocação sem argumentos), wstring fica vazia — sem UB
    m_raw = (lp_cmd_line != nullptr) ? std::wstring(lp_cmd_line) : std::wstring{};

    // Sem argumentos — nada a parsear
    if(m_raw.empty())
        return;

    // ---- Passo 2: CommandLineToArgvW ----------------------------------------
    // argc recebe o número de argumentos encontrados
    int argc = 0;

    // CommandLineToArgvW faz o parsing com as regras do CRT do Windows:
    //   aspas duplas delimitam argumentos com espaços
    //   \ antes de " é escape
    //   múltiplos espaços/tabs contam como um separador
    LPWSTR* argv = ::CommandLineToArgvW(m_raw.c_str(), &argc);

    // Falha improvável (memória esgotada) — argc fica 0 e m_args fica vazio
    if(argv == nullptr || argc <= 0)
        return;

    // ---- Passo 3: cópia para vector<wstring> --------------------------------
    // Reserva exactamente argc entradas para evitar reallocs
    m_args.reserve(static_cast<std::size_t>(argc));

    for(int i = 0; i < argc; ++i)
    {
        // argv[i] é um LPWSTR do Windows — copiamos para wstring própria
        // A wstring faz a cópia dos caracteres para o seu próprio heap (RAII)
        m_args.emplace_back(argv[i]);
    }

    // LocalFree: liberta o array LPWSTR* alocado por CommandLineToArgvW
    // Deve ser chamado uma vez por chamada a CommandLineToArgvW bem-sucedida
    ::LocalFree(argv);
    // argv aponta para memória libertada — não deve ser usada após este ponto
}

// ============================================================================
// Consulta
// ============================================================================

/**
 * @brief Vista imutável de todos os argumentos individuais.
 *
 * Retorna um std::span sobre o vector interno — zero cópias.
 * Válido enquanto este InitArgs existir (pertence a Memory).
 */
std::span<const std::wstring> InitArgs::GetArgs() const noexcept
{
    // std::span não aloca — aponta directamente para m_args.data()
    return std::span<const std::wstring>(m_args);
}

/**
 * @brief Número de argumentos (excluindo o nome do executável).
 *
 * Em wWinMain lpCmdLine não inclui o executável, portanto este
 * count já é equivalente a argc-1 do main() tradicional.
 */
int InitArgs::GetCount() const noexcept
{
    // Conversão segura: m_args.size() <= INT_MAX para qualquer linha de comandos realista
    return static_cast<int>(m_args.size());
}

/**
 * @brief Linha de comandos raw original recebida do wWinMain.
 *
 * Útil para logging ou para repassar a outro processo via CreateProcess.
 */
std::wstring_view InitArgs::GetRaw() const noexcept
{
    // string_view não aloca — referencia m_raw directamente
    return std::wstring_view(m_raw);
}

/**
 * @brief Argumento no índice dado (0-based, relativo a argv[1] do WinMain).
 *
 * GetArg(0) → primeiro argumento (ex: L"--noviewports")
 * GetArg(1) → segundo argumento, etc.
 *
 * @return O argumento como string_view ou std::nullopt se fora dos limites.
 */
std::optional<std::wstring_view> InitArgs::GetArg(std::size_t index) const noexcept
{
    // Verifica limites antes de indexar — sem UB
    if(index >= m_args.size())
        return std::nullopt;

    // Devolve string_view do wstring interno — sem cópia
    return std::wstring_view(m_args[index]);
}

/**
 * @brief Verifica se um flag está presente na lista de argumentos.
 *
 * Comparação case-sensitive — L"--File" != L"--file".
 * Para flags Windows convencionais (L"--noviewports", L"/help", etc.)
 * a comparação exacta é o comportamento esperado.
 *
 * @param flag  Flag a procurar (ex: L"--noviewports").
 * @return      true se encontrado em qualquer posição.
 */
bool InitArgs::HasFlag(std::wstring_view flag) const noexcept
{
    // Percorre todos os argumentos e compara com flag
    for(const std::wstring& arg : m_args)
    {
        // std::wstring::operator== com string_view — sem alocação temporária
        if(arg == flag)
            return true;
    }

    return false; // flag não encontrado
}

/**
 * @brief Devolve o valor imediatamente a seguir a um flag.
 *
 * Exemplo:
 *   args: L"--noviewports" L"--file" L"foo.gif" L"--scale" L"2"
 *   GetValue(L"--file")  → L"foo.gif"
 *   GetValue(L"--scale") → L"2"
 *   GetValue(L"--xxx")   → std::nullopt  (flag não existe)
 *   GetValue(L"--scale") com scale no último → std::nullopt (sem valor após)
 *
 * @param flag  Flag cujo argumento seguinte se quer obter.
 * @return      O argumento seguinte ou std::nullopt.
 */
std::optional<std::wstring_view> InitArgs::GetValue(std::wstring_view flag) const noexcept
{
    // Percorre todos os argumentos excepto o último (o último nunca tem valor após si)
    const std::size_t last = m_args.size();

    for(std::size_t i = 0; i < last; ++i)
    {
        // Encontrou o flag na posição i
        if(m_args[i] == flag)
        {
            // Verifica se existe um argumento seguinte (i+1 dentro dos limites)
            const std::size_t next = i + 1;
            if(next < last)
                return std::wstring_view(m_args[next]); // valor imediatamente a seguir

            // Flag existe mas é o último argumento — sem valor
            return std::nullopt;
        }
    }

    // Flag não encontrado em nenhuma posição
    return std::nullopt;
}