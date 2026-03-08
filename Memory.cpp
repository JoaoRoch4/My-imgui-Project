/**
 * @file Memory.cpp
 * @brief Implementação do singleton Memory.
 *
 * DIVISÃO DE RESPONSABILIDADES
 * -----------------------------
 * A inicialização é dividida entre wWinMain e App::Run:
 *
 *  wWinMain()                          App::Run()
 *  ─────────────────────               ─────────────────────────────────
 *  Memory::Init()           →          SDL_Init()
 *                                      SDL_CreateWindow(...)
 *                                      Memory::Get()->AllocAll(window)
 *  App app;                 →              AllocWindowsConsole()
 *  app.Run()                →              AllocVulkan()
 *                                          AllocImGui()
 *                                          AllocFontManager()
 *                                          AllocFontScale()
 *                                          AllocConsole()
 *                                          AllocStyleEditor()
 *                                          AllocMenuBar()
 *                                      [loop principal]
 *                                      SDL_DestroyWindow(window)
 *                                      SDL_Quit()
 *  Memory::Get()->DestroyAll()  ←      (App::Run retornou)
 *  Memory::Shutdown()
 *
 * POR QUE DestroyAll() fica em wWinMain e não em App::Run()?
 * -----------------------------------------------------------
 * Vulkan precisa que a VkSurfaceKHR seja destruída ANTES de VkInstance.
 * A surface é criada sobre a SDL_Window — portanto a janela deve existir
 * durante CleanupWindow(). SDL_DestroyWindow() é chamado DENTRO de App::Run()
 * antes de retornar, então DestroyAll() (que chama CleanupWindow) deve ocorrer
 * APÓS App::Run() retornar, onde a janela ainda é um handle válido mas já foi
 * passada para CleanupWindow antes de SDL_DestroyWindow.
 *
 * Na prática: App::Run() destrói a JANELA, wWinMain destrói os RECURSOS
 * VULKAN/IMGUI. A janela é destruída depois que todos os recursos dependentes
 * dela são liberados pela ordem de DestroyAll(): MenuBar → StyleEditor →
 * Console → FontScale → FontManager → ImGui → App → Vulkan → WindowsConsole
 *
 * CORREÇÃO DO ASSERT (info->ImageCount >= info->MinImageCount)
 * -------------------------------------------------------------
 * ImGui_ImplVulkan_Init() lê MinImageCount do swapchain já existente.
 * Se SetupWindow() ainda não foi chamado, o assert falha
 * (imgui_impl_vulkan.cpp:1323). SOLUÇÃO: SetupWindow() é chamado dentro de
 * AllocVulkan(), antes de AllocImGui().
 *
 * DETECÇÃO DO MONITOR CORRETO
 * ----------------------------
 * SDL_GetDisplayForWindow(m_window) identifica em qual monitor a janela está.
 * Em setups multi-monitor, o app pode abrir em um monitor secundário com DPI
 * diferente do primário. O m_window_scale resultante é reutilizado por
 * AllocImGui() e AllocFontManager() sem nova consulta ao SDL.
 */

#include "pch.hpp"

#include "Memory.hpp"

#include "App.hpp"
#include "Appsettings.hpp"
#include "Console.hpp"
#include "EmojiDebugHelper.h"
#include "FontManager.hpp"
#include "FontScale.hpp"
#include "ImGuiContext_Wrapper.hpp"
#include "MenuBar.hpp"
#include "MyResult.hpp"
#include "MyWindows.hpp"
#include "StyleEditor.hpp"
#include "VulkanContext_Wrapper.hpp"
#include "WindowsConsole.hpp"
#include "Imageviewerfactory.hpp"
#include "InitArgs.hpp"
#include "OnlineClock.hpp"

// ============================================================================
// Definição do ponteiro estático
// ============================================================================

/**
 * @brief Ponteiro estático para a única instância.
 *
 * Zero-inicializado pelo runtime C++ antes de wWinMain rodar.
 * Atribuído em Init() e zerado em Shutdown().
 */
Memory *Memory::s_instance = nullptr;

// ============================================================================
// Construtor / Destrutor
// ============================================================================

/**
 * @brief Construtor privado — todos os membros inicializados a
 * zero/nullptr/false.
 *
 * Privado: apenas Memory::Init() pode construir via new Memory().
 */
