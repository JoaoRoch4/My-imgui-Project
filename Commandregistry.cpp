/**
 * @file CommandRegistry.cpp
 * @brief Implementação do registro de comandos do Console.
 *
 * ESTRUTURA
 * ----------
 * RegisterAll()
 *   ├─ RegisterLifecycle()  — EXIT, QUIT, BREAK, forceexit, abort, system_pause, cpp_pause
 *   ├─ RegisterSystem()     — SPECS, VSYNC, NOVIEWPORTS, FONTRESET, system [cmd]
 *   ├─ RegisterTheme()      — MICA, NOMICA, theme [dark|light|classic]
 *   └─ RegisterDemo()       — implot, implot3d, test_emojis
 *
 * REGRA FUNDAMENTAL — NOMES SEM ESPAÇOS
 * ----------------------------------------
 * ExecCommand extrai o PRIMEIRO token (até o primeiro espaço) como chave de
 * despacho.  Portanto, nomes de comandos NUNCA podem conter espaços; use '_'.
 *
 *   ERRADO:  L"Test Emojis"   → token[0] = L"TEST"         → não encontrado
 *   CORRETO: L"test_emojis"   → token[0] = L"TEST_EMOJIS"  → encontrado ✓
 *
 * O espaço só é permitido ENTRE o nome e os argumentos:
 *
 *   "system dir /b"  → chave = L"SYSTEM", args = { L"dir", L"/b" }  ✓
 *
 * CAPTURA DE STDOUT  (_popen / _pclose)
 * ----------------------------------------
 * O comando "system [cmd]" usa _popen() para redirecionar stdout do processo
 * filho para um FILE*.  O FILE* é gerenciado por unique_ptr com deleter
 * customizado — _pclose() é garantido mesmo em saída antecipada por throw.
 */

#include "pch.hpp"
#include "CommandRegistry.hpp"

#include "App.hpp"
#include "Console.hpp"
#include "MyResult.hpp"
#include "FontScale.hpp"
#include "SystemInfo.hpp"
#include "MicaTheme.h"
#include "VulkanContext_Wrapper.hpp"
#include "ImGuiContext_Wrapper.hpp"
#include "Memory.hpp"

// ============================================================================
// Construtor
// ============================================================================

/**
 * @brief Armazena as referências não-possuidoras de App e Console.
 *
 * Nenhuma alocação é feita aqui — o objeto é apenas um organizador
 * de chamadas de RegisterCommand durante RegisterAll().
 *
 * @param app      Instância de App em execução.
 * @param console  Console ImGui onde os comandos serão registrados.
 */
CommandRegistry::CommandRegistry(App* app, Console* console) noexcept :
m_app(app) // ponteiro não-possuidor — App é dono de si mesmo
,
m_console(console) // ponteiro não-possuidor — Memory é dono do Console
{}

// ============================================================================
// RegisterAll
// ============================================================================

/**
 * @brief Registra todos os grupos de comandos no Console.
 *
 * @return MyResult::ok sempre; falhas individuais são logadas no console.
 */
[[nodiscard]] MyResult CommandRegistry::RegisterAll() {
	RegisterLifecycle(); // EXIT, QUIT, BREAK, forceexit, abort, system_pause, cpp_pause
	RegisterSystem();	 // SPECS, VSYNC, NOVIEWPORTS, FONTRESET, system [cmd]
	RegisterTheme();	 // MICA, NOMICA, theme [dark|light|classic]
	RegisterDemo();		 // implot, implot3d, test_emojis

	return MyResult::ok;
}

// ============================================================================
// RegisterLifecycle
// ============================================================================

/**
 * @brief Registra comandos de ciclo de vida da aplicação.
 *
 *  EXIT         — encerra via g_Done = true (built-in)
 *  QUIT         — alias de EXIT
 *  BREAK        — __debugbreak() se depurador presente
 *  forceexit    — std::exit(0) imediato, sem cleanup
 *  abort        — std::abort() para core dump em debug
 *  system_pause — system("pause") — apenas Windows
 *  cpp_pause    — std::cin.get()
 *
 * NOTA: "system_pause" usa '_' para não colidir com o comando "system [cmd]"
 * do RegisterSystem().  Se fosse "system pause", a chave de despacho seria
 * "SYSTEM" — igual ao comando de execução de shell.
 */
