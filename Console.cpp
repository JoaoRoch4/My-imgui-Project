/**
 * @file Console.cpp
 * @brief Implementação do console ImGui de debug — wide-char edition.
 *
 * DESPACHO DE COMANDOS COM ARGUMENTOS — CORREÇÃO PRINCIPAL
 * ----------------------------------------------------------
 * O problema original: ExecCommand uppercaseava a LINHA INTEIRA antes de
 * fazer o lookup. "theme dark" virava "THEME DARK" — chave inexistente.
 *
 * Correção: ExecCommand extrai apenas o PRIMEIRO token (até o primeiro espaço)
 * como chave de dispatch.  O handler registrado com a sobrecarga
 * void(vector<wstring>) lê InputBuf, tokeniza a linha completa e descarta
 * token[0] (nome do comando) para entregar apenas os argumentos ao handler.
 *
 * Exemplo de fluxo para "theme dark":
 *   ExecCommand("theme dark")
 *     → chave = L"THEME"              (primeiro token, uppercased)
 *     → DispatchTable["THEME"]()      (chama a lambda wrapper)
 *       → TokenizeLine(InputBuf)      = { L"theme", L"dark" }
 *       → args                        = { L"dark" }
 *       → handler(args)               → ImGui::StyleColorsDark()
 *
 * ESCALA DE FONTE EXCLUSIVA DO CONSOLE
 * ---------------------------------------
 * m_font_scale controla ImGui::SetWindowFontScale() APENAS dentro da região
 * de scroll. O tamanho global da UI não é afetado.  Botões "A+" / "A-" / "A="
 * na toolbar ajustam o valor; limites: [CONSOLE_FONT_SCALE_MIN, MAX].
 */

#include "pch.hpp"
#include "Console.hpp"
#include "Fontscale.hpp"

// ============================================================================
// Helpers de arquivo — tokenização e font emoji
// ============================================================================

/**
 * @brief Divide uma linha larga em tokens separados por espaços/tabs.
 *
 * Separadores consecutivos são ignorados — tokens nunca ficam vazios.
 *
 *   L"theme dark"     → { L"theme", L"dark" }
 *   L"theme  clear "  → { L"theme", L"clear" }
 *   L"exit"           → { L"exit" }
 *
 * @param line  Linha larga de entrada (não modificada).
 * @return      Tokens em ordem de aparição.
 */
[[nodiscard]] static std::vector<std::wstring> TokenizeLine(const std::wstring& line) {
	std::vector<std::wstring> tokens; // vetor resultado
	std::wstring			  token;  // acumulador do token atual

	for (const wchar_t ch : line) {
		if (ch == L' ' || ch == L'\t') // separador
		{
			if (!token.empty()) {
				tokens.push_back(std::move(token)); // salva token completo
				token.clear();						// reinicia acumulador
			}
			// separadores consecutivos são ignorados
		} else {
			token += ch; // acumula caractere no token atual
		}
	}

	if (!token.empty()) tokens.push_back(std::move(token)); // salva último token

	return tokens; // NRVO — sem cópia extra
}

/**
 * @brief Testa se um arquivo existe e é legível.
 * @param path  Caminho UTF-8 terminado em nulo.
 * @return      true se fopen() teve sucesso.
 */
[[nodiscard]] static bool FileExists(const char* path) {
	FILE* f = fopen(path, "rb"); // abre em modo binário para leitura
	if (!f) return false;
	fclose(f);
	return true;
}

/**
 * @brief Constrói lista ordenada de candidatos de fonte emoji por plataforma.
 *
 * Windows  → Segoe UI Emoji / Segoe UI Symbol
 * macOS    → Apple Color Emoji
 * Linux    → Noto Color Emoji, Noto Emoji, Symbola (vários caminhos de distro)
 *
 * @return  Caminhos absolutos em ordem de preferência.
 */
[[nodiscard]] static std::vector<std::string> GetCandidateEmojiPaths() {
	std::vector<std::string> candidates;

#if defined(_WIN32)
	char windir[MAX_PATH] = {}; // buffer WinAPI deve ser char*

	if (GetWindowsDirectoryA(windir, MAX_PATH) > 0) {
		candidates.push_back(std::string(windir) +
							 "\\Fonts\\seguiemj.ttf"); // Segoe UI Emoji (colorido)
		candidates.push_back(std::string(windir) +
							 "\\Fonts\\seguisym.ttf"); // Segoe UI Symbol (monocromático)
	}
	// Fallbacks hard-coded caso GetWindowsDirectoryA falhe
	candidates.push_back("C:\\Windows\\Fonts\\seguiemj.ttf");
	candidates.push_back("C:\\Windows\\Fonts\\seguisym.ttf");

#elif defined(__APPLE__)
	candidates.push_back("/System/Library/Fonts/Apple Color Emoji.ttc");
	candidates.push_back("/Library/Fonts/Apple Color Emoji.ttc");

#else
	candidates.push_back("/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf");
	candidates.push_back("/usr/share/fonts/noto/NotoColorEmoji.ttf");
	candidates.push_back("/usr/share/fonts/google-noto-emoji/NotoColorEmoji.ttf");
	candidates.push_back("/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf");
	candidates.push_back("/usr/share/fonts/noto/NotoEmoji-Regular.ttf");
	candidates.push_back("/usr/share/fonts/truetype/ancient-scripts/Symbola_hint.ttf");
	candidates.push_back("/usr/share/fonts/TTF/Symbola.ttf");
#endif

	return candidates;
}

// ============================================================================
// Console::GetEmojiGlyphRanges  (static público)
// ============================================================================

/**
 * @brief Retorna a tabela estática de ranges ImWchar para emoji e símbolos.
 *
 * Passe como 4º parâmetro de AddFontFromFileTTF sempre que carregar uma
 * fonte emoji.  Sem este parâmetro o ImGui rasteriza apenas Basic Latin,
 * independentemente do conteúdo do arquivo .ttf.
 *
 * Ranges cobertos:
 *  U+00A0-U+00FF  Latin-1 Supplement
 *  U+2000-U+27BF  Pontuação geral + Símbolos variados (✓ ⚠ ★)
 *  U+2B00-U+2BFF  Símbolos e Setas variados
 *  U+1F300-U+1F9FF Bloco Emoji principal (🚀 🎯 💻 😀)
 *
 * @return Ponteiro para array estático terminado em dois zeros (sentinela ImGui).
 */
