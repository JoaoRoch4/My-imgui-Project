/**
 * @file MyResult.cpp
 * @brief Implementação de MyResult — classe de retorno unificado com suporte
 *        a mensagens de erro via MessageBox (Windows) ou console ANSI.
 *
 * A novidade nesta versão é a sobrecarga que recebe um @c SourceLocation,
 * permitindo que macros como @c MR_CLS_ERR_LOC injetem automaticamente
 * o arquivo, a linha e o nome da função onde o erro ocorreu, sem poluir
 * a mensagem de texto com concatenação manual.
 */

#include "pch.hpp"       // cabeçalho pré-compilado (std::string, iostream, etc.)
#include "MyResult.hpp"  // declaração da classe MyResult e de SourceLocation

 // ====================================================================
 // Função auxiliar interna (arquivo-local)
 // ====================================================================

 /**
  * @brief Formata um prefixo de localização a partir de um SourceLocation.
  *
  * Transforma @p loc nos campos file/line/function em uma string no formato:
  * @code
  *   [src/main.cpp:42 in MyFunction]
  * @endcode
  * Essa string é prefixada à mensagem original nas sobrecargas _loc.
  *
  * @param loc  Struct preenchida pelas macros via __FILE__, __LINE__, __func__.
  * @return     String formatada com o prefixo de localização.
  */
static std::string format_loc(const MyResult::SourceLocation& loc) {
    // Monta o prefixo concatenando os três campos do struct.
    // std::to_string converte o inteiro `line` para texto.
    return std::string("[")          // abre colchete
        + loc.file                  // caminho do arquivo fonte
        + ":"                       // separador
        + std::to_string(loc.line)  // número da linha
        + " in "                    // separador legível
        + loc.function              // nome da função/método
        + "] ";                     // fecha colchete + espaço antes da msg
}

// ====================================================================
// Construtor privado
// ====================================================================

/**
 * @brief Construtor privado — só pode ser chamado pelos factory methods.
 *
 * Todos os campos são inicializados via lista de inicialização, que é
 * mais eficiente do que atribuição dentro do corpo.
 *
 * @param ok    true  → operação bem-sucedida; false → falhou.
 * @param msg   Mensagem opcional (std::nullopt quando não há texto).
 * @param type  Tipo de exibição do erro (MessageBox ou console).
 * @param exit  Se true, chama std::exit(EXIT_FAILURE) após exibir a msg.
 */
MyResult::MyResult(bool bOk,
    std::optional<std::string> msg,
    ErrorType type,
    bool exit)
    : success(bOk)           // armazena se a operação foi bem-sucedida
    , message(std::move(msg)) // move a string para evitar cópia desnecessária
    , error_type(type)      // define como a mensagem será exibida
    , should_exit(exit)     // define se o programa deve encerrar
{
    // corpo vazio: toda a inicialização está na lista acima
}

// ====================================================================
// Singleton MyResult::ok
// ====================================================================

/**
 * @brief Instância estática que representa sucesso sem mensagem.
 *
 * Pode ser retornada diretamente por funções que simplesmente precisam
 * indicar "tudo certo" sem alocar um novo objeto:
 * @code
 *   return MyResult::ok;
 * @endcode
 */
const MyResult MyResult::ok = MyResult(true); // success=true, demais campos em default

// ====================================================================
// handle_error — exibe a mensagem conforme o ErrorType
// ====================================================================

/**
 * @brief Despacha a mensagem para o canal correto e, se necessário, encerra.
 *
 * Este método é chamado internamente por todos os factory methods logo
 * após a construção do objeto.  O switch lê @c error_type e decide entre
 * MessageBox ou saída colorida no console.
 *
 * Após o switch, se @c should_exit for true, chama std::exit para
 * terminar o processo com código de falha.
 */
void MyResult::handle_error() {
    // Extrai a mensagem; se nullopt, usa string vazia
    const std::string msg = message.value_or("");

    switch(error_type) {
        // ---- MessageBox (Windows) ----------------------------------

        case ErrorType::MsgBox_Error:
            // Exibe caixa de diálogo com ícone de erro (X vermelho)
            MessageBoxA(nullptr, msg.c_str(), "Erro", MB_OK | MB_ICONERROR);
            break;

        case ErrorType::MsgBox_Warning:
            // Exibe caixa de diálogo com ícone de aviso (triângulo amarelo)
            MessageBoxA(nullptr, msg.c_str(), "Aviso", MB_OK | MB_ICONWARNING);
            break;

        case ErrorType::MsgBox_Ok:
            // Exibe caixa de diálogo informativa (balão azul)
            MessageBoxA(nullptr, msg.c_str(), "Info", MB_OK | MB_ICONINFORMATION);
            break;

            // ---- Console (ANSI escape codes) ---------------------------

        case ErrorType::MsgCls_Error:
            // Imprime em vermelho negrito no stderr
            std::cerr << CLS_RED << "[ERRO] " << msg << CLS_RESET << "\n";
            break;

        case ErrorType::MsgCls_Warning:
            // Imprime em amarelo negrito no stdout
            std::cout << CLS_YELLOW << "[AVISO] " << msg << CLS_RESET << "\n";
            break;

        case ErrorType::MsgCls_Normal:
            // Imprime sem cor especial (reset = branco/padrão do terminal)
            std::cout << CLS_WHITE << msg << CLS_RESET << "\n";
            break;

        default:
            // ErrorType::None ou valor inesperado: não faz nada
            break;
    }

    // Se o chamador pediu encerramento forçado, mata o processo agora
    if(should_exit)
        std::exit(EXIT_FAILURE);
}