Memory::Memory() noexcept :
app_settings_instance(nullptr),
windows_console_instance(nullptr),
vulkan_instance(nullptr),
app_instance(nullptr),
imgui_instance(nullptr),
font_manager_instance(nullptr),
font_scale_instance(nullptr),
console_instance(nullptr),
style_editor_instance(nullptr),
init_args_allocated(false),
OnlineClock_instance(nullptr),
menu_bar_instance(nullptr),
bApp_settings_allocated(false),
bmy_windows_allocated(false),
bvulkan_allocated(false),
bapp_allocated(false),
imgui_allocated(false),
font_manager_allocated(false),
font_scale_allocated(false),
console_allocated(false),
style_editor_allocated(false),
menu_bar_allocated(false),
OnlineClock_allocated(false),
m_window(nullptr),
m_window_scale(1.f),
m_display_id(0),
m_display_w(0),
m_display_h(0) {}

/**
 * @brief Destrutor privado — unique_ptrs destroem automaticamente qualquer
 * recurso restante caso DestroyAll() não tenha sido chamado explicitamente.
 * Isso é uma rede de segurança para crashes durante o shutdown.
 */
Memory::~Memory() = default;

// ============================================================================
// Init / Shutdown / Get
// ============================================================================

/**
 * @brief Cria a instância do singleton em heap.
 *
 * Deve ser a primeira linha executável de wWinMain(), antes de qualquer uso
 * de Memory::Get(). O assert em Debug impede chamadas duplicadas.
 *
 * Separado de AllocAll() para que o singleton possa ser consultado por
 * sistemas que precisam de Memory antes de App::Run() criar a janela SDL.
 */
const void Memory::Init() {
	assert(s_instance == nullptr && "Memory::Init() chamado mais de uma vez. "
									"Chame Memory::Shutdown() antes de chamar Init() novamente.");

	s_instance = new Memory(); // construtor privado — alocação explícita em heap

}

/**
 * @brief Destrói a instância do singleton.
 *
 * Deve ser chamado em wWinMain() após:
 *   1. Memory::Get()->DestroyAll()  — libera Vulkan, ImGui, fontes, etc.
 *   2. App::Run() já retornou       — SDL_DestroyWindow já foi chamado dentro
 * de Run()
 *
 * O delete chama ~Memory() que por sua vez destrói todos os unique_ptrs
 * restantes (proteção final contra recursos não liberados).
 */
void Memory::Shutdown() {
	assert(s_instance != nullptr &&
		   "Memory::Shutdown() chamado sem Memory::Init() ter sido chamado antes.");

	delete s_instance;	  // ~Memory() destrói todos os unique_ptrs restantes
	s_instance = nullptr; // Get() retorna nullptr após este ponto
}

/**
 * @brief Retorna o ponteiro para a instância do singleton.
 *
 * Thread-safe para leitura após Init() ter retornado.
 * @return Ponteiro válido entre Init() e Shutdown(); nullptr fora desse
 * intervalo.
 */
Memory *Memory::Get() {
	return s_instance; // nullptr se Init() não foi chamado ou Shutdown() já foi
}

// ============================================================================
// AllocAll / DestroyAll
// ============================================================================

/**
 * @brief Aloca todos os recursos na ordem correta.
 *
 * Chamado por App::Run() imediatamente após SDL_CreateWindow().
 * A janela é armazenada como referência não-possuidora em m_window —
 * ela continua pertencendo a App::Run() e será destruída por
 * SDL_DestroyWindow() dentro de Run(), antes de este retornar para wWinMain.
 *
 * @param window  SDL_Window* criado em App::Run(). Não pode ser nullptr.
 */