const ImWchar* Console::GetEmojiGlyphRanges() {
	// ImWchar = unsigned int; sentinela duplo exigido pelo ImGui.
	static const ImWchar s_ranges[] = {
		static_cast<ImWchar>(0x00A0),  static_cast<ImWchar>(0x00FF),  // Latin-1 Supplement
		static_cast<ImWchar>(0x2000),  static_cast<ImWchar>(0x27BF),  // Pontuação + Símbolos
		static_cast<ImWchar>(0x2B00),  static_cast<ImWchar>(0x2BFF),  // Símbolos e Setas
		static_cast<ImWchar>(0x1F300), static_cast<ImWchar>(0x1F9FF), // Bloco Emoji principal
		static_cast<ImWchar>(0),	   static_cast<ImWchar>(0)		  // sentinela obrigatório
	};
	return s_ranges;
}

// ============================================================================
// Console::LoadEmojiFont  (static público)
// ============================================================================

/**
 * @brief Auto-detecta a fonte emoji do sistema e mescla no atlas ImGui.
 *
 * Deve ser chamado UMA VEZ: APÓS ImGui::CreateContext() + Init dos backends,
 * ANTES do primeiro ImGui::NewFrame().
 *
 * Fluxo:
 *  1. AddFontDefault() → carrega ProggyClean como fonte base ASCII.
 *  2. Sonda GetCandidateEmojiPaths() pelo primeiro arquivo existente.
 *  3. AddFontFromFileTTF() com MergeMode=true mescla apenas os ranges
 *     emoji/símbolo no slot de fonte recém-criado.
 *
 * @param font_size  Tamanho em pixels para a fonte base e o overlay emoji.
 */
void Console::LoadEmojiFont(float font_size) {
	ImGuiIO& io = ImGui::GetIO(); // estado central do ImGui; possui o atlas de fontes

	io.Fonts->AddFontDefault(); // fonte base: ProggyClean (ASCII apenas)

	// Configuração de mesclagem — insere glifos NO slot de fonte anterior
	ImFontConfig merge_cfg;
	merge_cfg.MergeMode		  = true; // mescla no slot criado por AddFontDefault()
	merge_cfg.OversampleH	  = 1;	  // 1x suficiente para emoji grandes; economiza memória
	merge_cfg.OversampleV	  = 1;	  // idem
	merge_cfg.GlyphOffset.y	  = 1.0f; // empurra emoji 1px para baixo (alinhamento de baseline)
	merge_cfg.PixelSnapH	  = true; // alinha à grade de pixels; evita borramento
	merge_cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LoadColor; // emoji coloridos

	const ImWchar* emoji_ranges = GetEmojiGlyphRanges(); // tabela de codepoints a rasterizar

	bool loaded = false;
	for (const std::string& path : GetCandidateEmojiPaths()) {
		if (!FileExists(path.c_str())) continue; // pula arquivos ausentes silenciosamente

		ImFont* result =
			io.Fonts->AddFontFromFileTTF(path.c_str(), // caminho UTF-8 exigido pelo ImGui
										 font_size, &merge_cfg,
										 emoji_ranges // const ImWchar* — codepoints a rasterizar
			);

		if (result != nullptr) {
			printf("[Console] Emoji font loaded: %s\n", path.c_str());
			loaded = true;
			break;
		}
	}

	if (!loaded) {
		printf("[Console] WARNING: No emoji font found. Emoji will show as '?'.\n");
		printf("[Console] Install Segoe UI Emoji (Win) / Noto Emoji (Linux).\n");
	}
}

// ============================================================================
// Helpers de encoding  (wchar_t ↔ UTF-8)
// ============================================================================

/**
 * @brief Converte uma wchar_t* terminada em nulo para std::string UTF-8.
 *
 * Windows  : usa WideCharToMultiByte (wchar_t = UTF-16).
 * Linux/macOS : codifica manualmente cada codepoint UCS-4 de 32 bits.
 *
 * @param wstr  String wide de entrada; nullptr retorna "".
 * @return      std::string codificada em UTF-8.
 */
std::string Console::WideToUtf8(const wchar_t* wstr) {
	if (!wstr) return ""; // guarda contra nullptr

#ifdef _WIN32
	// Windows: o SO trata surrogates UTF-16 automaticamente
	int needed =
		WideCharToMultiByte(CP_UTF8, 0, // página de código alvo: UTF-8
							wstr, -1,	// fonte: string wide terminada em nulo
							nullptr, 0, // primeira chamada: consulta o número de bytes necessários
							nullptr, nullptr);

	if (needed <= 1) return ""; // string vazia ou erro de conversão

	std::string result(needed - 1, '\0'); // aloca sem o byte NUL
	WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], needed, nullptr, nullptr);
	return result;

#else
	// Linux/macOS: wchar_t é UCS-4 de 32 bits; codifica cada codepoint manualmente
	std::string result;
	while (*wstr) {
		const uint32_t cp = static_cast<uint32_t>(*wstr++); // valor do codepoint Unicode

		if (cp < 0x80u) {
			// 1 byte: 0xxxxxxx
			result += static_cast<char>(cp);
		} else if (cp < 0x800u) {
			// 2 bytes: 110xxxxx 10xxxxxx
			result += static_cast<char>(0xC0u | (cp >> 6));
			result += static_cast<char>(0x80u | (cp & 0x3Fu));
		} else if (cp < 0x10000u) {
			// 3 bytes: 1110xxxx 10xxxxxx 10xxxxxx
			result += static_cast<char>(0xE0u | (cp >> 12));
			result += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
			result += static_cast<char>(0x80u | (cp & 0x3Fu));
		} else {
			// 4 bytes: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
			result += static_cast<char>(0xF0u | (cp >> 18));
			result += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
			result += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
			result += static_cast<char>(0x80u | (cp & 0x3Fu));
		}
	}
	return result;
#endif
}

/**
 * @brief Converte uma char* UTF-8 terminada em nulo para std::wstring.
 *
 * Windows  : usa MultiByteToWideChar.
 * Linux/macOS : usa mbstowcs com o locale; chame setlocale(LC_ALL,"") uma vez
 *               no startup para parsing correto de multi-byte.
 *
 * @param str  String UTF-8 de entrada; nullptr retorna L"".
 * @return     std::wstring.
 */
std::wstring Console::Utf8ToWide(const char* str) {
	if (!str) return L""; // guarda contra nullptr

#ifdef _WIN32
	const int needed = MultiByteToWideChar(
		CP_UTF8, 0,	 // página de código fonte: UTF-8
		str, -1,	 // fonte: string UTF-8 terminada em nulo
		nullptr, 0); // primeira chamada: consulta o número de wchar_t necessários

	if (needed <= 0) return L"";

	std::wstring result(needed - 1, L'\0'); // aloca sem o NUL
	MultiByteToWideChar(CP_UTF8, 0, str, -1, &result[0], needed);
	return result;

#else
	// Linux/macOS: mbstowcs decodifica a codificação multi-byte do locale.
	// O locale deve suportar UTF-8 (chame setlocale(LC_ALL,"") no startup).
	size_t needed = mbstowcs(nullptr, str, 0); // consulta o tamanho necessário

	if (needed == static_cast<size_t>(-1)) return L""; // erro de conversão

	std::wstring result(needed, L'\0');
	mbstowcs(&result[0], str, needed + 1); // escreve wchar_t incluindo NUL
	return result;
#endif
}

