#pragma once
#include "pch.hpp"

class MyResult {
public:
    enum class ErrorType {
        MsgBox_Error,
        MsgBox_Warning,
        MsgBox_Ok,
        MsgCls_Error,
        MsgCls_Warning,
        MsgCls_Normal,
        None
    };

    /// @brief Holds source code location info injected by macros.
    struct SourceLocation {
        const wchar_t* file;     ///< __FILE__
        int            line;     ///< __LINE__
        const wchar_t* function; ///< __func__
    };

    bool                        success;
    bool                        should_exit;
    ErrorType                   error_type;
    std::optional<std::wstring> message;

    // ----------------------------------------------------------------
    static const MyResult ok;
    // ----------------------------------------------------------------

    struct msgbox {
        // --- original overloads (message only) ---
        static MyResult errorW      (const std::wstring& msgW = L"Erro desconhecido");
        static MyResult error_endW  (const std::wstring& msgW = L"Erro desconhecido");
        static MyResult warningW    (const std::wstring& msgW = L"Aviso");
        static MyResult warning_endW(const std::wstring& msgW = L"Aviso");
        static MyResult normalW     (const std::wstring& msgW = L"");

        // --- new overloads (message + source location) ---
        static MyResult errorW      (const std::wstring& msgW, const SourceLocation& loc);
        static MyResult error_endW  (const std::wstring& msgW, const SourceLocation& loc);
        static MyResult warningW    (const std::wstring& msgW, const SourceLocation& loc);
        static MyResult warning_endW(const std::wstring& msgW, const SourceLocation& loc);
        static MyResult normalW     (const std::wstring& msgW, const SourceLocation& loc);
    };

    // ----------------------------------------------------------------

    struct msgcls {
        // --- original overloads (message only) ---
        static MyResult errorW      (const std::wstring& msgW = L"Erro desconhecido");
        static MyResult error_endW  (const std::wstring& msgW = L"Erro desconhecido");
        static MyResult warningW    (const std::wstring& msgW = L"Aviso");
        static MyResult warning_endW(const std::wstring& msgW = L"Aviso");
        static MyResult normalW     (const std::wstring& msgW = L"");

        // --- new overloads (message + source location) ---
        static MyResult errorW      (const std::wstring& msgW, const SourceLocation& loc);
        static MyResult error_endW  (const std::wstring& msgW, const SourceLocation& loc);
        static MyResult warningW    (const std::wstring& msgW, const SourceLocation& loc);
        static MyResult warning_endW(const std::wstring& msgW, const SourceLocation& loc);
        static MyResult normalW     (const std::wstring& msgW, const SourceLocation& loc);
    };

    // ----------------------------------------------------------------
    operator bool() const { return success; }

private:
    MyResult(bool ok, std::optional<std::wstring> msgW = std::nullopt,
             ErrorType type = ErrorType::None, bool exit = false);

    void handle_errorW();

    static constexpr const wchar_t* CLS_RED    = L"\033[1;31m";
    static constexpr const wchar_t* CLS_YELLOW = L"\033[1;33m";
    static constexpr const wchar_t* CLS_WHITE  = L"\033[0m";
    static constexpr const wchar_t* CLS_RESET  = L"\033[0m";
};

// ====================================================================
// Helpers de string
// ====================================================================

#define WIDEN2(x) L ## x
#define WIDEN(x)  WIDEN2(x)
#define __WFILE__ WIDEN(__FILE__)

// ====================================================================
// Helper interno: captura localização do ponto de chamada
// ====================================================================

#define _MR_LOC() \
    MyResult::SourceLocation{ __WFILE__, __LINE__, ToWStr(__func__) }

#define MR_LOCATION _MR_LOC()

// ====================================================================
// Helper interno: formata mensagem + contexto embutido na string
// (macros originais sem SourceLocation separado)
// ====================================================================

