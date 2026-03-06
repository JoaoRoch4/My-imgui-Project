#include "pch.hpp"
#include "MyWindows.hpp"
#include "WindowsConsole.hpp"
#include "App.hpp"
#include "Memory.hpp"
#include "MyResult.hpp"
#include "ImGuiContext_Wrapper.hpp"
#include "MenuBar.hpp"
#include "Console.hpp"
#include "FontScale.hpp" // FontScale::ProcessEvent, SetSize, ResetToDefault
#include "StyleEditor.hpp"

#include <implot3d.h>
#include <implot3d_internal.h>
#include <implot3d_demo.cpp>


MyWindows::MyWindows() :
m_App(nullptr),
g_Vulkan(nullptr),
g_ImGui(nullptr),
g_Console(nullptr),
g_Style(nullptr),
g_MenuBar(nullptr),
g_Settings(nullptr)
{
	m_App = Memory::Get()->GetApp();
	if (!m_App) MR_BOTH_ERR_END_LOC("MyWindows: App instance not found in Memory.");
	g_Vulkan = Memory::Get()->GetVulkan();
	if (!g_Vulkan) MR_BOTH_ERR_END_LOC("MyWindows: VulkanContext instance not found in Memory.");
	g_ImGui = Memory::Get()->GetImGui();
	if (!g_ImGui) MR_BOTH_ERR_END_LOC("MyWindows: ImGui instance not found in Memory.");

	g_Console = Memory::Get()->GetConsole();
	if (!g_Console) MR_BOTH_ERR_END_LOC("MyWindows: Console instance not found in Memory.");

	g_Style = Memory::Get()->GetStyleEditor();
	if (!g_Style) MR_BOTH_ERR_END_LOC("MyWindows: StyleEditor instance not found in Memory.");
	g_MenuBar = Memory::Get()->GetMenuBar();
	if (!g_MenuBar) MR_BOTH_ERR_END_LOC("MyWindows: MenuBar instance not found in Memory.");
	g_Settings = Memory::Get()->GetAppSettings();
	if (!g_Settings) MR_BOTH_ERR_END_LOC("MyWindows: AppSettings instance not found in Memory.");

	
}

MyWindows::~MyWindows() { m_App = nullptr; }