// ============================================================================
// Helpers de string wide-char
// ============================================================================

/**
 * @brief strcmp case-insensitive para wide-char.
 *
 * Usa towupper() para case folding com suporte a letras acentuadas.
 *
 * @return 0 se iguais (case-insensitive); ≠0 na primeira diferença.
 */
int Console::Wcsicmp(const wchar_t* s1, const wchar_t* s2) {
	int d;
	while ((d = static_cast<int>(std::towupper(*s2)) - static_cast<int>(std::towupper(*s1))) == 0 &&
		   *s1) {
		s1++; // avança no primeiro operando
		s2++; // avança no segundo operando
	}
	return d; // 0 → iguais; ±N → primeira diferença
}

/**
 * @brief strncmp case-insensitive para wide-char (no máximo n caracteres).
 *
 * @return 0 se os primeiros n caracteres coincidem; ≠0 caso contrário.
 */
int Console::Wcsnicmp(const wchar_t* s1, const wchar_t* s2, int n) {
	int d = 0;
	while (n > 0 &&
		   (d = static_cast<int>(towupper(static_cast<wint_t>(*s2))) -
				static_cast<int>(towupper(static_cast<wint_t>(*s1)))) == 0 &&
		   *s1) {
		s1++; // avança s1
		s2++; // avança s2
		n--;  // decrementa contador de caracteres restantes
	}
	return d;
}

/**
 * @brief Duplica uma string wide usando o alocador do ImGui.
 *
 * Aloca (wcslen(s)+1) * sizeof(wchar_t) bytes via ImGui::MemAlloc para
 * que ImGui::MemFree possa liberá-la corretamente.
 *
 * @param s  String wide de origem (não pode ser nulo).
 * @return   Cópia no heap; o chamador deve chamar ImGui::MemFree().
 */
wchar_t* Console::Wcsdup(const wchar_t* s) {
	IM_ASSERT(s); // crash em builds Debug se s for nulo

	const size_t len   = wcslen(s) + 1;			 // contagem de wchar_t incluindo NUL
	const size_t bytes = len * sizeof(wchar_t);	 // contagem de bytes para wmemcpy
	void*		 buf   = ImGui::MemAlloc(bytes); // aloca via heap do ImGui

	IM_ASSERT(buf); // crash em falha de alocação

	return std::bit_cast<wchar_t*>(
		wmemcpy(std::bit_cast<wchar_t*>(buf), s, len)); // copia len wchar_t
}

/**
 * @brief Remove espaços L' ' finais de uma wide string in-place.
 *
 * @param s  String wide a aparar (modificada in-place).
 */
void Console::Wcstrim(wchar_t* s) {
	wchar_t* end = s + wcslen(s); // ponteiro um além do último wchar_t

	// recua enquanto estiver dentro da string e o caractere for espaço
	while (end > s && end[-1] == L' ') end--;

	*end = L'\0'; // escreve NUL terminador wide
}

// ============================================================================
// Registro de comandos
// ============================================================================

/**
 * @brief Valida que @p name existe em BuiltInCommands e então registra.
 *
 * Protege contra typos: se o nome não estiver no array compile-time,
 * um erro é logado e o registro é ignorado.
 */
void Console::RegisterBuiltIn(std::wstring_view name, std::function<void()> func) {
	bool found = false;

	for (const auto& cmd : BuiltInCommands) {
		if (cmd.name == name) // comparação wstring_view — sem alocação
		{
			found = true;
			break;
		}
	}

	if (!found) {
		AddLog(L"[error]Tentativa de registrar '%ls' não listado em BuiltInCommands![/]",
			   name.data());
		return;
	}

	RegisterCommand(std::wstring(name), func); // delega ao registro principal
}

/**
 * @brief Registro principal: lista de autocomplete + tabela de despacho.
 *
 * O nome é armazenado em wchar_t* no vetor Commands (autocomplete) e como
 * std::wstring UPPERCASE em DispatchTable (lookup O(log n)).
 *
 * @param name  Nome do comando (qualquer capitalização).
 * @param func  Callable a invocar na execução.
 */
void Console::RegisterCommand(const std::wstring& name, std::function<void()> func) {
	// ---- 1. Adiciona ao autocomplete se ainda não presente ----------------
	wchar_t* cmd_wcs		= Wcsdup(name.c_str()); // cópia wide no heap para o autocomplete
	bool	 already_exists = false;

	for (int i = 0; i < Commands.Size; i++) {
		if (Wcsicmp(Commands[i], cmd_wcs) == 0) // verificação de duplicata case-insensitive
		{
			already_exists = true;
			break;
		}
	}

	if (!already_exists) Commands.push_back(cmd_wcs); // armazena ponteiro no vetor de autocomplete
	else ImGui::MemFree(cmd_wcs);					  // libera duplicata — já está na lista

	// ---- 2. Uppercase da chave para a tabela de despacho -----------------
	std::wstring key = name;
	std::transform(key.begin(), key.end(), key.begin(),
				   [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });

	// ---- 3. Insere ou substitui a entrada na tabela de despacho ----------
	DispatchTable[key] = func; // sobrescreve se o comando já existia
}

/**
 * @brief Registra um comando com descrição wide (sem argumentos).
 */
void Console::RegisterCommand(const std::wstring& name, const std::wstring& desc,
							  std::function<void()> func) {
	RegisterCommand(name, func); // registra a lógica + autocomplete primeiro

	// Armazena a descrição com a mesma chave UPPERCASE
	std::wstring key = name;
	std::transform(key.begin(), key.end(), key.begin(),
				   [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });

	HelpDescriptions[key] = desc; // descrição exibida pelo comando HELP
}

/**
 * @brief Registra um comando cujo handler recebe argumentos em runtime.
 *
 * Cria um wrapper void() que:
 *  1. Lê InputBuf (wchar_t[]) — o texto bruto digitado pelo usuário.
 *  2. Tokeniza a linha inteira com TokenizeLine().
 *  3. Descarta token[0] (nome do comando).
 *  4. Entrega os tokens restantes como std::vector<std::wstring> ao handler.
 *
 * O DispatchTable mantém tipo uniforme std::map<wstring, function<void()>>,
 * sem quebrar a infra existente.
 */