MyResult Memory::AllocAll(SDL_Window *window) {
	if (!window)
		return MR_MSGBOX_ERR_LOC("Memory::AllocAll() recebeu SDL_Window* nulo. "
								 "SDL_CreateWindow() deve ser chamado em "
								 "App::Run() antes de AllocAll().");

	// Armazena referência à janela — todos os sub-métodos a lêem via m_window
	// sem precisar de parâmetros extras ou variáveis globais
	m_window = window;

	// 1. Console Win32: ativo primeiro para que printf() funcione desde o início
	if (!MR_IS_OK(AllocWindowsConsole()))
		return MR_BOTH_ERR_END_LOC("AllocAll: falhou em AllocWindowsConsole");

	// 2. Vulkan: detecta monitor, Initialize(), SetupWindow()
	//    swapchain DEVE existir antes de AllocImGui() — assert do ImGui
	if (!MR_IS_OK(AllocVulkan())) return MR_BOTH_ERR_END_LOC("AllocAll: falhou em AllocVulkan");


	// 3. App interno: lógica da aplicação; após Vulkan, antes do ImGui
	// if (!MR_IS_OK(AllocApp())) return MR_BOTH_ERR_END_LOC("AllocAll: falhou em AllocApp");


	// 4. ImGui: CreateContext + ImplSDL3 + ImplVulkan
	//    ImplVulkan_Init lê ImageCount do swapchain criado em AllocVulkan()
	if (!MR_IS_OK(AllocImGui())) return MR_BOTH_ERR_END_LOC("AllocAll: falhou em AllocImGui");

	// 5. FontManager: carrega TTFs + emoji; requer contexto ImGui ativo
	if (!MR_IS_OK(AllocFontManager()))
		return MR_BOTH_ERR_END_LOC("AllocAll: falhou em AllocFontManager");

	// 6. FontScale: depende das fontes carregadas em AllocFontManager()
	if (!MR_IS_OK(AllocFontScale()))
		return MR_BOTH_ERR_END_LOC("AllocAll: falhou em AllocFontScale");

	// 7. Console ImGui: usa ImGui::MemAlloc() — requer GImGui != nullptr
	if (!MR_IS_OK(AllocConsole())) return MR_BOTH_ERR_END_LOC("AllocAll: falhou em AllocConsole");


	if (!MR_IS_OK(AllocAppSettings()))
		return MR_BOTH_ERR_END_LOC("AllocAll: falhou em AllocAppSettings");

		if (!MR_IS_OK(AllocOnlineClock()))
		return MR_BOTH_ERR_END_LOC("AllocAll: falhou em AllocAppSettings");
	// 9. MenuBar: registra itens de menu; requer contexto ImGui ativo
	if (!MR_IS_OK(AllocMenuBar())) return MR_BOTH_ERR_END_LOC("AllocAll: falhou em AllocMenuBar");

	if (!MR_IS_OK(AllocMyWindows()))
		return MR_BOTH_ERR_END_LOC("AllocAll: falhou em AllocMyWindows");

	// 8. StyleEditor: carrega imgui_style.json; requer contexto ImGui ativo
	if (!MR_IS_OK(AllocStyleEditor()))
		return MR_BOTH_ERR_END_LOC("AllocAll: falhou em AllocStyleEditor");

		if (!MR_IS_OK(AllocImageViewerFactory()))
		return MR_BOTH_ERR_END_LOC("AllocAll: falhou em AllocImageViewerFactory");

	return MyResult::ok;
}

/**
 * @brief Destroi todos os recursos na ordem inversa de AllocAll().
 *
 * Chamado em wWinMain() APÓS App::Run() retornar.
 * Neste ponto SDL_DestroyWindow() já foi chamado dentro de App::Run(),
 * mas m_window ainda é o handle original — CleanupWindow() o usa para
 * destruir a VkSurfaceKHR antes que a VkInstance seja destruída.
 *
 * Cada sub-método verifica sua flag antes de agir — seguro mesmo se
 * AllocAll() falhou no meio e apenas parte dos recursos foi alocada.
 */
MyResult Memory::DestroyAll() {
	DestroyMenuBar();		 // 9 → sem dependentes após ele
	DestroyStyleEditor();	 // 8
	DestroyConsole();		 // 7 → usa ImGui: antes do ImGui
	DestroyFontScale();		 // 6 → usa fontes: antes do FontManager
	DestroyFontManager();	 // 5 → atlas ImGui: antes do ImGui
	DestroyImGui();			 // 4 → DestroyContext(): antes do Vulkan
	DestroyMyWindows();		 // 4.5 → MyWindows é usado por ImGui e App: antes do ImGui e do App
	DestroyApp();			 // 3 → pode usar Vulkan: antes do Vulkan
	DestroyVulkan();		 // 2 → device + instance: quase o último
	DestroyOnlineClock();
	DestroyWindowsConsole(); // 1 → último: printf() funciona até aqui
	DestroyInitArgs();
	DestroyAppSettings();
	m_window = nullptr; // limpa a referência; janela já destruída por App::Run()
	return MyResult::ok;
}

MyResult Memory::AllocAppSettings() {
	if (bApp_settings_allocated) return MR_BOTH_ERR_END_LOC("AppSettings já alocado, ignorando.");

	app_settings_instance = std::make_unique<AppSettings>();
	if (!app_settings_instance) return MR_MSGBOX_ERR_LOC("Falha ao alocar AppSettings.");

	bApp_settings_allocated = true;
	return MyResult::ok;
}