// ====================================================================
// msgbox — sobrecargas originais (apenas mensagem)
// ====================================================================

/**
 * @brief Exibe MessageBox de erro e retorna MyResult com success=false.
 * @param msg Texto exibido na caixa de diálogo.
 */
MyResult MyResult::msgbox::error(const std::string& msg) {
    MyResult r(false, msg, ErrorType::MsgBox_Error, false); // monta o objeto
    r.handle_error();  // exibe imediatamente
    return r;          // retorna para que o chamador possa verificar com if()
}

/**
 * @brief Exibe MessageBox de erro e encerra o processo.
 * @param msg Texto exibido na caixa de diálogo.
 * @note  Não retorna — std::exit é chamado dentro de handle_error.
 */
MyResult MyResult::msgbox::error_end(const std::string& msg) {
    MyResult r(false, msg, ErrorType::MsgBox_Error, true); // should_exit=true
    r.handle_error();
    return r; // nunca alcançado, mas necessário para o compilador
}

/**
 * @brief Exibe MessageBox de aviso e retorna MyResult com success=false.
 * @param msg Texto do aviso.
 */
MyResult MyResult::msgbox::warning(const std::string& msg) {
    MyResult r(false, msg, ErrorType::MsgBox_Warning, false);
    r.handle_error();
    return r;
}

/**
 * @brief Exibe MessageBox de aviso e encerra o processo.
 * @param msg Texto do aviso.
 */
MyResult MyResult::msgbox::warning_end(const std::string& msg) {
    MyResult r(false, msg, ErrorType::MsgBox_Warning, true);
    r.handle_error();
    return r;
}

/**
 * @brief Exibe MessageBox informativa e retorna MyResult com success=true.
 * @param msg Texto informativo.
 */
MyResult MyResult::msgbox::normal(const std::string& msg) {
    MyResult r(true, msg, ErrorType::MsgBox_Ok, false); // success=true pois é info
    r.handle_error();
    return r;
}

// ====================================================================
// msgbox — sobrecargas com SourceLocation
// ====================================================================

/**
 * @brief Versão de @c error que prefixa automaticamente a localização.
 *
 * Ao chamar via macro @c MR_MSGBOX_ERR_LOC("mensagem"), o compilador
 * preenche @p loc com __FILE__, __LINE__ e __func__ do ponto de chamada.
 * O prefixo formatado é concatenado antes da mensagem original.
 *
 * @param msg Texto do erro.
 * @param loc Localização do ponto de chamada (preenchida pela macro).
 */
MyResult MyResult::msgbox::error(const std::string& msg, const SourceLocation& loc) {
    // Junta o prefixo "[arquivo:linha in função] " com a mensagem do usuário
    const std::string full = format_loc(loc) + msg;

    MyResult r(false, full, ErrorType::MsgBox_Error, false);
    r.handle_error();
    return r;
}

/**
 * @brief Versão de @c error_end com prefixo de localização.
 * @param msg Texto do erro.
 * @param loc Localização do ponto de chamada.
 */
MyResult MyResult::msgbox::error_end(const std::string& msg, const SourceLocation& loc) {
    const std::string full = format_loc(loc) + msg; // prefixo + mensagem
    MyResult r(false, full, ErrorType::MsgBox_Error, true); // should_exit=true
    r.handle_error();
    return r;
}

/**
 * @brief Versão de @c warning com prefixo de localização.
 * @param msg Texto do aviso.
 * @param loc Localização do ponto de chamada.
 */
MyResult MyResult::msgbox::warning(const std::string& msg, const SourceLocation& loc) {
    const std::string full = format_loc(loc) + msg;
    MyResult r(false, full, ErrorType::MsgBox_Warning, false);
    r.handle_error();
    return r;
}

/**
 * @brief Versão de @c warning_end com prefixo de localização.
 * @param msg Texto do aviso.
 * @param loc Localização do ponto de chamada.
 */