void Console::RegisterCommand(const std::wstring&							 name,
							  std::function<void(std::vector<std::wstring>)> func) {
	// Captura 'func' por valor e 'this' para acessar InputBuf.
	// O lambda resultante é void() — compatível com DispatchTable.
	RegisterCommand(name, [this, func]() {
		// 1. Copia o buffer de entrada wide para uma wstring.
		//    InputBuf ainda contém "theme dark" quando este lambda executa.
		const std::wstring raw_line(InputBuf);

		// 2. Tokeniza: "theme dark" → { L"theme", L"dark" }
		std::vector<std::wstring> tokens = TokenizeLine(raw_line);

		// 3. Constrói o vetor de argumentos descartando token[0] (nome do comando).
		std::vector<std::wstring> args;

		if (tokens.size() > 1) // há pelo menos um argumento além do nome
		{
			args.assign(tokens.begin() + 1, // primeiro argumento real (índice 1)
						tokens.end());		// até o fim do vetor de tokens
		}
		// se tokens.size() <= 1, args fica vazio — o handler trata o caso

		func(std::move(args)); // entrega ao handler; move evita cópia desnecessária
	});
}

/**
 * @brief Registra um comando com descrição wide + handler com argumentos.
 */
void Console::RegisterCommand(const std::wstring& name, const std::wstring& desc,
							  std::function<void(std::vector<std::wstring>)> func) {
	RegisterCommand(name, func); // registra lógica + autocomplete + wrapper

	// Calcula chave UPPERCASE para HelpDescriptions — mesmo padrão das demais sobrecargas
	std::wstring key = name;
	std::transform(key.begin(), key.end(), key.begin(),
				   [](const wchar_t c) { return static_cast<wchar_t>(towupper(c)); });

	HelpDescriptions[key] = desc; // armazena descrição para o HELP
}

// ============================================================================
// Construtor / Destrutor
// ============================================================================

/**
 * @brief Inicializa o console e registra os quatro comandos internos.
 *
 * m_font_scale começa em 1.0f (100%) — escala neutra, sem magnificação.
 */
Console::Console() :
HistoryPos(-1),
AutoScroll(true),
ScrollToBottom(false),
m_font_scale(1.0f) // escala neutra — sem amplificação inicial
{
	ClearLog();
	wmemset(InputBuf, L'\0', IM_COUNTOF(InputBuf));	  // zera buffer wide de entrada
	memset(InputBufUtf8, '\0', sizeof(InputBufUtf8)); // zera buffer UTF-8 de staging

	AddLog(L"Bem-vindo ao Console ImGui!");
	AddLog(L"Digite '[yellow]HELP[/]' para ver a lista de comandos.");
	AddLog(L"[gray]Dica: use A+ / A- na toolbar para ajustar o tamanho do texto.[/]");

	// CLEAR — limpa o log
	RegisterBuiltIn(L"CLEAR", [this]() { this->ClearLog(); });

	// HELP — lista todos os comandos registrados
	RegisterBuiltIn(L"HELP", [this]() {
		AddLog(L"--- [yellow]Comandos Internos[/] ---");
		for (const auto& cmd : BuiltInCommands)
			AddLog(L"- [yellow]%ls[/]: %ls", cmd.name.data(), cmd.description.data());

		AddLog(L"\n--- [cyan]Comandos do Sistema[/] ---");
		for (const auto& pair : DispatchTable) {
			const std::wstring& cmd_name = pair.first; // já em UPPERCASE

			bool is_builtin = false;
			for (const auto& b : BuiltInCommands)
				if (b.name == cmd_name) {
					is_builtin = true;
					break;
				}

			if (!is_builtin) {
				auto it = HelpDescriptions.find(cmd_name);
				if (it != HelpDescriptions.end())
					AddLog(L"- [cyan]%ls[/]: %ls", cmd_name.c_str(), it->second.c_str());
				else AddLog(L"- [cyan]%ls[/]", cmd_name.c_str());
			}
		}
	});

	// HISTORY — mostra as últimas 10 entradas do histórico
	RegisterBuiltIn(L"HISTORY", [this]() {
		int first = History.Size - 10; // no máximo as últimas 10 entradas
		for (int i = first > 0 ? first : 0; i < History.Size; i++)
			AddLog(L"%3d: %ls\n", i, History[i]);
	});

	// EXIT — placeholder, deve ser sobrescrito pelo App
	RegisterBuiltIn(
		L"EXIT", [this]() { AddLog(L"[error]A lógica de saída deve ser sobrescrita no App![/]"); });
}

/**
 * @brief Libera todas as wstrings alocadas no heap em Items e History.
 */
Console::~Console() {
	ClearLog(); // libera cada Items[i] via ImGui::MemFree

	for (int i = 0; i < History.Size; i++)
		ImGui::MemFree(History[i]); // cada entrada foi alocada com Wcsdup
}

// ============================================================================
// Gerenciamento de log
// ============================================================================

/**
 * @brief Libera todos os itens de log e limpa o vetor Items.
 */
void Console::ClearLog() {
	for (int i = 0; i < Items.Size; i++)
		ImGui::MemFree(Items[i]); // alocado com Wcsdup — deve usar MemFree

	Items.clear();
}

/**
 * @brief Formata uma wide string (estilo vswprintf) e adiciona ao log.
 *
 * A string formatada é duplicada no heap do ImGui (via Wcsdup) para
 * persistir entre frames.
 *
 * @param fmt  String de formato wide, ex.: L"Score: %d  Player: %ls".
 * @param ...  Argumentos variádicos compatíveis com o formato.
 */
void Console::AddLog(const wchar_t* fmt, ...) {
	wchar_t buf[1024]; // buffer wide de stack para o texto formatado
	va_list args;

	va_start(args, fmt);
	vswprintf(buf, IM_COUNTOF(buf), fmt, args); // vprintf wide-char seguro
	buf[IM_COUNTOF(buf) - 1] = L'\0';			// garante terminação NUL wide
	va_end(args);

	Items.push_back(Wcsdup(buf)); // duplica no heap e armazena
}

// ============================================================================
// Escala de fonte exclusiva do console
// ============================================================================

/**
 * @brief Aumenta a escala de fonte do console em CONSOLE_FONT_SCALE_STEP.
 *
 * Clampeado a CONSOLE_FONT_SCALE_MAX (3.0).
 * Não altera ImGuiStyle global — apenas m_font_scale interno.
 */
void Console::IncreaseFontScale() {
	m_font_scale += CONSOLE_FONT_SCALE_STEP; // adiciona um incremento

	if (m_font_scale > CONSOLE_FONT_SCALE_MAX)
		m_font_scale = CONSOLE_FONT_SCALE_MAX; // clamp no limite superior
}