MyResult Memory::DestroyAppSettings() {
	if (!bApp_settings_allocated) return MyResult::ok;

	app_settings_instance.reset(); // ~AppSettings()
	bApp_settings_allocated = false;
	return MyResult::ok;
}
// ============================================================================
// WindowsConsole
// ============================================================================
/**
 * @brief Abre o console Win32 para saída de debug via printf() e std::cout.
 *
 * Em aplicações com subsistema Windows (wWinMain) não há console por padrão.
 * WindowsConsole::Open() chama ::AllocConsole() da Win32 API e redireciona
 * stdout, stderr e stdin para a nova janela de console.
 * Essencial em builds Debug para ver logs sem debugger anexado.
 */
MyResult Memory::AllocWindowsConsole() {
	if (bWindows_console_allocated)
		return MR_BOTH_ERR_END_LOC("WindowsConsole já alocado, ignorando.");

	windows_console_instance = std::make_unique<WindowsConsole>();
	if (!windows_console_instance) return MR_MSGBOX_ERR_LOC("Falha ao alocar WindowsConsole.");

	// Open(): chama ::AllocConsole() e redireciona stdout/stderr/stdin
	if (!windows_console_instance->init())
		return MR_MSGBOX_ERR_LOC("WindowsConsole::init() falhou.");

	bWindows_console_allocated = true;
	return MyResult::ok;
}

MyResult Memory::AllocInitArgs(LPWSTR lp_cmd_line)
{
    // Protege contra chamadas duplicadas — padrão de todos os AllocXxx()
    if(init_args_allocated)
        return MR_BOTH_ERR_END_LOC("InitArgs já alocado, ignorando.");

    // Constrói a instância — sem new explícito (RAII via unique_ptr)
    init_args_instance = std::make_unique<InitArgs>();

    // make_unique nunca devolve nullptr em MSVC (lança std::bad_alloc em falha)
    // O if é uma rede de segurança extra consistente com os outros AllocXxx()
    if(!init_args_instance)
        return MR_MSGBOX_ERR_LOC("Falha ao alocar InitArgs.");

    // Parseia lpCmdLine via CommandLineToArgvW (Shell32)
    // Seguro com nullptr — Init() trata o caso e deixa m_args vazio
    init_args_instance->Init(lp_cmd_line);

    // Marca como alocado — GetInitArgs() e DestroyInitArgs() verificam esta flag
    init_args_allocated = true;

    return MyResult::ok;
}

/**
 * @brief Destrói o InitArgs e liberta a memória das wstrings.
 *
 * Chamado por DestroyAll() ou directamente se necessário.
 * Seguro chamar mesmo que AllocInitArgs() não tenha sido chamado.
 */
MyResult Memory::DestroyInitArgs()
{
    // Flag falsa → nada a destruir — retorna ok sem log de erro
    if(!init_args_allocated)
        return MyResult::ok;

    // unique_ptr::reset() chama ~InitArgs() que destrói vector<wstring>
    // Todas as wstrings são libertadas aqui (RAII)
    init_args_instance.reset();

    // Marca como não alocado — chamadas subsequentes a GetInitArgs() retornam nullptr
    init_args_allocated = false;

    return MyResult::ok;
}


/**
 * @brief Fecha o console Win32 e restaura os streams padrão.
 */
MyResult Memory::DestroyWindowsConsole() {
	if (!bWindows_console_allocated) return MyResult::ok;

	windows_console_instance->shutdown(); // ::FreeConsole() + fecha os handles de stream
	windows_console_instance.reset();	  // ~WindowsConsole()
	bWindows_console_allocated = false;
	return MyResult::ok;
}

// ============================================================================
// Vulkan
// ============================================================================

/**
 * @brief Inicializa o VulkanContext, detecta o monitor e cria o swapchain.
 *
 * DETECÇÃO DO MONITOR:
 *   SDL_GetDisplayForWindow(m_window)         → m_display_id
 *   SDL_GetDisplayContentScale(m_display_id)  → m_window_scale
 *   SDL_GetCurrentDisplayMode(m_display_id)   → m_display_w, m_display_h (log)
 *
 * m_window_scale é reutilizado por AllocImGui() e AllocFontManager()
 * sem nova consulta ao SDL — garante DPI correto em setups multi-monitor.
 *
 * SetupWindow() cria VkSurfaceKHR + VkSwapchainKHR para m_window.
 * DEVE preceder AllocImGui(): ImGui_ImplVulkan_Init lê ImageCount do swapchain.
 *   imgui_impl_vulkan.cpp:1323  assert(info->ImageCount >= info->MinImageCount)
 */
