#include "pch.hpp"
#include "MyWindows.hpp"
#include "WindowsConsole.hpp"
#include "App.hpp"
#include "Memory.hpp"
#include "MyResult.hpp"



MyWindows::MyWindows() : m_App(nullptr) {
    m_App = Memory::Get()->GetApp();
	if(!m_App) MR_BOTH_ERR_END_LOC("MyWindows: App instance not found in Memory.");
}

MyWindows::~MyWindows() {
	m_App = nullptr;
}

MyResult MyWindows::CreateWindows() {

return MR_OK;
}