/**
 * @brief Diminui a escala de fonte do console em CONSOLE_FONT_SCALE_STEP.
 *
 * Clampeado a CONSOLE_FONT_SCALE_MIN (0.5).
 */
void Console::DecreaseFontScale() {
	m_font_scale -= CONSOLE_FONT_SCALE_STEP; // subtrai um incremento

	if (m_font_scale < CONSOLE_FONT_SCALE_MIN)
		m_font_scale = CONSOLE_FONT_SCALE_MIN; // clamp no limite inferior
}

/**
 * @brief Restaura a escala de fonte do console para 1.0 (100%).
 */
void Console::ResetFontScale() {
	m_font_scale = 1.0f; // escala neutra — sem magnificação
}

/**
 * @brief Retorna a escala de fonte atual do console.
 * @return Valor em [CONSOLE_FONT_SCALE_MIN, CONSOLE_FONT_SCALE_MAX].
 */
float Console::GetFontScale() const noexcept { return m_font_scale; }

// ============================================================================
// Execução de comandos
// ============================================================================

/**
 * @brief Analisa e executa uma string de comando wide-char.
 *
 * CORREÇÃO PRINCIPAL — extração do primeiro token como chave:
 *
 *   Antes: cmd = "THEME DARK" → DispatchTable.find("THEME DARK") → não encontrado
 *   Agora: cmd = "THEME"      → DispatchTable.find("THEME")      → encontrado ✓
 *
 * Fluxo:
 *  1. Ecoa a entrada bruta no log (prefixada com L'#').
 *  2. Deduplica e atualiza o Histórico.
 *  3. Extrai o PRIMEIRO token da linha como chave de lookup.
 *  4. Faz uppercase na chave.
 *  5. Chama a lambda registrada ou loga "comando desconhecido".
 *
 * @param command_line  Comando digitado pelo usuário (wide-char).
 */
void Console::ExecCommand(const wchar_t* command_line) {
	AddLog(L"# %ls\n", command_line); // eco com prefixo wide '#'

	// ---- 1. Histórico: remove duplicata e acrescenta no final ------------
	HistoryPos = -1; // reseta cursor de navegação

	for (int i = History.Size - 1; i >= 0; i--) {
		if (Wcsicmp(History[i], command_line) == 0) {
			ImGui::MemFree(History[i]);			// libera a duplicata wide
			History.erase(History.begin() + i); // remove do vetor
			break;
		}
	}

	History.push_back(Wcsdup(command_line)); // acrescenta cópia fresca no final

	// ---- 2. Extrai APENAS o primeiro token como chave de despacho --------
	//
	// ANTES (bug): a linha inteira era uppercased e usada como chave.
	//              "theme dark" → "THEME DARK" → não encontrava "THEME".
	//
	// AGORA (fix): tokeniza e usa apenas token[0] como chave.
	//              "theme dark" → tokens[0] = "theme" → uppercase = "THEME" → ✓
	//
	const std::wstring full_line(command_line);			 // cópia da linha completa
	const auto		   tokens = TokenizeLine(full_line); // divide em tokens

	if (tokens.empty()) {
		ScrollToBottom = true; // nada a executar — apenas rola
		return;
	}

	// Uppercase do primeiro token (nome do comando)
	std::wstring cmd_key = tokens[0];
	std::transform(cmd_key.begin(), cmd_key.end(), cmd_key.begin(),
				   [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });

	// ---- 3. Lookup O(log n) e despacho -----------------------------------
	const auto it = DispatchTable.find(cmd_key);

	if (it != DispatchTable.end()) it->second(); // chama a lambda registrada
	else AddLog(L"Comando desconhecido: '[error]%ls[/]'\n", command_line);

	ScrollToBottom = true; // rola para mostrar o resultado do comando
}

// ============================================================================
// Renderização ImGui
// ============================================================================

/**
 * @brief Renderiza a janela do console. Chame uma vez por frame.
 *
 * ESTRUTURA DA JANELA:
 *  ┌─ Toolbar (Clear, Copy, A+, A-, A=, escala%, Filter) ──────────────┐
 *  │  Região de scroll (SetWindowFontScale aplica m_font_scale aqui)   │
 *  └─ Input de comando ────────────────────────────────────────────────┘
 *
 * Fronteira ImGui:
 *  - O título wide é convertido para UTF-8 em ImGui::Begin.
 *  - Cada Items[i] (wchar_t*) é convertido para UTF-8 em TextUnformatted.
 *  - InputText usa InputBufUtf8 (char[]); no submit é reconvertido para wide.
 *
 * @param title   Título wide da janela.
 * @param p_open  Ponteiro para bool controlando visibilidade.
 */
void Console::Draw(const wchar_t* title, bool* p_open) {
	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);

	// Converte o título wide para UTF-8 — ImGui::Begin exige char*
	const std::string title_utf8 = WideToUtf8(title);

	if (!ImGui::Begin(title_utf8.c_str(), p_open)) {
		ImGui::End(); // End() DEVE ser chamado mesmo quando a janela está colapsada
		return;
	}
	ImGuiIO& io = ImGui::GetIO(); // IO global do ImGui — acesso ao MouseWheel acumulado

    // ImGuiHoveredFlags_RootAndChildWindows cobre a janela principal
    // e o child "ScrollingRegion" com um único teste.
    const bool console_hovered =
        ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

    // Informa FontScale::ProcessEvent() para o PRÓXIMO frame.
    // Quando true, ProcessEvent() pula o Ctrl+Scroll global.
    FontScale::SetScrollSuppressed(console_hovered);

    // Se o console está em foco E Ctrl está pressionado E há scroll:
    // aplica a escala LOCAL do console (não toca no ImGuiStyle global).
    if(console_hovered && io.KeyCtrl && io.MouseWheel != 0.0f)
    {
        if(io.MouseWheel > 0.0f)
            IncreaseFontScale();  // += CONSOLE_FONT_SCALE_STEP, clamp em MAX
        else
            DecreaseFontScale();  // -= CONSOLE_FONT_SCALE_STEP, clamp em MIN

        // Consome o MouseWheel para que o ImGui não o use para scroll
        // da região (sem isso o conteúdo rola E a fonte aumenta ao mesmo tempo).
        io.MouseWheel = 0.0f;
    }



	// Menu de contexto na barra de título → fechar o console
	if (ImGui::BeginPopupContextItem()) {
		if (ImGui::MenuItem("Close Console")) *p_open = false;
		ImGui::EndPopup();
	}

	// ---- Toolbar ---------------------------------------------------------

	if (ImGui::SmallButton("Clear")) // limpa todos os itens de log
		ClearLog();


	ImGui::SameLine();
	const bool copy_to_clipboard = ImGui::SmallButton("Copy"); // copia o log

	ImGui::SameLine();
	ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); // separador visual

	// ---- Botões de escala de fonte exclusiva do console -----------------
	//
	// Estes botões afetam APENAS SetWindowFontScale() dentro da região de
	// scroll deste console — o ImGuiStyle global NÃO é modificado.

	ImGui::SameLine();

	// Botão "A+" — aumenta o texto do console
	if (ImGui::SmallButton("A+"))
		IncreaseFontScale(); // += CONSOLE_FONT_SCALE_STEP, clampado em MAX
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Aumentar texto do console");

	ImGui::SameLine();

	// Botão "A-" — diminui o texto do console
	if (ImGui::SmallButton("A-"))
		DecreaseFontScale(); // -= CONSOLE_FONT_SCALE_STEP, clampado em MIN
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Diminuir texto do console");

	ImGui::SameLine();

	// Botão "A=" — restaura escala para 100%
	if (ImGui::SmallButton("A=")) ResetFontScale(); // m_font_scale = 1.0f
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restaurar tamanho padrão (100%%)");

	// Exibe o percentual atual como texto desabilitado (cinza, não interativo)
	ImGui::SameLine();
	ImGui::TextDisabled("%.0f%%", m_font_scale * 100.0f); // ex.: "130%"

	ImGui::SameLine();
	ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); // separador visual

	ImGui::SameLine();
	Filter.Draw("Filter", 180); // widget de filtro (UTF-8 interno)

	ImGui::Separator();

	// ---- Região de scroll -----------------------------------------------

	const float footer_height = ImGui::GetStyle().ItemSpacing.y
                           + ImGui::GetFrameHeightWithSpacing();

ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // linhas compactas