MyResult Memory::AllocVulkan() {
	if (bvulkan_allocated) return MR_BOTH_ERR_END_LOC("VulkanContext já alocado, ignorando.");

	if (!m_window)
		return MR_MSGBOX_ERR_LOC("AllocVulkan() chamado sem janela. "
								 "Chame AllocAll(window) — não AllocVulkan() diretamente.");

	// ---- Tamanho da janela ------------------------------------------------

	int w = 0, h = 0;
	SDL_GetWindowSize(m_window, &w, &h); // pixels lógicos — não do monitor

	// ---- Extensões Vulkan do SDL ------------------------------------------

	// SDL_Vulkan_GetInstanceExtensions retorna as extensões necessárias para
	// criar a VkSurfaceKHR para esta janela (ex.: VK_KHR_win32_surface)
	std::vector<const char *> extensions;
	uint32_t			   n	   = 0;
	const char *const	  *sdl_ext = SDL_Vulkan_GetInstanceExtensions(&n);
	for (uint32_t i = 0; i < n; ++i)
		extensions.push_back(sdl_ext[i]); // ponteiros do SDL — vida longa garantida

	// ---- VulkanContext ----------------------------------------------------

	vulkan_instance = std::make_unique<VulkanContext>();
	if (!vulkan_instance) return MR_MSGBOX_ERR_LOC("Falha ao alocar VulkanContext.");

	if (!vulkan_instance->Initialize(extensions)) // VkInstance + VkDevice + filas
		return MR_MSGBOX_ERR_LOC("VulkanContext::Initialize() falhou.");

	// SetupWindow: VkSurfaceKHR + VkSwapchainKHR para m_window
	// DEVE preceder AllocImGui() — assert do ImGui dependente do ImageCount
	if (!vulkan_instance->SetupWindow(m_window, w, h))
		return MR_MSGBOX_ERR_LOC("VulkanContext::SetupWindow() falhou.");

	bvulkan_allocated = true;
	return MyResult::ok;
}

/**
 * @brief Destrói swapchain, surface, device e instance Vulkan nessa ordem.
 */
MyResult Memory::DestroyVulkan() {
	if (!bvulkan_allocated) return MyResult::ok;

	vulkan_instance->CleanupWindow(); // VkSwapchainKHR + VkSurfaceKHR
	vulkan_instance->Cleanup();		  // VkDevice + VkInstance
	vulkan_instance.reset();
	bvulkan_allocated = false;
	return MyResult::ok;
}

// ============================================================================
// App
// ============================================================================

/**
 * @brief Constrói a instância interna de App.
 *
 * Este App interno ao Memory é separado do App externo criado na stack de
 * wWinMain. Use conforme a arquitetura do projeto — se App::Run() chama
 * AllocAll(), o App externo já existe e este AllocApp() cria um segundo
 * App interno para conter sub-sistemas da aplicação.
 */
MyResult Memory::AllocApp() {
	if (bapp_allocated) return MR_BOTH_ERR_END_LOC("App já alocado, ignorando.");

	app_instance = std::make_unique<App>();
	if (!app_instance) return MR_MSGBOX_ERR_LOC("Falha ao alocar App.");

	bapp_allocated = true;
	return MyResult::ok;
}

/** @brief Destrói o App interno. */
MyResult Memory::DestroyApp() {
	if (!bapp_allocated) return MyResult::ok;

	app_instance.reset(); // ~App()
	bapp_allocated = false;
	return MyResult::ok;
}

MyResult Memory::AllocMyWindows() {
	if (bmy_windows_allocated)
		return MR_BOTH_ERR_END_LOC("bmy_windows_allocated já alocado, ignorando.");

	my_windows_instance = std::make_unique<MyWindows>();
	if (!my_windows_instance) return MR_MSGBOX_ERR_LOC("Falha ao alocar MyWindows.");

	bmy_windows_allocated = true;
	return MyResult::ok;
}

MyResult Memory::DestroyMyWindows() {
	if (!bmy_windows_allocated) return MyResult::ok;

	my_windows_instance.reset(); // ~App()
	bmy_windows_allocated = false;
	return MyResult::ok;
}

// ============================================================================
// ImGui
// ============================================================================

/**
 * @brief Inicializa o ImGuiContext_Wrapper.
 *
 * Usa m_window e m_window_scale de AllocVulkan() — sem nova chamada SDL.
 * m_window_scale garante que o layout ImGui tenha o tamanho correto no
 * monitor em que a janela foi aberta (pode diferir do monitor primário).
 */