#define _MR_CTX(varW, msgW)                                    \
    (L"[" + ToWStr(__FILE__)) + L":" + std::to_wstring(__LINE__) + \
     L" in " + ToWStr(__func__) + L"] varW: " + ToWStr(#varW) + L" | " + TXT(msgW))

// ====================================================================
// __debugbreak() — só ativo em builds Debug com debugger anexado
// ====================================================================
// __debugbreak() é uma intrínseca do MSVC que emite uma instrução INT 3,
// pausando a execução exatamente na linha da macro quando o Visual Studio
// está rodando o programa com F5 (debugger anexado).
// IsDebuggerPresent() evita que o breakpoint dispare em builds sem debugger,
// o que causaria uma exceção de acesso não tratada (STATUS_BREAKPOINT).
// Em builds Release, o bloco inteiro é removido pelo pré-processador.
// ====================================================================

#if defined(_DEBUG)
    /// @brief Pausa no debugger MSVC se um debugger estiver anexado.
    /// Usado no final das macros de erro para apontar exatamente a linha
    /// que gerou o MyResult de falha.
    #define _MR_BREAK() \
        do { if(IsDebuggerPresent()) { __debugbreak(); } } while(false)
#else
    /// @brief No-op em builds Release — sem overhead em produção.
    #define _MR_BREAK() \
        do {} while(false)
#endif

// ====================================================================
// Macros originais (apenas mensagem + contexto embutido na string)
// ====================================================================

// msgbox — originais
#define MR_MSGBOX_ERR(var, msg) \
    ( MyResult::msgbox::errorW(_MR_CTX(TXT(var), TXT(msg))), _MR_BREAK(), MyResult::msgbox::errorW(_MR_CTX(TXT(var), TXT(msg))) )

// Nota: as macros originais acima duplicariam a chamada se usassem vírgula.
// Solução limpa: avaliar o resultado, quebrar, e retornar o resultado —
// usando um lambda imediato para encapsular as duas operações numa expressão.
// As macros abaixo usam esse padrão para todas as variantes.

#undef MR_MSGBOX_ERR  // remove o temporário acima

// ---- Padrão final para todas as macros --------------------------------
// [&]() { auto _r = <chamada>; _MR_BREAK(); return _r; }()
// O lambda captura por referência para ter acesso ao contexto local,
// é invocado imediatamente (IIFE) e retorna o MyResult.
// O compilador MSVC inlina completamente em Release (sem lambda overhead).
// -----------------------------------------------------------------------

// msgbox — originais (sem SourceLocation)
#define MR_MSGBOX_ERR(var, msg) \
    [&]() { auto _r = MyResult::msgbox::errorW(_MR_CTX(TXT(var), TXT(msg))); _MR_BREAK(); return _r; }()

#define MR_MSGBOX_ERR_END(var, msg) \
    [&]() { auto _r = MyResult::msgbox::error_endW(_MR_CTX(TXT(var), TXT(msg))); _MR_BREAK(); return _r; }()

#define MR_MSGBOX_WARN(var, msg) \
    [&]() { auto _r = MyResult::msgbox::warningW(_MR_CTX(TXT(var), TXT(msg))); _MR_BREAK(); return _r; }()

#define MR_MSGBOX_WARN_END(var, msg) \
    [&]() { auto _r = MyResult::msgbox::warning_endW(_MR_CTX(TXT(var), TXT(msg))); _MR_BREAK(); return _r; }()

#define MR_MSGBOX_OK(msg) \
    MyResult::msgbox::normalW(TXT(msg))  // ok não quebra — não é erro

// msgcls — originais (sem SourceLocation)
#define MR_CLS_ERR(var, msg) \
    [&]() { auto _r = MyResult::msgcls::errorW(_MR_CTX(TXT(var), TXT(msg))); _MR_BREAK(); return _r; }()

#define MR_CLS_ERR_END(var, msg) \
    [&]() { auto _r = MyResult::msgcls::error_endW(_MR_CTX(TXT(var), TXT(msg))); _MR_BREAK(); return _r; }()

#define MR_CLS_WARN(var, msg) \
    [&]() { auto _r = MyResult::msgcls::warningW(_MR_CTX(TXT(var), TXT(msg))); _MR_BREAK(); return _r; }()

#define MR_CLS_WARN_END(var, msg) \
    [&]() { auto _r = MyResult::msgcls::warning_endW(_MR_CTX(TXT(var), TXT(msg))); _MR_BREAK(); return _r; }()

#define MR_CLS_NORMAL(msg) \
    MyResult::msgcls::normalW(TXT(msg))  // normal não quebra — não é erro

// ====================================================================
// Macros com SourceLocation (_LOC) — erros e warnings quebram
// ====================================================================

// msgbox — com localização
#define MR_MSGBOX_ERR_LOC(msg) \
    [&]() { auto _r = MyResult::msgbox::errorW(TXT(msg), _MR_LOC()); _MR_BREAK(); return _r; }()

#define MR_MSGBOX_ERR_END_LOC(msg) \
    [&]() { auto _r = MyResult::msgbox::error_endW(TXT(msg), _MR_LOC()); _MR_BREAK(); return _r; }()

#define MR_MSGBOX_ERR_END_LOCATION(msg) \
    [&]() { auto _r = MyResult::msgbox::error_endW(TXT(msg), _MR_LOC()); _MR_BREAK(); return _r; }()

#define MR_MSGBOX_WARN_LOC(msg) \
    [&]() { auto _r = MyResult::msgbox::warningW(TXT(msg), _MR_LOC()); _MR_BREAK(); return _r; }()

#define MR_MSGBOX_WARN_END_LOC(msg) \
    [&]() { auto _r = MyResult::msgbox::warning_endW(TXT(msg), _MR_LOC()); _MR_BREAK(); return _r; }()

#define MR_MSGBOX_OK_LOC(msg) \
    MyResult::msgbox::normalW(TXT(msg), _MR_LOC())  // ok não quebra

// msgcls — com localização
#define MR_CLS_ERR_LOC(msg) \
    [&]() { auto _r = MyResult::msgcls::errorW(TXT(msg), _MR_LOC()); _MR_BREAK(); return _r; }()

#define MR_CLS_ERR_LOCATION(msg) \
    [&]() { auto _r = MyResult::msgcls::errorW(TXT(msg), _MR_LOC()); _MR_BREAK(); return _r; }()

#define MR_CLS_ERR_END_LOC(msg) \
    [&]() { auto _r = MyResult::msgcls::error_endW(TXT(msg), _MR_LOC()); _MR_BREAK(); return _r; }()

#define MR_CLS_ERR_END_LOCATION(msg) \
    [&]() { auto _r = MyResult::msgcls::error_endW(TXT(msg), _MR_LOC()); _MR_BREAK(); return _r; }()

#define MR_CLS_WARN_LOC(msg) \
    [&]() { auto _r = MyResult::msgcls::warningW(TXT(msg), _MR_LOC()); _MR_BREAK(); return _r; }()

#define MR_CLS_WARN_END_LOC(msg) \
    [&]() { auto _r = MyResult::msgcls::warning_endW(TXT(msg), _MR_LOC()); _MR_BREAK(); return _r; }()

#define MR_CLS_NORMAL_LOC(msg) \
    MyResult::msgcls::normalW(TXT(msg), _MR_LOC())  // normal não quebra

// ====================================================================
// Utilitários
// ====================================================================

/// @brief Verifica se um MyResult representa sucesso.
#define MR_IS_OK(result) ((result).success)

/// @brief Retorna MyResult::ok diretamente.
#define MR_OK MyResult::ok