if(ImGui::BeginChild("ScrollingRegion",
                      ImVec2(0, -footer_height), false,
                      ImGuiWindowFlags_HorizontalScrollbar))
{
    if(ImGui::BeginPopupContextWindow())
    {
        if(ImGui::Selectable("Clear")) ClearLog();
        ImGui::EndPopup();
    }

    // PushFont(nullptr, size): nullptr = fonte atual, size = novo tamanho.
    // Afeta APENAS o conteúdo deste BeginChild — sem tocar no ImGuiStyle global.
    const float scaled_size = ImGui::GetStyle().FontSizeBase * m_font_scale;
    ImGui::PushFont(nullptr, scaled_size);

    if(copy_to_clipboard) ImGui::LogToClipboard();

    for(wchar_t* item : Items)
    {
        const std::string item_utf8 = WideToUtf8(item);
        if(!Filter.PassFilter(item_utf8.c_str())) continue;
        RenderTermicolor(item);
    }

    if(copy_to_clipboard) ImGui::LogFinish();

    if(ScrollToBottom ||
       (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
        ImGui::SetScrollHereY(1.0f);

    ScrollToBottom = false;

    ImGui::PopFont(); // restaura o tamanho anterior — SEMPRE dentro do BeginChild
}
ImGui::EndChild();

ImGui::PopStyleVar(); // restaura ItemSpacing — SEMPRE após EndChild

	// ---- Input de comando -----------------------------------------------

	bool reclaim_focus = false; // flag para refocar o input após submit

	constexpr ImGuiInputTextFlags input_flags =
		ImGuiInputTextFlags_EnterReturnsTrue |	 // Enter executa o comando
		ImGuiInputTextFlags_EscapeClearsAll |	 // Esc limpa o buffer
		ImGuiInputTextFlags_CallbackCompletion | // Tab → autocomplete
		ImGuiInputTextFlags_CallbackHistory;	 // ↑↓ → navegação no histórico

	// InputText exige char* (UTF-8). Usamos InputBufUtf8 como buffer de staging
	// e convertemos para/de InputBuf (wchar_t[]) na fronteira.
	if (ImGui::InputText("Input", InputBufUtf8, IM_COUNTOF(InputBufUtf8), input_flags,
						 &TextEditCallbackStub, std::bit_cast<void*>(this))) {
		// Converte o resultado UTF-8 de volta para wide para processamento interno
		const std::wstring wide_input = Utf8ToWide(InputBufUtf8);

		// Copia para o buffer wide (trunca se necessário)
		wcsncpy(InputBuf, wide_input.c_str(), IM_COUNTOF(InputBuf) - 1);
		InputBuf[IM_COUNTOF(InputBuf) - 1] = L'\0'; // garante terminação NUL

		Wcstrim(InputBuf); // remove espaços finais wide

		if (InputBuf[0] != L'\0') ExecCommand(InputBuf); // executa o comando wide

		// Limpa ambos os buffers após o submit
		wmemset(InputBuf, L'\0', IM_COUNTOF(InputBuf));
		memset(InputBufUtf8, '\0', sizeof(InputBufUtf8));
		reclaim_focus = true; // devolve o foco ao input no próximo frame
	}

	ImGui::SetItemDefaultFocus();						// foco padrão no input
	if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1); // restaura foco após submit

	ImGui::End(); // SEMPRE pareado com Begin()
}

// ============================================================================
// Callbacks do InputText
// ============================================================================

/**
 * @brief Trampolim estático: recupera o Console* de UserData e delega.
 */
int Console::TextEditCallbackStub(ImGuiInputTextCallbackData* data) {
	Console* console = static_cast<Console*>(data->UserData);
	return console->TextEditCallback(data);
}

/**
 * @brief Gerencia TAB-completion e navegação Up/Down no histórico.
 *
 * O callback opera em data->Buf (char* UTF-8 interno do InputText).
 * Candidatos de Commands[] (wchar_t*) são comparados via Utf8ToWide/Wcsnicmp.
 * Entradas do History[] são inseridas após conversão para UTF-8.
 *
 * @param data  Estado mutável do widget InputText.
 * @return      0 (ImGui ignora o valor de retorno aqui).
 */
