#pragma once
#include "pch.hpp"
#include "App.hpp"
#include "MyResult.hpp"

class MyWindows : public App{
public:
	MyWindows();
	~MyWindows();

	MyResult CreateWindows();

private:
	class App*				  m_App;
	class VulkanContext*		  g_Vulkan;
	class ImGuiContext_Wrapper* g_ImGui;
	class Console*			  g_Console;
	class StyleEditor*		  g_Style;
	class MenuBar*			  g_MenuBar;
	class AppSettings*		  g_Settings;


};