MyResult MyWindows::CreateWindows() {
	// Sonda o hotkey do console externo Win32 (F1) fora do contexto ImGui.
	WindowsConsole::poll_hotkey();

	// Inicia o frame ImGui — deve vir antes de qualquer widget.
	g_ImGui->NewFrame();

	// Barra de menu principal — desenha BeginMainMenuBar/EndMainMenuBar.
	g_MenuBar->Draw();

	// ---- ImGui Demo Window ----------------------------------------------
	// Controlada por g_ShowDemo; passa &g_ShowDemo para que o X feche.
	if (m_App->g_ShowDemo) ImGui::ShowDemoWindow(&m_App->g_ShowDemo);

	// ---- Window Controls ------------------------------------------------
	// PADRÃO CORRETO: o if externo decide se a janela existe.
	// Begin/End ficam DENTRO das chaves — End() é sempre pareado com Begin().
	{
		// Detecta a transição open→closed para persistir o fechamento pelo X.
		static bool s_was_open = true;

		if (m_App->g_Settings->window.show_window_controls) {
			// Begin retorna true se a janela está visível e não colapsada.
			// Mesmo que retorne false (colapsada), End() DEVE ser chamado.
			ImGui::Begin("Window Controls",
						 &m_App->g_Settings->window.show_window_controls); // X zera o bool

			// ---- Botão fechar do programa (≠ fechar a janela ImGui) -----
			const float btn_w	= 60.0f;							 // largura do botão
			const float padding = ImGui::GetStyle().WindowPadding.x; // margem da janela
			// Posiciona o botão encostado na borda direita.
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - btn_w - padding);
			if (ImGui::Button("\xe2\x9d\x8c", ImVec2(btn_w, 0))) // ❌ em UTF-8
				m_App->g_Done = true; // MainLoop encerra na próxima iteração
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fechar o programa");

			ImGui::SameLine();
			ImGui::Text("Global Alpha Blending");

			// ---- Opacidade da janela SDL (não persistida) ---------------
			// SliderFloat retorna true quando o valor mudou.
			if (ImGui::SliderFloat("Window Opacity", &m_App->g_window_opacity, 0.1f, 1.0f))
				SDL_SetWindowOpacity(m_App->g_Window, m_App->g_window_opacity); // aplica imediatamente

			if (ImGui::Button("Reset to Opaque")) {
				m_App->g_window_opacity = 1.0f;			  // restaura opacidade total
				SDL_SetWindowOpacity(m_App->g_Window, 1.0f); // aplica imediatamente
			}

			ImGui::Separator();

			// ---- Cor de fundo do framebuffer Vulkan (persistida) --------
			// g_color_ptr aponta para g_Settings->clear_color[0].
			if (ImGui::ColorEdit3("Background Color", m_App->g_color_ptr))
				m_App->SaveConfig(); // persiste alteração imediatamente

			// ---- Toggle do tema Windows 11 Mica (persistido) -----------
			if (ImGui::Checkbox("Windows 11 Mica Theme", &m_App->g_Settings->use_mica_theme)) {
				m_App->ApplyStyleToImGui(); // reaplicação imediata (com ou sem Mica)
				m_App->SaveConfig();
			}

			ImGui::Separator();

			// ---- Escala de fonte — multiplicador (persistido) -----------
			if (ImGui::SliderFloat("Font Scale", &m_App->g_Settings->font.font_scale_main, 0.5f, 3.0f,
								   "%.2f")) {
				ImGui::GetStyle().FontScaleMain =
					m_App->g_Settings->font.font_scale_main; // aplica imediatamente
				m_App->SaveConfig();
			}

			// ---- Tamanho base da fonte em pixels (persistido) -----------
			if (ImGui::SliderFloat("Font Size Base", &m_App->g_Settings->font.font_size_base,
								   FontScale::FONT_SIZE_MIN, FontScale::FONT_SIZE_MAX, "%.1f px")) {
				FontScale::SetSize(m_App->g_Settings->font.font_size_base); // aplica
				m_App->SaveConfig();
			}

			if (ImGui::Button("Reset Font")) {
				FontScale::ResetToDefault();			 // restaura tamanho original
				m_App->g_Settings->font.font_scale_main = 1.0f; // reseta multiplicador
				ImGui::GetStyle().FontScaleMain	 = 1.0f; // aplica imediatamente
				m_App->SaveConfig();
			}

			// ---- Métrica de desempenho ----------------------------------
			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / m_App->g_io->Framerate,
						m_App->g_io->Framerate);

			ImGui::Separator();

			// Botão que abre/fecha o console externo Win32.
			WindowsConsole::render_imgui_button();

			// ---- Visibilidade do console ImGui interno (persistida) -----
			if (ImGui::Checkbox("Show ImGui Console", &m_App->g_Settings->window.show_console))
				m_App->SaveConfig();

			// ---- Flags de janelas (persistidas) -------------------------
			if (ImGui::Checkbox("Demo Window", &m_App->g_ShowDemo)) m_App->SaveConfig();
			if (ImGui::Checkbox("Style Editor", &m_App->g_ShowStyleEd)) m_App->SaveConfig();

			if (ImGui::Button("Log Test msg"))
				g_Console->AddLog(L"Botao pressionado no frame %d \U0001F680",
								  ImGui::GetFrameCount());

			// ---- Logo (condicional — só se carregada) -------------------
			if (m_App->g_Logo.IsLoaded()) {
				ImGui::Separator();
				m_App->g_Logo.DrawCentered(180.0f, 60.0f); // centraliza na largura da janela
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Logo %dx%d", m_App->g_Logo.GetWidth(), m_App->g_Logo.GetHeight());
			}

			// ---- Ícone de configurações (condicional) -------------------
			if (m_App->g_IconSettings.IsLoaded()) {
				// DrawButton retorna true se o botão foi clicado.
				if (m_App->g_IconSettings.DrawButton("##btn_settings", {16.0f, 16.0f}))
					m_App->g_ShowStyleEd = !m_App->g_ShowStyleEd; // toggle do StyleEditor
				ImGui::SameLine();
				ImGui::Text("Configuracoes");
			}

			ImGui::Separator();

			// ---- Checkboxes de janelas extras (todos persistidos) -------
			if (ImGui::Checkbox("grafico", &m_App->g_grafico)) m_App->SaveConfig();
			if (ImGui::Checkbox("Viewports", &m_App->bViewportDocking)) m_App->SaveConfig();
			if (ImGui::Checkbox("ImPlot3D RealtimePlots", &m_App->bImPlot3d_DemoRealtimePlots))
				m_App->SaveConfig();
			if (ImGui::Checkbox("ImPlot3D QuadPlots", &m_App->bImPlot3d_DemoQuadPlots)) m_App->SaveConfig();
			if (ImGui::Checkbox("ImPlot3D TickLabels", &m_App->bImPlot3d_DemoTickLabels)) m_App->SaveConfig();
			// OBRIGATÓRIO: sempre pareado com Begin(), mesmo se colapsado.
			ImGui::End();

		} // fim do if(show_window_controls)

		// Detecta o momento exato em que o X do ImGui fechou a janela
		// (show_window_controls foi de true para false neste frame).
		if (s_was_open && !m_App->g_Settings->window.show_window_controls)
			m_App->SaveConfig(); // persiste o fechamento para o próximo boot

		// Atualiza referência para o próximo frame.
		s_was_open = m_App->g_Settings->window.show_window_controls;
	} // fim do bloco Window Controls

	// ---- Console ImGui interno ------------------------------------------
	// Desenhado fora do bloco anterior — janela independente.
	if (m_App->g_Settings->window.show_console) {
		g_Console->Draw(L"Debug Console", &m_App->g_Settings->window.show_console);
		// Se o X do console foi clicado, show_console é false agora.
		if (!m_App->g_Settings->window.show_console) m_App->SaveConfig(); // persiste o fechamento
	}

	// ---- Style Editor ---------------------------------------------------
	if (m_App->g_ShowStyleEd) m_App->g_Style->Show(nullptr, &m_App->g_ShowStyleEd);
	// Detecta transição open→closed do StyleEditor para salvar uma última vez.
	{
		static bool s_style_was_open = false;
		if (s_style_was_open && !m_App->g_ShowStyleEd)
			m_App->SaveConfig(); // o editor foi fechado — persiste o estilo final
		s_style_was_open = m_App->g_ShowStyleEd;
	}

	// bViewportDocking é um one-shot: habilita no frame em que fica true,
	// mas é imediatamente zerado para não ficar re-aplicando todo frame.
	if (m_App->bViewportDocking) m_App->bViewportDocking = !m_App->bViewportDocking;

	// ---- Gráfico de exemplo (ImPlot) ------------------------------------
	if (m_App->g_grafico) {
		ImGui::Begin("Grafico de Exemplo", &m_App->g_grafico);
		if (ImPlot::BeginPlot("Gráfico de Exemplo")) {
			ImPlot::SetupAxes("Tempo", "Valor");
			static float x_data[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
			static float y_data[10] = {1, 3, 2, 4, 5, 3, 6, 5, 7, 8};
			ImPlot::PlotLine("Sinal A", x_data, y_data, 10);
			ImPlot::PlotLineG(
				"Cosseno", [](int idx, void*) { return ImPlotPoint(idx * 0.1f, cosf(idx * 0.1f)); },
				nullptr, 100);
			ImPlot::EndPlot();
		}
		ImGui::End(); // End() fica FORA do if(BeginPlot) — sempre pareado com Begin
	}

	// ---- ImPlot3D demos -------------------------------------------------
	if (m_App->bImPlot3d_DemoRealtimePlots) {
		ImGui::Begin("ImPlot3D Demo RealtimePlots", &m_App->bImPlot3d_DemoRealtimePlots);
		ImPlot3D::CreateContext();
		ImPlot3D::DemoRealtimePlots();
		ImPlot3D::DestroyContext();
		ImGui::End();
	}

	if (m_App->bImPlot3d_DemoQuadPlots) {
		ImGui::Begin("ImPlot3D Demo QuadPlots", &m_App->bImPlot3d_DemoQuadPlots);
		ImPlot3D::CreateContext();
		ImPlot3D::DemoQuadPlots();
		ImPlot3D::DestroyContext();
		ImGui::End();
	}

	if (m_App->bImPlot3d_DemoTickLabels) {
		ImGui::Begin("ImPlot3D Demo TickLabels", &m_App->bImPlot3d_DemoTickLabels);
		ImPlot3D::CreateContext();
		ImPlot3D::DemoTickLabels();
		ImPlot3D::DestroyContext();
		ImGui::End();
	}

	// ---- Registro do comando "theme" ------------------------------------
	// NOTA: RegisterCommand é idempotente no seu Console? Se não for,
	// mova este bloco para RegisterCommands() para evitar re-registro
	// a cada frame (~60x por segundo).
	g_Console->RegisterCommand(
		L"theme", L"Muda o tema da aplicação. Uso: theme [dark|light|clear]",
		[this](std::vector<std::wstring> args) {
			// Sem argumentos → exibe ajuda.
			if (args.empty()) {
				g_Console->AddLog(L"[yellow]Uso:[/] theme [dark|light|clear]");
				return;
			}

			// Cópia do primeiro argumento convertida para uppercase
			// para comparação case-insensitive.
			std::wstring sub = args[0];
			std::transform(sub.begin(), sub.end(), sub.begin(),
						   [](const wchar_t c) { return static_cast<wchar_t>(towupper(c)); });

			if (sub == L"DARK") {
				ImGui::StyleColorsDark();
				g_Console->AddLog(L"[cyan]Tema:[/] dark aplicado.");
			} else if (sub == L"LIGHT") {
				ImGui::StyleColorsLight();
				g_Console->AddLog(L"[cyan]Tema:[/] light aplicado.");
			} else if (sub == L"CLEAR" || sub == L"CLASSIC") {
				ImGui::StyleColorsClassic();
				g_Console->AddLog(L"[cyan]Tema:[/] classic aplicado.");
			} else {
				g_Console->AddLog(L"[error]Subcomando desconhecido:[/] '%ls'", args[0].c_str());
				g_Console->AddLog(L"[yellow]Uso:[/] theme [dark|light|clear]");
			}
		});
	return MR_OK;
}