int Console::TextEditCallback(ImGuiInputTextCallbackData* data) {
	switch (data->EventFlag) {
		// ==================================================================
		// TAB — autocomplete
		// ==================================================================
		case ImGuiInputTextFlags_CallbackCompletion: {
			// 1. Localiza a palavra sob o cursor no buffer UTF-8
			const char* word_end   = data->Buf + data->CursorPos;
			const char* word_start = word_end;

			while (word_start > data->Buf) {
				// Converte byte para unsigned char antes de alargar para evitar UB
				const wchar_t wc = static_cast<wchar_t>(static_cast<unsigned char>(word_start[-1]));

				// Delimitadores são todos ASCII — seguro comparar como wchar_t
				if (wc == L' ' || wc == L'\t' || wc == L',' || wc == L';') break;
				word_start--;
			}

			// 2. Converte o prefixo parcial para wide para comparação com Commands[]
			const std::string  prefix_utf8(word_start, word_end);
			const std::wstring prefix_wide = Utf8ToWide(prefix_utf8.c_str());
			const int		   prefix_len  = static_cast<int>(prefix_wide.size());

			// 3. Coleta candidatos que começam com o prefixo
			ImVector<const wchar_t*> candidates;
			for (int i = 0; i < Commands.Size; i++) {
				if (Wcsnicmp(Commands[i], prefix_wide.c_str(), prefix_len) == 0)
					candidates.push_back(Commands[i]);
			}

			if (candidates.Size == 0) {
				AddLog(L"Nenhuma correspondência para \"%ls\"!\n", prefix_wide.c_str());
			} else if (candidates.Size == 1) {
				// Correspondência única: substitui a palavra parcial pelo nome completo
				const std::string full_utf8 = WideToUtf8(candidates[0]);

				data->DeleteChars(static_cast<int>(word_start - data->Buf),
								  static_cast<int>(word_end - word_start));
				data->InsertChars(data->CursorPos, full_utf8.c_str()); // nome completo
				data->InsertChars(data->CursorPos, " ");			   // espaço final
			} else {
				// Múltiplos candidatos: encontra o maior prefixo wide comum
				int match_len = prefix_len; // começa do que já está digitado

				for (;;) {
					wchar_t ref_wc	  = L'\0'; // caractere de referência na posição match_len
					bool	all_match = true;

					for (int i = 0; i < candidates.Size && all_match; i++) {
						const wchar_t wc = static_cast<wchar_t>(
							towupper(static_cast<wint_t>(candidates[i][match_len])));

						if (i == 0) ref_wc = wc; // referência do primeiro candidato
						else if (ref_wc == L'\0' || ref_wc != wc)
							all_match = false; // divergência ou fim de candidato
					}

					if (!all_match) break;
					match_len++; // todos compartilham mais um caractere wide
				}

				// Substitui a palavra parcial pelo prefixo comum wide
				if (match_len > prefix_len) {
					const std::wstring common_wide(candidates[0], candidates[0] + match_len);
					const std::string  common_utf8 = WideToUtf8(common_wide.c_str());

					data->DeleteChars(static_cast<int>(word_start - data->Buf),
									  static_cast<int>(word_end - word_start));
					data->InsertChars(data->CursorPos, common_utf8.c_str());
				}

				// Lista todos os candidatos no log wide
				AddLog(L"Candidatos possíveis:");
				for (int i = 0; i < candidates.Size; i++) AddLog(L"- %ls", candidates[i]);
			}
			break;
		}

		// ==================================================================
		// ↑ / ↓ — navegação no histórico
		// ==================================================================
		case ImGuiInputTextFlags_CallbackHistory: {
			const int prev_pos = HistoryPos;

			if (data->EventKey == ImGuiKey_UpArrow) {
				if (HistoryPos == -1) HistoryPos = History.Size - 1; // salta para a última entrada
				else if (HistoryPos > 0) HistoryPos--;				 // recua uma entrada
			} else if (data->EventKey == ImGuiKey_DownArrow) {
				if (HistoryPos != -1 && ++HistoryPos >= History.Size)
					HistoryPos = -1; // passou do fim → volta para linha em branco
			}

			if (prev_pos != HistoryPos) {
				// Entradas do History[] são wchar_t*; converte para UTF-8 para o InputText
				const wchar_t* wide_entry = (HistoryPos >= 0) ? History[HistoryPos] : L"";

				const std::string utf8_entry = WideToUtf8(wide_entry);

				data->DeleteChars(0, data->BufTextLen);	  // limpa bytes UTF-8 atuais
				data->InsertChars(0, utf8_entry.c_str()); // insere entrada do histórico
			}
			break;
		}
	}

	return 0; // ImGui ignora o retorno neste contexto
}

// ============================================================================
// Renderizador de tags de cor (Termícolor) — wide-char edition
// ============================================================================

/**
 * @brief Converte um nome de cor wide ou string hex para ImVec4 RGBA.
 *
 * Formatos suportados:
 *  - Nomes wide: L"red", L"green", L"blue", L"yellow", L"orange",
 *                L"gray", L"purple", L"cyan", L"error"
 *  - Hex wide: L"#RRGGBB"  (ex.: L"#FF8800")
 *  - Padrão (desconhecido): branco (1,1,1,1)
 *
 * @param start  Ponteiro para o primeiro wchar_t da chave (após L'[').
 * @param end    Ponteiro um além do último wchar_t (antes de L']').
 * @return       Cor RGBA como ImVec4.
 */
ImVec4 Console::ParseColor(const wchar_t* start, const wchar_t* end) {
	const std::wstring key(start, end - start); // extrai a substring wide da chave

	// ---- Formato hex: [#RRGGBB] -----------------------------------------
	if (!key.empty() && key[0] == L'#') {
		unsigned int r = 0, g = 0, b = 0;
		// swscanf_s lê 2 dígitos hex por canal da string wide
		if (swscanf_s(key.c_str() + 1, L"%02x%02x%02x", &r, &g, &b) == 3)
			return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
	}

	// ---- Cores nomeadas -------------------------------------------------
	if (key == L"red") return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
	if (key == L"green") return ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
	if (key == L"blue") return ImVec4(0.4f, 0.6f, 1.0f, 1.0f);
	if (key == L"yellow") return ImVec4(1.0f, 1.0f, 0.4f, 1.0f);
	if (key == L"orange") return ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
	if (key == L"gray") return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
	if (key == L"purple") return ImVec4(0.8f, 0.4f, 0.8f, 1.0f);
	if (key == L"cyan") return ImVec4(0.4f, 1.0f, 1.0f, 1.0f);
	if (key == L"error") return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);

	return ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // padrão: branco
}

/**
 * @brief Analisa e renderiza uma wide string com tags inline [cor]…[/].
 *
 * Percorre a wide string procurando L'[' e L']'.  Como cada wchar_t é
 * um codepoint individual, a busca é um percurso simples de ponteiro —
 * sem ambiguidade de multi-byte.
 *
 * Segmentos de texto são convertidos para UTF-8 antes de passar para
 * ImGui::TextUnformatted (que exige char*).
 *
 *  - [cor] → empurra cor na pilha de estilo do ImGui.
 *  - [/]   → desempilha a última cor empurrada.
 *
 * Qualquer push não fechado é limpo ao final.
 *
 * @param text  Wide string terminada em nulo com tags opcionais [cor].
 */