MyResult Memory::AllocImGui() {
	if (imgui_allocated) return MR_BOTH_ERR_END_LOC("ImGuiContext_Wrapper já alocado, ignorando.");

	if (!bvulkan_allocated)
		return MR_MSGBOX_ERR_LOC("AllocImGui chamado antes de AllocVulkan. "
								 "O swapchain precisa existir para ImGui_ImplVulkan_Init.");

	imgui_instance = std::make_unique<ImGuiContext_Wrapper>();
	if (!imgui_instance) return MR_MSGBOX_ERR_LOC("Falha ao alocar ImGuiContext_Wrapper.");

	// m_window: janela da sessão | m_window_scale: DPI do monitor correto
	if (!imgui_instance->Initialize(m_window, vulkan_instance.get(), m_window_scale))
		return MR_MSGBOX_ERR_LOC("ImGuiContext_Wrapper::Initialize() falhou.");

	imgui_allocated = true; // GImGui != nullptr a partir daqui
	return MyResult::ok;
}

/** @brief Destrói ImGui (shutdowns dos backends + DestroyContext). */
MyResult Memory::DestroyImGui() {
	if (!imgui_allocated) return MyResult::ok;

	imgui_instance->Shutdown(); // ImplVulkan_Shutdown + ImplSDL3_Shutdown + DestroyContext
	imgui_instance.reset();
	imgui_allocated = false;
	return MyResult::ok;
}

// ============================================================================
// FontManager
// ============================================================================

/**
 * @brief Carrega todas as fontes TTF + emoji no atlas ImGui.
 *
 * m_window_scale de AllocVulkan() determina o tamanho físico correto das
 * fontes. O atlas é enviado à GPU no primeiro NewFrame() — deve ser chamado
 * antes dele.
 */
MyResult Memory::AllocFontManager() {
	if (font_manager_allocated) return MR_BOTH_ERR_END_LOC("FontManager já alocado, ignorando.");

	if (!imgui_allocated) return MR_MSGBOX_ERR_LOC("AllocFontManager chamado antes de AllocImGui.");

	font_manager_instance = std::make_unique<FontManager>();
	if (!font_manager_instance) return MR_MSGBOX_ERR_LOC("Falha ao alocar FontManager.");

	// 13.0f é o tamanho base em pontos; * m_window_scale = pixels físicos
	// corretos
	const bool emoji_ok = font_manager_instance->LoadAllFontsWithEmoji(13.0f * m_window_scale);

	printf("\n=== Font Status ===\n");
	printf("Fonts loaded: %d\n", font_manager_instance->GetFontCount());
	printf("Base size:    %.1f px  (13.0 x %.2f scale)\n", 13.0f * m_window_scale, m_window_scale);
	EmojiDebugHelper::PrintEmojiStatus(emoji_ok);
	printf("===================\n\n");

	font_manager_allocated = true;
	return MyResult::ok;
}

MyResult Memory::DestroyFontManager() {
	if (!font_manager_allocated) return MyResult::ok;

	font_manager_instance.reset();
	font_manager_allocated = false;
	return MyResult::ok;
}

MyResult Memory::AllocOnlineClock() { if (OnlineClock_allocated)
		return MR_BOTH_ERR_END_LOC("OnlineClock já alocado, ignorando.");

	OnlineClock_instance = std::make_unique<OnlineClock>();
	if (!OnlineClock_instance) return MR_MSGBOX_ERR_LOC("Falha ao alocar OnlineClock.");

	OnlineClock_allocated = true;
	return MyResult::ok; }

MyResult Memory::DestroyOnlineClock() { if (!OnlineClock_allocated) return MyResult::ok;

	OnlineClock_instance.reset();
	OnlineClock_allocated = false;
	return MyResult::ok; }

// ============================================================================
// FontScale
// ============================================================================

/**
 * @brief Inicializa o FontScale (escala de fontes por DPI).
 * Depende das fontes já carregadas pelo FontManager.
 */
MyResult Memory::AllocFontScale() {
	if (font_scale_allocated) return MR_BOTH_ERR_END_LOC("FontScale já alocado, ignorando.");

	if (!font_manager_allocated)
		return MR_MSGBOX_ERR_LOC("AllocFontScale chamado antes de AllocFontManager.");

	font_scale_instance = std::make_unique<FontScale>();
	if (!font_scale_instance) return MR_MSGBOX_ERR_LOC("Falha ao alocar FontScale.");

	font_scale_allocated = true;
	return MyResult::ok;
}

