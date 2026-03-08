#pragma once
#include "pch.hpp"

// Forward declarations — evita incluir headers pesados no .hpp
class AppSettings;
class MyResult;
class FontManager;
class FontScale;
class App;
class MyWindows;
class VulkanContext;
class ImGuiContext_Wrapper;
class Console;
class StyleEditor;
class MenuBar;
class WindowsConsole;
class ImageViewerFactory;
class InitArgs;
class OnlineClock;

/**
 * @brief Singleton que centraliza o ciclo de vida de todos os recursos da aplicação.
 *
 * RESPONSABILIDADES POR CAMADA
 * -----------------------------
 * A inicialização é dividida entre dois pontos de entrada distintos:
 *
 *  wWinMain()
 *    → Memory::Init()       — cria a instância do singleton (new Memory)
 *    → App::Run()           — cria janela, chama AllocAll(), roda o loop, destrói janela
 *    → Memory::Get()->DestroyAll()  — destrói todos os recursos na ordem inversa
 *    → Memory::Shutdown()   — destrói a instância do singleton (delete Memory)
 *
 *  App::Run()  (chamado de wWinMain)
 *    → SDL_Init / SDL_CreateWindow
 *    → Memory::Get()->AllocAll(window)   — aloca Vulkan, ImGui, fontes, console...
 *    → loop principal
 *    → SDL_DestroyWindow / SDL_Quit
 *
 * Fluxo completo em wWinMain():
 * @code
 *   Memory::Init();           // 1. cria o singleton antes de tudo
 *
 *   App app;
 *   app.Run();                // 2. janela + AllocAll + loop + DestroyWindow
 *
 *   Memory::Get()->DestroyAll(); // 3. libera Vulkan, ImGui, fontes, etc.
 *   Memory::Shutdown();          // 4. deleta o singleton
 * @endcode
 *
 * POR QUE ESSA DIVISÃO?
 * ----------------------
 * - wWinMain controla apenas o ciclo de vida do singleton (Init/Shutdown).
 * - App encapsula tudo que depende da janela SDL (criação, loop, destruição).
 * - Memory::DestroyAll() fica em wWinMain (após App::Run retornar) para garantir
 *   que os recursos Vulkan/ImGui sejam liberados ANTES de SDL_DestroyWindow e
 *   ANTES de Memory::Shutdown deletar o singleton.
 *
 * ORDEM DE ALLOC — encapsulada em AllocAll():
 * --------------------------------------------
 *  1. AllocWindowsConsole()  — console Win32 para debug (printf / OutputDebugString)
 *  2. AllocVulkan()          — Initialize() + SetupWindow() + detecção do monitor
 *  3. AllocApp()             — instância interna de App (lógica da aplicação)
 *  4. AllocImGui()           — CreateContext + ImplSDL3_Init + ImplVulkan_Init
 *  5. AllocFontManager()     — carrega TTFs + emoji no atlas ImGui
 *  6. AllocFontScale()       — gerencia escala de fontes por DPI
 *  7. AllocConsole()         — console ImGui (wide-char, histórico, comandos)
 *  8. AllocStyleEditor()     — editor de estilo ImGui + carga de imgui_style.json
 *  9. AllocMenuBar()         — barra de menu principal da aplicação
 *
 * ORDEM DE DESTROY — DestroyAll() executa a ordem inversa automaticamente.
 *
 * DETECÇÃO DO MONITOR CORRETO
 * ----------------------------
 * SDL_GetDisplayForWindow(window) retorna o display em que a janela está,
 * independente de qual é o monitor primário do sistema.
 * O resultado é guardado em m_window_scale e reutilizado por AllocImGui()
 * e AllocFontManager() sem nova chamada SDL, garantindo DPI correto em
 * setups multi-monitor onde o app pode abrir em um monitor secundário.
 */
class Memory {
public:
	Memory() noexcept;
	~Memory();
	// -------------------------------------------------------------------------
	// Controle explícito do singleton — chamados em wWinMain
	// -------------------------------------------------------------------------

	/**
	 * @brief Cria a instância do singleton em heap via new.
	 *
	 * Deve ser a PRIMEIRA linha de wWinMain(), antes de qualquer chamada a Get().
	 * Chamar Get() antes de Init() retorna nullptr.
	 *
	 * Separado de AllocAll() para que o singleton exista antes mesmo de
	 * SDL_CreateWindow() ser chamado — caso algum sistema precise de Memory
	 * durante a fase de inicialização do SDL ou da janela em App::Run().
	 *
	 * @pre  s_instance == nullptr (assert em Debug se chamado duas vezes)
	 * @post s_instance != nullptr; Get() retorna ponteiro válido
	 */
	const static void Init();

	/**
	 * @brief Destroi a instância do singleton via delete.
	 *
	 * Deve ser a ÚLTIMA linha de wWinMain(), após DestroyAll() e após App::Run()
	 * ter retornado (garantindo que SDL_DestroyWindow já foi chamado).
	 * Após esta chamada Get() retorna nullptr.
	 *
	 * @pre  DestroyAll() já foi chamado
	 * @post s_instance == nullptr; Get() retorna nullptr
	 */
	static void Shutdown();