void CommandRegistry::RegisterLifecycle() {
	App*	 app = m_app;
	Console* con = m_console;

	// ---- EXIT / QUIT --------------------------------------------------------

	auto cmd_quit = [app, con]() {
		con->AddLog(L"Saindo..."); // notifica o usuário no log
		app->g_Done = true;		   // sinaliza o MainLoop para encerrar
	};

	con->RegisterBuiltIn(L"EXIT", cmd_quit); // valida contra BuiltInCommands[]
	con->RegisterCommand(L"QUIT", L"Alias de EXIT — encerra o programa.", cmd_quit);

	// ---- BREAK --------------------------------------------------------------

	con->RegisterCommand(L"BREAK", L"Dispara __debugbreak() se um depurador estiver presente.",
						 [con]() {
							 if (IsDebuggerPresent())
								 __debugbreak(); // interrompe no depurador (MSVC/WinDbg)
							 else con->AddLog(L"[yellow]AVISO:[/] Nenhum depurador detectado.");
						 });

	// ---- forceexit ----------------------------------------------------------

	con->RegisterCommand(L"forceexit", L"Encerra imediatamente via std::exit(0) — sem cleanup.",
						 [con]() {
							 con->AddLog(L"[error]FORCE EXIT[/] — saindo sem cleanup.");
							 std::exit(0); // saída imediata; destrutores NÃO são chamados
						 });

	// ---- abort --------------------------------------------------------------

	con->RegisterCommand(L"abort", L"Aborta o processo via std::abort() — gera core dump em debug.",
						 []() {
							 std::abort(); // SIGABRT → core dump Linux / diálogo MSVC Windows
						 });

	// ---- system_pause -------------------------------------------------------

	con->RegisterCommand(L"system_pause",
						 L"Pausa a execução via system(\"pause\") — apenas Windows.", []() {
							 std::system("pause"); // bloqueia até o usuário pressionar Enter
						 });

	// ---- cpp_pause ----------------------------------------------------------

	con->RegisterCommand(L"cpp_pause", L"Pausa a execução via std::cin.get().", []() {
		std::cin.get(); // aguarda Enter no stdin
	});
}

// ============================================================================
// RegisterSystem
// ============================================================================

/**
 * @brief Registra comandos de sistema, hardware e execução de shell.
 *
 *  SPECS        — especificações de hardware via SystemInfo
 *  VSYNC        — toggle de VSync no VulkanContext
 *  NOVIEWPORTS  — desabilita viewports flutuantes
 *  FONTRESET    — restaura tamanho original da fonte global
 *
 *  system [cmd] — executa qualquer comando do CMD e exibe stdout no console
 *
 * IMPLEMENTAÇÃO DE "system [cmd]"
 * ---------------------------------
 * _popen(cmd, "r") abre um pipe de LEITURA para o stdout do processo filho.
 * Modo "r" = texto → tradução automática \r\n → \n no Windows.
 *
 * O FILE* retornado pelo _popen é embrulhado em um unique_ptr com deleter
 * customizado [](FILE* f){ _pclose(f); }.  Isso garante que _pclose() seja
 * chamado no destrutor — sem vazamento de handle mesmo que o loop lance.
 *
 * Múltiplos argumentos são suportados naturalmente: "system dir /b C:\\"
 * chega como args = { L"dir", L"/b", L"C:\\" } e é reunido com espaços
 * antes de passar ao _popen.
 *
 * Limite de MAX_OUTPUT_LINES linhas por execução protege o console contra
 * saídas muito longas (ex.: "system dir /s C:\\").
 */
