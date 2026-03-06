#pragma once
#include "pch.hpp"
#include "App.hpp"
#include "MyResult.hpp"

class MyWindows {
public:
	MyWindows();
	~MyWindows();

	MyResult CreateWindows();

private:
	App* m_App;

};
