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
    const wchar_t *file;     ///< __FILE__
    int line;                ///< __LINE__
    const wchar_t *function; ///< __func__
  };

  bool success;
  bool should_exit;
  ErrorType error_type;
  std::optional<std::wstring> message;

  // ----------------------------------------------------------------
  static const MyResult ok;
  // ----------------------------------------------------------------

  struct msgbox {
    // --- original overloads (message only) ---
    static MyResult errorW(const std::wstring &msgW = L"Erro desconhecido");
    static MyResult error_endW(const std::wstring &msgW = L"Erro desconhecido");
    static MyResult warningW(const std::wstring &msgW = L"Aviso");
    static MyResult warning_endW(const std::wstring &msgW = L"Aviso");
    static MyResult normalW(const std::wstring &msgW = L"");

    // --- new overloads (message + source location) ---
    static MyResult errorW(const std::wstring &msgW, const SourceLocation &loc);
    static MyResult error_endW(const std::wstring &msgW,
                               const SourceLocation &loc);
    static MyResult warningW(const std::wstring &msgW,
                             const SourceLocation &loc);
    static MyResult warning_endW(const std::wstring &msgW,
                                 const SourceLocation &loc);
    static MyResult normalW(const std::wstring &msgW,
                            const SourceLocation &loc);
  };

  // ----------------------------------------------------------------

  struct msgcls {
    // --- original overloads (message only) ---
    static MyResult errorW(const std::wstring &msgW = L"Erro desconhecido");
    static MyResult error_endW(const std::wstring &msgW = L"Erro desconhecido");
    static MyResult warningW(const std::wstring &msgW = L"Aviso");
    static MyResult warning_endW(const std::wstring &msgW = L"Aviso");
    static MyResult normalW(const std::wstring &msgW = L"");

    // --- new overloads (message + source location) ---
    static MyResult errorW(const std::wstring &msgW, const SourceLocation &loc);
    static MyResult error_endW(const std::wstring &msgW,
                               const SourceLocation &loc);
    static MyResult warningW(const std::wstring &msgW,
                             const SourceLocation &loc);
    static MyResult warning_endW(const std::wstring &msgW,
                                 const SourceLocation &loc);
    static MyResult normalW(const std::wstring &msgW,
                            const SourceLocation &loc);
  };

  // ----------------------------------------------------------------
  operator bool() const { return success; }

private:
  MyResult(bool ok, std::optional<std::wstring> msgW = std::nullopt,
           ErrorType type = ErrorType::None, bool exit = false);

  void handle_errorW();

  static constexpr const wchar_t *CLS_RED = L"\033[1;31m";
  static constexpr const wchar_t *CLS_YELLOW = L"\033[1;33m";
  static constexpr const wchar_t *CLS_WHITE = L"\033[0m";
  static constexpr const wchar_t *CLS_RESET = L"\033[0m";
};

// ====================================================================
// Helper interno: formata mensagem + localização
// ====================================================================
// Usado pelas macros *_LOC para montar a string final antes de chamar
// a sobrecarga que recebe SourceLocation.
#define WIDEN2(x) L ## x
#define WIDEN(x) WIDEN2(x)
#define __WFILE__ WIDEN(__FILE__)

#define _MR_LOC()                                                              \
  MyResult::SourceLocation { __WFILE__, __LINE__, ToWStr(__func__) }
#define MR_LOCATION _MR_LOC()

// ====================================================================
// Macros originais (apenas mensagem + contexto embutido na string)
// ====================================================================
#define _MR_CTX(varW, msgW)                                                    \
    (L"[" + ToWStr(__FILE__)) + L":" + std::to_wstring(__LINE__) + \
     L" in " + ToWStr(__func__) + L"] varW: " + ToWStr(#varW) + L" | " + TXT(msgW))

// msgbox — originais
#define MR_MSGBOX_ERR(var, msg)                                                \
  MyResult::msgbox::errorW(_MR_CTX(TXT(var), TXT(msg)))
#define MR_MSGBOX_ERR_END(var, msg)                                            \
  MyResult::msgbox::error_endW(_MR_CTX(TXT(var), TXT(msg)))
#define MR_MSGBOX_WARN(var, msg)                                               \
  MyResult::msgbox::warningW(_MR_CTX(TXT(var), TXT(msg)))
#define MR_MSGBOX_WARN_END(var, msg)                                           \
  MyResult::msgbox::warning_endW(_MR_CTX(TXT(var), TXT(msg)))
#define MR_MSGBOX_OK(msg) MyResult::msgbox::normalW(TXT(msg))
// msgcls — originais
#define MR_CLS_ERR(var, msg)                                                   \
  MyResult::msgcls::errorW(_MR_CTX(TXT(var), TXT(msg)))
#define MR_CLS_ERR_END(var, msg)                                               \
  MyResult::msgcls::error_endW(_MR_CTX(TXT(var), TXT(msg)))
#define MR_CLS_WARN(var, msg)                                                  \
  MyResult::msgcls::warningW(_MR_CTX(TXT(var), TXT(msg)))
#define MR_CLS_WARN_END(var, msg)                                              \
  MyResult::msgcls::warning_endW(_MR_CTX(TXT(var), TXT(msg)))
#define MR_CLS_NORMAL(msg) MyResult::msgcls::normalW(TXT(msg))

// ====================================================================
// Macros novas: passam SourceLocation como segundo argumento separado
// A formatação do prefixo "[file:line in func]" é feita dentro do .cpp
// ====================================================================

// msgbox — com localização
#define MR_MSGBOX_ERR_LOC(msg) MyResult::msgbox::errorW(TXT(msg), _MR_LOC())
#define MR_MSGBOX_ERR_END_LOC(msg) MyResult::msgbox::errorW(TXT(msg), _MR_LOC())
#define MR_MSGBOX_ERR_END_LOCATION(msg)                                        \
  MyResult::msgbox::error_endW(TXT(msg), _MR_LOC())
#define MR_MSGBOX_WARN_LOC(msg) MyResult::msgbox::warningW(TXT(msg), _MR_LOC())
#define MR_MSGBOX_WARN_END_LOC(msg)                                            \
  MyResult::msgbox::warning_endW(TXT(msg), _MR_LOC())
#define MR_MSGBOX_OK_LOC(msg) MyResult::msgbox::normalW(TXT(msg), _MR_LOC())

// msgcls — com localização
#define MR_CLS_ERR_LOC(msg) MyResult::msgcls::errorW(TXT(msg), _MR_LOC())
#define MR_CLS_ERR_LOCATION(msg) MyResult::msgcls::errorW(TXT(msg), _MR_LOC())
#define MR_CLS_ERR_END_LOC(msg)                                                \
  MyResult::msgcls::error_endW(TXT(msg), _MR_LOC())
#define MR_CLS_ERR_END_LOCATION(msg)                                           \
  MyResult::msgcls::error_end(TXT(msg), _MR_LOC())

#define MR_CLS_WARN_LOC(msg) MyResult::msgcls::warningW(TXT(msg), _MR_LOC())
#define MR_CLS_WARN_END_LOC(msg)                                               \
  MyResult::msgcls::warning_endW(TXT(msg), _MR_LOC())
#define MR_CLS_NORMAL_LOC(msg) MyResult::msgcls::normalW(TXT(msg), _MR_LOC())

#define MR_OK MyResult::ok

#define MR_IS_OK(result) ((result).success)