	/**
	 * @brief Retorna o ponteiro para a instância do singleton.
	 *
	 * @return Ponteiro válido entre Init() e Shutdown(); nullptr fora desse intervalo.
	 */
	static Memory* Get();

	// -------------------------------------------------------------------------
	// Lifecycle — chamados internamente por App::Run()
	// -------------------------------------------------------------------------

	/**
	 * @brief Aloca todos os recursos na ordem correta.
	 *
	 * Chamado por App::Run() após SDL_CreateWindow(), passando a janela SDL
	 * recém-criada. A janela é armazenada em m_window para uso interno por
	 * todos os sub-métodos — sem parâmetros adicionais nem variáveis globais.
	 *
	 * Memory não possui a janela — ela pertence a App::Run() e é destruída
	 * por SDL_DestroyWindow() após DestroyAll() ter sido chamado.
	 *
	 * @param window  SDL_Window* criado em App::Run(). Não pode ser nullptr.
	 * @return        MyResult::ok se todos os recursos foram alocados com sucesso.
	 */
	MyResult AllocAll(SDL_Window* window);

	/**
	 * @brief Destroi todos os recursos na ordem inversa de AllocAll().
	 *
	 * Chamado em wWinMain() após App::Run() retornar — garantindo que a janela
	 * SDL ainda existe quando os recursos Vulkan são destruídos (Vulkan precisa
	 * da janela para destruir a surface antes de destruir a instance).
	 *
	 * Seguro chamar mesmo que apenas parte dos recursos tenha sido alocada.
	 * Após esta chamada todos os getters retornam nullptr.
	 */
	MyResult DestroyAll();

	// -------------------------------------------------------------------------
	// Alloc / Destroy individuais (públicos para uso em testes ou init parcial)
	// -------------------------------------------------------------------------
	MyResult AllocAppSettings();   ///< Aloca AppSettings
	MyResult DestroyAppSettings(); ///< Destroi AppSettings

	/**
	 * @brief Abre o console Win32 para saída de debug.
	 *
	 * Chama ::AllocConsole() da Win32 API e redireciona stdout/stderr/stdin.
	 * Útil em builds Debug de aplicações que usam subsistema Windows (sem
	 * console por padrão). Em builds Release geralmente é um no-op.
	 */
	MyResult AllocWindowsConsole();
	MyResult DestroyWindowsConsole(); ///< Fecha e libera o console Win32.

	/**
	 * @brief Inicializa o VulkanContext e configura a surface/swapchain.
	 *
	 * Detecta o monitor via SDL_GetDisplayForWindow(m_window) e salva
	 * m_window_scale para AllocImGui() e AllocFontManager().
	 * SetupWindow() é chamado AQUI porque ImGui_ImplVulkan_Init() exige
	 * que o swapchain já exista (assert: info->ImageCount >= info->MinImageCount).
	 */
	MyResult AllocVulkan();
	MyResult DestroyVulkan(); ///< Destrói swapchain + surface + device + instance.

	/**
	 * @brief Constrói a instância interna de App (lógica da aplicação).
	 *
	 * Nota: o App externo (criado na stack de wWinMain) chama Run() que por
	 * sua vez chama AllocAll(). Este AllocApp() aloca um App *interno* ao
	 * Memory, separado do App externo — use conforme a arquitetura do projeto.
	 */
	MyResult AllocApp();
	MyResult DestroyApp(); ///< Destrói a instância interna de App.

	MyResult AllocMyWindows();
	MyResult DestroyMyWindows();
	/**
	 * @brief Inicializa o ImGuiContext_Wrapper (CreateContext + backends SDL3/Vulkan).
	 *
	 * Usa m_window e m_window_scale detectados em AllocVulkan().
	 * DEVE ser chamado após AllocVulkan() — o swapchain precisa existir.
	 */
	MyResult AllocImGui();
	MyResult DestroyImGui(); ///< Destrói o ImGuiContext (ImGui::DestroyContext + shutdowns).

	/**
	 * @brief Carrega todas as fontes TTF + emoji no atlas ImGui.
	 *
	 * Usa m_window_scale de AllocVulkan(). Tamanho base = 13.0f * m_window_scale.
	 * Deve ser chamado ANTES do primeiro NewFrame() — o atlas é enviado à GPU neste ponto.
	 */
	MyResult AllocFontManager();
	MyResult DestroyFontManager(); ///< Libera o FontManager.

	MyResult AllocOnlineClock();
	MyResult DestroyOnlineClock();
	/**
	 * @brief Inicializa o FontScale (escala de fontes por DPI).
	 * Deve ser chamado após AllocFontManager().
	 */
	MyResult AllocFontScale();
	MyResult DestroyFontScale(); ///< Destrói o FontScale.

	/**
	 * @brief Constrói o Console ImGui (wide-char, histórico, autocomplete).
	 * DEVE ser chamado após AllocImGui() — Console usa ImGui::MemAlloc().
	 */
	MyResult AllocConsole();
	MyResult DestroyConsole(); ///< Destrói o Console ImGui e libera wide strings.