void CommandRegistry::RegisterSystem() {
	App*	 app = m_app;
	Console* con = m_console;

	// ---- SPECS --------------------------------------------------------------

	con->RegisterCommand(
		L"SPECS", L"Exibe especificações de hardware (CPU, GPU, memória).",
		[con]() { 
		auto vk = Memory::Get()->GetVulkan();
		SystemInfo::Collect(vk, L"Vulkan").PrintToConsole(con); });

	// ---- VSYNC --------------------------------------------------------------

	con->RegisterCommand(L"VSYNC", L"Liga ou desliga o VSync do swapchain Vulkan.", [con]() {
		auto vk = Memory::Get()->GetVulkan();
		const bool novo = !vk->GetVSync(); // inverte o estado atual
		vk->SetVSync(novo);
		con->AddLog(novo ? L"[green]VSync ON[/]" : L"[yellow]VSync OFF[/]");
	});

	// ---- NOVIEWPORTS --------------------------------------------------------

	con->RegisterCommand(L"NOVIEWPORTS", L"Desabilita viewports flutuantes e reposiciona janelas.",
						 [app, con]() {
							 app->DisableViewportDocking();
							 if (app->bViewportDocking) app->bViewportDocking = false;
						 });

	// ---- FONTRESET ----------------------------------------------------------

	con->RegisterCommand(L"FONTRESET",
						 L"Restaura o tamanho original da fonte global (desfaz Ctrl+Scroll).",
						 [app]() {
							 FontScale::ResetToDefault(); // volta ao tamanho inicial capturado
							 app->SaveConfig();			  // persiste o reset para o próximo boot
						 });

	// ---- system [cmd] -------------------------------------------------------

	con->RegisterCommand(
		L"system", L"Executa um comando do CMD e exibe a saída aqui. Uso: system [cmd]",
		[con](std::vector<std::wstring> args) {
			// ---- Sem argumentos → ajuda --------------------------------------
			if (args.empty()) {
				con->AddLogSys(L"[yellow]Uso:[/] system [comando] [argumentos...]\n");
				con->AddLogSys(L"[gray]Exemplos:[/]\n");
				con->AddLogSys(L"[gray]  system dir[/]\n");
				con->AddLogSys(L"[gray]  system dir /b[/]\n");
				con->AddLogSys(L"[gray]  system ipconfig[/]\n");
				con->AddLogSys(L"[gray]  system echo hello world[/]\n");
				con->AddLogSys(L"[gray]Dica: adicione 2>&1 ao final para capturar stderr.[/]\n");
				return;
			}

			// ---- Monta a linha de comando reunindo todos os args --------------
			// args = { L"dir", L"/b" } → wide_cmd = L"dir /b"
			std::wstring wide_cmd;
			for (std::size_t i = 0; i < args.size(); ++i) {
				if (i > 0) wide_cmd += L' '; // espaço separador entre tokens
				wide_cmd += args[i];		 // acumula cada argumento
			}

			// Converte o comando wide para narrow — _popen exige char*
			   // Converte o comando wide para narrow — _popen exige char*
            // Console::WideToUtf8Public é o mesmo WideToUtf8 exposto como público
            const std::string narrow_cmd = Console::WideToUtf8Public(wide_cmd.c_str());

            con->AddLog(L"[cyan]>[/] %ls", wide_cmd.c_str()); // eco do comando execu

			// ---- Abre pipe de leitura para o stdout do processo filho --------
			// unique_ptr com deleter lambda: _pclose() chamado no destrutor.
			// "r" = modo texto: \r\n traduzido para \n automaticamente (Windows).
			// The lambda type is stateless → default-constructible in C++20.
			struct PipeCloser {
				void operator()(FILE* f) const noexcept { _pclose(f); }
			};

			std::unique_ptr<FILE, PipeCloser> pipe{_popen(narrow_cmd.c_str(), "r")};

			if (!pipe) {
				// _popen retorna nullptr em falha (permissão, PATH inválido, etc.)
				con->AddLog(L"[error]Falha ao abrir pipe para:[/] %ls", wide_cmd.c_str());
				return;
			}

			// ---- Lê stdout linha a linha -------------------------------------
			constexpr int MAX_OUTPUT_LINES = 512; // proteção contra saídas imensas
			int			  line_count	   = 0;	  // contador de linhas já adicionadas

			std::array<char, 1024> buf{}; // buffer de leitura; zero-inicializado

			// fgets lê até '\n' ou EOF, devolvendo nullptr no fim do pipe
			while (fgets(buf.data(), static_cast<int>(buf.size()), pipe.get()) != nullptr) {
				// Remove '\n' e '\r' finais que fgets preserva no buffer
				std::string_view sv(buf.data()); // view sem alocação extra
				while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r')) {
					sv.remove_suffix(1); // descarta último byte da view
				}

				// Converte a linha (char* UTF-8) para wide — todo o armazenamento
				// interno do console usa wchar_t
				const std::wstring wide_line = Console::Utf8ToWidePublic(std::string(sv).c_str());

				con->AddLog(L"%ls", wide_line.c_str()); // adiciona ao log do console

				if (++line_count >= MAX_OUTPUT_LINES) {
					// Avisa o usuário e para de ler (processo filho continua rodando
					// até _pclose() ser chamado pelo unique_ptr ao sair do escopo)
					con->AddLog(L"[yellow]... saída truncada em %d linhas.[/]", MAX_OUTPUT_LINES);
					break;
				}
			}
			// unique_ptr destrói aqui → deleter chama _pclose(pipe.get()) ✓

			con->AddLog(L"[gray]--- fim ---[/]"); // marca o fim da saída capturada
		});

		con->RegisterCommand(
		L"echo", L"Executa um comando do CMD e exibe a saída aqui. Uso: system [cmd]",
		[con](std::vector<std::wstring> args) {
			
			});
}