MyResult MyResult::msgbox::warning_end(const std::string& msg, const SourceLocation& loc) {
    const std::string full = format_loc(loc) + msg;
    MyResult r(false, full, ErrorType::MsgBox_Warning, true);
    r.handle_error();
    return r;
}

/**
 * @brief Versão de @c normal com prefixo de localização.
 * @param msg Texto informativo.
 * @param loc Localização do ponto de chamada.
 */
MyResult MyResult::msgbox::normal(const std::string& msg, const SourceLocation& loc) {
    const std::string full = format_loc(loc) + msg;
    MyResult r(true, full, ErrorType::MsgBox_Ok, false); // success=true (info)
    r.handle_error();
    return r;
}

// ====================================================================
// msgcls — sobrecargas originais (apenas mensagem)
// ====================================================================

/**
 * @brief Imprime erro no console (vermelho) e retorna success=false.
 * @param msg Texto do erro.
 */
MyResult MyResult::msgcls::error(const std::string& msg) {
    MyResult r(false, msg, ErrorType::MsgCls_Error, false);
    r.handle_error();
    return r;
}

/**
 * @brief Imprime erro no console e encerra o processo.
 * @param msg Texto do erro.
 */
MyResult MyResult::msgcls::error_end(const std::string& msg) {
    MyResult r(false, msg, ErrorType::MsgCls_Error, true);
    r.handle_error();
    return r;
}

/**
 * @brief Imprime aviso no console (amarelo) e retorna success=false.
 * @param msg Texto do aviso.
 */
MyResult MyResult::msgcls::warning(const std::string& msg) {
    MyResult r(false, msg, ErrorType::MsgCls_Warning, false);
    r.handle_error();
    return r;
}

/**
 * @brief Imprime aviso no console e encerra o processo.
 * @param msg Texto do aviso.
 */
MyResult MyResult::msgcls::warning_end(const std::string& msg) {
    MyResult r(false, msg, ErrorType::MsgCls_Warning, true);
    r.handle_error();
    return r;
}

/**
 * @brief Imprime mensagem normal no console e retorna success=true.
 * @param msg Texto informativo.
 */
MyResult MyResult::msgcls::normal(const std::string& msg) {
    MyResult r(true, msg, ErrorType::MsgCls_Normal, false);
    r.handle_error();
    return r;
}

// ====================================================================
// msgcls — sobrecargas com SourceLocation
// ====================================================================

/**
 * @brief Imprime erro colorido no console com prefixo de localização.
 *
 * Exemplo de saída no terminal:
 * @code
 *   [ERRO] [src/loader.cpp:88 in load_file] arquivo não encontrado
 * @endcode
 *
 * @param msg Texto do erro.
 * @param loc Localização preenchida pela macro @c MR_CLS_ERR_LOC.
 */
MyResult MyResult::msgcls::error(const std::string& msg, const SourceLocation& loc) {
    const std::string full = format_loc(loc) + msg; // prefixo de localização + texto
    MyResult r(false, full, ErrorType::MsgCls_Error, false);
    r.handle_error(); // imprime "[ERRO] [arquivo:linha in func] mensagem"
    return r;
}

/**
 * @brief Imprime erro colorido no console com localização e encerra.
 * @param msg Texto do erro.
 * @param loc Localização do ponto de chamada.
 */
MyResult MyResult::msgcls::error_end(const std::string& msg, const SourceLocation& loc) {
    const std::string full = format_loc(loc) + msg;
    MyResult r(false, full, ErrorType::MsgCls_Error, true); // should_exit=true
    r.handle_error();
    return r;
}

/**
 * @brief Imprime aviso amarelo no console com prefixo de localização.
 * @param msg Texto do aviso.
 * @param loc Localização do ponto de chamada.
 */
MyResult MyResult::msgcls::warning(const std::string& msg, const SourceLocation& loc) {
    const std::string full = format_loc(loc) + msg;
    MyResult r(false, full, ErrorType::MsgCls_Warning, false);
    r.handle_error();
    return r;
}

/**
 * @brief Imprime aviso amarelo no console com localização e encerra.
 * @param msg Texto do aviso.
 * @param loc Localização do ponto de chamada.
 */
MyResult MyResult::msgcls::warning_end(const std::string& msg, const SourceLocation& loc) {
    const std::string full = format_loc(loc) + msg;
    MyResult r(false, full, ErrorType::MsgCls_Warning, true);
    r.handle_error();
    return r;
}

/**
 * @brief Imprime mensagem normal no console com prefixo de localização.
 * @param msg Texto informativo.
 * @param loc Localização do ponto de chamada.
 */
MyResult MyResult::msgcls::normal(const std::string& msg, const SourceLocation& loc) {
    const std::string full = format_loc(loc) + msg;
    MyResult r(true, full, ErrorType::MsgCls_Normal, false); // success=true
    r.handle_error();
    return r;
}
