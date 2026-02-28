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
        const char* file;     ///< __FILE__
        int         line;     ///< __LINE__
        const char* function; ///< __func__
    };

    bool        success;
    bool        should_exit;
    ErrorType   error_type;
    std::optional<std::string> message;

    // ----------------------------------------------------------------
    static const MyResult ok;
    // ----------------------------------------------------------------

    struct msgbox {
        // --- original overloads (message only) ---
        static MyResult error(const std::string& msg = "Erro desconhecido");
        static MyResult error_end(const std::string& msg = "Erro desconhecido");
        static MyResult warning(const std::string& msg = "Aviso");
        static MyResult warning_end(const std::string& msg = "Aviso");
        static MyResult normal(const std::string& msg = "");

        // --- new overloads (message + source location) ---
        static MyResult error(const std::string& msg, const SourceLocation& loc);
        static MyResult error_end(const std::string& msg, const SourceLocation& loc);
        static MyResult warning(const std::string& msg, const SourceLocation& loc);
        static MyResult warning_end(const std::string& msg, const SourceLocation& loc);
        static MyResult normal(const std::string& msg, const SourceLocation& loc);
    };

    // ----------------------------------------------------------------

    struct msgcls {
        // --- original overloads (message only) ---
        static MyResult error(const std::string& msg = "Erro desconhecido");
        static MyResult error_end(const std::string& msg = "Erro desconhecido");
        static MyResult warning(const std::string& msg = "Aviso");
        static MyResult warning_end(const std::string& msg = "Aviso");
        static MyResult normal(const std::string& msg = "");

        // --- new overloads (message + source location) ---
        static MyResult error(const std::string& msg, const SourceLocation& loc);
        static MyResult error_end(const std::string& msg, const SourceLocation& loc);
        static MyResult warning(const std::string& msg, const SourceLocation& loc);
        static MyResult warning_end(const std::string& msg, const SourceLocation& loc);
        static MyResult normal(const std::string& msg, const SourceLocation& loc);
    };

    // ----------------------------------------------------------------
    operator bool() const { return success; }

private:
    MyResult(bool ok,
        std::optional<std::string> msg = std::nullopt,
        ErrorType type = ErrorType::None,
        bool exit = false);

    void handle_error();

    static constexpr const char* CLS_RED = "\033[1;31m";
    static constexpr const char* CLS_YELLOW = "\033[1;33m";
    static constexpr const char* CLS_WHITE = "\033[0m";
    static constexpr const char* CLS_RESET = "\033[0m";
};

// ====================================================================
// Helper interno: formata mensagem + localização
// ====================================================================
// Usado pelas macros *_LOC para montar a string final antes de chamar
// a sobrecarga que recebe SourceLocation.
#define _MR_LOC() MyResult::SourceLocation{__FILE__, __LINE__, __func__}
#define MR_LOCATION _MR_LOC()

// ====================================================================
// Macros originais (apenas mensagem + contexto embutido na string)
// ====================================================================
#define _MR_CTX(var, msg) \
    ("[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
     " in " + __func__ + "] var: " + #var + " | " + (msg))

// msgbox — originais
#define MR_MSGBOX_ERR(var, msg)       MyResult::msgbox::error      (_MR_CTX(var, msg))
#define MR_MSGBOX_ERR_END(var, msg)   MyResult::msgbox::error_end  (_MR_CTX(var, msg))
#define MR_MSGBOX_WARN(var, msg)      MyResult::msgbox::warning    (_MR_CTX(var, msg))
#define MR_MSGBOX_WARN_END(var, msg)  MyResult::msgbox::warning_end(_MR_CTX(var, msg))
#define MR_MSGBOX_OK(msg)             MyResult::msgbox::normal     (msg)

// msgcls — originais
#define MR_CLS_ERR(var, msg)          MyResult::msgcls::error      (_MR_CTX(var, msg))
#define MR_CLS_ERR_END(var, msg)      MyResult::msgcls::error_end  (_MR_CTX(var, msg))
#define MR_CLS_WARN(var, msg)         MyResult::msgcls::warning    (_MR_CTX(var, msg))
#define MR_CLS_WARN_END(var, msg)     MyResult::msgcls::warning_end(_MR_CTX(var, msg))
#define MR_CLS_NORMAL(msg)            MyResult::msgcls::normal     (msg)

// ====================================================================
// Macros novas: passam SourceLocation como segundo argumento separado
// A formatação do prefixo "[file:line in func]" é feita dentro do .cpp
// ====================================================================

// msgbox — com localização
#define MR_MSGBOX_ERR_LOC(msg)        MyResult::msgbox::error      (msg, _MR_LOC())
#define MR_MSGBOX_ERR_END_LOC(msg)        MyResult::msgbox::error      (msg, _MR_LOC())
#define MR_MSGBOX_ERR_END_LOCATION(msg)    MyResult::msgbox::error_end  (msg, _MR_LOC())
#define MR_MSGBOX_WARN_LOC(msg)       MyResult::msgbox::warning    (msg, _MR_LOC())
#define MR_MSGBOX_WARN_END_LOC(msg)   MyResult::msgbox::warning_end(msg, _MR_LOC())
#define MR_MSGBOX_OK_LOC(msg)         MyResult::msgbox::normal     (msg, _MR_LOC())

// msgcls — com localização
#define MR_CLS_ERR_LOC(msg)           MyResult::msgcls::error      (msg, _MR_LOC())
#define MR_CLS_ERR_LOCATION(msg)           MyResult::msgcls::error      (msg, _MR_LOC())
#define MR_CLS_ERR_END_LOC(msg)       MyResult::msgcls::error_end  (msg, _MR_LOC())
#define MR_CLS_ERR_END_LOCATION(msg)       MyResult::msgcls::error_end  (msg, _MR_LOC())

#define MR_CLS_WARN_LOC(msg)          MyResult::msgcls::warning    (msg, _MR_LOC())
#define MR_CLS_WARN_END_LOC(msg)      MyResult::msgcls::warning_end(msg, _MR_LOC())
#define MR_CLS_NORMAL_LOC(msg)        MyResult::msgcls::normal     (msg, _MR_LOC())

#define MR_OK MyResult::ok

#define MR_IS_OK(result) ((result).success)