// ============================================================================
// RegisterTheme
// ============================================================================

/**
 * @brief Registra comandos de tema visual do ImGui.
 *
 *  MICA         — ativa o tema Windows 11 Mica e persiste
 *  NOMICA       — desativa o tema Mica e persiste
 *  theme [arg]  — aplica preset: dark | light | classic (case-insensitive)
 */
void CommandRegistry::RegisterTheme() {
	App*	 app = m_app;
	Console* con = m_console;

	// ---- MICA ---------------------------------------------------------------

	con->RegisterCommand(L"MICA", L"Ativa o tema Windows 11 Mica e persiste em settings.json.",
						 [app, con]() {
							 app->g_Settings->use_mica_theme = true;
							 MicaTheme::ApplyMicaTheme(app->g_Settings->mica_theme);
							 app->SaveConfig();
							 con->AddLog(L"[cyan]Tema Mica ativado.[/]");
						 });

	// ---- NOMICA -------------------------------------------------------------

	con->RegisterCommand(L"NOMICA", L"Desativa o tema Mica (usa style+color salvos) e persiste.",
						 [app, con]() {
							 app->g_Settings->use_mica_theme = false;
							 app->ApplyStyleToImGui();
							 app->SaveConfig();
							 con->AddLog(L"[yellow]Tema Mica desativado.[/]");
						 });

	// ---- theme [dark|light|classic] -----------------------------------------

	con->RegisterCommand(
		L"theme", L"Aplica um preset de tema ImGui. Uso: theme [dark|light|classic]",
		[con](std::vector<std::wstring> args) {
			if (args.empty()) {
				con->AddLog(L"[yellow]Uso:[/] theme [dark|light|classic]");
				return;
			}

			// Copia e converte para UPPERCASE para comparação case-insensitive.
			// L"Dark" == L"DARK" == L"dark" depois desta transformação.
			std::wstring sub = args[0];
			std::transform(sub.begin(), sub.end(), sub.begin(),
						   [](const wchar_t c) { return static_cast<wchar_t>(towupper(c)); });

			if (sub == L"DARK") {
				ImGui::StyleColorsDark();
				con->AddLog(L"[cyan]Tema:[/] dark aplicado.");
			} else if (sub == L"LIGHT") {
				ImGui::StyleColorsLight();
				con->AddLog(L"[cyan]Tema:[/] light aplicado.");
			} else if (sub == L"CLASSIC" || sub == L"CLEAR") {
				ImGui::StyleColorsClassic();
				con->AddLog(L"[cyan]Tema:[/] classic aplicado.");
			} else {
				con->AddLog(L"[error]Subcomando desconhecido:[/] '%ls'", args[0].c_str());
				con->AddLog(L"[yellow]Uso:[/] theme [dark|light|classic]");
			}
		});
}