	/**
	 * @brief Constrói o StyleEditor e carrega imgui_style.json.
	 * Silencioso se o arquivo não existir — mantém estilo padrão.
	 */
	MyResult AllocStyleEditor();
	MyResult DestroyStyleEditor(); ///< Destrói o StyleEditor.


	MyResult AllocInitArgs(LPWSTR lp_cmd_line);
    MyResult DestroyInitArgs(); ///< Destrói o InitArgs.


	/**
	 * @brief Constrói a MenuBar principal da aplicação.
	 * DEVE ser chamado após AllocImGui() — depende do contexto ImGui ativo.
	 */
	MyResult AllocMenuBar();
	MyResult DestroyMenuBar(); ///< Destrói a MenuBar.

	MyResult AllocImageViewerFactory();
	MyResult DestroyImageViewerFactory();
	// -------------------------------------------------------------------------
	// Getters — retornam nullptr se o recurso correspondente não foi alocado
	// -------------------------------------------------------------------------

	WindowsConsole*		  GetWindowsConsole(); ///< nullptr se não alocado
	VulkanContext*		  GetVulkan();		   ///< nullptr se não alocado
	App*				  GetApp();			   ///< nullptr se não alocado
	MyWindows*			  GetMyWindows();	   ///< nullptr se não alocado
	ImGuiContext_Wrapper* GetImGui();		   ///< nullptr se não alocado
	FontManager*		  GetFontManager();	   ///< nullptr se não alocado
	FontScale*			  GetFontScale();	   ///< nullptr se não alocado
	Console*			  GetConsole();		   ///< nullptr se não alocado
	StyleEditor*		  GetStyleEditor();	   ///< nullptr se não alocado
	MenuBar*			  GetMenuBar();		   ///< nullptr se não alocado
	InitArgs*			  GetInitArgs() const;
	SDL_Window*			  GetWindow() const;
	AppSettings*		  GetAppSettings() const; ///< Atalho para GetApp()->GetAppSettings()
	ImageViewerFactory*	  GetImageViewerFactory() const;
	OnlineClock* GetOnlineClock() const;

private:
	// Construtor/destrutor privados — apenas Init()/Shutdown() criam/destroem


	// Ponteiro estático — nullptr fora do intervalo Init()…Shutdown()
	static Memory* s_instance; ///< Controlado exclusivamente por Init() e Shutdown()

	// -------------------------------------------------------------------------
	// Recursos gerenciados
	// -------------------------------------------------------------------------
	std::unique_ptr<AppSettings>		  app_settings_instance;
	std::unique_ptr<WindowsConsole>		  windows_console_instance;
	std::unique_ptr<VulkanContext>		  vulkan_instance;
	std::unique_ptr<App>				  app_instance;
	std::unique_ptr<MyWindows>			  my_windows_instance;
	std::unique_ptr<ImGuiContext_Wrapper> imgui_instance;
	std::unique_ptr<FontManager>		  font_manager_instance;
	std::unique_ptr<FontScale>			  font_scale_instance;
	std::unique_ptr<Console>			  console_instance;
	std::unique_ptr<StyleEditor>		  style_editor_instance;
	std::unique_ptr<MenuBar>			  menu_bar_instance;
	std::unique_ptr<ImageViewerFactory>	  ImageViewerFactory_instance;
	    std::unique_ptr<InitArgs> init_args_instance;
		std::unique_ptr<OnlineClock> OnlineClock_instance;

	// -------------------------------------------------------------------------
	// Flags de estado — true somente após o Alloc correspondente ter sucedido
	// -------------------------------------------------------------------------

	bool bApp_settings_allocated;	 ///< true após AllocAppSettings()
	bool bWindows_console_allocated; ///< true após AllocWindowsConsole()
	bool bvulkan_allocated;			 ///< true após AllocVulkan()
	bool bapp_allocated;			 ///< true após AllocApp()
	bool bmy_windows_allocated;		 ///< true após AllocMyWindows()
	bool imgui_allocated;			 ///< true após AllocImGui()
	bool font_manager_allocated;	 ///< true após AllocFontManager()
	bool font_scale_allocated;		 ///< true após AllocFontScale()
	bool console_allocated;			 ///< true após AllocConsole()
	bool style_editor_allocated;	 ///< true após AllocStyleEditor()
	bool menu_bar_allocated;		 ///< true após AllocMenuBar()
	bool ImageViewerFactory_allocated;
	bool OnlineClock_allocated;
	bool init_args_allocated;

	// -------------------------------------------------------------------------
	// Janela e dados do monitor — detectados em AllocVulkan()
	// -------------------------------------------------------------------------

	SDL_Window*	  m_window;		  ///< Referência não-possuidora — pertence a App::Run()
	float		  m_window_scale; ///< SDL_GetDisplayContentScale() do monitor da janela
	SDL_DisplayID m_display_id;	  ///< SDL_GetDisplayForWindow() — 0 se não detectado
	int			  m_display_w;	  ///< Resolução horizontal do monitor em pixels
	int			  m_display_h;	  ///< Resolução vertical do monitor em pixels
};