MyResult Memory::DestroyFontScale() {
	if (!font_scale_allocated) return MyResult::ok;

	font_scale_instance.reset();
	font_scale_allocated = false;
	return MyResult::ok;
}

// ============================================================================
// Console
// ============================================================================

/**
 * @brief Constrói o Console ImGui.
 *
 * Console::Console() chama ImGui::MemAlloc() via Wcsdup().
 * DEVE ser chamado após AllocImGui() — exige GImGui != nullptr.
 *   Assertion failed: GImGui != 0  (imgui.cpp)
 */
MyResult Memory::AllocConsole() {
	if (console_allocated) return MR_BOTH_ERR_END_LOC("Console já alocado, ignorando.");

	if (!imgui_allocated)
		return MR_MSGBOX_ERR_LOC("AllocConsole chamado antes de AllocImGui. "
								 "Console usa ImGui::MemAlloc() — exige GImGui != nullptr.");

	console_instance = std::make_unique<Console>();
	if (!console_instance) return MR_MSGBOX_ERR_LOC("Falha ao alocar Console.");

	console_allocated = true;
	return MyResult::ok;
}

MyResult Memory::DestroyConsole() {
	if (!console_allocated) return MyResult::ok;

	console_instance.reset(); // ~Console(): ClearLog() + libera History
	console_allocated = false;
	return MyResult::ok;
}

// ============================================================================
// StyleEditor
// ============================================================================

/**
 * @brief Constrói o StyleEditor e carrega imgui_style.json.
 * Silencioso se o arquivo não existir — mantém estilo padrão do ImGui.
 */
MyResult Memory::AllocStyleEditor() {
	if (style_editor_allocated) return MR_BOTH_ERR_END_LOC("StyleEditor já alocado, ignorando.");

	style_editor_instance = std::make_unique<StyleEditor>();
	if (!style_editor_instance) return MR_MSGBOX_ERR_LOC("Falha ao alocar StyleEditor.");

	style_editor_instance->LoadFromFile("imgui_style.json"); // sem erro se não existir
	style_editor_allocated = true;
	return MyResult::ok;
}

MyResult Memory::DestroyStyleEditor() {
	if (!style_editor_allocated) return MyResult::ok;

	style_editor_instance.reset();
	style_editor_allocated = false;
	return MyResult::ok;
}

// ============================================================================
// MenuBar
// ============================================================================

/**
 * @brief Constrói a MenuBar principal.
 * Registra itens de menu no contexto ImGui ativo — requer AllocImGui() antes.
 */
MyResult Memory::AllocMenuBar() {
	if (menu_bar_allocated) return MR_BOTH_ERR_END_LOC("MenuBar já alocado, ignorando.");

	if (!imgui_allocated) return MR_MSGBOX_ERR_LOC("AllocMenuBar chamado antes de AllocImGui.");

	menu_bar_instance = std::make_unique<MenuBar>();
	if (!menu_bar_instance) return MR_MSGBOX_ERR_LOC("Falha ao alocar MenuBar.");

	menu_bar_allocated = true;
	return MyResult::ok;
}

MyResult Memory::DestroyMenuBar() {
	if (!menu_bar_allocated) return MyResult::ok;

	menu_bar_instance.reset();
	menu_bar_allocated = false;
	return MyResult::ok;
}

MyResult Memory::AllocImageViewerFactory() {
	if (ImageViewerFactory_allocated) return MR_BOTH_ERR_END_LOC("ImageViewerFactory_allocated já alocado, ignorando.");


	ImageViewerFactory_instance = std::make_unique<ImageViewerFactory>();
	if (!ImageViewerFactory_instance) return MR_MSGBOX_ERR_LOC("Falha ao alocar MenuBar.");

	ImageViewerFactory_allocated = true;
	return MyResult::ok;
}

MyResult Memory::DestroyImageViewerFactory() {
	if (!ImageViewerFactory_allocated) return MyResult::ok;

	ImageViewerFactory_instance.reset();
	ImageViewerFactory_allocated = false;
	return MyResult::ok;
}

// ============================================================================
// Getters
// ============================================================================

/** @brief Retorna o WindowsConsole ou nullptr se não alocado. */
WindowsConsole *Memory::GetWindowsConsole() {
	if (!bWindows_console_allocated) {
		MR_BOTH_ERR_END_LOC("GetWindowsConsole() chamado antes de AllocWindowsConsole().");
		return nullptr;
	}
	return windows_console_instance.get();
}