void Console::RenderTermicolor(const wchar_t* text) {
	const wchar_t* p		   = text;				  // posição atual de análise
	const wchar_t* text_end	   = text + wcslen(text); // um além do último wchar_t
	int			   color_stack = 0;					  // profundidade de cores empurradas

	while (p < text_end) {
		// Busca o próximo L'[' wide
		const wchar_t* tag_start = p;
		while (tag_start < text_end && *tag_start != L'[') tag_start++;

		if (tag_start >= text_end) {
			// Sem mais tags — desenha o texto restante
			if (p != text) ImGui::SameLine(0, 0);  // anexa ao segmento anterior
			const std::string seg = WideToUtf8(p); // wide → UTF-8 para ImGui
			ImGui::TextUnformatted(seg.c_str());
			break;
		}

		// Busca o L']' correspondente
		const wchar_t* tag_end = tag_start + 1;
		while (tag_end < text_end && *tag_end != L']') tag_end++;

		if (tag_end >= text_end) {
			// Encontrou L'[' sem L']' — trata o resto como texto plano
			if (p != text) ImGui::SameLine(0, 0);
			const std::string seg = WideToUtf8(p);
			ImGui::TextUnformatted(seg.c_str());
			break;
		}

		// ---- Desenha o segmento de texto ANTES da tag ------------------
		if (tag_start > p) {
			if (p != text) ImGui::SameLine(0, 0); // sem gap entre segmentos

			// Fatia o segmento wide e converte para UTF-8
			const std::wstring wide_seg(p, tag_start - p);
			const std::string  utf8_seg = WideToUtf8(wide_seg.c_str());
			ImGui::TextUnformatted(utf8_seg.c_str());
		}

		// ---- Processa a tag ---------------------------------------------
		const ptrdiff_t inner_len = tag_end - tag_start - 1; // chars entre '[' e ']'

		// Tag de fechamento: [/] (len=1, char='/') ou [] (len=0)
		const bool is_close = (inner_len == 1 && tag_start[1] == L'/') || (inner_len == 0);

		if (is_close) {
			if (color_stack > 0) {
				ImGui::PopStyleColor(); // restaura cor anterior
				color_stack--;
			}
		} else {
			// Tag de abertura: analisa o nome wide da cor
			const ImVec4 col = ParseColor(tag_start + 1, tag_end);
			ImGui::PushStyleColor(ImGuiCol_Text, col); // empurra cor wide
			color_stack++;
		}

		p = tag_end + 1; // avança além do L']'
	}

	// Segurança: desempilha cores que nunca foram fechadas com [/]
	while (color_stack > 0) {
		ImGui::PopStyleColor();
		color_stack--;
	}
}

// ============================================================================
// Helpers de emoji
// ============================================================================

/**
 * @brief Adiciona uma linha de log prefixada com um emoji wide-char.
 *
 * Como todo o armazenamento é wchar_t, o emoji é simplesmente prefixado
 * como um caractere wide — sem conversão de encoding neste ponto.
 *
 * @param emoji  Emoji wide-char, ex.: L"🚀" ou L"\U0001F680".
 * @param fmt    String de formato wide para o corpo da mensagem.
 * @param ...    Argumentos variádicos.
 */
void Console::AddLogWithEmoji(const wchar_t* emoji, const wchar_t* fmt, ...) {
	wchar_t buf[1024]; // buffer wide para o corpo formatado da mensagem
	va_list args;

	va_start(args, fmt);
	vswprintf(buf, IM_COUNTOF(buf), fmt, args); // vprintf wide
	va_end(args);

	// Concatena emoji + espaço + mensagem numa única wide string
	const std::wstring msg = std::wstring(emoji) + L" " + std::wstring(buf);
	AddLog(L"%ls", msg.c_str()); // delega para AddLog (que chama Wcsdup)
}

/**
 * @brief Retorna um emoji wide-char aleatório de uma tabela interna estática.
 *
 * Usa literais wide (L"…" ou L"\Uxxxxxxxx") para que os emoji sejam
 * armazenados como codepoints wchar_t nativos — sem decodificação UTF-8
 * em nenhum ponto do pipeline interno.
 *
 * Requer que Console::LoadEmojiFont() (ou FontManager) tenha sido chamado
 * para que os glifos sejam renderizados pelo ImGui.
 *
 * @return  std::wstring com um emoji, ou L"" se a tabela estiver vazia.
 */
std::wstring Console::GetRandomEmoji() const {
	static std::vector<std::wstring> emojis; // tabela estática; preenchida uma vez
	static bool						 initialized = false;

	if (!initialized) {
		emojis.push_back(L"\u2713");	 // U+2713  CHECK MARK          ✓
		emojis.push_back(L"\u2717");	 // U+2717  BALLOT X            ✗
		emojis.push_back(L"\u26A0");	 // U+26A0  WARNING SIGN        ⚠
		emojis.push_back(L"\u2139");	 // U+2139  INFORMATION SOURCE  ℹ
		emojis.push_back(L"\u26A1");	 // U+26A1  HIGH VOLTAGE        ⚡
		emojis.push_back(L"\U0001F527"); // U+1F527 WRENCH              🔧
		emojis.push_back(L"\U0001F680"); // U+1F680 ROCKET              🚀
		emojis.push_back(L"\U0001F4DD"); // U+1F4DD MEMO                📝
		emojis.push_back(L"\U0001F4CA"); // U+1F4CA BAR CHART           📊
		emojis.push_back(L"\U0001F3AF"); // U+1F3AF DIRECT HIT          🎯
		emojis.push_back(L"\U0001F4A1"); // U+1F4A1 LIGHT BULB          💡
		emojis.push_back(L"\U0001F50D"); // U+1F50D MAGNIFYING GLASS    🔍
		emojis.push_back(L"\u23F1");	 // U+23F1  STOPWATCH           ⏱
		emojis.push_back(L"\U0001F4E6"); // U+1F4E6 PACKAGE             📦
		emojis.push_back(L"\U0001F31F"); // U+1F31F GLOWING STAR        🌟
		emojis.push_back(L"\U0001F4BE"); // U+1F4BE FLOPPY DISK         💾
		emojis.push_back(L"\U0001F4E5"); // U+1F4E5 INBOX TRAY          📥
		emojis.push_back(L"\U0001F4E4"); // U+1F4E4 OUTBOX TRAY         📤
		emojis.push_back(L"\U0001F510"); // U+1F510 CLOSED LOCK+KEY     🔐
		emojis.push_back(L"\U0001F511"); // U+1F511 KEY                 🔑

		initialized = true;
	}

	if (emojis.empty()) return L"";

	return emojis[rand() % emojis.size()]; // retorna emoji wide aleatório
}