// ============================================================================
// RegisterDemo
// ============================================================================

/**
 * @brief Registra comandos de demonstração e testes visuais.
 *
 *  implot       — abre a janela de demo do ImPlot
 *  implot3d     — placeholder para demo do ImPlot3D
 *  test_emojis  — imprime sequência de emoji para verificar fonte e ranges
 *
 * POR QUE test_emojis E NÃO "Test Emojis"?
 * -------------------------------------------
 * TokenizeLine divide no espaço:
 *   "Test Emojis" → tokens = { L"Test", L"Emojis" }
 *   chave de despacho = uppercase(tokens[0]) = L"TEST" → não encontrado
 *
 *   "test_emojis" → tokens = { L"test_emojis" }
 *   chave de despacho = L"TEST_EMOJIS" → encontrado ✓
 */
void CommandRegistry::RegisterDemo() {
	Console* con = m_console;

	// ---- implot -------------------------------------------------------------

	con->RegisterCommand(L"implot", L"Abre a janela de demo do ImPlot.", []() {
		ImPlot::ShowDemoWindow(); // abre a janela de demo do ImPlot
	});

	// ---- implot3d -----------------------------------------------------------

	con->RegisterCommand(L"implot3d",
						 L"Placeholder para a demo do ImPlot3D (ative via checkbox na UI).",
						 []() {}); // funcionalidade real está nos checkboxes da janela principal

	// ---- test_emojis --------------------------------------------------------

	// Literais wide L"..." adjacentes são unidos pelo compilador em tempo de
	// compilação — sem concatenação em runtime.  Cada L"😀" é um codepoint
	// wchar_t individual — sem decodificação UTF-8 necessária neste ponto.
	static constexpr const wchar_t* k_emoji_test = L"😀 😁 😂 🤣 😃 😄 😅 😆 😉 😊\n"
												   L"😋 😎 🥳 🥺 🙏 😍 🤩 😘 🤗 🤔\n"
												   L"😐 😑 😶 😏 😒 🙄 😬 🤥 😌 😔\n"
												   L"\n"
												   L"👋 🤚 🖐 ✋ 🖖 👌 ✌ 🤞 👍 👎\n"
												   L"✊ 👊 🤛 🤜 👏 🙌 👐 🤲 🙏\n"
												   L"\n"
												   L"💻 🖥 🖨 ⌨ 🖱 💾 💿 📀 📱 ☎\n"
												   L"🔋 🔌 💡 🔦 🔎 🔬 🔭 📡 🧯\n"
												   L"\n"
												   L"🎮 🕹 🎲 ♟ 🎯 🎳 🎰 🎨 🎵 🎶\n"
												   L"🎷 🎸 🥁 🎤 🎧 🎼 🎬 🎭 🎪\n"
												   L"\n"
												   L"🚀 🛸 🌍 🌙 ⭐ 🌟 💫 ✨ ☄ 🌈\n"
												   L"⚡ 🔥 💧 🌊 🌀 🌪 ❄ ☃ ⛄\n"
												   L"\n"
												   L"❤ 🧡 💛 💚 💙 💜 🖤 🤍 🤎 💔\n"
												   L"💕 💞 💓 💗 💖 💘 💝 💟\n"
												   L"\n"
												   L"🐶 🐱 🐭 🐹 🐰 🦊 🐻 🐼 🐨 🐯\n"
												   L"🦁 🐮 🐷 🐸 🐵 🙈 🙉 🙊 🐔 🐧\n"
												   L"\n"
												   L"✓ ✗ ⚠ ℹ ⚡ 🔧 🚀 📝 📊 🎯\n"
												   L"💡 🔍 ⏱ 📦 🌟 💾 📥 📤 🔐 🔑\n";

	con->RegisterCommand(L"test_emojis",
						 L"Imprime emoji no console para verificar a fonte e os ranges.", [con]() {
							 con->AddLog(L"[cyan]=== Teste de Emoji ===[/]");
							 con->AddLog(k_emoji_test); // imprime toda a sequência de uma vez
							 con->AddLog(L"[cyan]=== Fim do teste ===[/]");
						 });
}