/** @brief Retorna o VulkanContext ou nullptr se não alocado. */
VulkanContext *Memory::GetVulkan() {
	if (!bvulkan_allocated) {
		MR_BOTH_ERR_END_LOC("GetVulkan() chamado antes de AllocVulkan().");
		return nullptr;
	}
	return vulkan_instance.get();
}

/** @brief Retorna o App interno ou nullptr se não alocado. */
App *Memory::GetApp() {
	if (!bapp_allocated) {
		MR_BOTH_ERR_END_LOC("GetApp() chamado antes de AllocApp().");
		return nullptr;
	}
	return app_instance.get();
}

MyWindows *Memory::GetMyWindows() {
	if (!bmy_windows_allocated) {
		MR_BOTH_ERR_END_LOC("GetMyWindows() chamado antes de AllocApp().");
		return nullptr;
	}
	return my_windows_instance.get();
}

/** @brief Retorna o ImGuiContext_Wrapper ou nullptr se não alocado. */
ImGuiContext_Wrapper *Memory::GetImGui() {
	if (!imgui_allocated) {
		MR_BOTH_ERR_END_LOC("GetImGui() chamado antes de AllocImGui().");
		return nullptr;
	}
	return imgui_instance.get();
}

/** @brief Retorna o FontManager ou nullptr se não alocado. */
FontManager *Memory::GetFontManager() {
	if (!font_manager_allocated) {
		MR_BOTH_ERR_END_LOC("GetFontManager() chamado antes de AllocFontManager().");
		return nullptr;
	}
	return font_manager_instance.get();
}

/** @brief Retorna o FontScale ou nullptr se não alocado. */
FontScale *Memory::GetFontScale() {
	if (!font_scale_allocated) {
		MR_BOTH_ERR_END_LOC("GetFontScale() chamado antes de AllocFontScale().");
		return nullptr;
	}
	return font_scale_instance.get();
}

/** @brief Retorna o Console ImGui ou nullptr se não alocado. */
Console *Memory::GetConsole() {
	if (!console_allocated) {
		MR_BOTH_ERR_END_LOC("GetConsole() chamado antes de AllocConsole().");
		return nullptr;
	}
	return console_instance.get();
}

InitArgs* Memory::GetInitArgs() const
{
    // Log de aviso se chamado antes de AllocInitArgs()
    if(!init_args_allocated)
    {
        MR_BOTH_ERR_END_LOC("GetInitArgs() chamado antes de AllocInitArgs().");
        return nullptr;
    }

    // get() devolve o ponteiro raw — a posse permanece no unique_ptr
    return init_args_instance.get();
}


/** @brief Retorna o StyleEditor ou nullptr se não alocado. */
StyleEditor *Memory::GetStyleEditor() {
	if (!style_editor_allocated) {
		MR_BOTH_ERR_END_LOC("GetStyleEditor() chamado antes de AllocStyleEditor().");
		return nullptr;
	}
	return style_editor_instance.get();
}

/** @brief Retorna a MenuBar ou nullptr se não alocada. */
MenuBar *Memory::GetMenuBar() {
	if (!menu_bar_allocated) {
		MR_BOTH_ERR_END_LOC("GetMenuBar() chamado antes de AllocMenuBar().");
		return nullptr;
	}
	return menu_bar_instance.get();
}

/**
 * @brief Retorna a SDL_Window* armazenada em AllocAll(). nullptr antes disso.
 *
 * A janela pertence a App::Run() — Memory apenas guarda a referência.
 * Após App::Run() retornar, SDL_DestroyWindow() já terá sido chamado.
 */
SDL_Window *Memory::GetWindow() const {
	return m_window; // referência não-possuidora
}

AppSettings *Memory::GetAppSettings() const {
	if (!bApp_settings_allocated) {
		MR_BOTH_ERR_END_LOC("GetAppSettings() chamado antes de AllocAppSettings().");
		return nullptr;
	}
	return app_settings_instance.get();
}

ImageViewerFactory *Memory::GetImageViewerFactory() const {
	if (!ImageViewerFactory_allocated) {
		MR_BOTH_ERR_END_LOC("GetAppSettings() chamado antes de AllocAppSettings().");
		return nullptr;
	}
	return ImageViewerFactory_instance.get();
}

OnlineClock *Memory::GetOnlineClock() const { 	
if (!OnlineClock_allocated) {
		MR_BOTH_ERR_END_LOC("OnlineClock_allocated not allocated.");
		return nullptr;
	}
	return OnlineClock_instance.get();
	}
